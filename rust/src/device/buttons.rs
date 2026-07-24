use crate::error::Result;
use crate::protocol::constants;
use crate::timed;
use crate::types::Button;

use super::Device;

impl Device {
    /// Force a button down (held).
    pub fn button_down(&self, button: Button) -> Result<()> {
        timed!("button_down", self.exec(constants::button_down_cmd(button)))
    }

    /// Silent release — does not override a physically held button.
    pub fn button_up(&self, button: Button) -> Result<()> {
        timed!("button_up", self.exec(constants::button_up_cmd(button)))
    }

    /// Force release a button even if the user is physically holding it.
    pub fn button_up_force(&self, button: Button) -> Result<()> {
        timed!(
            "button_up_force",
            self.exec(constants::button_force_up_cmd(button))
        )
    }

    /// Query whether a button is currently pressed.
    pub fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query(constants::button_query_cmd(button))?;
            Ok(value.trim() == "1")
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
            self.exec(constants::button_down_cmd(button)).await
        )
    }

    pub async fn button_up(&self, button: Button) -> Result<()> {
        timed!(
            "button_up",
            self.exec(constants::button_up_cmd(button)).await
        )
    }

    pub async fn button_up_force(&self, button: Button) -> Result<()> {
        timed!(
            "button_up_force",
            self.exec(constants::button_force_up_cmd(button)).await
        )
    }

    pub async fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query(constants::button_query_cmd(button)).await?;
            Ok(value.trim() == "1")
        })
    }
}
