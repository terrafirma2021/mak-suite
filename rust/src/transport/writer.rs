use std::collections::VecDeque;
use std::io::Write;
use std::sync::{Arc, Mutex};

use crossbeam_channel as channel;
use serialport::SerialPort;

pub(crate) struct WritePayload {
    pub data: Vec<u8>,
    pub response_tx: Option<channel::Sender<Vec<u8>>>,
}

pub(crate) fn writer_thread(
    mut port: Box<dyn SerialPort>,
    rx: channel::Receiver<WritePayload>,
    pending_responses: Arc<Mutex<VecDeque<channel::Sender<Vec<u8>>>>>,
) {
    let mut coalesced = Vec::with_capacity(512);
    let mut response_txs: Vec<channel::Sender<Vec<u8>>> = Vec::new();

    loop {
        // Block on first payload.
        let payload = match rx.recv() {
            Ok(p) => p,
            Err(_) => return,
        };

        coalesced.clear();
        response_txs.clear();

        coalesced.extend_from_slice(&payload.data);
        if let Some(tx) = payload.response_tx {
            response_txs.push(tx);
        }

        // Drain additional pending payloads for coalescing.
        while let Ok(payload) = rx.try_recv() {
            coalesced.extend_from_slice(&payload.data);
            if let Some(tx) = payload.response_tx {
                response_txs.push(tx);
            }
        }

        // Register response receivers BEFORE writing — at 4 Mbaud the device
        // can respond before write_all returns, and the reader must already
        // have the sender in the queue to deliver the response.
        if !response_txs.is_empty() {
            let mut pending = pending_responses.lock().unwrap();
            for tx in response_txs.drain(..) {
                pending.push_back(tx);
            }
        }

        // Single write_all for all coalesced data.
        if port.write_all(&coalesced).is_err() {
            return;
        }
        let _ = port.flush();
    }
}
