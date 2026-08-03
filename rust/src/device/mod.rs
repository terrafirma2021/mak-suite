mod buttons;
mod catch;
mod controller;
mod info;
mod keyboard;
mod locks;
mod movement;
mod stream;

use std::cell::Cell;
use std::ops::Deref;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use crossbeam_channel as channel;

use crate::error::{MakxdError, Result};
use crate::protocol::api::{ApiOpcode, ApiProtocol, ApiVerb};
use crate::protocol::parser::{self, ResponseKind};
use crate::transport::TransportHandle;
use crate::transport::serial;
use crate::types::{ConnectionConfig, ConnectionState};

// Thread-local fire-and-forget override. When set, `exec()` and
// `exec_dynamic()` skip waiting for responses. Thread-local ensures no
// cross-thread interference when a `Device` is shared via `Arc`.
thread_local! {
    static FF_OVERRIDE: Cell<bool> = const { Cell::new(false) };
}

fn is_ff_override() -> bool {
    FF_OVERRIDE.with(|c| c.get())
}

/// RAII guard that enables fire-and-forget for the current thread.
/// Restores the previous value on drop.
struct FfGuard {
    prev: bool,
}

impl FfGuard {
    fn new() -> Self {
        let prev = FF_OVERRIDE.with(|c| c.replace(true));
        Self { prev }
    }
}

impl Drop for FfGuard {
    fn drop(&mut self) {
        FF_OVERRIDE.with(|c| c.set(self.prev));
    }
}

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
    /// When true, all commands are fire-and-forget by default.
    pub fire_and_forget: bool,
    /// Encrypt all COM API commands locally. This does not change the device setting.
    pub encryption_enabled: bool,
    /// Local AES-128 key used only when `encryption_enabled` is true.
    pub encryption_key: Option<[u8; 16]>,
    /// Command encoding used after the KM-only baud-rate probe succeeds.
    pub api_protocol: ApiProtocol,
}

impl std::fmt::Debug for DeviceConfig {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("DeviceConfig")
            .field("connection", &self.connection)
            .field("command_timeout", &self.command_timeout)
            .field("reconnect", &self.reconnect)
            .field("reconnect_backoff", &self.reconnect_backoff)
            .field("fire_and_forget", &self.fire_and_forget)
            .field("encryption_enabled", &self.encryption_enabled)
            .field("encryption_key_set", &self.encryption_key.is_some())
            .field("api_protocol", &self.api_protocol)
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
            fire_and_forget: false,
            encryption_enabled: false,
            encryption_key: None,
            api_protocol: ApiProtocol::MakApi,
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
            config.api_protocol,
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
    km_echo_enabled: AtomicBool,
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
        let device = Self {
            transport,
            config,
            km_echo_enabled: AtomicBool::new(false),
        };
        if device.config.api_protocol == ApiProtocol::Km {
            device.km_echo_enabled.store(
                device.query(b"km.echo()\r\n")? == "1",
                Ordering::Release,
            );
        }
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

