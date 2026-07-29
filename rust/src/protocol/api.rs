use crate::error::{MakxdError, Result};
use crate::types::{Button, ControllerButton, KeyboardKey};

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
    Version = 0x01,
    Device = 0x02,
    Buttons = 0x10,
    Left = 0x11,
    Right = 0x12,
    Middle = 0x13,
    Side1 = 0x14,
    Side2 = 0x15,
    Move = 0x18,
    Wheel = 0x19,
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
    ControllerLt = 0x43,
    ControllerRt = 0x44,
    ControllerLeftStick = 0x45,
    ControllerRightStick = 0x46,
    ControllerAux = 0x47,
    ControllerHatLeft = 0x48,
    ControllerHatRight = 0x49,
    ControllerHatDown = 0x4a,
    ControllerHatUp = 0x4b,
    ControllerLtMask = 0x52,
    ControllerRtMask = 0x53,
    ControllerLeftStickMask = 0x54,
    ControllerRightStickMask = 0x55,
    ControllerAuxMask = 0x56,
    ControllerHatLeftMask = 0x58,
    ControllerHatRightMask = 0x59,
    ControllerHatDownMask = 0x5a,
    ControllerHatUpMask = 0x5b,
    ControllerButton1 = 0x60,
    ControllerButton2 = 0x61,
    ControllerButton3 = 0x62,
    ControllerButton4 = 0x63,
    ControllerButton5 = 0x64,
    ControllerButton6 = 0x65,
    ControllerButton7 = 0x66,
    ControllerButton8 = 0x67,
    ControllerButton9 = 0x68,
    ControllerButton10 = 0x69,
    ControllerButton11 = 0x6a,
    ControllerButton12 = 0x6b,
    ControllerButton13 = 0x6c,
    ControllerButton14 = 0x6d,
    ControllerButton15 = 0x6e,
    ControllerButton16 = 0x6f,
    ControllerButton17 = 0x70,
    ControllerButton18 = 0x71,
    ControllerButton19 = 0x72,
    ControllerButton20 = 0x73,
    ControllerButton21 = 0x74,
    ControllerButton22 = 0x75,
    ControllerButton23 = 0x76,
    ControllerButton24 = 0x77,
    ControllerButton25 = 0x78,
    ControllerButton26 = 0x79,
    ControllerButton27 = 0x7a,
    ControllerButton28 = 0x7b,
    ControllerButton29 = 0x7c,
    ControllerButton30 = 0x7d,
    ControllerButton31 = 0x7e,
    ControllerButton32 = 0x7f,
    ControllerButton1Mask = 0x80,
    ControllerButton2Mask = 0x81,
    ControllerButton3Mask = 0x82,
    ControllerButton4Mask = 0x83,
    ControllerButton5Mask = 0x84,
    ControllerButton6Mask = 0x85,
    ControllerButton7Mask = 0x86,
    ControllerButton8Mask = 0x87,
    ControllerButton9Mask = 0x88,
    ControllerButton10Mask = 0x89,
    ControllerButton11Mask = 0x8a,
    ControllerButton12Mask = 0x8b,
    ControllerButton13Mask = 0x8c,
    ControllerButton14Mask = 0x8d,
    ControllerButton15Mask = 0x8e,
    ControllerButton16Mask = 0x8f,
    ControllerButton17Mask = 0x90,
    ControllerButton18Mask = 0x91,
    ControllerButton19Mask = 0x92,
    ControllerButton20Mask = 0x93,
    ControllerButton21Mask = 0x94,
    ControllerButton22Mask = 0x95,
    ControllerButton23Mask = 0x96,
    ControllerButton24Mask = 0x97,
    ControllerButton25Mask = 0x98,
    ControllerButton26Mask = 0x99,
    ControllerButton27Mask = 0x9a,
    ControllerButton28Mask = 0x9b,
    ControllerButton29Mask = 0x9c,
    ControllerButton30Mask = 0x9d,
    ControllerButton31Mask = 0x9e,
    ControllerButton32Mask = 0x9f,
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

pub(crate) fn controller_button_opcode(button: ControllerButton) -> ApiOpcode {
    match button {
        ControllerButton::Button1 => ApiOpcode::ControllerButton1,
        ControllerButton::Button2 => ApiOpcode::ControllerButton2,
        ControllerButton::Button3 => ApiOpcode::ControllerButton3,
        ControllerButton::Button4 => ApiOpcode::ControllerButton4,
        ControllerButton::Button5 => ApiOpcode::ControllerButton5,
        ControllerButton::Button6 => ApiOpcode::ControllerButton6,
        ControllerButton::Button7 => ApiOpcode::ControllerButton7,
        ControllerButton::Button8 => ApiOpcode::ControllerButton8,
        ControllerButton::Button9 => ApiOpcode::ControllerButton9,
        ControllerButton::Button10 => ApiOpcode::ControllerButton10,
        ControllerButton::Button11 => ApiOpcode::ControllerButton11,
        ControllerButton::Button12 => ApiOpcode::ControllerButton12,
        ControllerButton::Button13 => ApiOpcode::ControllerButton13,
        ControllerButton::Button14 => ApiOpcode::ControllerButton14,
        ControllerButton::Button15 => ApiOpcode::ControllerButton15,
        ControllerButton::Button16 => ApiOpcode::ControllerButton16,
        ControllerButton::Button17 => ApiOpcode::ControllerButton17,
        ControllerButton::Button18 => ApiOpcode::ControllerButton18,
        ControllerButton::Button19 => ApiOpcode::ControllerButton19,
        ControllerButton::Button20 => ApiOpcode::ControllerButton20,
        ControllerButton::Button21 => ApiOpcode::ControllerButton21,
        ControllerButton::Button22 => ApiOpcode::ControllerButton22,
        ControllerButton::Button23 => ApiOpcode::ControllerButton23,
        ControllerButton::Button24 => ApiOpcode::ControllerButton24,
        ControllerButton::Button25 => ApiOpcode::ControllerButton25,
        ControllerButton::Button26 => ApiOpcode::ControllerButton26,
        ControllerButton::Button27 => ApiOpcode::ControllerButton27,
        ControllerButton::Button28 => ApiOpcode::ControllerButton28,
        ControllerButton::Button29 => ApiOpcode::ControllerButton29,
        ControllerButton::Button30 => ApiOpcode::ControllerButton30,
        ControllerButton::Button31 => ApiOpcode::ControllerButton31,
        ControllerButton::Button32 => ApiOpcode::ControllerButton32,
    }
}

