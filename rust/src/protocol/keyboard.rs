use crate::error::{MakxdError, Result};
use crate::types::KeyboardKey;

pub const KEYBOARD_STRING_MAX_LEN: usize = 256;

pub fn build_down(key: &KeyboardKey) -> Result<String> {
    build_key_command("km.down", key)
}

pub fn build_up(key: &KeyboardKey) -> Result<String> {
    build_key_command("km.up", key)
}

pub fn build_press(
    key: &KeyboardKey,
    hold_ms: Option<u32>,
    rand_ms: Option<u32>,
) -> Result<String> {
    let key_arg = key_argument(key)?;
    let mut command = format!("km.press({key_arg}");
    if let Some(hold_ms) = hold_ms {
        command.push_str(&format!(",{hold_ms}"));
        if let Some(rand_ms) = rand_ms {
            command.push_str(&format!(",{rand_ms}"));
        }
    }
    command.push_str(")\r\n");
    Ok(command)
}

pub fn build_string(text: &str) -> Result<String> {
    if text.len() > KEYBOARD_STRING_MAX_LEN {
        return Err(MakxdError::OutOfRange {
            value: text.len() as i64,
            min: 0,
            max: KEYBOARD_STRING_MAX_LEN as i64,
        });
    }
    if !text.is_ascii() {
        return Err(MakxdError::Protocol(
            "keyboard string must contain ASCII bytes".into(),
        ));
    }
    Ok(format!("km.string(\"{}\")\r\n", escape_double_quoted(text)))
}

pub fn build_init() -> String {
    "km.init()\r\n".to_owned()
}

pub fn build_is_down(key: &KeyboardKey) -> Result<String> {
    build_key_command("km.isdown", key)
}

pub fn build_key_list(command_name: &str, keys: &[KeyboardKey]) -> Result<String> {
    if keys.is_empty() {
        return Err(MakxdError::Protocol("keyboard key list cannot be empty".into()));
    }

    let mut command = format!("{command_name}(");
    for (index, key) in keys.iter().enumerate() {
        if index != 0 {
            command.push(',');
        }
        command.push_str(&key_argument(key)?);
    }
    command.push_str(")\r\n");
    Ok(command)
}

pub fn build_keys(enabled: Option<bool>) -> String {
    match enabled {
        Some(enabled) => format!("km.keys({})\r\n", if enabled { 1 } else { 0 }),
        None => "km.keys()\r\n".to_owned(),
    }
}

pub fn build_mask(key: &KeyboardKey, enable: bool) -> Result<String> {
    let key_arg = key_argument(key)?;
    Ok(format!(
        "km.mask({key_arg},{})\r\n",
        if enable { 1 } else { 0 }
    ))
}

pub fn build_remap(source: &KeyboardKey, target: &KeyboardKey) -> Result<String> {
    Ok(format!(
        "km.remap({},{})\r\n",
        key_argument(source)?,
        key_argument(target)?
    ))
}

fn build_key_command(command: &str, key: &KeyboardKey) -> Result<String> {
    Ok(format!("{command}({})\r\n", key_argument(key)?))
}

fn key_argument(key: &KeyboardKey) -> Result<String> {
    match key {
        KeyboardKey::Hid(code) => Ok(code.to_string()),
        KeyboardKey::Name(name) if name.is_empty() => Err(MakxdError::Protocol(
            "keyboard key name cannot be empty".into(),
        )),
        KeyboardKey::Name(name) => Ok(format!("'{}'", escape_single_quoted(name))),
    }
}

fn escape_single_quoted(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len());
    for byte in value.bytes() {
        match byte {
            b'\\' => escaped.push_str("\\\\"),
            b'\'' => escaped.push_str("\\'"),
            b'\n' => escaped.push_str("\\n"),
            b'\r' => escaped.push_str("\\r"),
            b'\t' => escaped.push_str("\\t"),
            byte if byte.is_ascii_control() => {
                escaped.push_str(&format!("\\x{byte:02X}"));
            }
            byte => escaped.push(byte as char),
        }
    }
    escaped
}

fn escape_double_quoted(value: &str) -> String {
    let mut escaped = String::with_capacity(value.len());
    for byte in value.bytes() {
        match byte {
            b'\\' => escaped.push_str("\\\\"),
            b'"' => escaped.push_str("\\\""),
            b'\n' => escaped.push_str("\\n"),
            b'\r' => escaped.push_str("\\r"),
            b'\t' => escaped.push_str("\\t"),
            byte if byte.is_ascii_control() => {
                escaped.push_str(&format!("\\x{byte:02X}"));
            }
            byte => escaped.push(byte as char),
        }
    }
    escaped
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn builds_named_and_hid_commands() {
        assert_eq!(
            build_down(&KeyboardKey::from("enter")).unwrap(),
            "km.down('enter')\r\n"
        );
        assert_eq!(build_up(&KeyboardKey::from(4u8)).unwrap(), "km.up(4)\r\n");
    }

    #[test]
    fn builds_press_variants() {
        let key = KeyboardKey::from("a");
        assert_eq!(build_press(&key, None, None).unwrap(), "km.press('a')\r\n");
        assert_eq!(
            build_press(&key, Some(10), None).unwrap(),
            "km.press('a',10)\r\n"
        );
        assert_eq!(
            build_press(&key, Some(10), Some(3)).unwrap(),
            "km.press('a',10,3)\r\n"
        );
    }

    #[test]
    fn rejects_empty_names_and_non_ascii_strings() {
        assert!(build_down(&KeyboardKey::from("")).is_err());
        assert!(build_string("é").is_err());
    }
}
