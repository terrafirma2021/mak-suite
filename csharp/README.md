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

device.connect("COM1");
device.move(100, 100);
device.click(MouseButton.Left, 1);
```

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
device.keyboard_up(4);
device.keyboard_press("space", 25);
device.keyboard_string("hello");
device.keyboard_mask("a", true);
device.keyboard_remap("a", "b");
```
