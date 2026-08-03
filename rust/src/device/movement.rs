use crate::error::{MakxdError, Result};
use crate::protocol::api::{ApiOpcode, button_mask_opcode};
use crate::timed;
use crate::types::Button;

use super::Device;

fn movement_range_check(value: i32) -> Result<()> {
    if !(-32768..=32767).contains(&value) {
        return Err(MakxdError::OutOfRange {
            value: value as i64,
            min: -32768,
            max: 32767,
        });
    }
    Ok(())
}

fn movement_dt_check(dt_uframes: u16) -> Result<()> {
    if dt_uframes > 0x3fff {
        return Err(MakxdError::OutOfRange {
            value: dt_uframes as i64,
            min: 0,
            max: 0x3fff,
        });
    }
    Ok(())
}

impl Device {
    pub fn button_mask(&self, button: Button, enabled: bool) -> Result<()> {
        self.write_api(button_mask_opcode(button), &[enabled as u8])
    }

    pub fn left_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Left, enabled)
    }

    pub fn right_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Right, enabled)
    }

    pub fn middle_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Middle, enabled)
    }

    pub fn side1_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Side1, enabled)
    }

    pub fn side2_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Side2, enabled)
    }

    pub fn move_mask(&self, left: bool, right: bool, down: bool, up: bool) -> Result<()> {
        let payload = [left as u8, right as u8, down as u8, up as u8];
        self.write_api(ApiOpcode::MoveMask, &payload)
    }

    pub fn wheel_mask(&self, down: bool, up: bool) -> Result<()> {
        let payload = [down as u8, up as u8];
        self.write_api(ApiOpcode::WheelMask, &payload)
    }

    /// Relative mouse move. Coordinates are in HID units, range ±32767.
    pub fn move_xy(&self, x: i32, y: i32) -> Result<()> {
        timed!("move_xy", {
            movement_range_check(x)?;
            movement_range_check(y)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            self.write_api(ApiOpcode::Move, &payload)
        })
    }

    pub fn move_xy_dt(&self, x: i32, y: i32, dt_uframes: u16) -> Result<()> {
        timed!("move_xy_dt", {
            movement_range_check(x)?;
            movement_range_check(y)?;
            movement_dt_check(dt_uframes)?;
            let mut payload = Vec::with_capacity(6);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::Move, &payload)
        })
    }

    /// Scroll wheel. Range ±127. Positive = up, negative = down.
    pub fn wheel(&self, delta: i32) -> Result<()> {
        timed!("wheel", {
            movement_range_check(delta)?;
            self.write_api(ApiOpcode::Wheel, &(delta as i16).to_le_bytes())
        })
    }

    pub fn wheel_dt(&self, delta: i32, dt_uframes: u16) -> Result<()> {
        timed!("wheel_dt", {
            movement_range_check(delta)?;
            movement_dt_check(dt_uframes)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(delta as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::Wheel, &payload)
        })
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn button_mask(&self, button: Button, enabled: bool) -> Result<()> {
        self.write_api(button_mask_opcode(button), &[enabled as u8])
            .await
    }

    pub async fn left_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Left, enabled).await
    }

    pub async fn right_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Right, enabled).await
    }

    pub async fn middle_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Middle, enabled).await
    }

    pub async fn side1_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Side1, enabled).await
    }

    pub async fn side2_mask(&self, enabled: bool) -> Result<()> {
        self.button_mask(Button::Side2, enabled).await
    }

    pub async fn move_mask(&self, left: bool, right: bool, down: bool, up: bool) -> Result<()> {
        let payload = [left as u8, right as u8, down as u8, up as u8];
        self.write_api(ApiOpcode::MoveMask, &payload).await
    }

    pub async fn wheel_mask(&self, down: bool, up: bool) -> Result<()> {
        let payload = [down as u8, up as u8];
        self.write_api(ApiOpcode::WheelMask, &payload).await
    }

    pub async fn move_xy(&self, x: i32, y: i32) -> Result<()> {
        timed!("move_xy", {
            movement_range_check(x)?;
            movement_range_check(y)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            self.write_api(ApiOpcode::Move, &payload).await
        })
    }

    pub async fn move_xy_dt(&self, x: i32, y: i32, dt_uframes: u16) -> Result<()> {
        timed!("move_xy_dt", {
            movement_range_check(x)?;
            movement_range_check(y)?;
            movement_dt_check(dt_uframes)?;
            let mut payload = Vec::with_capacity(6);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::Move, &payload).await
        })
    }

    /// Scroll wheel. Range ±127.
    pub async fn wheel(&self, delta: i32) -> Result<()> {
        timed!("wheel", {
            movement_range_check(delta)?;
            self.write_api(ApiOpcode::Wheel, &(delta as i16).to_le_bytes())
                .await
        })
    }

    pub async fn wheel_dt(&self, delta: i32, dt_uframes: u16) -> Result<()> {
        timed!("wheel_dt", {
            movement_range_check(delta)?;
            movement_dt_check(dt_uframes)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(delta as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::Wheel, &payload).await
        })
    }
}
