use std::sync::Arc;

use crate::error::Result;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UdpWireMode {
    Host,
    Raw,
}

pub trait BleConnectionIo: Send + Sync {
    fn connect(&self, address: &str) -> Result<()>;
    fn write(&self, bytes: &[u8]) -> Result<()>;
    fn read_notification(&self) -> Result<Vec<u8>>;
    fn close(&self);
}

#[derive(Clone)]
pub enum ConnectionConfig {
    Com {
        port: Option<String>,
    },
    Udp {
        host: String,
        port: u16,
        mode: UdpWireMode,
        bind_address: Option<String>,
        interface: Option<String>,
        vlan_id: Option<u16>,
    },
    Ble {
        address: String,
        io: Arc<dyn BleConnectionIo>,
    },
}

impl std::fmt::Debug for ConnectionConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Com { port } => f.debug_struct("Com").field("port", port).finish(),
            Self::Udp {
                host,
                port,
                mode,
                bind_address,
                interface,
                vlan_id,
            } => f
                .debug_struct("Udp")
                .field("host", host)
                .field("port", port)
                .field("mode", mode)
                .field("bind_address", bind_address)
                .field("interface", interface)
                .field("vlan_id", vlan_id)
                .finish(),
            Self::Ble { address, .. } => f.debug_struct("Ble").field("address", address).finish(),
        }
    }
}

impl ConnectionConfig {
    pub fn com(port: Option<String>) -> Self {
        Self::Com { port }
    }

    pub fn udp(
        host: impl Into<String>,
        port: u16,
        mode: UdpWireMode,
        bind_address: Option<String>,
        interface: Option<String>,
        vlan_id: Option<u16>,
    ) -> Result<Self> {
        let host = host.into();
        if host.is_empty() || port == 0 {
            return Err(crate::error::MakxdError::Protocol(
                "UDP host and port are required".into(),
            ));
        }
        if let Some(vlan) = vlan_id {
            if !(1..=4094).contains(&vlan) {
                return Err(crate::error::MakxdError::Protocol(
                    "VLAN ID must be in 1..=4094".into(),
                ));
            }
            if bind_address.is_none() && interface.is_none() {
                return Err(crate::error::MakxdError::Protocol(
                    "VLAN requires a VLAN interface or bind address".into(),
                ));
            }
        }
        Ok(Self::Udp {
            host,
            port,
            mode,
            bind_address,
            interface,
            vlan_id,
        })
    }

    pub fn ble(address: impl Into<String>, io: Arc<dyn BleConnectionIo>) -> Result<Self> {
        let address = address.into();
        if address.is_empty() {
            return Err(crate::error::MakxdError::Protocol(
                "BLE address is required".into(),
            ));
        }
        Ok(Self::Ble { address, io })
    }
}
