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
    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize>;
    fn write_all_wire(&mut self, bytes: &[u8]) -> std::io::Result<()>;
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

    fn read_wire(&mut self, bytes: &mut [u8]) -> std::io::Result<usize> {
        self.port.read(bytes)
    }

    fn write_all_wire(&mut self, bytes: &[u8]) -> std::io::Result<()> {
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
            let expected = self
                .shared
                .transactions
                .lock()
                .unwrap()
                .pop_front()
                .ok_or_else(|| {
                    std::io::Error::new(
                        std::io::ErrorKind::InvalidData,
                        "raw UDP response has no pending transaction",
                    )
                })?;
            if body[1..9] != expected {
                return Err(std::io::Error::new(
                    std::io::ErrorKind::InvalidData,
                    "raw UDP transaction does not match",
                ));
            }
            body = &body[9..];
        }
        network_response_normalize(body, &mut self.pending);
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

    fn write_all_wire(&mut self, bytes: &[u8]) -> std::io::Result<()> {
        let mut wire = network_request(bytes)?;
        if self.shared.mode == UdpWireMode::Raw && wire.first() != Some(&0x03) {
            let mut transaction = [0u8; 8];
            getrandom::fill(&mut transaction)
                .map_err(|error| std::io::Error::other(error.to_string()))?;
            self.shared
                .transactions
                .lock()
                .unwrap()
                .push_back(transaction);
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
            network_response_normalize(&packet, &mut self.pending);
        }
        let count = bytes.len().min(self.pending.len());
        for byte in &mut bytes[..count] {
            *byte = self.pending.pop_front().unwrap();
        }
        Ok(count)
    }

    fn write_all_wire(&mut self, bytes: &[u8]) -> std::io::Result<()> {
        let wire = network_request(bytes)?;
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

fn network_request(bytes: &[u8]) -> std::io::Result<Vec<u8>> {
    if bytes.len() >= 5 && bytes[..2] == [0xde, 0xad] {
        let offset = if bytes[4] == 0 { 5 } else { 4 };
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

fn network_response_normalize(bytes: &[u8], output: &mut VecDeque<u8>) {
    match bytes.first() {
        Some(0x00) => {
            output.extend([0xde, 0xad]);
            output.extend((bytes.len() as u16).to_le_bytes());
            output.extend(bytes);
        }
        Some(0x03) => {
            output.extend([0xde, 0xad]);
            output.extend(((bytes.len() - 1) as u16).to_le_bytes());
            output.extend(bytes);
        }
        _ => output.extend(bytes),
    }
}

pub(crate) fn establish_network_connection(
    config: &ConnectionConfig,
    encryption: Option<&TransportEncryption>,
) -> Result<(Box<dyn WirePort>, String)> {
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
    let proof = (|| -> Result<String> {
        let command = b"km.version()\r\n";
        let (wire, expected_nonce) = if let Some(encryption) = encryption {
            let (frame, nonce) = encryption.encode_command(command)?;
            (frame, Some(nonce))
        } else {
            (command.to_vec(), None)
        };
        port.write_all_wire(&wire)?;
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
                    if response.windows(4).any(|window| window == b">>> ") {
                        break;
                    }
                }
                Ok(_) => {}
                Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {}
                Err(error) => return Err(error.into()),
            }
        }
        if !String::from_utf8_lossy(&response)
            .lines()
            .map(str::trim)
            .any(|line| line == "km.MAKXD")
        {
            return Err(MakxdError::Protocol(
                "km.version() did not return km.MAKXD".into(),
            ));
        }
        Ok("km.MAKXD".into())
    })();
    match proof {
        Ok(version) => Ok((port, version)),
        Err(error) => {
            if let ConnectionConfig::Ble { io, .. } = config {
                io.close();
            }
            Err(error)
        }
    }
}
