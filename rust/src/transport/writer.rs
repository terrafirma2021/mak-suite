use std::collections::VecDeque;
use std::sync::{Arc, Mutex};

use crossbeam_channel as channel;

use super::PendingResponse;
use super::wire::WirePort;

pub(crate) struct WritePayload {
    pub data: Vec<u8>,
    pub response_tx: Option<channel::Sender<Vec<u8>>>,
    pub expected_nonce: Option<[u8; 12]>,
    pub expected_opcode: Option<u8>,
}

pub(crate) fn writer_thread(
    mut port: Box<dyn WirePort>,
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
                expected_opcode: payload.expected_opcode,
            });
        }

        // Drain additional pending payloads for coalescing.
        while let Ok(payload) = rx.try_recv() {
            coalesced.extend_from_slice(&payload.data);
            if let Some(tx) = payload.response_tx {
                responses.push(PendingResponse {
                    response_tx: tx,
                    expected_nonce: payload.expected_nonce,
                    expected_opcode: payload.expected_opcode,
                });
            }
        }

        // Register response receivers BEFORE writing at the runtime baud
        // can respond before write_all returns, and the reader must already
        // have the sender in the queue to deliver the response.
        if !responses.is_empty() {
            let mut pending = pending_responses.lock().unwrap();
            for response in responses.drain(..) {
                pending.push_back(response);
            }
        }

        // Single write_all for all coalesced data.
        if port.write_all_wire(&coalesced).is_err() {
            return;
        }
        let _ = port.flush_wire();
    }
}
