use std::collections::{HashMap, VecDeque};
use std::io::{Read, Write};
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::atomic::{AtomicU16, Ordering};
use std::sync::{Arc, Condvar, Mutex};
use std::time::Duration;

use super::encryption::{EncryptedFrameDecoder, TransportEncryption};
use crate::error::{MakxdError, Result};
use crate::types::{BleConnectionIo, ConnectionConfig, UdpWireMode};

pub(crate) trait WirePort: Send {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>>;
    fn write_coalescing_supported(&self) -> bool;
    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize>;
    fn write_all_wire(&mut self, bytes: &[u8], response_expected: bool) -> std::io::Result<()>;
    fn write_queued_wire(
        &mut self,
        records: &[Vec<u8>],
        response_expected: bool,
    ) -> std::io::Result<()> {
        let bytes = records.iter().map(Vec::len).sum();
        let mut coalesced = Vec::with_capacity(bytes);
        for record in records {
            coalesced.extend_from_slice(record);
        }
        self.write_all_wire(&coalesced, response_expected)
    }
    fn flush_wire(&mut self) -> std::io::Result<()>;
}

pub(crate) struct SerialWirePort {
    port: Box<dyn serialport::SerialPort>,
}

impl SerialWirePort {
    pub fn new(port: Box<dyn serialport::SerialPort>) -> Self {
        Self { port }
    }
}

impl WirePort for SerialWirePort {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>> {
        Ok(Box::new(Self::new(
            self.port.try_clone().map_err(MakxdError::Port)?,
        )))
    }

    fn write_coalescing_supported(&self) -> bool {
        true
    }

    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        self.port.read(bytes)
    }

    fn write_all_wire(&mut self, bytes: &[u8], _response_expected: bool) -> std::io::Result<()> {
        self.port.write_all(bytes)
    }

    fn flush_wire(&mut self) -> std::io::Result<()> {
        self.port.flush()
    }
}

struct UdpShared {
    socket: UdpSocket,
    mode: UdpWireMode,
    transactions: Mutex<VecDeque<[u8; 8]>>,
}

pub(crate) struct UdpWirePort {
    shared: Arc<UdpShared>,
    pending: VecDeque<u8>,
}

impl UdpWirePort {
    pub fn connect(config: &ConnectionConfig) -> Result<Self> {
        let ConnectionConfig::Udp {
            host,
            port,
            mode,
            bind_address,
            interface,
            vlan_id,
        } = config
        else {
            return Err(MakxdError::Protocol(
                "UDP connection configuration is required".into(),
            ));
        };
        if vlan_id.is_some() && bind_address.is_none() && interface.is_none() {
            return Err(MakxdError::Protocol(
                "VLAN requires a VLAN interface or bind address".into(),
            ));
        }
        #[cfg(windows)]
        if vlan_id.is_some() && bind_address.is_none() {
            return Err(MakxdError::Protocol(
                "Windows VLAN UDP requires the VLAN interface bind address".into(),
            ));
        }
        let bind = bind_address.as_deref().unwrap_or("0.0.0.0");
        let socket = UdpSocket::bind((bind, 0))?;
        #[cfg(any(target_os = "linux", target_os = "android"))]
        if let Some(interface) = interface {
            socket2::SockRef::from(&socket).bind_device(Some(interface.as_bytes()))?;
        }
        let remote: SocketAddr = (host.as_str(), *port)
            .to_socket_addrs()?
            .next()
            .ok_or_else(|| MakxdError::Protocol("UDP host did not resolve".into()))?;
        socket.connect(remote)?;
        socket.set_read_timeout(Some(Duration::from_millis(200)))?;
        Ok(Self {
            shared: Arc::new(UdpShared {
                socket,
                mode: *mode,
                transactions: Mutex::new(VecDeque::new()),
            }),
            pending: VecDeque::new(),
        })
    }

    fn receive_packet(&mut self) -> std::io::Result<()> {
        let mut packet = [0u8; 512];
        let count = self.shared.socket.recv(&mut packet)?;
        let mut body = &packet[..count];
        if self.shared.mode == UdpWireMode::Raw && body.first() == Some(&0x55) {
            if body.len() < 10 {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "raw UDP response header is invalid",
                ));
            }
            let event = body.len() >= 14
                && body[9..11] == [0xde, 0xad]
                && body[13] == 0x53
                && body.len() == 14 + u16::from_le_bytes([body[11], body[12]]) as usize;
            if !event {
                let mut transactions = self.shared.transactions.lock().unwrap();
                let matching_index = transactions
                    .iter()
                    .position(|transaction| body[1..9] == transaction[..]);
                if let Some(index) = matching_index {
                    transactions.remove(index);
                } else {
                    return Ok(());
                }
                drop(transactions);
            }
            body = &body[9..];
        }
        udp_response_normalize(body, &mut self.pending);
        Ok(())
    }
}

