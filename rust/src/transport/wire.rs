use std::collections::VecDeque;
use std::io::{Read, Write};
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::{Arc, Mutex};
use std::time::Duration;

use super::encryption::{EncryptedFrameDecoder, TransportEncryption};
use crate::error::{MakxdError, Result};
use crate::types::{BleConnectionIo, ConnectionConfig, UdpWireMode};

pub(crate) trait WirePort: Send {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>>;
    fn write_coalescing_supported(&self) -> bool;
    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize>;
    fn write_all_wire(&mut self, bytes: &[u8], response_expected: bool) -> std::io::Result<()>;
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

pub(crate) struct BleWirePort {
    io: Arc<dyn BleConnectionIo>,
    pending: VecDeque<u8>,
}

impl BleWirePort {
    pub fn new(io: Arc<dyn BleConnectionIo>) -> Self {
        Self {
            io,
            pending: VecDeque::new(),
        }
    }
}

impl WirePort for BleWirePort {
    fn try_clone_wire(&self) -> Result<Box<dyn WirePort>> {
        Ok(Box::new(Self::new(Arc::clone(&self.io))))
    }

    fn write_coalescing_supported(&self) -> bool {
        false
    }

    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        if self.pending.is_empty() {
            let packet = self
                .io
                .read_notification()
                .map_err(|error| std::io::Error::other(error.to_string()))?;
            if packet.is_empty() || packet.len() > 64 {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "BLE notification length is invalid",
                ));
            }
            direct_response_normalize(&packet, &mut self.pending);
        }
        let count = bytes.len().min(self.pending.len());
        for byte in &mut bytes[..count] {
            *byte = self.pending.pop_front().unwrap();
        }
        Ok(count)
    }

    fn write_all_wire(&mut self, bytes: &[u8], _response_expected: bool) -> std::io::Result<()> {
        let wire = direct_request(bytes)?;
        if wire.len() > 64 {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "MAKXD BLE writes are limited to 64 bytes",
            ));
        }
        self.io
            .write(&wire)
            .map_err(|error| std::io::Error::other(error.to_string()))
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
