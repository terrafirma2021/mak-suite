# MAKXD C# SDK

Compile `mouse.cs` and `makxd_stream.cs` into the application or a library.

```csharp
using Mouse;

device.connect();
device.move(40, 0);
device.keyboard_press(new KeyboardKey("A"));
device.controller_control(ControllerControl.South, 1);
device.controller_control(ControllerControl.South, 0, 250);
DeviceKinds kinds = device.device_kinds();
bool xboxGip = kinds.Has(DeviceKind.XboxGip);
device.disconnect();
```

Typed methods use `MAK_API`. The full command contract is in
[`../protocol/MAK_API.md`](../protocol/MAK_API.md).
