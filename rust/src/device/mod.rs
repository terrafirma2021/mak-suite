mod buttons;
mod controller;
mod info;
mod keyboard;
mod movement;
mod stream;

use std::time::Duration;

use crossbeam_channel as channel;

use crate::error::{MakxdError, Result};
use crate::protocol::api::ApiOpcode;
use crate::transport::TransportHandle;
use crate::transport::serial;
use crate::types::{ConnectionConfig, ConnectionState};

/// Default command timeout.
const DEFAULT_TIMEOUT: Duration = Duration::from_millis(500);

/// Configuration for connecting to a MAKXD device.
#[derive(Clone)]
pub struct DeviceConfig {
    /// Exact COM, UDP, or BLE carrier owned by this client.
    pub connection: ConnectionConfig,
    /// Timeout for each command response.
    pub command_timeout: Duration,
    /// Enable automatic reconnection on disconnect.
    pub reconnect: bool,
    /// Initial reconnection backoff delay.
    pub reconnect_backoff: Duration,
    /// Encrypt all COM API commands locally. This does not change the device setting.
    pub encryption_enabled: bool,
    /// Local AES-128 key used only when `encryption_enabled` is true.
    pub encryption_key: Option<[u8; 16]>,
}

impl std::fmt::Debug for DeviceConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DeviceConfig")
            .field("connection", &self.connection)
            .field("command_timeout", &self.command_timeout)
            .field("reconnect", &self.reconnect)
            .field("reconnect_backoff", &self.reconnect_backoff)
            .field("encryption_enabled", &self.encryption_enabled)
            .field("encryption_key_set", &self.encryption_key.is_some())
            .finish()
    }
}

impl Default for DeviceConfig {
    fn default() -> Self {
        Self {
            connection: ConnectionConfig::com(None),
            command_timeout: DEFAULT_TIMEOUT,
            reconnect: true,
            reconnect_backoff: Duration::from_millis(100),
            encryption_enabled: false,
            encryption_key: None,
        }
    }
}

fn connect_transport(config: &DeviceConfig) -> Result<TransportHandle> {
    if matches!(&config.connection, ConnectionConfig::Ble { .. })
        && (config.encryption_enabled || config.encryption_key.is_some())
    {
        return Err(MakxdError::Protocol(
            "BLE does not use MAKXD AES transport encryption".into(),
        ));
    }
    let connections = match &config.connection {
        ConnectionConfig::Com { port: Some(_) } => {
            vec![config.connection.clone()]
        }
        ConnectionConfig::Com { port: None } => serial::find_ports()?
            .into_iter()
            .map(|port| ConnectionConfig::com(Some(port)))
            .collect(),
        _ => vec![config.connection.clone()],
    };
    let mut last_error = None;

    for connection in connections {
        let transport_encryption = crate::transport::encryption::TransportEncryption::from_config(
            config.encryption_enabled,
            config.encryption_key,
        )?;
        match TransportHandle::connect(
            connection,
            config.reconnect,
            config.reconnect_backoff,
            transport_encryption,
        ) {
            Ok(transport) => return Ok(transport),
            Err(error) => last_error = Some(error),
        }
    }

    Err(last_error.unwrap_or(MakxdError::NotFound))
}

// ===========================================================================
// Device (sync)
// ===========================================================================

/// An open connection to a MAKXD device.
///
/// All methods take `&self` — the underlying I/O goes through channels.
/// `Device` is `Send + Sync` and can be wrapped in `Arc` for shared use.
pub struct Device {
    transport: TransportHandle,
    config: DeviceConfig,
}

impl std::fmt::Debug for Device {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Device")
            .field("port", &self.transport.port_name())
            .field("connected", &self.transport.is_connected())
            .finish()
    }
}

// Compile-time assertions that Device is Send + Sync.
#[allow(dead_code)]
const _: () = {
    fn assert_send_sync<T: Send + Sync>() {}
    fn _assertions() {
        assert_send_sync::<Device>();
    }
};

impl Device {
    /// Find and connect to the first available MAKXD device.
    pub fn connect() -> Result<Self> {
        Self::with_config(DeviceConfig::default())
    }

    /// Connect to a specific port.
    pub fn connect_port(port: &str) -> Result<Self> {
        Self::with_config(DeviceConfig {
            connection: ConnectionConfig::com(Some(port.to_string())),
            ..Default::default()
        })
    }

    /// Connect with a custom configuration.
    pub fn with_config(config: DeviceConfig) -> Result<Self> {
        let transport = connect_transport(&config)?;
        let device = Self { transport, config };
        Ok(device)
    }

    /// Disconnect from the device, shutting down all threads.
    pub fn disconnect(&self) {
        self.transport.shutdown();
    }

