use crate::types::{Button, LockTarget};

// -- USB identifiers --
pub const USB_VID: u16 = 0x1A86;
pub const USB_PID: u16 = 0x55D3;

// -- Baud rates --
pub const BAUD_4M: u32 = 4_000_000;
pub const BAUD_DEFAULT: u32 = 115_200;

/// Binary frame that switches the device from 115200 to 4 Mbaud.
pub const BAUD_FRAME_4M: &[u8] = &[0xDE, 0xAD, 0x05, 0x00, 0xA5, 0x00, 0x09, 0x3D, 0x00];

/// The prompt terminator the device sends after each command response.
pub const PROMPT: &[u8] = b">>> ";

/// The `km.` prefix used by button stream events.
pub const KM_PREFIX: &[u8] = b"km.";

// -- Static commands (no arguments) --
pub const CMD_VERSION: &[u8] = b"km.version()\r\n";
pub const CMD_BUTTONS_ON: &[u8] = b"km.buttons(1)\r\n";
pub const CMD_BUTTONS_OFF: &[u8] = b"km.buttons(0)\r\n";
pub const CMD_BUTTONS_QUERY: &[u8] = b"km.buttons()\r\n";
pub const CMD_SERIAL_GET: &[u8] = b"km.serial()\r\n";
pub const CMD_SERIAL_RESET: &[u8] = b"km.serial(0)\r\n";

// -- Button commands --
pub const CMD_LEFT_DOWN: &[u8] = b"km.left(1)\r\n";
pub const CMD_LEFT_UP: &[u8] = b"km.left(0)\r\n";
pub const CMD_LEFT_FORCE_UP: &[u8] = b"km.left(2)\r\n";
pub const CMD_LEFT_QUERY: &[u8] = b"km.left()\r\n";

pub const CMD_RIGHT_DOWN: &[u8] = b"km.right(1)\r\n";
pub const CMD_RIGHT_UP: &[u8] = b"km.right(0)\r\n";
pub const CMD_RIGHT_FORCE_UP: &[u8] = b"km.right(2)\r\n";
pub const CMD_RIGHT_QUERY: &[u8] = b"km.right()\r\n";

pub const CMD_MIDDLE_DOWN: &[u8] = b"km.middle(1)\r\n";
pub const CMD_MIDDLE_UP: &[u8] = b"km.middle(0)\r\n";
pub const CMD_MIDDLE_FORCE_UP: &[u8] = b"km.middle(2)\r\n";
pub const CMD_MIDDLE_QUERY: &[u8] = b"km.middle()\r\n";

pub const CMD_SIDE1_DOWN: &[u8] = b"km.side1(1)\r\n";
pub const CMD_SIDE1_UP: &[u8] = b"km.side1(0)\r\n";
pub const CMD_SIDE1_FORCE_UP: &[u8] = b"km.side1(2)\r\n";
pub const CMD_SIDE1_QUERY: &[u8] = b"km.side1()\r\n";

pub const CMD_SIDE2_DOWN: &[u8] = b"km.side2(1)\r\n";
pub const CMD_SIDE2_UP: &[u8] = b"km.side2(0)\r\n";
pub const CMD_SIDE2_FORCE_UP: &[u8] = b"km.side2(2)\r\n";
pub const CMD_SIDE2_QUERY: &[u8] = b"km.side2()\r\n";

// -- Catch commands --
pub const CMD_CATCH_LEFT_ON: &[u8] = b"km.catch_ml(0)\r\n";
pub const CMD_CATCH_RIGHT_ON: &[u8] = b"km.catch_mr(0)\r\n";
pub const CMD_CATCH_MIDDLE_ON: &[u8] = b"km.catch_mm(0)\r\n";
pub const CMD_CATCH_SIDE1_ON: &[u8] = b"km.catch_ms1(0)\r\n";
pub const CMD_CATCH_SIDE2_ON: &[u8] = b"km.catch_ms2(0)\r\n";

// -- Lock commands --
pub const CMD_LOCK_X_ON: &[u8] = b"km.lock_mx(1)\r\n";
pub const CMD_LOCK_X_OFF: &[u8] = b"km.lock_mx(0)\r\n";
pub const CMD_LOCK_X_QUERY: &[u8] = b"km.lock_mx()\r\n";

pub const CMD_LOCK_Y_ON: &[u8] = b"km.lock_my(1)\r\n";
pub const CMD_LOCK_Y_OFF: &[u8] = b"km.lock_my(0)\r\n";
pub const CMD_LOCK_Y_QUERY: &[u8] = b"km.lock_my()\r\n";

pub const CMD_LOCK_LEFT_ON: &[u8] = b"km.lock_ml(1)\r\n";
pub const CMD_LOCK_LEFT_OFF: &[u8] = b"km.lock_ml(0)\r\n";
pub const CMD_LOCK_LEFT_QUERY: &[u8] = b"km.lock_ml()\r\n";

pub const CMD_LOCK_RIGHT_ON: &[u8] = b"km.lock_mr(1)\r\n";
pub const CMD_LOCK_RIGHT_OFF: &[u8] = b"km.lock_mr(0)\r\n";
pub const CMD_LOCK_RIGHT_QUERY: &[u8] = b"km.lock_mr()\r\n";

