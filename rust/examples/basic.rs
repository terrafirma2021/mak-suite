use makxd::{Button, ControllerControl, Device, KeyboardKey, Result};

fn main() -> Result<()> {
    let device = Device::connect()?;
    let route = device.device()?;
    println!("connected on {}: {:?}", device.port_name(), route);

    device.move_xy(40, 0)?;
    device.wheel(-1)?;
    device.button_down(Button::Left)?;
    device.button_up(Button::Left)?;
    device.keyboard_press(KeyboardKey::from("A"))?;
    device.controller_control(ControllerControl::South, 1)?;
    device.controller_control(ControllerControl::South, 0)?;

    device.disconnect();
    Ok(())
}
