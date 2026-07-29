pub(crate) mod encryption;
#[cfg(feature = "mock")]
pub mod mock;
pub(crate) mod monitor;
pub(crate) mod reader;
pub(crate) mod serial;
pub(crate) mod writer;
pub(crate) mod wire;

use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

use crossbeam_channel as channel;

use crate::error::{MakxdError, Result};
use crate::protocol::api::{ApiOpcode, ApiProtocol, ApiVerb, mak_api_command};
use crate::types::{
    ButtonMask, CatchEvent, ConnectionConfig, ConnectionState,
};

use self::encryption::TransportEncryption;
use self::reader::ReaderSignal;
use self::writer::WritePayload;

pub(crate) struct PendingResponse {
    pub response_tx: channel::Sender<Vec<u8>>,
    pub expected_nonce: Option<[u8; 12]>,
    pub expected_opcode: Option<u8>,
}

// ---------------------------------------------------------------------------
// TransportHandle — public(crate) API surface
// ---------------------------------------------------------------------------

/// Handle to the transport layer. Cheaply cloneable (wraps Arc).
#[derive(Clone)]
pub(crate) struct TransportHandle {
    inner: Arc<TransportInner>,
}

/// Shared interior — one per connection, referenced by handle + monitor.
pub(crate) struct TransportInner {
    pub conn_state: AtomicU8,
    pub shutdown: AtomicBool,
    pub port_name: Mutex<String>,
    pub api_protocol: AtomicU8,

    // Channel for sending commands to the writer thread.
    // Wrapped in Mutex<Option<>> so shutdown() can drop the sender to unblock
    // the writer thread (which blocks on recv).
    write_tx: Mutex<Option<channel::Sender<WritePayload>>>,
    // Receiver clone for spawning new writer threads on reconnect.
    write_rx: channel::Receiver<WritePayload>,

    // Pending response oneshots: writer pushes, reader pops. Shared Arc.
    pending_responses: Arc<Mutex<VecDeque<PendingResponse>>>,
    pub transport_encryption: Option<Arc<TransportEncryption>>,

    // Button event subscribers.
    button_subs: Arc<Mutex<Vec<channel::Sender<ButtonMask>>>>,

    // Catch event subscribers.
    catch_subs: Arc<Mutex<Vec<channel::Sender<CatchEvent>>>>,

    // Connection state subscribers.
    pub state_subs: Mutex<Vec<channel::Sender<ConnectionState>>>,

    // Reader signal for disconnect notification (replaced on reconnect).
    pub reader_signal: Mutex<Option<Arc<ReaderSignal>>>,

    // Thread handles (for join on shutdown).
    threads: Mutex<Vec<JoinHandle<()>>>,

    // Reconnection backoff (initial value; doubles up to 5s).
    pub reconnect_backoff: Duration,
    pub connection: ConnectionConfig,
}

impl TransportInner {
    /// Spawn fresh reader + writer threads for the given port.
    /// Used both on initial connect and on reconnect.
    pub fn spawn_io_threads(&self, port: Box<dyn wire::WirePort>) -> Result<()> {
        let reader_port = port.try_clone_wire()?;
        let writer_port = port;

        let signal = Arc::new(ReaderSignal::new());

        // Spawn writer.
        let write_rx = self.write_rx.clone();
        let pending = Arc::clone(&self.pending_responses);
        let writer_handle = std::thread::Builder::new()
            .name("makxd-writer".into())
            .spawn(move || {
                writer::writer_thread(writer_port, write_rx, pending);
            })
            .map_err(MakxdError::Io)?;

        // Spawn reader.
        let reader_pending = Arc::clone(&self.pending_responses);
        let reader_buttons = Arc::clone(&self.button_subs);
        let reader_catch = Arc::clone(&self.catch_subs);
        let reader_signal = Arc::clone(&signal);
        let reader_encryption = self.transport_encryption.clone();
        let reader_api_protocol = self.api_protocol.load(Ordering::Acquire);
        let reader_handle = std::thread::Builder::new()
            .name("makxd-reader".into())
            .spawn(move || {
                reader::reader_thread(
                    reader_port,
                    reader_pending,
                    reader_buttons,
                    reader_catch,
                    reader_signal,
                    reader_encryption,
                    reader_api_protocol,
                );
            })
            .map_err(MakxdError::Io)?;

        // Store signal for monitor to wait on.
        *self.reader_signal.lock().unwrap() = Some(signal);

        // Drain old thread handles (they've exited by now on reconnect —
        // old reader exits on port read error, old writer exits on port
        // write error). Join them to reclaim resources.
        let mut threads = self.threads.lock().unwrap();
        let old: Vec<_> = threads.drain(..).collect();
        drop(threads);
        for handle in old {
            let _ = handle.join();
        }

        // Store new thread handles.
        let mut threads = self.threads.lock().unwrap();
        threads.push(reader_handle);
        threads.push(writer_handle);

        Ok(())
    }

