use crate::error::{MakxdError, Result};
use crate::protocol::api::button_opcode;
use crate::timed;
use crate::types::Button;

use super::Device;

impl Device {
    /// Force a button down (held).
    pub fn button_down(&self, button: Button) -> Result<()> {
        timed!("button_down", self.write_api(button_opcode(button), &[1]))
    }

    pub fn button_down_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_down_dt", {
            if dt_uframes > 0x3fff {
                return Err(MakxdError::OutOfRange {
                    value: dt_uframes as i64,
                    min: 0,
                    max: 0x3fff,
                });
            }
            let mut payload = vec![1];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(button_opcode(button), &payload)
        })
    }

    /// Silent release — does not override a physically held button.
    pub fn button_up(&self, button: Button) -> Result<()> {
        timed!("button_up", self.write_api(button_opcode(button), &[0]))
    }

    pub fn button_up_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_up_dt", {
            if dt_uframes > 0x3fff {
                return Err(MakxdError::OutOfRange {
                    value: dt_uframes as i64,
                    min: 0,
                    max: 0x3fff,
                });
            }
            let mut payload = vec![0];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(button_opcode(button), &payload)
        })
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

    pub async fn button_down_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_down_dt", {
            if dt_uframes > 0x3fff {
                return Err(MakxdError::OutOfRange {
                    value: dt_uframes as i64,
                    min: 0,
                    max: 0x3fff,
                });
            }
            let mut payload = vec![1];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(button_opcode(button), &payload).await
        })
    }

    pub async fn button_up(&self, button: Button) -> Result<()> {
        timed!(
            "button_up",
            self.write_api(button_opcode(button), &[0]).await
        )
    }

    pub async fn button_up_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_up_dt", {
            if dt_uframes > 0x3fff {
                return Err(MakxdError::OutOfRange {
                    value: dt_uframes as i64,
                    min: 0,
                    max: 0x3fff,
                });
            }
            let mut payload = vec![0];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(button_opcode(button), &payload).await
        })
    }

    pub async fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query_api(button_opcode(button), &[]).await?;
            Ok(value == b"1" || value == b"\x01")
        })
    }
}
