#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct DeviceRoute {
    pub route_mask: u8,
    pub mouse_uframes: u16,
    pub keyboard_uframes: u16,
    pub controller_uframes: u16,
    pub generation: u32,
}

impl DeviceRoute {
    pub fn mouse(self) -> bool {
        self.route_mask & 0x01 != 0
    }

    pub fn keyboard(self) -> bool {
        self.route_mask & 0x02 != 0
    }

    pub fn controller(self) -> bool {
        self.route_mask & 0x04 != 0
    }

    fn hz(uframes: u16) -> f32 {
        if uframes == 0 {
            0.0
        } else {
            8000.0 / uframes as f32
        }
    }

    pub fn mouse_hz(self) -> f32 {
        Self::hz(self.mouse_uframes)
    }

    pub fn keyboard_hz(self) -> f32 {
        Self::hz(self.keyboard_uframes)
    }

    pub fn controller_hz(self) -> f32 {
        Self::hz(self.controller_uframes)
    }
}