    /// Send a payload through the write channel. Returns Disconnected if the
    /// channel has been shut down.
    fn send_payload(&self, payload: WritePayload) -> Result<()> {
        let guard = self.write_tx.lock().unwrap();
        if let Some(tx) = guard.as_ref() {
            tx.send(payload).map_err(|_| MakxdError::Disconnected)
        } else {
            Err(MakxdError::Disconnected)
        }
    }

    pub fn notify_state(&self, state: ConnectionState) {
        let mut subs = self.state_subs.lock().unwrap();
        subs.retain(|sub| sub.send(state).is_ok());
    }
}

impl TransportHandle {
    /// Connect to the device and spawn I/O threads.
    pub fn connect(
        connection: ConnectionConfig,
        reconnect: bool,
        reconnect_backoff: Duration,
        transport_encryption: Option<TransportEncryption>,
        api_protocol: ApiProtocol,
    ) -> Result<Self> {
        let transport_encryption = transport_encryption.map(Arc::new);
        let (port, _version, port_name) = match &connection {
            ConnectionConfig::Com { port: Some(port_name) } => {
                let (port, version) = serial::establish_connection(
                    port_name,
                    transport_encryption.as_deref(),
                )?;
                (port, version, port_name.clone())
            }
            ConnectionConfig::Com { port: None } => {
                return Err(MakxdError::Protocol(
                    "resolved COM port is required".into(),
                ));
            }
            ConnectionConfig::Udp { host, port: udp_port, .. } => {
                let (port, version) = wire::establish_network_connection(
                    &connection,
                    transport_encryption.as_deref(),
                )?;
                (port, version, format!("udp://{host}:{udp_port}"))
            }
            ConnectionConfig::Ble { address, .. } => {
                let (port, version) = wire::establish_network_connection(
                    &connection,
                    transport_encryption.as_deref(),
                )?;
                (port, version, format!("ble://{address}"))
            }
        };
        let (write_tx, write_rx) = channel::unbounded::<WritePayload>();

        let inner = Arc::new(TransportInner {
            conn_state: AtomicU8::new(ConnectionState::Connected as u8),
            shutdown: AtomicBool::new(false),
            port_name: Mutex::new(port_name),
            api_protocol: AtomicU8::new(api_protocol as u8),
            write_tx: Mutex::new(Some(write_tx)),
            write_rx,
            pending_responses: Arc::new(Mutex::new(VecDeque::new())),
            transport_encryption,
            button_subs: Arc::new(Mutex::new(Vec::new())),
            catch_subs: Arc::new(Mutex::new(Vec::new())),
            state_subs: Mutex::new(Vec::new()),
            reader_signal: Mutex::new(None),
            threads: Mutex::new(Vec::new()),
            reconnect_backoff,
            connection,
        });

        inner.spawn_io_threads(port)?;

        // Spawn monitor if reconnection is enabled.
        if reconnect {
            let monitor_inner = Arc::clone(&inner);
            std::thread::Builder::new()
                .name("makxd-monitor".into())
                .spawn(move || {
                    monitor::monitor_thread(monitor_inner);
                })
                .map_err(MakxdError::Io)?;
        }

        Ok(Self { inner })
    }

    /// Create a handle wrapping a mock transport.
    /// Spawns a mock worker thread that routes commands through MockTransport.
    #[cfg(feature = "mock")]
    pub fn from_mock() -> (Self, Arc<mock::MockTransport>) {
        let (write_tx, write_rx) = channel::unbounded::<WritePayload>();
        let mock_transport = Arc::new(mock::MockTransport::new());
        let button_subs = Arc::new(Mutex::new(Vec::new()));

        let catch_subs = Arc::new(Mutex::new(Vec::new()));

        let inner = Arc::new(TransportInner {
            conn_state: AtomicU8::new(ConnectionState::Connected as u8),
            shutdown: AtomicBool::new(false),
            port_name: Mutex::new("mock".into()),
            api_protocol: AtomicU8::new(ApiProtocol::Km as u8),
            write_tx: Mutex::new(Some(write_tx)),
            write_rx: write_rx.clone(),
            pending_responses: Arc::new(Mutex::new(VecDeque::new())),
            transport_encryption: None,
            button_subs: Arc::clone(&button_subs),
            catch_subs: Arc::clone(&catch_subs),
            state_subs: Mutex::new(Vec::new()),
            reader_signal: Mutex::new(None),
            threads: Mutex::new(Vec::new()),
            reconnect_backoff: Duration::from_millis(100),
            connection: ConnectionConfig::com(None),
        });

        // Spawn mock worker thread that processes commands through MockTransport.
        let mock_clone = Arc::clone(&mock_transport);
        let subs_clone = Arc::clone(&button_subs);
        let catch_clone = Arc::clone(&catch_subs);
        let handle = std::thread::Builder::new()
            .name("makxd-mock-worker".into())
            .spawn(move || {
                mock::mock_worker(write_rx, mock_clone, subs_clone, catch_clone);
            })
            .unwrap();

        inner.threads.lock().unwrap().push(handle);

        (Self { inner }, mock_transport)
    }

