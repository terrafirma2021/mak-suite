use makxd::{Button, Device, KeyboardKey, Result};

fn main() -> Result<()> {
    let (device, mock) = Device::mock();

    device.move_xy(100, -50)?;
    device.button_down(Button::Left)?;
    device.button_up(Button::Left)?;
    device.keyboard_press(KeyboardKey::from("A"))?;
    device.controller_stream(true)?;
    device.controller_stream(false)?;

    println!("{} MAK_API commands sent", mock.sent_commands().len());
    Ok(())
}