impl WirePort for UdpWirePort {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>> {
        Ok(Box::new(Self {
            shared: Arc::clone(&self.shared),
            pending: VecDeque::new(),
        }))
    }

    fn write_coalescing_supported(&self) -> bool {
        false
    }

    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        if self.pending.is_empty() {
            self.receive_packet()?;
        }
        let count = bytes.len().min(self.pending.len());
        for byte in &mut bytes[..count] {
            *byte = self.pending.pop_front().unwrap();
        }
        Ok(count)
    }

    fn write_all_wire(&mut self, bytes: &[u8], response_expected: bool) -> std::io::Result<()> {
        let mut wire = udp_request(bytes)?;
        if self.shared.mode == UdpWireMode::Raw && wire.first() != Some(&0x03) {
            let mut transaction = [0u8; 8];
            getrandom::fill(&mut transaction)
                .map_err(|error| std::io::Error::other(error.to_string()))?;
            if response_expected {
                self.shared
                    .transactions
                    .lock()
                    .unwrap()
                    .push_back(transaction);
            }
            let mut raw = Vec::with_capacity(9 + wire.len());
            raw.push(0x55);
            raw.extend_from_slice(&transaction);
            raw.append(&mut wire);
            wire = raw;
        }
        let count = self.shared.socket.send(&wire)?;
        if count == wire.len() {
            Ok(())
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::WriteZero,
                "UDP command was partially sent",
            ))
        }
    }

    fn flush_wire(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

const BLE_BATCH_REQUEST_MAGIC: &[u8; 4] = b"MBAT";
const BLE_BATCH_RESPONSE_MAGIC: &[u8; 4] = b"MBAR";
const BLE_BATCH_VERSION: u8 = 1;
const BLE_BATCH_COMMANDS_MAX: usize = 64;
const BLE_BATCH_PIPELINE_MAX: u8 = 5;
const BLE_BATCH_WRITE_BYTES_MAX: usize = 514;
const BLE_BATCH_WRITE_BYTES_MIN: usize = 77;

struct BleShared {
    io: Arc<dyn BleConnectionIo>,
    next_batch_id: AtomicU16,
    credits: (Mutex<u8>, Condvar),
    commands: Mutex<HashMap<u16, Vec<u8>>>,
}

pub(crate) struct BleWirePort {
    shared: Arc<BleShared>,
    pending: VecDeque<u8>,
}

impl BleWirePort {
    pub fn new(io: Arc<dyn BleConnectionIo>) -> Self {
        Self {
            shared: Arc::new(BleShared {
                io,
                next_batch_id: AtomicU16::new(1),
                credits: (Mutex::new(BLE_BATCH_PIPELINE_MAX), Condvar::new()),
                commands: Mutex::new(HashMap::new()),
            }),
            pending: VecDeque::new(),
        }
    }

    fn maximum_write(&self) -> usize {
        self.shared
            .io
            .maximum_write_without_response()
            .min(BLE_BATCH_WRITE_BYTES_MAX)
    }

    fn batch_credit_take(&self) {
        let (lock, ready) = &self.shared.credits;
        let mut credits = lock.lock().unwrap();
        while *credits == 0 {
            credits = ready.wait(credits).unwrap();
        }
        *credits -= 1;
    }

    fn batch_credit_restore(&self, reported: u8) {
        if reported == 0 {
            return;
        }
        let (lock, ready) = &self.shared.credits;
        *lock.lock().unwrap() = reported.min(BLE_BATCH_PIPELINE_MAX);
        ready.notify_all();
    }

    fn batch_response_consume(&mut self, packet: &[u8]) -> std::io::Result<()> {
        if packet.len() < 11 || &packet[..4] != BLE_BATCH_RESPONSE_MAGIC {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "BLE batch response is invalid",
            ));
        }
        if packet[4] != BLE_BATCH_VERSION {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "BLE batch response version is unsupported",
            ));
        }
        let batch_id = u16::from_le_bytes([packet[6], packet[7]]);
        let commands = self
            .shared
            .commands
            .lock()
            .unwrap()
            .get(&batch_id)
            .cloned()
            .ok_or_else(|| {
                std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "BLE batch response does not match a request",
                )
            })?;
        let first = packet[8] as usize;
        let count = packet[9] as usize;
        if packet[10] as usize != commands.len() || first + count > commands.len() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "BLE batch response range is invalid",
            ));
        }
        let mut offset = 11;
        for index in 0..count {
            if offset + 2 > packet.len() {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "BLE batch response ended early",
                ));
            }
            let status = packet[offset];
            let response_bytes = packet[offset + 1] as usize;
            offset += 2;
            if offset + response_bytes > packet.len() {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "BLE batch record ended early",
                ));
            }
            let response = &packet[offset..offset + response_bytes];
            offset += response_bytes;
            if status == 0 && !response.is_empty() {
                direct_response_normalize(response, &mut self.pending);
            } else if status == 2 {
                direct_response_normalize(&[commands[first + index], 0xff], &mut self.pending);
            }
        }
        if offset != packet.len() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "BLE batch response has trailing bytes",
            ));
        }
        if packet[5] & 0x01 != 0 {
            self.shared.commands.lock().unwrap().remove(&batch_id);
            self.batch_credit_restore((packet[5] >> 1) & 0x07);
        }
        Ok(())
    }

    fn batch_write(&mut self, records: &[Vec<u8>]) -> std::io::Result<()> {
        self.batch_credit_take();
        let batch_id = self.shared.next_batch_id.fetch_add(1, Ordering::Relaxed);
        let mut request =
            Vec::with_capacity(9 + records.iter().map(|record| 1 + record.len()).sum::<usize>());
        request.extend_from_slice(BLE_BATCH_REQUEST_MAGIC);
        request.extend([BLE_BATCH_VERSION, 0]);
        request.extend(batch_id.to_le_bytes());
        request.push(records.len() as u8);
        let mut opcodes = Vec::with_capacity(records.len());
        for record in records {
            request.push(record.len() as u8);
            request.extend(record);
            opcodes.push(record[0]);
        }
        self.shared
            .commands
            .lock()
            .unwrap()
            .insert(batch_id, opcodes);
        if let Err(error) = self.shared.io.write(&request) {
            self.shared.commands.lock().unwrap().remove(&batch_id);
            self.batch_credit_restore(1);
            for record in records {
                self.shared
                    .io
                    .write(record)
                    .map_err(|_| std::io::Error::other(error.to_string()))?;
            }
        }
        Ok(())
    }
}

