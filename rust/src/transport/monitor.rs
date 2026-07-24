use std::sync::Arc;
use std::sync::atomic::Ordering;
use std::time::Duration;

use crate::types::ConnectionState;

use super::TransportInner;

const BACKOFF_MAX: Duration = Duration::from_secs(5);

pub(crate) fn monitor_thread(inner: Arc<TransportInner>) {
    loop {
        // Wait for the reader to signal disconnection.
        // Clone the Arc<ReaderSignal> so we release the reader_signal mutex
        // before blocking on the condvar — shutdown() needs this lock.
        let sig = {
            let guard = inner.reader_signal.lock().unwrap();
            match guard.as_ref() {
                Some(s) => Arc::clone(s),
                None => return,
            }
        };
        {
            let (lock, cvar) = &sig.disconnect_notify;
            let mut disconnected = lock.lock().unwrap();
            while !*disconnected {
                disconnected = cvar.wait(disconnected).unwrap();
            }
        }

        if inner.shutdown.load(Ordering::Acquire) {
            return;
        }

        inner
            .conn_state
            .store(ConnectionState::Disconnected as u8, Ordering::Release);
        inner.notify_state(ConnectionState::Disconnected);

        // Exponential backoff reconnection.
        let mut backoff = inner.reconnect_backoff;
        loop {
            if inner.shutdown.load(Ordering::Acquire) {
                return;
            }

            inner
                .conn_state
                .store(ConnectionState::Connecting as u8, Ordering::Release);
            inner.notify_state(ConnectionState::Connecting);

            std::thread::sleep(backoff);

            let port_name = inner.port_name.lock().unwrap().clone();
            match super::serial::establish_connection(&port_name, true) {
                Ok((port, _version)) => {
                    if inner.spawn_io_threads(port).is_ok() {
                        inner
                            .conn_state
                            .store(ConnectionState::Connected as u8, Ordering::Release);
                        inner.notify_state(ConnectionState::Connected);
                        break;
                    }
                }
                Err(_) => {
                    backoff = (backoff * 2).min(BACKOFF_MAX);
                }
            }
        }
    }
}
