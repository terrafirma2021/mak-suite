use crate::error::{MakxdError, Result};
use crate::types::{Button, KeyboardKey};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
#[repr(u8)]
pub enum ApiProtocol {
    #[default]
    Km = 0,
    MakApi = 1,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ApiVerb {
    Get = 0x00,
    Set = 0x01,
    Exec = 0x02,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ApiOpcode {
    Device = 0x02,
    Buttons = 0x10,
    Left = 0x11,
    Right = 0x12,
    Middle = 0x13,
    Side1 = 0x14,
    Side2 = 0x15,
    MoveMask = 0x16,
    WheelMask = 0x17,
    Move = 0x18,
    Wheel = 0x19,
    LeftMask = 0x1a,
    RightMask = 0x1b,
    MiddleMask = 0x1c,
    Side1Mask = 0x1d,
    Side2Mask = 0x1e,
    KeyDown = 0x20,
    KeyUp = 0x21,
    KeyInit = 0x22,
    KeyPress = 0x23,
    KeyString = 0x24,
    KeyIsDown = 0x25,
    KeyMultiDown = 0x26,
    KeyMultiUp = 0x27,
    KeyMultiPress = 0x28,
    KeyMask = 0x29,
    KeyRemap = 0x2a,
    KeyKeys = 0x2b,
    ControllerState = 0x40,
    ControllerControl = 0x41,
    ControllerMask = 0x51,
}

pub(crate) fn mak_api_command(opcode: ApiOpcode, verb: ApiVerb, payload: &[u8]) -> Result<Vec<u8>> {
    let payload_len = 3usize + payload.len();
    if payload_len > 251 {
        return Err(MakxdError::Protocol(
            "MAK_API command exceeds the COM frame limit".into(),
        ));
    }
    let mut frame = Vec::with_capacity(5 + payload_len);
    frame.extend_from_slice(&[0xde, 0xad]);
    frame.extend_from_slice(&(payload_len as u16).to_le_bytes());
    frame.push(0);
    frame.extend_from_slice(&[0, opcode as u8, verb as u8]);
    frame.extend_from_slice(payload);
    Ok(frame)
}

pub(crate) fn button_opcode(button: Button) -> ApiOpcode {
    match button {
        Button::Left => ApiOpcode::Left,
        Button::Right => ApiOpcode::Right,
        Button::Middle => ApiOpcode::Middle,
        Button::Side1 => ApiOpcode::Side1,
        Button::Side2 => ApiOpcode::Side2,
    }
}

pub(crate) fn button_mask_opcode(button: Button) -> ApiOpcode {
    match button {
        Button::Left => ApiOpcode::LeftMask,
        Button::Right => ApiOpcode::RightMask,
        Button::Middle => ApiOpcode::MiddleMask,
        Button::Side1 => ApiOpcode::Side1Mask,
        Button::Side2 => ApiOpcode::Side2Mask,
    }
}

pub(crate) fn keyboard_code(key: &KeyboardKey) -> Result<u8> {
    let KeyboardKey::Name(name) = key else {
        return match key {
            KeyboardKey::Hid(code) => Ok(*code),
            KeyboardKey::Name(_) => unreachable!(),
        };
    };
    let name = name.to_ascii_lowercase();
    if name.len() == 1 {
        let byte = name.as_bytes()[0];
        return match byte {
            b'a'..=b'z' => Ok(4 + byte - b'a'),
            b'1'..=b'9' => Ok(30 + byte - b'1'),
            b'0' => Ok(39),
            _ => Err(MakxdError::Protocol(format!(
                "unknown keyboard key name: {name}"
            ))),
        };
    }
    if let Some(number) = name
        .strip_prefix('f')
        .and_then(|value| value.parse::<u8>().ok())
        && (1..=12).contains(&number)
    {
        return Ok(57 + number);
    }
    if name.len() == 3 && (name.starts_with("kp") || name.starts_with("np")) {
        if let Some(number) = name.as_bytes()[2]
            .checked_sub(b'0')
            .filter(|number| *number <= 9)
        {
            return Ok(if number == 0 { 98 } else { 88 + number });
        }
    }
    let code = match name.as_str() {
        "enter" | "return" => 40,
        "escape" | "esc" => 41,
        "backspace" | "back" => 42,
        "tab" => 43,
        "space" | "spacebar" => 44,
        "minus" | "dash" | "hyphen" => 45,
        "equals" | "equal" => 46,
        "leftbracket" | "lbracket" | "openbracket" => 47,
        "rightbracket" | "rbracket" | "closebracket" => 48,
        "backslash" | "bslash" => 49,
        "nonus_hash" => 50,
        "semicolon" | "semi" => 51,
        "quote" | "apostrophe" | "singlequote" => 52,
        "grave" | "backtick" | "tilde" => 53,
        "comma" => 54,
        "period" | "dot" => 55,
        "slash" | "forwardslash" | "fslash" => 56,
        "capslock" | "caps" => 57,
        "printscreen" | "prtsc" | "print" => 70,
        "scrolllock" | "scroll" => 71,
        "pause" | "break" => 72,
        "insert" | "ins" => 73,
        "home" => 74,
        "pageup" | "pgup" => 75,
        "delete" | "del" => 76,
        "end" => 77,
        "pagedown" | "pgdown" | "pgdn" => 78,
        "right" | "rightarrow" => 79,
        "left" | "leftarrow" => 80,
        "down" | "downarrow" => 81,
        "up" | "uparrow" => 82,
        "numlock" | "num" => 83,
        "kpdivide" | "npdivide" => 84,
        "kpmultiply" | "npmultiply" => 85,
        "kpminus" | "npminus" => 86,
        "kpplus" | "npplus" => 87,
        "kpenter" | "npenter" => 88,
        "kpperiod" | "kpdot" | "npperiod" | "npdot" => 99,
        "leftctrl" | "lctrl" | "leftcontrol" | "lcontrol" | "ctrl" | "control" => 224,
        "leftshift" | "lshift" | "shift" => 225,
        "leftalt" | "lalt" | "alt" => 226,
        "leftgui" | "lgui" | "leftwin" | "lwin" | "gui" | "win" | "windows" | "super" | "meta"
        | "cmd" | "command" => 227,
        "rightctrl" | "rctrl" | "rightcontrol" | "rcontrol" => 228,
        "rightshift" | "rshift" => 229,
        "rightalt" | "ralt" => 230,
        "rightgui" | "rgui" | "rightwin" | "rwin" | "rightwindows" => 231,
        _ => {
            return Err(MakxdError::Protocol(format!(
                "unknown keyboard key name: {name}"
            )));
        }
    };
    Ok(code)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn mak_api_move_omits_or_appends_dt_exactly() {
        let no_dt = mak_api_command(ApiOpcode::Move, ApiVerb::Exec, &[1, 0, 2, 0]).unwrap();
        assert_eq!(no_dt, [0xde, 0xad, 7, 0, 0, 0, 0x18, 2, 1, 0, 2, 0]);

        let with_dt = mak_api_command(ApiOpcode::Move, ApiVerb::Exec, &[1, 0, 2, 0, 0, 0]).unwrap();
        assert_eq!(with_dt, [0xde, 0xad, 9, 0, 0, 0, 0x18, 2, 1, 0, 2, 0, 0, 0]);
    }

    #[test]
    fn mak_api_mouse_masks_have_exact_named_opcodes() {
        assert_eq!(ApiOpcode::MoveMask as u8, 0x16);
        assert_eq!(ApiOpcode::WheelMask as u8, 0x17);
        assert_eq!(button_mask_opcode(Button::Left) as u8, 0x1a);
        assert_eq!(button_mask_opcode(Button::Right) as u8, 0x1b);
        assert_eq!(button_mask_opcode(Button::Middle) as u8, 0x1c);
        assert_eq!(button_mask_opcode(Button::Side1) as u8, 0x1d);
        assert_eq!(button_mask_opcode(Button::Side2) as u8, 0x1e);
    }

    #[test]
    fn mak_api_key_names_match_firmware_hid_codes() {
        assert_eq!(keyboard_code(&KeyboardKey::name("a")).unwrap(), 4);
        assert_eq!(keyboard_code(&KeyboardKey::name("f12")).unwrap(), 69);
        assert_eq!(keyboard_code(&KeyboardKey::name("rightctrl")).unwrap(), 228);
        assert!(keyboard_code(&KeyboardKey::name("missing")).is_err());
    }

    #[test]
    fn mak_api_controller_has_one_semantic_opcode_set() {
        assert_eq!(ApiOpcode::ControllerState as u8, 0x40);
        assert_eq!(ApiOpcode::ControllerControl as u8, 0x41);
        assert_eq!(ApiOpcode::ControllerMask as u8, 0x51);
    }

    #[test]
    fn public_default_and_controller_names_match_the_final_contract() {
        use crate::device::DeviceConfig;
        use crate::types::ControllerControl;

        assert_eq!(DeviceConfig::default().api_protocol, ApiProtocol::MakApi);
        assert_eq!(ControllerControl::South as u8, 0);
        assert_eq!(ControllerControl::GripRight as u8, 22);
        assert_eq!(ControllerControl::Extra32 as u8, 54);
        assert_eq!(ControllerControl::South.name(), "south");
        assert_eq!(ControllerControl::LeftStickX.name(), "left_stick_x");
        assert_eq!(ControllerControl::Extra32.name(), "extra_32");
    }
}