pub const CMD_LOCK_MIDDLE_ON: &[u8] = b"km.lock_mm(1)\r\n";
pub const CMD_LOCK_MIDDLE_OFF: &[u8] = b"km.lock_mm(0)\r\n";
pub const CMD_LOCK_MIDDLE_QUERY: &[u8] = b"km.lock_mm()\r\n";

pub const CMD_LOCK_SIDE1_ON: &[u8] = b"km.lock_ms1(1)\r\n";
pub const CMD_LOCK_SIDE1_OFF: &[u8] = b"km.lock_ms1(0)\r\n";
pub const CMD_LOCK_SIDE1_QUERY: &[u8] = b"km.lock_ms1()\r\n";

pub const CMD_LOCK_SIDE2_ON: &[u8] = b"km.lock_ms2(1)\r\n";
pub const CMD_LOCK_SIDE2_OFF: &[u8] = b"km.lock_ms2(0)\r\n";
pub const CMD_LOCK_SIDE2_QUERY: &[u8] = b"km.lock_ms2()\r\n";

// -- Helpers --

pub fn button_down_cmd(button: Button) -> &'static [u8] {
    match button {
        Button::Left => CMD_LEFT_DOWN,
        Button::Right => CMD_RIGHT_DOWN,
        Button::Middle => CMD_MIDDLE_DOWN,
        Button::Side1 => CMD_SIDE1_DOWN,
        Button::Side2 => CMD_SIDE2_DOWN,
    }
}

pub fn button_up_cmd(button: Button) -> &'static [u8] {
    match button {
        Button::Left => CMD_LEFT_UP,
        Button::Right => CMD_RIGHT_UP,
        Button::Middle => CMD_MIDDLE_UP,
        Button::Side1 => CMD_SIDE1_UP,
        Button::Side2 => CMD_SIDE2_UP,
    }
}

pub fn button_force_up_cmd(button: Button) -> &'static [u8] {
    match button {
        Button::Left => CMD_LEFT_FORCE_UP,
        Button::Right => CMD_RIGHT_FORCE_UP,
        Button::Middle => CMD_MIDDLE_FORCE_UP,
        Button::Side1 => CMD_SIDE1_FORCE_UP,
        Button::Side2 => CMD_SIDE2_FORCE_UP,
    }
}

pub fn button_query_cmd(button: Button) -> &'static [u8] {
    match button {
        Button::Left => CMD_LEFT_QUERY,
        Button::Right => CMD_RIGHT_QUERY,
        Button::Middle => CMD_MIDDLE_QUERY,
        Button::Side1 => CMD_SIDE1_QUERY,
        Button::Side2 => CMD_SIDE2_QUERY,
    }
}

pub fn catch_enable_cmd(button: Button) -> &'static [u8] {
    match button {
        Button::Left => CMD_CATCH_LEFT_ON,
        Button::Right => CMD_CATCH_RIGHT_ON,
        Button::Middle => CMD_CATCH_MIDDLE_ON,
        Button::Side1 => CMD_CATCH_SIDE1_ON,
        Button::Side2 => CMD_CATCH_SIDE2_ON,
    }
}

/// Map a Button to the corresponding LockTarget for catch operations.
pub fn button_to_lock_target(button: Button) -> LockTarget {
    match button {
        Button::Left => LockTarget::Left,
        Button::Right => LockTarget::Right,
        Button::Middle => LockTarget::Middle,
        Button::Side1 => LockTarget::Side1,
        Button::Side2 => LockTarget::Side2,
    }
}

pub fn lock_set_cmd(target: LockTarget, locked: bool) -> &'static [u8] {
    match (target, locked) {
        (LockTarget::X, true) => CMD_LOCK_X_ON,
        (LockTarget::X, false) => CMD_LOCK_X_OFF,
        (LockTarget::Y, true) => CMD_LOCK_Y_ON,
        (LockTarget::Y, false) => CMD_LOCK_Y_OFF,
        (LockTarget::Left, true) => CMD_LOCK_LEFT_ON,
        (LockTarget::Left, false) => CMD_LOCK_LEFT_OFF,
        (LockTarget::Right, true) => CMD_LOCK_RIGHT_ON,
        (LockTarget::Right, false) => CMD_LOCK_RIGHT_OFF,
        (LockTarget::Middle, true) => CMD_LOCK_MIDDLE_ON,
        (LockTarget::Middle, false) => CMD_LOCK_MIDDLE_OFF,
        (LockTarget::Side1, true) => CMD_LOCK_SIDE1_ON,
        (LockTarget::Side1, false) => CMD_LOCK_SIDE1_OFF,
        (LockTarget::Side2, true) => CMD_LOCK_SIDE2_ON,
        (LockTarget::Side2, false) => CMD_LOCK_SIDE2_OFF,
    }
}

pub fn lock_query_cmd(target: LockTarget) -> &'static [u8] {
    match target {
        LockTarget::X => CMD_LOCK_X_QUERY,
        LockTarget::Y => CMD_LOCK_Y_QUERY,
        LockTarget::Left => CMD_LOCK_LEFT_QUERY,
        LockTarget::Right => CMD_LOCK_RIGHT_QUERY,
        LockTarget::Middle => CMD_LOCK_MIDDLE_QUERY,
        LockTarget::Side1 => CMD_LOCK_SIDE1_QUERY,
        LockTarget::Side2 => CMD_LOCK_SIDE2_QUERY,
    }
}
