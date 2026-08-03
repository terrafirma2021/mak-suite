# MAKXD C++ and C SDK

```powershell
cmake -S . -B build
cmake --build build --config Release
```

```cpp
#include <makxd.h>

makxd::Device device;
device.connect();
device.mouseMove(40, 0);
device.keyboardPress(std::string{"A"});
device.controllerControl(makxd::ControllerControl::SOUTH, 1);
device.controllerControl(makxd::ControllerControl::SOUTH, 0, 250);
auto kinds = device.device();
if (kinds && kinds->has(makxd::DeviceKind::XBOX_GIP)) {
    // Xbox GIP route
}
```

```c
#include <makxd_c.h>

makxd_device_t *device = makxd_device_create();
makxd_connect(device, NULL);
makxd_mouse_move(device, 40, 0);
makxd_keyboard_down(device, 0x04);
makxd_keyboard_up(device, 0x04);
makxd_controller_control(device, MAKXD_CONTROLLER_SOUTH, 1);
makxd_device_kinds_t kinds;
makxd_get_device_kinds(device, &kinds);
bool xbox_gip = (kinds.kinds & MAKXD_DEVICE_XBOX_GIP) != 0;
makxd_device_destroy(device);
```

Typed functions use `MAK_API`. The full command contract is in
[`../protocol/MAK_API.md`](../protocol/MAK_API.md).