    /// Returns a fire-and-forget guard. While the guard is alive, all
    /// commands sent through it on the current thread skip waiting for
    /// responses. Derefs to `Device`, so every method is available.
    pub fn ff(&self) -> FireAndForget<'_> {
        FireAndForget {
            device: self,
            _guard: FfGuard::new(),
        }
    }

    /// Send raw command bytes (escape hatch for unwrapped firmware commands).
    /// The `\r\n` terminator must already be included.
    ///
    /// In fire-and-forget mode the command is sent without waiting for a
    /// response and an empty `Vec` is returned.
    pub fn send_raw(&self, cmd: &[u8]) -> Result<Vec<u8>> {
        let ff = self.is_ff();
        let resp = self
            .transport
            .send_static(cmd, ff, self.config.command_timeout)?;
        match resp {
            Some(data) => Ok(data),
            None if ff => Ok(Vec::new()),
            None => Err(MakxdError::Timeout),
        }
    }

    /// Start building a batch of commands.
    #[cfg(feature = "batch")]
    pub fn batch(&self) -> crate::batch::BatchBuilder<'_> {
        crate::batch::BatchBuilder::new(self)
    }

    // -- Internal helpers --

    pub(crate) fn is_ff(&self) -> bool {
        self.config.fire_and_forget || is_ff_override()
    }

    pub(crate) fn exec(&self, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        if self.is_ff() || !self.km_echo_enabled.load(Ordering::Acquire) {
            self.transport
                .send_static(cmd, true, self.config.command_timeout)?;
            return Ok(());
        }
        let raw = self.send_raw(cmd)?;
        match parser::classify_response(&raw) {
            ResponseKind::Executed | ResponseKind::ValueOrEcho(_) | ResponseKind::Value(_) => {
                Ok(())
            }
        }
    }

    pub(crate) fn query(&self, cmd: &[u8]) -> Result<String> {
        self.require_km()?;
        let raw = self
            .transport
            .send_static(cmd, false, self.config.command_timeout)?
            .ok_or(MakxdError::Timeout)?;
        classify_as_value(&raw)
    }

    pub(crate) fn exec_dynamic(&self, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        let fire_and_forget =
            self.is_ff() || !self.km_echo_enabled.load(Ordering::Acquire);
        let response = self.transport.send_command(
            cmd.to_vec(),
            fire_and_forget,
            self.config.command_timeout,
        )?;
        match response {
            Some(raw) => {
                parser::classify_response(&raw);
                Ok(())
            }
            None if fire_and_forget => Ok(()),
            None => Err(MakxdError::Timeout),
        }
    }

    pub(crate) fn set_km_echo(&self, enabled: bool, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        let response = self.transport.send_command(
            cmd.to_vec(),
            !enabled,
            self.config.command_timeout,
        )?;
        if enabled {
            let raw = response.ok_or(MakxdError::Timeout)?;
            parser::classify_response(&raw);
        }
        self.km_echo_enabled.store(enabled, Ordering::Release);
        Ok(())
    }

    pub(crate) fn query_dynamic(&self, cmd: &[u8]) -> Result<String> {
        self.require_km()?;
        let raw = self
            .transport
            .send_command(cmd.to_vec(), false, self.config.command_timeout)?
            .ok_or(MakxdError::Timeout)?;
        classify_as_value(&raw)
    }

    #[cfg(feature = "batch")]
    pub(crate) fn timeout(&self) -> Duration {
        self.config.command_timeout
    }

    pub(crate) fn transport(&self) -> &TransportHandle {
        &self.transport
    }

    pub(crate) fn exec_api(
        &self,
        _km_command: &[u8],
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
    ) -> Result<()> {
        if self.config.api_protocol != ApiProtocol::MakApi {
            return Err(MakxdError::Protocol(
                "typed SDK commands require MAK_API mode".into(),
            ));
        }
        if verb == ApiVerb::Set {
            self.transport.send_mak_api_no_response(
                opcode,
                verb,
                payload,
                self.config.command_timeout,
            )?;
        } else {
            self.transport.send_mak_api(
                opcode,
                verb,
                payload,
                self.config.command_timeout,
            )?;
        }
        Ok(())
    }

    pub(crate) fn query_api(
        &self,
        _km_command: &[u8],
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
    ) -> Result<Vec<u8>> {
        if self.config.api_protocol != ApiProtocol::MakApi {
            return Err(MakxdError::Protocol(
                "typed SDK commands require MAK_API mode".into(),
            ));
        }
        self.transport
            .send_mak_api(opcode, verb, payload, self.config.command_timeout)
    }

    fn require_km(&self) -> Result<()> {
        if self.config.api_protocol == ApiProtocol::Km {
            Ok(())
        } else {
            Err(MakxdError::Protocol(
                "this command has no MAK_API opcode".into(),
            ))
        }
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
            km_echo_enabled: AtomicBool::new(false),
        };
        (device, mock)
    }
}

// ===========================================================================
// FireAndForget (sync)
// ===========================================================================

/// Fire-and-forget RAII guard. While this guard is alive, all commands sent
/// through the referenced `Device` on the current thread skip waiting for
/// responses. Derefs to `Device`, so every method (including extras) is
/// available.
///
/// Query methods (`version()`, `button_state()`, etc.) always wait for a
/// response regardless of fire-and-forget mode, since they must return a value.
pub struct FireAndForget<'d> {
    device: &'d Device,
    _guard: FfGuard,
}

impl Deref for FireAndForget<'_> {
    type Target = Device;

    fn deref(&self) -> &Device {
        self.device
    }
}

// ===========================================================================
// AsyncDevice
// ===========================================================================

#[cfg(feature = "async")]
pub struct AsyncDevice {
    transport: TransportHandle,
    config: DeviceConfig,
    km_echo_enabled: AtomicBool,
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

        let device = Self {
            transport,
            config,
            km_echo_enabled: AtomicBool::new(false),
        };
        if device.config.api_protocol == ApiProtocol::Km {
            device.km_echo_enabled.store(
                device.query(b"km.echo()\r\n").await? == "1",
                Ordering::Release,
            );
        }
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