impl WirePort for BleWirePort {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>> {
        Ok(Box::new(Self {
            shared: Arc::clone(&self.shared),
            pending: VecDeque::new(),
        }))
    }

    fn write_coalescing_supported(&self) -> bool {
        self.maximum_write() >= BLE_BATCH_WRITE_BYTES_MIN
    }

    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        if self.pending.is_empty() {
            let packet = self
                .shared
                .io
                .read_notification()
                .map_err(|error| std::io::Error::other(error.to_string()))?;
            if packet.is_empty() || packet.len() > BLE_BATCH_WRITE_BYTES_MAX {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "BLE notification length is invalid",
                ));
            }
            if packet.starts_with(BLE_BATCH_RESPONSE_MAGIC) {
                self.batch_response_consume(&packet)?;
            } else {
                direct_response_normalize(&packet, &mut self.pending);
            }
        }
        let count = bytes.len().min(self.pending.len());
        for byte in &mut bytes[..count] {
            *byte = self.pending.pop_front().unwrap();
        }
        Ok(count)
    }

    fn write_all_wire(&mut self, bytes: &[u8], _response_expected: bool) -> std::io::Result<()> {
        let wire = direct_request(bytes)?;
        if wire.len() > 64 || wire.len() > self.maximum_write() {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "MAKXD BLE command length exceeds the negotiated limit",
            ));
        }
        self.shared
            .io
            .write(&wire)
            .map_err(|error| std::io::Error::other(error.to_string()))
    }

    fn write_queued_wire(
        &mut self,
        queued: &[Vec<u8>],
        _response_expected: bool,
    ) -> std::io::Result<()> {
        let maximum_write = self.maximum_write();
        let mut records = Vec::with_capacity(queued.len());
        for bytes in queued {
            records.push(direct_request(bytes)?);
        }
        let mut first = 0;
        while first < records.len() {
            let mut count = 0;
            let mut request_bytes = 9;
            while first + count < records.len() && count < BLE_BATCH_COMMANDS_MAX {
                let candidate = &records[first + count];
                if candidate.is_empty() || candidate.len() > 64 {
                    return Err(std::io::Error::new(
                        std::io::ErrorKind::InvalidInput,
                        "MAKXD BLE command length is invalid",
                    ));
                }
                if request_bytes + 1 + candidate.len() > maximum_write {
                    break;
                }
                request_bytes += 1 + candidate.len();
                count += 1;
            }
            if count == 0 {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    "MAKXD BLE command exceeds the negotiated write size",
                ));
            }
            if count == 1 {
                self.shared
                    .io
                    .write(&records[first])
                    .map_err(|error| std::io::Error::other(error.to_string()))?;
            } else {
                self.batch_write(&records[first..first + count])?;
            }
            first += count;
        }
        Ok(())
    }

    fn flush_wire(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

fn direct_request(bytes: &[u8]) -> std::io::Result<Vec<u8>> {
    if bytes.len() >= 5 && bytes[..2] == [0xde, 0xad] {
        let offset = 4;
        if bytes.len() <= offset {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "transport frame is empty",
            ));
        }
        return Ok(bytes[offset..].to_vec());
    }
    Ok(bytes.to_vec())
}

