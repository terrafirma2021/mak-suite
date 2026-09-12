use crate::error::Result;
use crate::protocol::api::button_opcode;
use crate::timed;
use crate::types::Button;

use super::Device;

impl Device {
    /// Force a button down (held).
    pub fn button_down(&self, button: Button) -> Result<()> {
        timed!("button_down", self.write_api(button_opcode(button), &[1]))
    }

    /// Silent release — does not override a physically held button.
    pub fn button_up(&self, button: Button) -> Result<()> {
        timed!("button_up", self.write_api(button_opcode(button), &[0]))
    }

    /// Query whether a button is currently pressed.
    pub fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query_api(button_opcode(button), &[])?;
            Ok(value == b"1" || value == b"\x01")
        })
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn button_down(&self, button: Button) -> Result<()> {
        timed!(
            "button_down",
            self.write_api(button_opcode(button), &[1]).await
        )
    }

    pub async fn button_up(&self, button: Button) -> Result<()> {
        timed!(
            "button_up",
            self.write_api(button_opcode(button), &[0]).await
        )
    }

    pub async fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query_api(button_opcode(button), &[]).await?;
            Ok(value == b"1" || value == b"\x01")
        })
    }
}
