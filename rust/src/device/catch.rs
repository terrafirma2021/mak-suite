use crossbeam_channel as channel;

use crate::error::Result;
use crate::protocol::constants;
use crate::timed;
use crate::types::{Button, CatchEvent};

use super::Device;

impl Device {
    /// Enable the catch stream for a button.
    ///
    /// The button **must be locked** via `set_lock()` before calling this.
    /// Catch produces no events without an active lock.
    ///
    /// Use `set_catch` to change catch state without changing the lock state.
    pub fn enable_catch(&self, button: Button) -> Result<()> {
        timed!(
            "enable_catch",
            self.exec(constants::catch_enable_cmd(button))
        )
    }

    pub fn set_catch(&self, button: Button, enabled: bool) -> Result<()> {
        let alias = catch_alias(button);
        timed!("set_catch", {
            let command = format!("km.catch_{alias}({})\r\n", if enabled { 0 } else { 1 });
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn catch_state(&self, button: Button) -> Result<String> {
        let alias = catch_alias(button);
        timed!("catch_state", {
            let command = format!("km.catch_{alias}()\r\n");
            self.query_dynamic(command.as_bytes())
        })
    }

    /// Subscribe to catch events. Returns a receiver that yields `CatchEvent`
    /// values whenever a locked button with catch enabled is physically
    /// pressed or released.
    ///
    /// You must call `set_lock()` then `enable_catch()` first for events to flow.
    pub fn catch_events(&self) -> channel::Receiver<CatchEvent> {
        self.transport().subscribe_catch()
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Enable the catch stream for a button.
    ///
    /// The button **must be locked** via `set_lock()` before calling this.
    /// Catch produces no events without an active lock. `set_catch` changes
    /// catch state without changing that lock.
    pub async fn enable_catch(&self, button: Button) -> Result<()> {
        timed!(
            "enable_catch",
            self.exec(constants::catch_enable_cmd(button)).await
        )
    }

    pub async fn set_catch(&self, button: Button, enabled: bool) -> Result<()> {
        let alias = catch_alias(button);
        timed!("set_catch", {
            let command = format!("km.catch_{alias}({})\r\n", if enabled { 0 } else { 1 });
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn catch_state(&self, button: Button) -> Result<String> {
        let alias = catch_alias(button);
        timed!("catch_state", {
            let command = format!("km.catch_{alias}()\r\n");
            self.query_dynamic(command.as_bytes()).await
        })
    }

    /// Subscribe to catch events.
    pub fn catch_events(&self) -> channel::Receiver<CatchEvent> {
        self.transport().subscribe_catch()
    }
}

fn catch_alias(button: Button) -> &'static str {
    match button {
        Button::Left => "ml",
        Button::Right => "mr",
        Button::Middle => "mm",
        Button::Side1 => "ms1",
        Button::Side2 => "ms2",
    }
}
