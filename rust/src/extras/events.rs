use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use crate::error::Result;
use crate::types::{Button, ButtonMask};

use crate::device::Device;

/// Handle to a registered event callback. Dropping it unregisters the callback.
pub struct EventHandle {
    alive: Arc<AtomicBool>,
}

impl std::fmt::Debug for EventHandle {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("EventHandle")
            .field("active", &self.alive.load(Ordering::Relaxed))
            .finish()
    }
}

impl EventHandle {
    pub(crate) fn new(alive: Arc<AtomicBool>) -> Self {
        Self { alive }
    }
}

impl Drop for EventHandle {
    fn drop(&mut self) {
        self.alive.store(false, Ordering::Release);
    }
}

/// Polling interval for event listener threads to check the `alive` flag.
pub(crate) const POLL_INTERVAL: Duration = Duration::from_millis(50);

impl Device {
    /// Register a callback fired whenever the given button changes state.
    ///
    /// Automatically enables the button stream if not already enabled.
    /// Spawns an internal listener thread that diffs the `ButtonMask` stream.
    /// Returns a handle that unregisters the callback when dropped.
    ///
    /// `f` receives `true` when pressed, `false` when released.
    pub fn on_button_press<F>(&self, button: Button, f: F) -> Result<EventHandle>
    where
        F: Fn(bool) + Send + 'static,
    {
        self.enable_button_stream()?;

        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.button_events();

        std::thread::Builder::new()
            .name("makxd-event-press".into())
            .spawn(move || {
                let mut prev_state = false;
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(mask) => {
                            let current = mask.is_pressed(button);
                            if current != prev_state {
                                prev_state = current;
                                f(current);
                            }
                        }
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle { alive })
    }

    /// Register a callback that fires on any button state change with the full mask.
    ///
    /// Automatically enables the button stream if not already enabled.
    pub fn on_button_event<F>(&self, f: F) -> Result<EventHandle>
    where
        F: Fn(ButtonMask) + Send + 'static,
    {
        self.enable_button_stream()?;

        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.button_events();

        std::thread::Builder::new()
            .name("makxd-event-any".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(mask) => f(mask),
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle { alive })
    }
}

// -- Async --

#[cfg(feature = "async")]
use crate::device::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Register a callback for button press/release (async).
    ///
    /// Automatically enables the button stream if not already enabled.
    pub async fn on_button_press<F>(&self, button: Button, f: F) -> Result<EventHandle>
    where
        F: Fn(bool) + Send + 'static,
    {
        self.enable_button_stream().await?;

        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.button_events();

        std::thread::Builder::new()
            .name("makxd-async-event-press".into())
            .spawn(move || {
                let mut prev_state = false;
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(mask) => {
                            let current = mask.is_pressed(button);
                            if current != prev_state {
                                prev_state = current;
                                f(current);
                            }
                        }
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle { alive })
    }

    /// Register a callback for any button state change (async).
    ///
    /// Automatically enables the button stream if not already enabled.
    pub async fn on_button_event<F>(&self, f: F) -> Result<EventHandle>
    where
        F: Fn(ButtonMask) + Send + 'static,
    {
        self.enable_button_stream().await?;

        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.button_events();

        std::thread::Builder::new()
            .name("makxd-async-event-any".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(mask) => f(mask),
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle { alive })
    }
}
