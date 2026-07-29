/// A keyboard key accepted by the MAKXD keyboard command family.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum KeyboardKey {
    /// An unsigned HID usage code.
    Hid(u8),
    /// A firmware-recognized key name.
    Name(String),
}

impl KeyboardKey {
    pub fn hid(code: u8) -> Self {
        Self::Hid(code)
    }

    pub fn name(name: impl Into<String>) -> Self {
        Self::Name(name.into())
    }
}

impl From<u8> for KeyboardKey {
    fn from(code: u8) -> Self {
        Self::Hid(code)
    }
}

impl From<&str> for KeyboardKey {
    fn from(name: &str) -> Self {
        Self::Name(name.to_owned())
    }
}

impl From<String> for KeyboardKey {
    fn from(name: String) -> Self {
        Self::Name(name)
    }
}
