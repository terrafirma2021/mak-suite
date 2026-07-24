use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use crate::error::Result;
use crate::protocol::constants;
use crate::timed;
use crate::types::{Button, CatchEvent};

use crate::device::Device;

use super::EventHandle;
use super::events::POLL_INTERVAL;

impl Device {
    /// Lock the button and enable catch in one call.
    ///
    /// Equivalent to `set_lock(target, true)` then `enable_catch(button)`.
    pub fn start_catch(&self, button: Button) -> Result<()> {
        timed!("start_catch", {
            let target = constants::button_to_lock_target(button);
            self.set_lock(target, true)?;
            self.enable_catch(button)
        })
    }

    /// Unlock the button, stopping the catch stream.
    ///
    /// This is the only way to disable catch — there is no explicit
    /// catch disable command in the firmware. This also releases the
    /// button lock.
    pub fn stop_catch(&self, button: Button) -> Result<()> {
        timed!("stop_catch", {
            let target = constants::button_to_lock_target(button);
            self.set_lock(target, false)
        })
    }

    /// Register a callback fired on catch press/release events for a button.
    ///
    /// The button must have catch enabled (via `start_catch` or manual
    /// `set_lock` + `enable_catch`) for events to flow.
    ///
    /// `f` receives `true` for press, `false` for release.
    /// Returns a handle that unregisters the callback when dropped.
    pub fn on_catch<F>(&self, button: Button, f: F) -> Result<EventHandle>
    where
        F: Fn(bool) + Send + 'static,
    {
        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.catch_events();

        std::thread::Builder::new()
            .name("makxd-event-catch".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(event) => {
                            if event.button == button {
                                f(event.pressed);
                            }
                        }
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle::new(alive))
    }

    /// Register a callback fired on all catch events (any button).
    ///
    /// `f` receives the full `CatchEvent`.
    /// Returns a handle that unregisters the callback when dropped.
    pub fn on_catch_event<F>(&self, f: F) -> Result<EventHandle>
    where
        F: Fn(CatchEvent) + Send + 'static,
    {
        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.catch_events();

        std::thread::Builder::new()
            .name("makxd-event-catch-all".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(event) => f(event),
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle::new(alive))
    }
}

// -- Async --

#[cfg(feature = "async")]
use crate::device::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Lock the button and enable catch in one call.
    pub async fn start_catch(&self, button: Button) -> Result<()> {
        timed!("start_catch", {
            let target = constants::button_to_lock_target(button);
            self.set_lock(target, true).await?;
            self.enable_catch(button).await
        })
    }

    /// Unlock the button, stopping the catch stream.
    /// This also releases the button lock.
    pub async fn stop_catch(&self, button: Button) -> Result<()> {
        timed!("stop_catch", {
            let target = constants::button_to_lock_target(button);
            self.set_lock(target, false).await
        })
    }

    /// Register a callback for catch press/release events on a button (async).
    pub async fn on_catch<F>(&self, button: Button, f: F) -> Result<EventHandle>
    where
        F: Fn(bool) + Send + 'static,
    {
        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.catch_events();

        std::thread::Builder::new()
            .name("makxd-async-event-catch".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(event) => {
                            if event.button == button {
                                f(event.pressed);
                            }
                        }
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle::new(alive))
    }

    /// Register a callback for all catch events (async).
    pub async fn on_catch_event<F>(&self, f: F) -> Result<EventHandle>
    where
        F: Fn(CatchEvent) + Send + 'static,
    {
        let alive = Arc::new(AtomicBool::new(true));
        let alive_clone = Arc::clone(&alive);
        let rx = self.catch_events();

        std::thread::Builder::new()
            .name("makxd-async-event-catch-all".into())
            .spawn(move || {
                while alive_clone.load(Ordering::Acquire) {
                    match rx.recv_timeout(POLL_INTERVAL) {
                        Ok(event) => f(event),
                        Err(crossbeam_channel::RecvTimeoutError::Timeout) => continue,
                        Err(crossbeam_channel::RecvTimeoutError::Disconnected) => break,
                    }
                }
            })?;

        Ok(EventHandle::new(alive))
    }
}
