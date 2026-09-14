use std::collections::VecDeque;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Condvar, Mutex};

use crossbeam_channel as channel;

use crate::stream::{
    InputChange, StreamFrame, StreamFrameDecoder, StreamKind, decode_input_change,
};
use crate::types::ButtonMask;

use super::PendingResponse;
use super::encryption::{EncryptedFrameDecoder, TransportEncryption};
use super::wire::WirePort;

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
    mut port: Box<dyn WirePort>,
    pending_responses: Arc<Mutex<VecDeque<PendingResponse>>>,
    button_subs: Arc<Mutex<Vec<channel::Sender<ButtonMask>>>>,
    change_subs: Arc<Mutex<Vec<channel::Sender<InputChange>>>>,
    signal: Arc<ReaderSignal>,
    transport_encryption: Option<Arc<TransportEncryption>>,
) {
    let mut events = EventSink {
        button_subs,
        change_subs,
        mouse: 0,
    };
    let mut encrypted_decoder = EncryptedFrameDecoder::new();
    let mut parser_buffer = Vec::with_capacity(512);
    let mut buf = [0u8; 256];

    loop {
        match port.read_wire(&mut buf) {
            Ok(n) => {
                // Check shutdown flag on every read — with active devices
                // (e.g. mouse), the port may never time out.
                if !signal.alive.load(Ordering::Acquire) {
                    break;
                }
                let decoded_frames = if let Some(encryption) = transport_encryption.as_deref() {
                    match encrypted_decoder.feed(encryption, &buf[..n]) {
                        Ok(frames) => frames,
                        Err(_) => break,
                    }
                } else {
                    Vec::new()
                };
                if transport_encryption.is_some() {
                    for (plaintext, transaction_nonce) in decoded_frames {
                        mak_api_response_deliver(
                            &plaintext,
                            Some(&transaction_nonce),
                            &pending_responses,
                            &mut events,
                        );
                    }
                    continue;
                }
                mak_api_frames_feed(
                    &buf[..n],
                    &mut parser_buffer,
                    &pending_responses,
                    &mut events,
                );
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

struct EventSink {
    button_subs: Arc<Mutex<Vec<channel::Sender<ButtonMask>>>>,
    change_subs: Arc<Mutex<Vec<channel::Sender<InputChange>>>>,
    mouse: u8,
}
impl EventSink {
    fn deliver(&mut self, event: InputChange) {
        self.change_subs
            .lock()
            .unwrap()
            .retain(|sub| sub.send(event).is_ok());
        if event.kind == StreamKind::Mouse {
            if event.overflow {
                self.mouse = 0;
            } else if event.control < 8 {
                let bit = 1u8 << event.control;
                if event.value != 0 {
                    self.mouse |= bit;
                } else {
                    self.mouse &= !bit;
                }
            }
            self.button_subs
                .lock()
                .unwrap()
                .retain(|sub| sub.send(ButtonMask(self.mouse)).is_ok());
        }
    }
}

fn mak_api_response_deliver(
    body: &[u8],
    transaction_nonce: Option<&[u8; 12]>,
    pending_responses: &Arc<Mutex<VecDeque<PendingResponse>>>,
    events: &mut EventSink,
) {
    // Encrypted notifications contain an authenticated complete MAK event frame.
    if body.starts_with(&[0xde, 0xad]) {
        let mut decoder = StreamFrameDecoder::new();
        decoder.feed(body);
        for frame in decoder {
            if let Some(event) = decode_input_change(&frame) {
                events.deliver(event);
            }
        }
        return;
    }
    if body.first() == Some(&0x53) {
        if let Some(event) = decode_input_change(&StreamFrame {
            command: 0x53,
            payload: body[1..].to_vec(),
        }) {
            events.deliver(event);
        }
        return;
    }
    if body.len() < 2 {
        return;
    }
    let opcode = body[0];
    if body.len()==14 && opcode==0x3e && body[1]==0x12 { return; }
    let mut pending = pending_responses.lock().unwrap();
    let index = pending.iter().position(|response| {
        response.expected_opcode == Some(opcode)
            && match (response.expected_nonce.as_ref(), transaction_nonce) {
                (Some(expected), Some(actual)) => expected == actual,
                (None, None) => true,
                _ => false,
            }
    });
    if let Some(response) = index.and_then(|index| pending.remove(index)) {
        let _ = response.response_tx.send(body.to_vec());
    }
}

fn mak_api_frames_feed(
    data: &[u8],
    buffer: &mut Vec<u8>,
    pending_responses: &Arc<Mutex<VecDeque<PendingResponse>>>,
    events: &mut EventSink,
) {
    const MAX_PAYLOAD: usize = 251;
    buffer.extend_from_slice(data);
    loop {
        let marker = buffer.windows(2).position(|window| window == [0xde, 0xad]);
        let Some(marker) = marker else {
            let keep_de = buffer.last() == Some(&0xde);
            buffer.clear();
            if keep_de {
                buffer.push(0xde);
            }
            return;
        };
        if marker != 0 {
            buffer.drain(..marker);
        }
        if buffer.len() < 5 {
            return;
        }
        let payload_len = u16::from_le_bytes([buffer[2], buffer[3]]) as usize;
        if payload_len == 0 || payload_len > MAX_PAYLOAD {
            buffer.remove(0);
            continue;
        }
        let frame_len = 5 + payload_len;
        if buffer.len() < frame_len {
            return;
        }
        let mut body = Vec::with_capacity(payload_len + 1);
        body.push(buffer[4]);
        body.extend_from_slice(&buffer[5..frame_len]);
        buffer.drain(..frame_len);
        mak_api_response_deliver(&body, None, pending_responses, events);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn fragmented_mak_api_response_returns_only_matching_opcode() {
        let pending = Arc::new(Mutex::new(VecDeque::new()));
        let (tx, rx) = channel::bounded(1);
        pending.lock().unwrap().push_back(PendingResponse {
            response_tx: tx,
            expected_nonce: None,
            expected_opcode: Some(0x25),
        });
        let mut buffer = Vec::new();
        let mut events = EventSink {
            button_subs: Arc::new(Mutex::new(Vec::new())),
            change_subs: Arc::new(Mutex::new(Vec::new())),
            mouse: 0,
        };
        mak_api_frames_feed(&[0xde, 0xad, 1], &mut buffer, &pending, &mut events);
        assert!(rx.try_recv().is_err());
        mak_api_frames_feed(&[0, 0x25, 1], &mut buffer, &pending, &mut events);
        assert_eq!(rx.recv().unwrap(), [0x25, 1]);
    }
    #[test]
    fn events_do_not_consume_pending_nonce_or_opcode() {
        let pending = Arc::new(Mutex::new(VecDeque::new()));
        let (reply_tx, reply_rx) = channel::bounded(1);
        let (event_tx, event_rx) = channel::unbounded();
        let mut events = EventSink {
            button_subs: Arc::new(Mutex::new(Vec::new())),
            change_subs: Arc::new(Mutex::new(vec![event_tx])),
            mouse: 0,
        };
        pending.lock().unwrap().push_back(PendingResponse {
            response_tx: reply_tx,
            expected_nonce: Some([1; 12]),
            expected_opcode: Some(0x52),
        });
        mak_api_response_deliver(
            &[0xde, 0xad, 4, 0, 0x53, 3, 10, 255, 3],
            Some(&[2; 12]),
            &pending,
            &mut events,
        );
        assert_eq!(event_rx.recv().unwrap().value, 1023);
        assert!(reply_rx.try_recv().is_err());
        mak_api_response_deliver(&[0x52, 1], Some(&[2; 12]), &pending, &mut events);
        assert!(reply_rx.try_recv().is_err());
        mak_api_response_deliver(&[0x52, 1], Some(&[1; 12]), &pending, &mut events);
        assert_eq!(reply_rx.recv().unwrap(), [0x52, 1]);
    }
}
