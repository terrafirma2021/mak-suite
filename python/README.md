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
print(device.firmware_version())
if info.has(DeviceKind.XBOX_GIP):
    print("Xbox GIP")
device.disconnect()
```

COM is auto-detected. `ConnectionConfig.com`, `.udp`, and `.ble` select an
explicit connection. Typed methods use `MAK_API`.

Queued BLE calls are batched automatically using the negotiated payload size.
No batching API or setting is required, and a lone call is sent immediately.
`ConnectionConfig.ble()` discovers by service automatically; pass an address
only when a specific nearby unit must be selected.

The full command contract is in
[`../protocol/MAK_API.md`](../protocol/MAK_API.md).
