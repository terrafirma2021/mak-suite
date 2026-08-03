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
        let mut response_expected = payload.response_tx.is_some();

        coalesced.extend_from_slice(&payload.data);
        if let Some(tx) = payload.response_tx {
            responses.push(PendingResponse {
                response_tx: tx,
                expected_nonce: payload.expected_nonce,
                expected_opcode: payload.expected_opcode,
            });
        }

        if port.write_coalescing_supported() {
            while let Ok(payload) = rx.try_recv() {
                coalesced.extend_from_slice(&payload.data);
                response_expected |= payload.response_tx.is_some();
                if let Some(tx) = payload.response_tx {
                    responses.push(PendingResponse {
                        response_tx: tx,
                        expected_nonce: payload.expected_nonce,
                        expected_opcode: payload.expected_opcode,
                    });
                }
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
        if port.write_all_wire(&coalesced, response_expected).is_err() {
            return;
        }
        let _ = port.flush_wire();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::Result;

    struct DatagramCapturePort {
        writes: Arc<Mutex<Vec<Vec<u8>>>>,
    }

    impl WirePort for DatagramCapturePort {
        fn try_clone_wire(&self) -> Result<Box<dyn WirePort>> {
            Ok(Box::new(Self {
                writes: Arc::clone(&self.writes),
            }))
        }

        fn write_coalescing_supported(&self) -> bool {
            false
        }

        fn read_wire(&mut self, _bytes: &mut [u8]) -> std::io::Result<usize> {
            Ok(0)
        }

        fn write_all_wire(
            &mut self,
            bytes: &[u8],
            _response_expected: bool,
        ) -> std::io::Result<()> {
            self.writes.lock().unwrap().push(bytes.to_vec());
            Ok(())
        }

        fn flush_wire(&mut self) -> std::io::Result<()> {
            Ok(())
        }
    }

    #[test]
    fn datagram_commands_are_not_coalesced() {
        let writes = Arc::new(Mutex::new(Vec::new()));
        let (tx, rx) = channel::unbounded();
        let pending = Arc::new(Mutex::new(VecDeque::new()));
        for data in [vec![1, 2], vec![3, 4]] {
            tx.send(WritePayload {
                data,
                response_tx: None,
                expected_nonce: None,
                expected_opcode: None,
            })
            .unwrap();
        }
        drop(tx);
        writer_thread(
            Box::new(DatagramCapturePort {
                writes: Arc::clone(&writes),
            }),
            rx,
            pending,
        );
        assert_eq!(*writes.lock().unwrap(), [vec![1, 2], vec![3, 4]]);
    }
}
