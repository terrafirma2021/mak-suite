use crossbeam_channel as channel;

use crate::error::MakxdError;
use crate::error::Result;
use crate::protocol::api::ApiOpcode;
use crate::stream::{InputChange, StreamKind};
use crate::timed;
use crate::types::ButtonMask;

pub(super) fn stream_state_parse(value: &[u8]) -> Result<bool> {
    match value {
        [0] => Ok(false),
        [1] => Ok(true),
        _ => Err(MakxdError::Protocol("invalid stream state".into())),
    }
}

use super::Device;

impl Device {
    pub fn input_stream(&self, kind: StreamKind, enabled: bool) -> Result<()> {
        self.write_api(ApiOpcode::InputStream, &[kind as u8, u8::from(enabled)])
    }
    pub fn input_stream_state(&self, kind: StreamKind) -> Result<bool> {
        let value = self.query_api(ApiOpcode::InputStream, &[kind as u8])?;
        stream_state_parse(&value)
    }
    pub fn input_changes(&self) -> channel::Receiver<InputChange> {
        self.transport().subscribe_input_changes()
    }

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
            stream_state_parse(&value)
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
    pub async fn input_stream(&self, kind: StreamKind, enabled: bool) -> Result<()> {
        self.write_api(ApiOpcode::InputStream, &[kind as u8, u8::from(enabled)])
            .await
    }
    pub async fn input_stream_state(&self, kind: StreamKind) -> Result<bool> {
        let value = self
            .query_api(ApiOpcode::InputStream, &[kind as u8])
            .await?;
        stream_state_parse(&value)
    }
    pub fn input_changes(&self) -> channel::Receiver<InputChange> {
        self.transport().subscribe_input_changes()
    }

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
            stream_state_parse(&value)
        })
    }

    pub fn button_events(&self) -> channel::Receiver<ButtonMask> {
        self.transport().subscribe_buttons()
    }
}
