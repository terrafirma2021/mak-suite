use std::collections::HashMap;
use std::sync::{Arc, Mutex};

use crossbeam_channel as channel;

use crate::types::{ButtonMask, CatchEvent};

use super::writer::WritePayload;

/// A mock transport for testing without hardware.
///
/// Configurable response map: command bytes → response bytes.
/// Use `Device::mock()` to create a Device backed by this transport.
#[cfg(feature = "mock")]
pub struct MockTransport {
    responses: Mutex<HashMap<Vec<u8>, Vec<u8>>>,
    button_queue: Mutex<Vec<ButtonMask>>,
    catch_queue: Mutex<Vec<CatchEvent>>,
    sent_commands: Mutex<Vec<Vec<u8>>>,
}

#[cfg(feature = "mock")]
impl Default for MockTransport {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(feature = "mock")]
impl MockTransport {
    pub fn new() -> Self {
        Self {
            responses: Mutex::new(HashMap::new()),
            button_queue: Mutex::new(Vec::new()),
            catch_queue: Mutex::new(Vec::new()),
            sent_commands: Mutex::new(Vec::new()),
        }
    }

    /// Register a response for a given command.
    /// When the command is sent, the response bytes are returned to the caller.
    pub fn on_command(&self, command: &[u8], response: &[u8]) {
        self.responses
            .lock()
            .unwrap()
            .insert(command.to_vec(), response.to_vec());
    }

    /// Queue a button event that will be emitted to subscribers on the next command.
    pub fn inject_button_event(&self, mask: ButtonMask) {
        self.button_queue.lock().unwrap().push(mask);
    }

    /// Queue a catch event that will be emitted to subscribers on the next command.
    pub fn inject_catch_event(&self, event: CatchEvent) {
        self.catch_queue.lock().unwrap().push(event);
    }

    /// Get all commands that have been sent through this transport.
    pub fn sent_commands(&self) -> Vec<Vec<u8>> {
        self.sent_commands.lock().unwrap().clone()
    }

    /// Clear the sent commands log.
    pub fn clear_sent(&self) {
        self.sent_commands.lock().unwrap().clear();
    }

    /// Process a command: record it, return configured response.
    pub(crate) fn process_command(&self, data: &[u8]) -> Vec<u8> {
        self.sent_commands.lock().unwrap().push(data.to_vec());
        let responses = self.responses.lock().unwrap();
        responses.get(data).cloned().unwrap_or_default()
    }

    /// Drain queued button events.
    pub(crate) fn drain_button_events(&self) -> Vec<ButtonMask> {
        std::mem::take(&mut *self.button_queue.lock().unwrap())
    }

    /// Drain queued catch events.
    pub(crate) fn drain_catch_events(&self) -> Vec<CatchEvent> {
        std::mem::take(&mut *self.catch_queue.lock().unwrap())
    }
}

/// Worker thread that processes commands through the MockTransport.
#[cfg(feature = "mock")]
pub(crate) fn mock_worker(
    rx: channel::Receiver<WritePayload>,
    mock: Arc<MockTransport>,
    button_subs: Arc<Mutex<Vec<channel::Sender<ButtonMask>>>>,
    catch_subs: Arc<Mutex<Vec<channel::Sender<CatchEvent>>>>,
) {
    loop {
        match rx.recv() {
            Ok(payload) => {
                // Process command and get response.
                let response = mock.process_command(&payload.data);

                // Send response to waiting caller if confirmed (not F&F).
                if let Some(tx) = payload.response_tx {
                    let _ = tx.send(response);
                }

                // Dispatch any queued button events.
                let events = mock.drain_button_events();
                if !events.is_empty() {
                    let mut subs = button_subs.lock().unwrap();
                    for event in &events {
                        subs.retain(|sub| sub.send(*event).is_ok());
                    }
                }

                // Dispatch any queued catch events.
                let catch_events = mock.drain_catch_events();
                if !catch_events.is_empty() {
                    let mut subs = catch_subs.lock().unwrap();
                    for event in &catch_events {
                        subs.retain(|sub| sub.send(*event).is_ok());
                    }
                }
            }
            Err(_) => return,
        }
    }
}