pub(crate) fn controller_button_mask_opcode(button: ControllerButton) -> ApiOpcode {
    match button {
        ControllerButton::Button1 => ApiOpcode::ControllerButton1Mask,
        ControllerButton::Button2 => ApiOpcode::ControllerButton2Mask,
        ControllerButton::Button3 => ApiOpcode::ControllerButton3Mask,
        ControllerButton::Button4 => ApiOpcode::ControllerButton4Mask,
        ControllerButton::Button5 => ApiOpcode::ControllerButton5Mask,
        ControllerButton::Button6 => ApiOpcode::ControllerButton6Mask,
        ControllerButton::Button7 => ApiOpcode::ControllerButton7Mask,
        ControllerButton::Button8 => ApiOpcode::ControllerButton8Mask,
        ControllerButton::Button9 => ApiOpcode::ControllerButton9Mask,
        ControllerButton::Button10 => ApiOpcode::ControllerButton10Mask,
        ControllerButton::Button11 => ApiOpcode::ControllerButton11Mask,
        ControllerButton::Button12 => ApiOpcode::ControllerButton12Mask,
        ControllerButton::Button13 => ApiOpcode::ControllerButton13Mask,
        ControllerButton::Button14 => ApiOpcode::ControllerButton14Mask,
        ControllerButton::Button15 => ApiOpcode::ControllerButton15Mask,
        ControllerButton::Button16 => ApiOpcode::ControllerButton16Mask,
        ControllerButton::Button17 => ApiOpcode::ControllerButton17Mask,
        ControllerButton::Button18 => ApiOpcode::ControllerButton18Mask,
        ControllerButton::Button19 => ApiOpcode::ControllerButton19Mask,
        ControllerButton::Button20 => ApiOpcode::ControllerButton20Mask,
        ControllerButton::Button21 => ApiOpcode::ControllerButton21Mask,
        ControllerButton::Button22 => ApiOpcode::ControllerButton22Mask,
        ControllerButton::Button23 => ApiOpcode::ControllerButton23Mask,
        ControllerButton::Button24 => ApiOpcode::ControllerButton24Mask,
        ControllerButton::Button25 => ApiOpcode::ControllerButton25Mask,
        ControllerButton::Button26 => ApiOpcode::ControllerButton26Mask,
        ControllerButton::Button27 => ApiOpcode::ControllerButton27Mask,
        ControllerButton::Button28 => ApiOpcode::ControllerButton28Mask,
        ControllerButton::Button29 => ApiOpcode::ControllerButton29Mask,
        ControllerButton::Button30 => ApiOpcode::ControllerButton30Mask,
        ControllerButton::Button31 => ApiOpcode::ControllerButton31Mask,
        ControllerButton::Button32 => ApiOpcode::ControllerButton32Mask,
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
    fn mak_api_key_names_match_firmware_hid_codes() {
        assert_eq!(keyboard_code(&KeyboardKey::name("a")).unwrap(), 4);
        assert_eq!(keyboard_code(&KeyboardKey::name("f12")).unwrap(), 69);
        assert_eq!(keyboard_code(&KeyboardKey::name("rightctrl")).unwrap(), 228);
        assert!(keyboard_code(&KeyboardKey::name("missing")).is_err());
    }

    #[test]
    fn mak_api_controller_buttons_have_exact_named_opcodes() {
        let buttons = [
            ControllerButton::Button1,
            ControllerButton::Button2,
            ControllerButton::Button3,
            ControllerButton::Button4,
            ControllerButton::Button5,
            ControllerButton::Button6,
            ControllerButton::Button7,
            ControllerButton::Button8,
            ControllerButton::Button9,
            ControllerButton::Button10,
            ControllerButton::Button11,
            ControllerButton::Button12,
            ControllerButton::Button13,
            ControllerButton::Button14,
            ControllerButton::Button15,
            ControllerButton::Button16,
            ControllerButton::Button17,
            ControllerButton::Button18,
            ControllerButton::Button19,
            ControllerButton::Button20,
            ControllerButton::Button21,
            ControllerButton::Button22,
            ControllerButton::Button23,
            ControllerButton::Button24,
            ControllerButton::Button25,
            ControllerButton::Button26,
            ControllerButton::Button27,
            ControllerButton::Button28,
            ControllerButton::Button29,
            ControllerButton::Button30,
            ControllerButton::Button31,
            ControllerButton::Button32,
        ];
        for (index, button) in buttons.into_iter().enumerate() {
            assert_eq!(controller_button_opcode(button) as u8, 0x60 + index as u8);
            assert_eq!(
                controller_button_mask_opcode(button) as u8,
                0x80 + index as u8
            );
        }
    }
}
