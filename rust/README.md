# MAKXD Rust SDK

```toml
[dependencies]
makxd = "0.4"
```

```rust
use makxd::{Button, ControllerControl, Device, DeviceKind, KeyboardKey, Result};

fn main() -> Result<()> {
    let device = Device::connect()?;
    device.move_xy(40, 0)?;
    device.button_down(Button::Left)?;
    device.button_up(Button::Left)?;
    device.keyboard_press(KeyboardKey::from("A"))?;
    device.controller_control(ControllerControl::South, 1)?;
    device.controller_control(ControllerControl::South, 0)?;
    let kinds = device.device()?;
    println!("Firmware: {}", device.firmware_version()?);
    println!("Xbox GIP: {}", kinds.has(DeviceKind::XboxGip));
    Ok(())
}
```

Typed methods use `MAK_API`. The full command contract is in
[`../protocol/MAK_API.md`](../protocol/MAK_API.md).

Queued BLE calls are batched automatically. Existing sync and async methods
are unchanged, and a lone call is sent without a batching delay.
`ConnectionConfig::ble` may use an empty address to request service-based
discovery from the supplied BLE adapter.
