use crate::error::Result;
use crate::protocol::api::{ApiOpcode, ApiVerb, button_mask_opcode};
use crate::protocol::builder;
use crate::timed;
use crate::types::Button;

use super::Device;

impl Device {
    pub fn button_mask(&self, button: Button, enabled: bool) -> Result<()> {
        let name = match button {
            Button::Left => "left_mask",
            Button::Right => "right_mask",
            Button::Middle => "middle_mask",
            Button::Side1 => "side1_mask",
            Button::Side2 => "side2_mask",
        };
        let command = builder::build_mouse_mask(name, &[enabled])?;
        self.exec_api(
            command.as_bytes(),
            button_mask_opcode(button),
            ApiVerb::Set,
            &[enabled as u8],
        )
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
        let values = [left, right, down, up];
        let payload = [left as u8, right as u8, down as u8, up as u8];
        let command = builder::build_mouse_mask("move_mask", &values)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::MoveMask,
            ApiVerb::Set,
            &payload,
        )
    }

    pub fn wheel_mask(&self, down: bool, up: bool) -> Result<()> {
        let values = [down, up];
        let payload = [down as u8, up as u8];
        let command = builder::build_mouse_mask("wheel_mask", &values)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::WheelMask,
            ApiVerb::Set,
            &payload,
        )
    }

    /// Relative mouse move. Coordinates are in HID units, range ±32767.
    pub fn move_xy(&self, x: i32, y: i32) -> Result<()> {
        timed!("move_xy", {
            let command = builder::build_move(x, y)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            self.exec_api(command.as_bytes(), ApiOpcode::Move, ApiVerb::Exec, &payload)
        })
    }

    pub fn move_xy_dt(&self, x: i32, y: i32, dt_uframes: u16) -> Result<()> {
        timed!("move_xy_dt", {
            let command = builder::build_move_dt(x, y, dt_uframes)?;
            let mut payload = Vec::with_capacity(6);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(command.as_bytes(), ApiOpcode::Move, ApiVerb::Exec, &payload)
        })
    }

    pub fn move_controls(
        &self,
        x: i32,
        y: i32,
        segments: u32,
        ctrl_x1: i32,
        ctrl_y1: i32,
        ctrl_x2: Option<i32>,
        ctrl_y2: Option<i32>,
    ) -> Result<()> {
        timed!("move_controls", {
            let command =
                builder::build_move_controls(x, y, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn click_count(&self, button: u8, count: u32, delay_ms: u32) -> Result<()> {
        timed!(
            "click_count",
            self.exec_dynamic(builder::build_click(button, count, delay_ms).as_bytes())
        )
    }

    pub fn axis_stream(&self) -> Result<String> {
        timed!("axis_stream", {
            self.query_dynamic(builder::build_mode_query("axis").as_bytes())
        })
    }

    pub fn set_axis_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_axis_stream", {
            let command = builder::build_mode("axis", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn mouse_stream(&self) -> Result<String> {
        timed!("mouse_stream", {
            self.query_dynamic(builder::build_mode_query("mouse").as_bytes())
        })
    }

    pub fn set_mouse_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_mouse_stream", {
            let command = builder::build_mode("mouse", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn button_stream(&self) -> Result<String> {
        timed!("button_stream", {
            self.query_dynamic(builder::build_mode_query("buttons").as_bytes())
        })
    }

    pub fn set_button_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_button_stream", {
            let command = builder::build_mode("buttons", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn echo(&self) -> Result<String> {
        timed!("echo", {
            self.query_dynamic(builder::build_echo(None).as_bytes())
        })
    }

    pub fn set_echo(&self, enabled: bool) -> Result<()> {
        timed!(
            "set_echo",
            self.exec_dynamic(builder::build_echo(Some(enabled)).as_bytes())
        )
    }

    /// Left-down → move(x,y) → left-up in two HID frames.
    /// Useful for drag-like repositioning without a visible click.
    pub fn silent_move(&self, x: i32, y: i32) -> Result<()> {
        timed!("silent_move", {
            self.exec_dynamic(builder::build_silent_move(x, y)?.as_bytes())
        })
    }

    /// Scroll wheel. Range ±127. Positive = up, negative = down.
    pub fn wheel(&self, delta: i32) -> Result<()> {
        timed!("wheel", {
            let command = builder::build_wheel(delta)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::Wheel,
                ApiVerb::Exec,
                &(delta as i16).to_le_bytes(),
            )
        })
    }

    pub fn wheel_dt(&self, delta: i32, dt_uframes: u16) -> Result<()> {
        timed!("wheel_dt", {
            let command = builder::build_wheel_dt(delta, dt_uframes)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(delta as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::Wheel,
                ApiVerb::Exec,
                &payload,
            )
        })
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn button_mask(&self, button: Button, enabled: bool) -> Result<()> {
        let name = match button {
            Button::Left => "left_mask",
            Button::Right => "right_mask",
            Button::Middle => "middle_mask",
            Button::Side1 => "side1_mask",
            Button::Side2 => "side2_mask",
        };
        let command = builder::build_mouse_mask(name, &[enabled])?;
        self.exec_api(
            command.as_bytes(),
            button_mask_opcode(button),
            ApiVerb::Set,
            &[enabled as u8],
        )
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
        let values = [left, right, down, up];
        let payload = [left as u8, right as u8, down as u8, up as u8];
        let command = builder::build_mouse_mask("move_mask", &values)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::MoveMask,
            ApiVerb::Set,
            &payload,
        )
        .await
    }

    pub async fn wheel_mask(&self, down: bool, up: bool) -> Result<()> {
        let values = [down, up];
        let payload = [down as u8, up as u8];
        let command = builder::build_mouse_mask("wheel_mask", &values)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::WheelMask,
            ApiVerb::Set,
            &payload,
        )
        .await
    }

    pub async fn move_xy(&self, x: i32, y: i32) -> Result<()> {
        timed!("move_xy", {
            let command = builder::build_move(x, y)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            self.exec_api(command.as_bytes(), ApiOpcode::Move, ApiVerb::Exec, &payload)
                .await
        })
    }

    pub async fn move_xy_dt(&self, x: i32, y: i32, dt_uframes: u16) -> Result<()> {
        timed!("move_xy_dt", {
            let command = builder::build_move_dt(x, y, dt_uframes)?;
            let mut payload = Vec::with_capacity(6);
            payload.extend_from_slice(&(x as i16).to_le_bytes());
            payload.extend_from_slice(&(y as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(command.as_bytes(), ApiOpcode::Move, ApiVerb::Exec, &payload)
                .await
        })
    }

    pub async fn move_controls(
        &self,
        x: i32,
        y: i32,
        segments: u32,
        ctrl_x1: i32,
        ctrl_y1: i32,
        ctrl_x2: Option<i32>,
        ctrl_y2: Option<i32>,
    ) -> Result<()> {
        timed!("move_controls", {
            let command =
                builder::build_move_controls(x, y, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn click_count(&self, button: u8, count: u32, delay_ms: u32) -> Result<()> {
        timed!(
            "click_count",
            self.exec_dynamic(builder::build_click(button, count, delay_ms).as_bytes())
                .await
        )
    }

    pub async fn axis_stream(&self) -> Result<String> {
        timed!("axis_stream", {
            self.query_dynamic(builder::build_mode_query("axis").as_bytes())
                .await
        })
    }

    pub async fn set_axis_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_axis_stream", {
            let command = builder::build_mode("axis", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn mouse_stream(&self) -> Result<String> {
        timed!("mouse_stream", {
            self.query_dynamic(builder::build_mode_query("mouse").as_bytes())
                .await
        })
    }

    pub async fn set_mouse_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_mouse_stream", {
            let command = builder::build_mode("mouse", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn button_stream(&self) -> Result<String> {
        timed!("button_stream", {
            self.query_dynamic(builder::build_mode_query("buttons").as_bytes())
                .await
        })
    }

    pub async fn set_button_stream(&self, mode: &str, period_ms: Option<u16>) -> Result<()> {
        timed!("set_button_stream", {
            let command = builder::build_mode("buttons", mode, period_ms)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn echo(&self) -> Result<String> {
        timed!("echo", {
            self.query_dynamic(builder::build_echo(None).as_bytes())
                .await
        })
    }

    pub async fn set_echo(&self, enabled: bool) -> Result<()> {
        timed!(
            "set_echo",
            self.exec_dynamic(builder::build_echo(Some(enabled)).as_bytes())
                .await
        )
    }

    /// Left-down → move(x,y) → left-up in two HID frames.
    pub async fn silent_move(&self, x: i32, y: i32) -> Result<()> {
        timed!("silent_move", {
            self.exec_dynamic(builder::build_silent_move(x, y)?.as_bytes())
                .await
        })
    }

    /// Scroll wheel. Range ±127.
    pub async fn wheel(&self, delta: i32) -> Result<()> {
        timed!("wheel", {
            let command = builder::build_wheel(delta)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::Wheel,
                ApiVerb::Exec,
                &(delta as i16).to_le_bytes(),
            )
            .await
        })
    }

    pub async fn wheel_dt(&self, delta: i32, dt_uframes: u16) -> Result<()> {
        timed!("wheel_dt", {
            let command = builder::build_wheel_dt(delta, dt_uframes)?;
            let mut payload = Vec::with_capacity(4);
            payload.extend_from_slice(&(delta as i16).to_le_bytes());
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::Wheel,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }
}
