#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ControllerButton {
    Button1 = 1,
    Button2 = 2,
    Button3 = 3,
    Button4 = 4,
    Button5 = 5,
    Button6 = 6,
    Button7 = 7,
    Button8 = 8,
    Button9 = 9,
    Button10 = 10,
    Button11 = 11,
    Button12 = 12,
    Button13 = 13,
    Button14 = 14,
    Button15 = 15,
    Button16 = 16,
    Button17 = 17,
    Button18 = 18,
    Button19 = 19,
    Button20 = 20,
    Button21 = 21,
    Button22 = 22,
    Button23 = 23,
    Button24 = 24,
    Button25 = 25,
    Button26 = 26,
    Button27 = 27,
    Button28 = 28,
    Button29 = 29,
    Button30 = 30,
    Button31 = 31,
    Button32 = 32,
}

impl ControllerButton {
    pub const fn number(self) -> u8 {
        self as u8
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct ControllerState {
    pub buttons: u32,
    pub hat: u8,
    pub lt: u16,
    pub rt: u16,
    pub x: i16,
    pub y: i16,
    pub rx: i16,
    pub ry: i16,
    pub z: i16,
    pub rz: i16,
}