fn udp_request(bytes: &[u8]) -> std::io::Result<Vec<u8>> {
    if bytes.len() >= 5 && bytes[..2] == [0xde, 0xad] && bytes[4] == 0x03 {
        return direct_request(bytes);
    }
    Ok(bytes.to_vec())
}

fn direct_response_normalize(bytes: &[u8], output: &mut VecDeque<u8>) {
    if let Some(_) = bytes.first() {
        output.extend([0xde, 0xad]);
        output.extend(((bytes.len() - 1) as u16).to_le_bytes());
        output.extend(bytes);
    }
}

fn udp_response_normalize(bytes: &[u8], output: &mut VecDeque<u8>) {
    if bytes.starts_with(&[0xde, 0xad]) {
        output.extend(bytes);
    } else if bytes.first() == Some(&0x03) {
        direct_response_normalize(bytes, output);
    } else {
        output.extend(bytes);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct BleCaptureIo {
        writes: Mutex<Vec<Vec<u8>>>,
        notifications: Mutex<VecDeque<Vec<u8>>>,
    }

    impl BleConnectionIo for BleCaptureIo {
        fn connect(&self, _address: &str) -> Result<()> {
            Ok(())
        }

        fn write(&self, bytes: &[u8]) -> Result<()> {
            self.writes.lock().unwrap().push(bytes.to_vec());
            Ok(())
        }

        fn read_notification(&self) -> Result<Vec<u8>> {
            self.notifications
                .lock()
                .unwrap()
                .pop_front()
                .ok_or_else(|| MakxdError::Protocol("notification queue is empty".into()))
        }

        fn close(&self) {}

        fn maximum_write_without_response(&self) -> usize {
            514
        }
    }

    #[test]
    fn queued_ble_commands_use_one_batch_and_restore_individual_replies() {
        let io = Arc::new(BleCaptureIo {
            writes: Mutex::new(Vec::new()),
            notifications: Mutex::new(VecDeque::new()),
        });
        let mut port = BleWirePort::new(io.clone());
        port.write_queued_wire(
            &[
                vec![0xde, 0xad, 1, 0, 0x10, 2],
                vec![0xde, 0xad, 1, 0, 0x11, 2],
            ],
            true,
        )
        .unwrap();
        let writes = io.writes.lock().unwrap();
        assert_eq!(writes.len(), 1);
        assert_eq!(&writes[0][..4], b"MBAT");
        assert_eq!(writes[0][8], 2);
        drop(writes);

        io.notifications.lock().unwrap().push_back(vec![
            b'M',
            b'B',
            b'A',
            b'R',
            1,
            1 | (5 << 1),
            1,
            0,
            0,
            2,
            2,
            0,
            2,
            0x10,
            1,
            0,
            2,
            0x11,
            1,
        ]);
        let mut response = [0u8; 12];
        let bytes = port.read_wire(&mut response).unwrap();
        assert_eq!(
            &response[..bytes],
            &[0xde, 0xad, 1, 0, 0x10, 1, 0xde, 0xad, 1, 0, 0x11, 1]
        );
    }

    #[test]
    fn raw_udp_silent_set_does_not_own_next_get_transaction() {
        let server = UdpSocket::bind("127.0.0.1:0").unwrap();
        server
            .set_read_timeout(Some(Duration::from_secs(1)))
            .unwrap();
        let client = UdpSocket::bind("127.0.0.1:0").unwrap();
        client.connect(server.local_addr().unwrap()).unwrap();
        client
            .set_read_timeout(Some(Duration::from_secs(1)))
            .unwrap();
        let shared = Arc::new(UdpShared {
            socket: client,
            mode: UdpWireMode::Raw,
            transactions: Mutex::new(VecDeque::new()),
        });
        let mut port = UdpWirePort {
            shared: Arc::clone(&shared),
            pending: VecDeque::new(),
        };

        port.write_all_wire(&[0xde, 0xad, 1, 0, 0x11, 0x01], false)
            .unwrap();
        let mut silent_packet = [0u8; 64];
        let (silent_bytes, client_address) = server.recv_from(&mut silent_packet).unwrap();
        assert_eq!(silent_bytes, 15);
        assert!(shared.transactions.lock().unwrap().is_empty());

        port.write_all_wire(&[0xde, 0xad, 0, 0, 0x11], true)
            .unwrap();
        let mut get_packet = [0u8; 64];
        let (get_bytes, _) = server.recv_from(&mut get_packet).unwrap();
        assert_eq!(get_bytes, 14);
        assert_eq!(shared.transactions.lock().unwrap().len(), 1);

        let mut silent_error = Vec::from(&silent_packet[..9]);
        silent_error.extend([0xde, 0xad, 1, 0, 0x11, 0xff]);
        server.send_to(&silent_error, client_address).unwrap();
        port.receive_packet().unwrap();
        assert!(port.pending.is_empty());
        assert_eq!(shared.transactions.lock().unwrap().len(), 1);

        let mut get_response = Vec::from(&get_packet[..9]);
        get_response.extend([0xde, 0xad, 1, 0, 0x11, 0x01]);
        server.send_to(&get_response, client_address).unwrap();
        port.receive_packet().unwrap();
        assert_eq!(port.pending, VecDeque::from([0xde, 0xad, 1, 0, 0x11, 1]));
        assert!(shared.transactions.lock().unwrap().is_empty());
    }
}

pub(crate) fn establish_network_connection(
    config: &ConnectionConfig,
    encryption: Option<&TransportEncryption>,
) -> Result<(Box<dyn WirePort>, u8)> {
    let mut port: Box<dyn WirePort> = match config {
        ConnectionConfig::Udp { .. } => Box::new(UdpWirePort::connect(config)?),
        ConnectionConfig::Ble { address, io } => {
            io.connect(address)?;
            Box::new(BleWirePort::new(Arc::clone(io)))
        }
        ConnectionConfig::Com { .. } => {
            return Err(MakxdError::Protocol(
                "network or BLE connection is required".into(),
            ));
        }
    };
    let proof = (|| -> Result<u8> {
        let command = [crate::protocol::api::ApiOpcode::Device as u8];
        let (wire, expected_nonce) = if let Some(encryption) = encryption {
            let (frame, nonce) = encryption.encode_command(&command)?;
            (frame, Some(nonce))
        } else {
            (vec![0xde, 0xad, 0, 0, command[0]], None)
        };
        port.write_all_wire(&wire, true)?;
        port.flush_wire()?;
        let deadline = std::time::Instant::now() + Duration::from_millis(750);
        let mut response = Vec::new();
        let mut decoder = EncryptedFrameDecoder::new();
        let mut bytes = [0u8; 256];
        while std::time::Instant::now() < deadline {
            match port.read_wire(&mut bytes) {
                Ok(count) if count != 0 => {
                    if let Some(encryption) = encryption {
                        for (plaintext, nonce) in decoder.feed(encryption, &bytes[..count])? {
                            if expected_nonce.as_ref() != Some(&nonce) {
                                return Err(MakxdError::Protocol(
                                    "encrypted response nonce does not match".into(),
                                ));
                            }
                            response = plaintext;
                            break;
                        }
                    } else {
                        response.extend_from_slice(&bytes[..count]);
                    }
                    if (encryption.is_some() && response.len() >= 2)
                        || (encryption.is_none()
                            && response
                                .windows(6)
                                .any(|value| value[..5] == [0xde, 0xad, 1, 0, command[0]]))
                    {
                        break;
                    }
                }
                Ok(_) => {}
                Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {}
                Err(error) => return Err(error.into()),
            }
        }
        let kinds = if encryption.is_some() {
            (response.len() == 2 && response[0] == command[0] && response[1] != 0xff)
                .then_some(response[1])
        } else {
            response
                .windows(6)
                .find(|value| value[..5] == [0xde, 0xad, 1, 0, command[0]])
                .map(|value| value[5])
                .filter(|value| *value != 0xff)
        };
        kinds.ok_or_else(|| MakxdError::Protocol("device identity probe failed".into()))
    })();
    match proof {
        Ok(kinds) => Ok((port, kinds)),
        Err(error) => {
            if let ConnectionConfig::Ble { io, .. } = config {
                io.close();
            }
            Err(error)
        }
    }
}
