pub const USB_VID: u16 = 0x1A86;
pub const USB_PID_CH343: u16 = 0x55D3;
pub const USB_PID_CH340: u16 = 0x7523;
pub const SUPPORTED_USB_IDS: [(u16, u16); 2] = [(USB_VID, USB_PID_CH343), (USB_VID, USB_PID_CH340)];
pub const BAUD_CANDIDATES: [u32; 3] = [115_200, 1_000_000, 4_000_000];
