#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum DeviceKind {
    None = 0x00,
    Mouse = 0x01,
    Keyboard = 0x02,
    GenericHid = 0x04,
    Ds4 = 0x08,
    DualSenseDs5 = 0x10,
    DualSenseEdge = 0x20,
    XboxGip = 0x40,
    Xbox360XInput = 0x80,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DeviceKinds {
    pub kinds: u8,
}

impl DeviceKinds {
    pub fn has(self, kind: DeviceKind) -> bool {
        self.kinds & kind as u8 != 0
    }

    pub fn mouse(self) -> bool {
        self.has(DeviceKind::Mouse)
    }

    pub fn keyboard(self) -> bool {
        self.has(DeviceKind::Keyboard)
    }
}

pub(crate) fn device_kinds_parse(value: &[u8]) -> crate::error::Result<DeviceKinds> {
    if value.len() != 1 {
        return Err(crate::error::MakxdError::Protocol(
            "MAK_API device response length is invalid".into(),
        ));
    }
    Ok(DeviceKinds { kinds: value[0] })
}
