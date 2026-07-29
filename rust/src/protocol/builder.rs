use std::io::Write;

use crate::error::{MakxdError, Result};
use crate::types::Button;

/// Maximum absolute value for move/silent_move coordinates (firmware limit).
pub const MOVE_RANGE: i32 = 32767;
/// Maximum absolute value for wheel scroll (firmware limit).
pub const WHEEL_RANGE: i32 = 127;

/// Stack-allocated command buffer for parametric commands.
/// Avoids heap allocation on the move/wheel hot path.
pub struct CommandBuf {
    buf: [u8; 128],
    len: usize,
}

impl CommandBuf {
    fn new() -> Self {
        Self {
            buf: [0u8; 128],
            len: 0,
        }
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.buf[..self.len]
    }
}

pub const DT_UFRAMES_MAX: u16 = 0x3FFF;

fn check_dt_uframes(dt_uframes: u16) -> Result<()> {
    if dt_uframes > DT_UFRAMES_MAX {
        return Err(MakxdError::OutOfRange {
            value: dt_uframes as i64,
            min: 0,
            max: DT_UFRAMES_MAX as i64,
        });
    }
    Ok(())
}

pub(crate) fn build_controller_command(
    name: &str,
    values: &[i64],
    dt_uframes: Option<u16>,
) -> Result<CommandBuf> {
    if let Some(dt) = dt_uframes {
        check_dt_uframes(dt)?;
    }
    build_cmd(|buf| {
        write!(buf, "km.{name}(")?;
        for (index, value) in values.iter().enumerate() {
            if index != 0 {
                write!(buf, ",")?;
            }
            write!(buf, "{value}")?;
        }
        if let Some(dt) = dt_uframes {
            write!(buf, ",{dt}")?;
        }
        write!(buf, ")\r\n")
    })
}

/// Build `km.move(x,y)\r\n`. Returns an error if coordinates exceed ±32767.
pub fn build_move(x: i32, y: i32) -> Result<CommandBuf> {
    check_move_range(x, "x")?;
    check_move_range(y, "y")?;
    build_cmd(|buf| write!(buf, "km.move({},{})\r\n", x, y))
}

pub fn build_move_dt(x: i32, y: i32, dt_uframes: u16) -> Result<CommandBuf> {
    check_move_range(x, "x")?;
    check_move_range(y, "y")?;
    check_dt_uframes(dt_uframes)?;
    build_cmd(|buf| write!(buf, "km.move({x},{y},{dt_uframes})\r\n"))
}

pub fn build_button_dt(button: Button, pressed: bool, dt_uframes: u16) -> Result<CommandBuf> {
    check_dt_uframes(dt_uframes)?;
    let name = match button {
        Button::Left => "left",
        Button::Right => "right",
        Button::Middle => "middle",
        Button::Side1 => "side1",
        Button::Side2 => "side2",
    };
    let state = if pressed { 1 } else { 0 };
    build_cmd(|buf| write!(buf, "km.{name}({state},{dt_uframes})\r\n"))
}

/// Build the full relative move form accepted by the device parser.
pub fn build_move_controls(
    x: i32,
    y: i32,
    segments: u32,
    ctrl_x1: i32,
    ctrl_y1: i32,
    ctrl_x2: Option<i32>,
    ctrl_y2: Option<i32>,
) -> Result<String> {
    check_move_range(x, "x")?;
    check_move_range(y, "y")?;
    check_move_range(ctrl_x1, "ctrl_x1")?;
    check_move_range(ctrl_y1, "ctrl_y1")?;
    let ctrl_x2 = ctrl_x2.unwrap_or(ctrl_x1);
    let ctrl_y2 = ctrl_y2.unwrap_or(ctrl_y1);
    check_move_range(ctrl_x2, "ctrl_x2")?;
    check_move_range(ctrl_y2, "ctrl_y2")?;
    Ok(format!(
        "km.move({x},{y},{segments},{ctrl_x1},{ctrl_y1},{ctrl_x2},{ctrl_y2})\r\n"
    ))
}

pub fn build_click(button: u8, count: u32, delay_ms: u32) -> String {
    format!("km.click({button},{count},{delay_ms})\r\n")
}

pub fn build_mode(command: &str, mode: &str, period_ms: Option<u16>) -> Result<String> {
    let mode = mode.trim();
    if mode.is_empty() {
        return Err(MakxdError::Protocol("stream mode cannot be empty".into()));
    }
    Ok(match period_ms {
        Some(period) => format!("km.{command}({mode},{period})\r\n"),
        None => format!("km.{command}({mode})\r\n"),
    })
}

pub fn build_mode_query(command: &str) -> String {
    format!("km.{command}()\r\n")
}

pub fn build_echo(enabled: Option<bool>) -> String {
    match enabled {
        Some(enabled) => format!("km.echo({})\r\n", if enabled { 1 } else { 0 }),
        None => "km.echo()\r\n".to_owned(),
    }
}

