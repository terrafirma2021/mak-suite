# MAKXD Python SDK

```bash
pip install makxd
```

```python
from makxd import ControllerControl, DeviceKind, MouseButton, create_controller

device = create_controller()
device.move(40, 0)
device.click(MouseButton.LEFT)
device.keyboard_press("A")
device.gamepad.control(ControllerControl.SOUTH, 1)
device.gamepad.control(ControllerControl.SOUTH, 0, dt_uframes=250)
info = device.device()
if info.has(DeviceKind.XBOX_GIP):
    print("Xbox GIP")
device.disconnect()
```

COM is auto-detected. `ConnectionConfig.com`, `.udp`, and `.ble` select an
explicit connection. Typed methods use `MAK_API`.

The full command contract is in
[`../protocol/MAK_API.md`](../protocol/MAK_API.md).