    /// Send a command. Returns the response bytes if not fire-and-forget.
    pub fn send_command(
        &self,
        data: Vec<u8>,
        fire_and_forget: bool,
        timeout: Duration,
    ) -> Result<Option<Vec<u8>>> {
        if !self.is_connected() {
            return Err(MakxdError::Disconnected);
        }

        let (data, expected_nonce) =
            if let Some(encryption) = self.inner.transport_encryption.as_deref() {
                let (frame, nonce) = encryption.encode_command(&data)?;
                (frame, Some(nonce))
            } else {
                (data, None)
            };

        if fire_and_forget {
            self.inner.send_payload(WritePayload {
                data,
                response_tx: None,
                expected_nonce,
                expected_opcode: None,
            })?;
            Ok(None)
        } else {
            let (tx, rx) = channel::bounded(1);
            self.inner.send_payload(WritePayload {
                data,
                response_tx: Some(tx),
                expected_nonce,
                expected_opcode: None,
            })?;
            match rx.recv_timeout(timeout) {
                Ok(response) => Ok(Some(response)),
                Err(channel::RecvTimeoutError::Timeout) => Err(MakxdError::Timeout),
                Err(channel::RecvTimeoutError::Disconnected) => Err(MakxdError::Disconnected),
            }
        }
    }

    /// Async version — offloads the blocking recv to spawn_blocking.
    #[cfg(feature = "async")]
    pub async fn send_command_async(
        &self,
        data: Vec<u8>,
        fire_and_forget: bool,
        timeout: Duration,
    ) -> Result<Option<Vec<u8>>> {
        if !self.is_connected() {
            return Err(MakxdError::Disconnected);
        }

        let (data, expected_nonce) =
            if let Some(encryption) = self.inner.transport_encryption.as_deref() {
                let (frame, nonce) = encryption.encode_command(&data)?;
                (frame, Some(nonce))
            } else {
                (data, None)
            };

        if fire_and_forget {
            self.inner.send_payload(WritePayload {
                data,
                response_tx: None,
                expected_nonce,
                expected_opcode: None,
            })?;
            Ok(None)
        } else {
            let (tx, rx) = channel::bounded(1);
            self.inner.send_payload(WritePayload {
                data,
                response_tx: Some(tx),
                expected_nonce,
                expected_opcode: None,
            })?;

            tokio::task::spawn_blocking(move || match rx.recv_timeout(timeout) {
                Ok(response) => Ok(Some(response)),
                Err(channel::RecvTimeoutError::Timeout) => Err(MakxdError::Timeout),
                Err(channel::RecvTimeoutError::Disconnected) => Err(MakxdError::Disconnected),
            })
            .await
            .map_err(|e| MakxdError::Protocol(format!("tokio join error: {}", e)))?
        }
    }

    pub fn send_mak_api(
        &self,
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
        timeout: Duration,
    ) -> Result<Vec<u8>> {
        if !self.is_connected() {
            return Err(MakxdError::Disconnected);
        }
        let identified = {
            let mut bytes = Vec::with_capacity(3 + payload.len());
            bytes.extend_from_slice(&[0, opcode as u8, verb as u8]);
            bytes.extend_from_slice(payload);
            bytes
        };
        let (data, expected_nonce) =
            if let Some(encryption) = self.inner.transport_encryption.as_deref() {
                let (frame, nonce) = encryption.encode_command(&identified)?;
                (frame, Some(nonce))
            } else {
                (mak_api_command(opcode, verb, payload)?, None)
            };
        let (tx, rx) = channel::bounded(1);
        self.inner.send_payload(WritePayload {
            data,
            response_tx: Some(tx),
            expected_nonce,
            expected_opcode: Some(opcode as u8),
        })?;
        match rx.recv_timeout(timeout) {
            Ok(response) if response.len() >= 3 && response[2] == 1 => Ok(response[3..].to_vec()),
            Ok(response) if response.len() >= 3 => Err(MakxdError::Protocol(format!(
                "MAK_API opcode 0x{:02x} returned status 0x{:02x}",
                response[1], response[2]
            ))),
            Ok(_) => Err(MakxdError::Protocol(
                "MAK_API response is malformed".into(),
            )),
            Err(channel::RecvTimeoutError::Timeout) => Err(MakxdError::Timeout),
            Err(channel::RecvTimeoutError::Disconnected) => Err(MakxdError::Disconnected),
        }
    }

