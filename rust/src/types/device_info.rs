/// Information about a connected device.
#[derive(Debug, Clone)]
pub struct DeviceInfo {
    pub port: String,
    pub firmware: String,
}

impl std::fmt::Display for DeviceInfo {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{} (firmware: {})", self.port, self.firmware)
    }
}

/// Connection state, observable via `Device::connection_events()`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ConnectionState {
    Disconnected = 0,
    Connecting = 1,
    Connected = 2,
}

impl ConnectionState {
    pub(crate) fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Disconnected,
            1 => Self::Connecting,
            2 => Self::Connected,
            _ => Self::Disconnected,
        }
    }
}
