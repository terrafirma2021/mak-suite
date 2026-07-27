use std::collections::VecDeque;
use std::io::Write;
use std::sync::{Arc, Mutex};

use crossbeam_channel as channel;
use serialport::SerialPort;

use super::PendingResponse;

pub(crate) struct WritePayload {
    pub data: Vec<u8>,
    pub response_tx: Option<channel::Sender<Vec<u8>>>,
    pub expected_nonce: Option<[u8; 12]>,
}

pub(crate) fn writer_thread(
    mut port: Box<dyn SerialPort>,
    rx: channel::Receiver<WritePayload>,
    pending_responses: Arc<Mutex<VecDeque<PendingResponse>>>,
) {
    let mut coalesced = Vec::with_capacity(512);
    let mut responses: Vec<PendingResponse> = Vec::new();

    loop {
        // Block on first payload.
        let payload = match rx.recv() {
            Ok(p) => p,
            Err(_) => return,
        };

        coalesced.clear();
        responses.clear();

        coalesced.extend_from_slice(&payload.data);
        if let Some(tx) = payload.response_tx {
            responses.push(PendingResponse {
                response_tx: tx,
                expected_nonce: payload.expected_nonce,
            });
        }

        // Drain additional pending payloads for coalescing.
        while let Ok(payload) = rx.try_recv() {
            coalesced.extend_from_slice(&payload.data);
            if let Some(tx) = payload.response_tx {
                responses.push(PendingResponse {
                    response_tx: tx,
                    expected_nonce: payload.expected_nonce,
                });
            }
        }

        // Register response receivers BEFORE writing — at 4 Mbaud the device
        // can respond before write_all returns, and the reader must already
        // have the sender in the queue to deliver the response.
        if !responses.is_empty() {
            let mut pending = pending_responses.lock().unwrap();
            for response in responses.drain(..) {
                pending.push_back(response);
            }
        }

        // Single write_all for all coalesced data.
        if port.write_all(&coalesced).is_err() {
            return;
        }
        let _ = port.flush();
    }
}