/// Build `km.silent(x,y)\r\n`. Returns an error if coordinates exceed ±32767.
pub fn build_silent_move(x: i32, y: i32) -> Result<CommandBuf> {
    check_move_range(x, "x")?;
    check_move_range(y, "y")?;
    build_cmd(|buf| write!(buf, "km.silent({},{})\r\n", x, y))
}

/// Build `km.wheel(delta)\r\n`. Returns an error if delta exceeds ±127.
pub fn build_wheel(delta: i32) -> Result<CommandBuf> {
    if !(-WHEEL_RANGE..=WHEEL_RANGE).contains(&delta) {
        return Err(MakxdError::OutOfRange {
            value: delta as i64,
            min: -WHEEL_RANGE as i64,
            max: WHEEL_RANGE as i64,
        });
    }
    build_cmd(|buf| write!(buf, "km.wheel({})\r\n", delta))
}

pub fn build_wheel_dt(delta: i32, dt_uframes: u16) -> Result<CommandBuf> {
    if !(-WHEEL_RANGE..=WHEEL_RANGE).contains(&delta) {
        return Err(MakxdError::OutOfRange {
            value: delta as i64,
            min: -WHEEL_RANGE as i64,
            max: WHEEL_RANGE as i64,
        });
    }
    check_dt_uframes(dt_uframes)?;
    build_cmd(|buf| write!(buf, "km.wheel({delta},{dt_uframes})\r\n"))
}
fn check_move_range(v: i32, _axis: &str) -> Result<()> {
    if !(-MOVE_RANGE..=MOVE_RANGE).contains(&v) {
        return Err(MakxdError::OutOfRange {
            value: v as i64,
            min: -MOVE_RANGE as i64,
            max: MOVE_RANGE as i64,
        });
    }
    Ok(())
}

fn build_cmd(f: impl FnOnce(&mut &mut [u8]) -> std::io::Result<()>) -> Result<CommandBuf> {
    let mut cmd = CommandBuf::new();
    let mut buf: &mut [u8] = &mut cmd.buf[..];
    f(&mut buf).map_err(|_| MakxdError::Protocol("command too long for buffer".into()))?;
    cmd.len = fmt_len(&cmd.buf);
    Ok(cmd)
}

/// Build `km.serial('value')\r\n`
///
/// Returns an error if the value is too long to fit in the 64-byte command buffer.
/// The maximum value length is ~45 characters.
/// Maximum length for a serial number value (firmware limit).
pub const SERIAL_MAX_LEN: usize = 45;

pub fn build_serial_set(value: &str) -> Result<CommandBuf> {
    // km.serial('')\r\n = 16 bytes overhead, leaving ~48 chars for value
    if value.len() > SERIAL_MAX_LEN {
        return Err(MakxdError::OutOfRange {
            value: value.len() as i64,
            min: 0,
            max: SERIAL_MAX_LEN as i64,
        });
    }
    build_cmd(|buf| write!(buf, "km.serial('{}')\r\n", value))
}

/// Find the actual length of the formatted string in the buffer.
fn fmt_len(buf: &[u8]) -> usize {
    // Find the \n that terminates our command
    buf.iter()
        .position(|&b| b == b'\n')
        .map(|p| p + 1)
        .unwrap_or(0)
}

#[cfg(test)]
mod dt_tests {
    use super::*;

    #[test]
    fn builds_timed_mouse_commands() {
        assert_eq!(
            build_button_dt(Button::Left, true, 0)
                .unwrap()
                .as_bytes(),
            b"km.left(1,0)\r\n"
        );
        assert_eq!(
            build_move_dt(12, -7, 16383).unwrap().as_bytes(),
            b"km.move(12,-7,16383)\r\n"
        );
        assert_eq!(
            build_wheel_dt(-2, 9).unwrap().as_bytes(),
            b"km.wheel(-2,9)\r\n"
        );
    }

    #[test]
    fn rejects_out_of_range_dt() {
        assert!(build_button_dt(Button::Left, true, 16384).is_err());
        assert!(build_move_dt(0, 0, 16384).is_err());
        assert!(build_wheel_dt(0, 16384).is_err());
        assert!(build_controller_command(
            "controller_hat_up_mask", &[1], Some(16384)).is_err());
    }

    #[test]
    fn builds_controller_tuple_and_single_mask_commands() {
        assert_eq!(
            build_controller_command(
                "controller",
                &[3, 8, 10, 20, -1, 2, -3, 4, -5, 6],
                None,
            )
            .unwrap()
            .as_bytes(),
            b"km.controller(3,8,10,20,-1,2,-3,4,-5,6)\r\n"
        );
        assert_eq!(
            build_controller_command(
                "controller_right_stick_mask", &[1, 0, 1, 0], Some(0),
            )
            .unwrap()
            .as_bytes(),
            b"km.controller_right_stick_mask(1,0,1,0,0)\r\n"
        );
    }
}