    #[cfg(feature = "async")]
    pub async fn send_mak_api_async(
        &self,
        opcode: ApiOpcode,
        verb: ApiVerb,
        payload: &[u8],
        timeout: Duration,
    ) -> Result<Vec<u8>> {
        let handle = self.clone();
        let payload = payload.to_vec();
        tokio::task::spawn_blocking(move || handle.send_mak_api(opcode, verb, &payload, timeout))
            .await
            .map_err(|error| MakxdError::Protocol(format!("tokio join error: {error}")))?
    }

    /// Convenience: send pre-built command bytes.
    pub fn send_static(
        &self,
        cmd: &[u8],
        fire_and_forget: bool,
        timeout: Duration,
    ) -> Result<Option<Vec<u8>>> {
        self.send_command(cmd.to_vec(), fire_and_forget, timeout)
    }

    /// Async convenience: send pre-built command bytes.
    #[cfg(feature = "async")]
    pub async fn send_static_async(
        &self,
        cmd: &[u8],
        fire_and_forget: bool,
        timeout: Duration,
    ) -> Result<Option<Vec<u8>>> {
        self.send_command_async(cmd.to_vec(), fire_and_forget, timeout)
            .await
    }

    pub fn is_connected(&self) -> bool {
        self.connection_state() == ConnectionState::Connected
    }

    pub fn connection_state(&self) -> ConnectionState {
        ConnectionState::from_u8(self.inner.conn_state.load(Ordering::Acquire))
    }

    /// Get the port name this transport is connected to.
    pub fn port_name(&self) -> String {
        self.inner.port_name.lock().unwrap().clone()
    }

    /// Subscribe to connection state changes.
    pub fn subscribe_state(&self) -> channel::Receiver<ConnectionState> {
        let (tx, rx) = channel::unbounded();
        self.inner.state_subs.lock().unwrap().push(tx);
        rx
    }

    /// Subscribe to button events from the device stream.
    pub fn subscribe_buttons(&self) -> channel::Receiver<ButtonMask> {
        let (tx, rx) = channel::unbounded();
        self.inner.button_subs.lock().unwrap().push(tx);
        rx
    }

    /// Subscribe to catch events (per-button press/release stream).
    pub fn subscribe_catch(&self) -> channel::Receiver<CatchEvent> {
        let (tx, rx) = channel::unbounded();
        self.inner.catch_subs.lock().unwrap().push(tx);
        rx
    }

    pub fn shutdown(&self) {
        self.inner.shutdown.store(true, Ordering::Release);
        self.inner
            .conn_state
            .store(ConnectionState::Disconnected as u8, Ordering::Release);

        // Drop the write channel sender to unblock the writer thread.
        *self.inner.write_tx.lock().unwrap() = None;

        // Signal reader thread to exit (it checks this on timeout).
        // Wake monitor if waiting.
        if let Some(sig) = self.inner.reader_signal.lock().unwrap().as_ref() {
            sig.alive.store(false, Ordering::Release);
            let (lock, cvar) = &sig.disconnect_notify;
            let mut d = lock.lock().unwrap();
            *d = true;
            cvar.notify_all();
        }

        // Note: we do NOT join threads here — the reader thread will exit
        // on its next port read timeout (max 200ms). The writer
        // thread will exit because we dropped write_tx above. The reader
        // thread will exit on its next port read error or timeout. Thread
        // handles are cleaned up when TransportInner is dropped.
    }
}

impl Drop for TransportInner {
    fn drop(&mut self) {
        self.shutdown.store(true, Ordering::Release);
        // Drop the write channel sender.
        *self.write_tx.lock().unwrap() = None;
        if let Some(sig) = self.reader_signal.lock().unwrap().as_ref() {
            let (lock, cvar) = &sig.disconnect_notify;
            let mut d = lock.lock().unwrap();
            *d = true;
            cvar.notify_all();
        }
        if let ConnectionConfig::Ble { io, .. } = &self.connection {
            io.close();
        }
    }
}