    /// Check if the device is currently connected.
    pub fn is_connected(&self) -> bool {
        self.transport.is_connected()
    }

    /// Get the port name this device is connected to.
    pub fn port_name(&self) -> String {
        self.transport.port_name()
    }

    /// Get the configuration this device was created with.
    pub fn config(&self) -> &DeviceConfig {
        &self.config
    }

    /// Subscribe to connection state changes.
    pub fn connection_events(&self) -> channel::Receiver<ConnectionState> {
        self.transport.subscribe_state()
    }

    // -- Internal helpers --

    pub(crate) fn transport(&self) -> &TransportHandle {
        &self.transport
    }

    pub(crate) fn write_api(&self, opcode: ApiOpcode, payload: &[u8]) -> Result<()> {
        self.transport
            .send_mak_api_no_response(opcode, payload, self.config.command_timeout)
    }

    pub(crate) fn query_api(&self, opcode: ApiOpcode, payload: &[u8]) -> Result<Vec<u8>> {
        self.transport
            .send_mak_api(opcode, payload, self.config.command_timeout)
    }
}

#[cfg(feature = "mock")]
impl Device {
    /// Create a Device backed by a mock transport (for testing).
    pub fn mock() -> (Self, std::sync::Arc<crate::transport::mock::MockTransport>) {
        let (transport, mock) = TransportHandle::from_mock();
        let device = Self {
            transport,
            config: DeviceConfig::default(),
        };
        (device, mock)
    }
}

// ===========================================================================
// AsyncDevice
// ===========================================================================

#[cfg(feature = "async")]
pub struct AsyncDevice {
    transport: TransportHandle,
    config: DeviceConfig,
}

// Compile-time assertions that AsyncDevice is Send + Sync.
#[cfg(feature = "async")]
#[allow(dead_code)]
const _: () = {
    fn assert_send_sync<T: Send + Sync>() {}
    fn _assertions() {
        assert_send_sync::<AsyncDevice>();
    }
};

#[cfg(feature = "async")]
impl std::fmt::Debug for AsyncDevice {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AsyncDevice")
            .field("port", &self.transport.port_name())
            .field("connected", &self.transport.is_connected())
            .finish()
    }
}

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Find and connect to the first available MAKXD device.
    pub async fn connect() -> Result<Self> {
        Self::with_config(DeviceConfig::default()).await
    }

    /// Connect to a specific port.
    pub async fn connect_port(port: &str) -> Result<Self> {
        Self::with_config(DeviceConfig {
            connection: ConnectionConfig::com(Some(port.to_string())),
            ..Default::default()
        })
        .await
    }

    /// Connect with a custom configuration.
    pub async fn with_config(config: DeviceConfig) -> Result<Self> {
        let cfg = config.clone();
        let (transport, config) = tokio::task::spawn_blocking(move || -> Result<_> {
            let transport = connect_transport(&cfg)?;
            Ok((transport, cfg))
        })
        .await
        .map_err(|e| MakxdError::Protocol(format!("join error: {}", e)))??;

        let device = Self { transport, config };
        Ok(device)
    }

    /// Disconnect from the device, shutting down all threads.
    pub fn disconnect(&self) {
        self.transport.shutdown();
    }

    /// Check if the device is currently connected.
    pub fn is_connected(&self) -> bool {
        self.transport.is_connected()
    }

    /// Get the port name this device is connected to.
    pub fn port_name(&self) -> String {
        self.transport.port_name()
    }

    /// Get the configuration this device was created with.
    pub fn config(&self) -> &DeviceConfig {
        &self.config
    }

    /// Subscribe to connection state changes.
    pub fn connection_events(&self) -> channel::Receiver<ConnectionState> {
        self.transport.subscribe_state()
    }

    // -- Internal async helpers --

    pub(crate) fn transport(&self) -> &TransportHandle {
        &self.transport
    }

    pub(crate) async fn write_api(&self, opcode: ApiOpcode, payload: &[u8]) -> Result<()> {
        self.transport
            .send_mak_api_no_response_async(opcode, payload, self.config.command_timeout)
            .await
    }

    pub(crate) async fn query_api(&self, opcode: ApiOpcode, payload: &[u8]) -> Result<Vec<u8>> {
        self.transport
            .send_mak_api_async(opcode, payload, self.config.command_timeout)
            .await
    }
}

#[cfg(all(feature = "async", feature = "mock"))]
impl AsyncDevice {
    /// Create an AsyncDevice backed by a mock transport (for testing).
    pub fn mock() -> (Self, std::sync::Arc<crate::transport::mock::MockTransport>) {
        let (transport, mock) = TransportHandle::from_mock();
        let device = Self {
            transport,
            config: DeviceConfig::default(),
        };
        (device, mock)
    }
}