    /// Returns a fire-and-forget guard. Derefs to `AsyncDevice`.
    pub fn ff(&self) -> AsyncFireAndForget<'_> {
        AsyncFireAndForget {
            device: self,
            _guard: FfGuard::new(),
        }
    }

    /// Send raw command bytes (async escape hatch).
    ///
    /// In fire-and-forget mode the command is sent without waiting for a
    /// response and an empty `Vec` is returned.
    pub async fn send_raw(&self, cmd: &[u8]) -> Result<Vec<u8>> {
        let ff = self.is_ff();
        let resp = self
            .transport
            .send_static_async(cmd, ff, self.config.command_timeout)
            .await?;
        match resp {
            Some(data) => Ok(data),
            None if ff => Ok(Vec::new()),
            None => Err(MakxdError::Timeout),
        }
    }

    /// Start building a batch of commands.
    #[cfg(feature = "batch")]
    pub fn batch(&self) -> crate::batch::AsyncBatchBuilder<'_> {
        crate::batch::AsyncBatchBuilder::new(self)
    }

    // -- Internal async helpers --

    pub(crate) fn is_ff(&self) -> bool {
        self.config.fire_and_forget || is_ff_override()
    }

    pub(crate) async fn exec(&self, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        if self.is_ff() || !self.km_echo_enabled.load(Ordering::Acquire) {
            self.transport
                .send_static(cmd, true, self.config.command_timeout)?;
            return Ok(());
        }
        let raw = self.send_raw(cmd).await?;
        match parser::classify_response(&raw) {
            ResponseKind::Executed | ResponseKind::ValueOrEcho(_) | ResponseKind::Value(_) => {
                Ok(())
            }
        }
    }

    pub(crate) async fn query(&self, cmd: &[u8]) -> Result<String> {
        self.require_km()?;
        let raw = self
            .transport
            .send_static_async(cmd, false, self.config.command_timeout)
            .await?
            .ok_or(MakxdError::Timeout)?;
        classify_as_value(&raw)
    }

    pub(crate) async fn exec_dynamic(&self, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        let fire_and_forget =
            self.is_ff() || !self.km_echo_enabled.load(Ordering::Acquire);
        let response = self
            .transport
            .send_command_async(
                cmd.to_vec(),
                fire_and_forget,
                self.config.command_timeout,
            )
            .await?;
        match response {
            Some(raw) => {
                parser::classify_response(&raw);
                Ok(())
            }
            None if fire_and_forget => Ok(()),
            None => Err(MakxdError::Timeout),
        }
    }

    pub(crate) async fn set_km_echo(&self, enabled: bool, cmd: &[u8]) -> Result<()> {
        self.require_km()?;
        let response = self
            .transport
            .send_command_async(
                cmd.to_vec(),
                !enabled,
                self.config.command_timeout,
            )
            .await?;
        if enabled {
            let raw = response.ok_or(MakxdError::Timeout)?;
            parser::classify_response(&raw);
        }
        self.km_echo_enabled.store(enabled, Ordering::Release);
        Ok(())
    }

    pub(crate) async fn query_dynamic(&self, cmd: &[u8]) -> Result<String> {
        self.require_km()?;
        let raw = self
            .transport
            .send_command_async(cmd.to_vec(), false, self.config.command_timeout)
            .await?
            .ok_or(MakxdError::Timeout)?;
        classify_as_value(&raw)
    }

    pub(crate) fn transport(&self) -> &TransportHandle {
        &self.transport
    }

    pub(crate) async fn exec_api(
        &self,
        _km_command: &[u8],
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
    ) -> Result<()> {
        if self.config.api_protocol != ApiProtocol::MakApi {
            return Err(MakxdError::Protocol(
                "typed SDK commands require MAK_API mode".into(),
            ));
        }
        if verb == ApiVerb::Set {
            self.transport
                .send_mak_api_no_response_async(
                    opcode,
                    verb,
                    payload,
                    self.config.command_timeout,
                )
                .await?;
        } else {
            self.transport
                .send_mak_api_async(
                    opcode,
                    verb,
                    payload,
                    self.config.command_timeout,
                )
                .await?;
        }
        Ok(())
    }

    pub(crate) async fn query_api(
        &self,
        _km_command: &[u8],
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
    ) -> Result<Vec<u8>> {
        if self.config.api_protocol != ApiProtocol::MakApi {
            return Err(MakxdError::Protocol(
                "typed SDK commands require MAK_API mode".into(),
            ));
        }
        self.transport
            .send_mak_api_async(opcode, verb, payload, self.config.command_timeout)
            .await
    }

    fn require_km(&self) -> Result<()> {
        if self.config.api_protocol == ApiProtocol::Km {
            Ok(())
        } else {
            Err(MakxdError::Protocol(
                "this command has no MAK_API opcode".into(),
            ))
        }
    }

    #[cfg(feature = "batch")]
    pub(crate) fn timeout(&self) -> std::time::Duration {
        self.config.command_timeout
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
            km_echo_enabled: AtomicBool::new(false),
        };
        (device, mock)
    }
}

// ===========================================================================
// AsyncFireAndForget
// ===========================================================================

#[cfg(feature = "async")]
pub struct AsyncFireAndForget<'d> {
    device: &'d AsyncDevice,
    _guard: FfGuard,
}

#[cfg(feature = "async")]
impl Deref for AsyncFireAndForget<'_> {
    type Target = AsyncDevice;

    fn deref(&self) -> &AsyncDevice {
        self.device
    }
}

// ===========================================================================
// Shared helpers
// ===========================================================================

fn classify_as_value(raw: &[u8]) -> Result<String> {
    match parser::classify_response(raw) {
        ResponseKind::Value(v) => Ok(v),
        ResponseKind::ValueOrEcho(v) => Ok(v),
        ResponseKind::Executed => Err(MakxdError::Protocol(
            "expected a value but got EXECUTED".into(),
        )),
    }
}
