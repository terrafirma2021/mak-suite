/// A mouse button.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Button {
    Left,
    Right,
    Middle,
    /// Side button 1 (back).
    Side1,
    /// Side button 2 (forward).
    Side2,
}

/// Full button state snapshot emitted on every button stream event.
///
/// Bit layout: `bit0`=left, `bit1`=right, `bit2`=middle, `bit3`=side1, `bit4`=side2.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ButtonMask(pub(crate) u8);

impl ButtonMask {
    pub fn left(&self) -> bool {
        self.0 & 0x01 != 0
    }

    pub fn right(&self) -> bool {
        self.0 & 0x02 != 0
    }

    pub fn middle(&self) -> bool {
        self.0 & 0x04 != 0
    }

    pub fn side1(&self) -> bool {
        self.0 & 0x08 != 0
    }

    pub fn side2(&self) -> bool {
        self.0 & 0x10 != 0
    }

    pub fn is_pressed(&self, button: Button) -> bool {
        match button {
            Button::Left => self.left(),
            Button::Right => self.right(),
            Button::Middle => self.middle(),
            Button::Side1 => self.side1(),
            Button::Side2 => self.side2(),
        }
    }

    pub fn raw(&self) -> u8 {
        self.0
    }
}
