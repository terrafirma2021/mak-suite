use crossbeam_channel as channel;

use crate::error::Result;
use crate::protocol::api::ApiOpcode;
use crate::timed;
use crate::types::ButtonMask;

use super::Device;

impl Device {
    /// Enable the button-state-change stream on the device.
    pub fn enable_button_stream(&self) -> Result<()> {
        timed!(
            "enable_button_stream",
            self.write_api(ApiOpcode::Buttons, &[1])
        )
    }

    /// Disable the button-state-change stream.
    pub fn disable_button_stream(&self) -> Result<()> {
        timed!(
            "disable_button_stream",
            self.write_api(ApiOpcode::Buttons, &[0])
        )
    }

    /// Query whether the button stream is currently enabled on the device.
    pub fn button_stream_state(&self) -> Result<bool> {
        timed!("button_stream_state", {
            let value = self.query_api(ApiOpcode::Buttons, &[])?;
            Ok(value == b"1" || value == b"\x01")
        })
    }

    /// Subscribe to button events. Returns a receiver that yields `ButtonMask`
    /// values whenever the device reports a button state change.
    ///
    /// You must call `enable_button_stream()` first for events to flow.
    pub fn button_events(&self) -> channel::Receiver<ButtonMask> {
        self.transport().subscribe_buttons()
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn enable_button_stream(&self) -> Result<()> {
        timed!(
            "enable_button_stream",
            self.write_api(ApiOpcode::Buttons, &[1]).await
        )
    }

    pub async fn disable_button_stream(&self) -> Result<()> {
        timed!(
            "disable_button_stream",
            self.write_api(ApiOpcode::Buttons, &[0]).await
        )
    }

    /// Query whether the button stream is currently enabled on the device.
    pub async fn button_stream_state(&self) -> Result<bool> {
        timed!("button_stream_state", {
            let value = self.query_api(ApiOpcode::Buttons, &[]).await?;
            Ok(value == b"1" || value == b"\x01")
        })
    }

    pub fn button_events(&self) -> channel::Receiver<ButtonMask> {
        self.transport().subscribe_buttons()
    }
}
