use crate::error::Result;
use crate::protocol::api::{ApiVerb, button_opcode};
use crate::protocol::builder;
use crate::protocol::constants;
use crate::timed;
use crate::types::Button;

use super::Device;

impl Device {
    /// Force a button down (held).
    pub fn button_down(&self, button: Button) -> Result<()> {
        timed!(
            "button_down",
            self.exec_api(
                constants::button_down_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[1]
            )
        )
    }

    pub fn button_down_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_down_dt", {
            let command = builder::build_button_dt(button, true, dt_uframes)?;
            let mut payload = vec![1];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                button_opcode(button),
                ApiVerb::Set,
                &payload,
            )
        })
    }

    /// Silent release — does not override a physically held button.
    pub fn button_up(&self, button: Button) -> Result<()> {
        timed!(
            "button_up",
            self.exec_api(
                constants::button_up_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[0]
            )
        )
    }

    pub fn button_up_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_up_dt", {
            let command = builder::build_button_dt(button, false, dt_uframes)?;
            let mut payload = vec![0];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                button_opcode(button),
                ApiVerb::Set,
                &payload,
            )
        })
    }

    /// Force release a button even if the user is physically holding it.
    pub fn button_up_force(&self, button: Button) -> Result<()> {
        timed!(
            "button_up_force",
            self.exec_api(
                constants::button_force_up_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[2]
            )
        )
    }

    /// Query whether a button is currently pressed.
    pub fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self.query_api(
                constants::button_query_cmd(button),
                button_opcode(button),
                ApiVerb::Get,
                &[],
            )?;
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
            self.exec_api(
                constants::button_down_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[1]
            )
            .await
        )
    }

    pub async fn button_down_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_down_dt", {
            let command = builder::build_button_dt(button, true, dt_uframes)?;
            let mut payload = vec![1];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                button_opcode(button),
                ApiVerb::Set,
                &payload,
            )
            .await
        })
    }

    pub async fn button_up(&self, button: Button) -> Result<()> {
        timed!(
            "button_up",
            self.exec_api(
                constants::button_up_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[0]
            )
            .await
        )
    }

    pub async fn button_up_dt(&self, button: Button, dt_uframes: u16) -> Result<()> {
        timed!("button_up_dt", {
            let command = builder::build_button_dt(button, false, dt_uframes)?;
            let mut payload = vec![0];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                button_opcode(button),
                ApiVerb::Set,
                &payload,
            )
            .await
        })
    }

    pub async fn button_up_force(&self, button: Button) -> Result<()> {
        timed!(
            "button_up_force",
            self.exec_api(
                constants::button_force_up_cmd(button),
                button_opcode(button),
                ApiVerb::Set,
                &[2]
            )
            .await
        )
    }

    pub async fn button_state(&self, button: Button) -> Result<bool> {
        timed!("button_state", {
            let value = self
                .query_api(
                    constants::button_query_cmd(button),
                    button_opcode(button),
                    ApiVerb::Get,
                    &[],
                )
                .await?;
            Ok(value == b"1" || value == b"\x01")
        })
    }
}
