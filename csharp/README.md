# makxd-csharp

## DISCLAIMER
- This was made with the intention for 2 PC Setup only
- This is for educational purposes only and i am not responsible for any bans, penalties or other consequences that you may encounter
- The command surface follows `protocol/MAKXD_PROTOCOL.md`, including movement,
  stream configuration, device settings, mouse catch/lock controls, and keyboard
  multi-key commands.

## Prerequisites
- **Requires System.IO.Ports NuGet Package**
- Install via .NET CLI using the command
- ```
  dotnet add package System.IO.Ports
  ```
- Can also be installed using the NuGet Package Manager in visual studio

## Basic C# Usage:
```csharp
using Mouse;

device.connect();
device.move(100, 100);
device.move(100, 100, 250);
device.press(MouseButton.Left, 1, 250);
device.click(MouseButton.Left, 1);
device.mouse_left_mask(true);
device.mouse_move_mask(true, false, false, true);
device.mouse_wheel_mask(true, false);
```

With no port argument, C# probes all CH343 (`1A86:55D3`) ports before all
CH340 (`1A86:7523`) ports and tries every supported baud rate on each port.
Pass a COM port explicitly to probe only that port.

The nullable `dtUframes` argument on direct mouse and keyboard mutations is
optional and must be in `0..16383`. Omitting it sends no trailing DT parameter;
passing `0` explicitly sends a trailing zero.

## COM, UDP, and BLE connections

```csharp
var udp = ConnectionConfig.Udp(
    "192.168.7.1",
    8080,
    UdpWireMode.Raw,
    ApiProtocol.MakApi,
    "00112233445566778899aabbccddeeff",
    bindAddress: "192.168.120.10",
    vlanId: 120);
device.connect(udp);
```

UDP serves Ethernet and Wi-Fi. VLAN IDs select the operating-system VLAN
interface through its bind address and are never inserted into KM, `MAK_API`, or AES
payloads. `ConnectionConfig.Ble` accepts the application-owned GATT
connect/write/notification-read/close delegates for the fixed MAKXD UUIDs.
The SDK invokes them in lifecycle order. BLE carries
plain application records and has no MAKXD AES transport option.

Successful KM actions and `MAK_API` SET calls are silent by default. GETs,
EXECs, errors, and event streams keep their response behavior. `km.echo(1)`
persistently enables KM action echoes and `km.echo(0)` persistently disables
them; the client reads the saved state when it connects.

## Encrypted COM or UDP API

Configure the device for the matching 16-byte key, then pass that key when
connecting the local client:

```csharp
device.connect(
    "COM1",
    true,
    "00112233445566778899aabbccddeeff");
```

Every command method uses encryption automatically. These arguments configure
only the C# client and cannot change the device security setting. BLE rejects
this setting.

## KM or MAK_API

Pass the selected API as the fourth connection argument. Existing mouse,
keyboard, and controller methods keep the same signatures:

```csharp
device.connect("", false, "", ApiProtocol.MakApi);
DeviceRoute route = device.device_route();
device.move(100, 100, 250);
```

Baud discovery still uses `km.version()`. If encryption is enabled the probe
uses AES-CCM and accepts only the matching authenticated `km.MAKXD` response;
there is no plaintext fallback.

## Performance (Results may vary)
- Mouse Movement (100 rapid moves tested): Total elapsed time: 46ms, (0.46 ms avg)
- Mouse Clicks (50 rapid clicks): Total elapsed time: 155ms, (1.55 ms avg)
- NOTE: Mouse clicks had a 1ms delay added between each command sent to ensure the command is sent.
 
**On Average performs 10x Faster than the most recent Python release at the time of testing**

## Acknowledgements

- [Makxd Discord Server](https://discord.gg/frvh3P4Qeg) community
- [Original Python library](https://github.com/SleepyTotem/makcu-py-lib) by [SleepyTotem](https://github.com/SleepyTotem)

## Original project attribution

This C# API is carried into `mak-suite` from [1claim-gh/makcu-csharp](https://github.com/1claim-gh/makcu-csharp), originally authored and maintained by [1claim-gh](https://github.com/1claim-gh).

## Keyboard API

The keyboard API follows [`protocol/MAKXD_PROTOCOL.md`](../protocol/MAKXD_PROTOCOL.md):

```csharp
device.keyboard_down("a");
device.keyboard_down("a", 250);
device.keyboard_up(4);
device.keyboard_init(250);
device.keyboard_press("space", 25);
device.keyboard_string("hello");
device.keyboard_mask("a", true);
device.keyboard_remap("a", "b");
```
