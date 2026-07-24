use std::collections::VecDeque;
use std::io::Read;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use crossbeam_channel as channel;
use serialport::SerialPort;

use crate::protocol::parser::{self, ParseEvent, StreamParser};
use crate::types::{ButtonMask, CatchEvent};

/// Shared state that the reader thread signals on exit.
pub(crate) struct ReaderSignal {
    pub alive: AtomicBool,
    pub disconnect_notify: (Mutex<bool>, Condvar),
}

impl ReaderSignal {
    pub fn new() -> Self {
        Self {
            alive: AtomicBool::new(true),
            disconnect_notify: (Mutex::new(false), Condvar::new()),
        }
    }
}

pub(crate) fn reader_thread(
    mut port: Box<dyn SerialPort>,
    pending_responses: Arc<Mutex<VecDeque<channel::Sender<Vec<u8>>>>>,
    button_subs: Arc<Mutex<Vec<channel::Sender<ButtonMask>>>>,
    catch_subs: Arc<Mutex<Vec<channel::Sender<CatchEvent>>>>,
    signal: Arc<ReaderSignal>,
) {
    let mut parser = StreamParser::new();
    let mut buf = [0u8; 256];

    loop {
        match port.read(&mut buf) {
            Ok(n) => {
                // Check shutdown flag on every read — with active devices
                // (e.g. mouse), the port may never time out.
                if !signal.alive.load(Ordering::Acquire) {
                    break;
                }
                for &byte in &buf[..n] {
                    if let Some(event) = parser.feed(byte) {
                        match event {
                            ParseEvent::ButtonEvent(mask) => {
                                let mut subs = button_subs.lock().unwrap();
                                subs.retain(|sub| sub.send(ButtonMask(mask)).is_ok());
                            }
                            ParseEvent::Response(data) => {
                                // Check if this is an unsolicited catch event before
                                // routing to pending command responses.
                                if let Some(catch_event) = parser::parse_catch_event(&data) {
                                    let mut subs = catch_subs.lock().unwrap();
                                    subs.retain(|sub| sub.send(catch_event).is_ok());
                                } else {
                                    let mut pending = pending_responses.lock().unwrap();
                                    if let Some(tx) = pending.pop_front() {
                                        let _ = tx.send(data);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {
                // Check if shutdown was requested during the timeout.
                if !signal.alive.load(Ordering::Acquire) {
                    break;
                }
                continue;
            }
            Err(_) => break,
        }
    }

    // Signal disconnection.
    signal.alive.store(false, Ordering::Release);
    let (lock, cvar) = &signal.disconnect_notify;
    let mut disconnected = lock.lock().unwrap();
    *disconnected = true;
    cvar.notify_all();
}
