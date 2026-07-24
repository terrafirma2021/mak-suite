# MAKXD NET C++ Client

Lightweight Windows C++ client for applications that need to use the KM NET
UDP interface through MAKXD.

> `kmNetMakxdBridge.exe` must already be running. This library talks to the
> Bridge over UDP; it does not find a MAKXD Device or open a COM port itself.

The client is a clean interoperability implementation of the supported KM NET
wire subset. It is intentionally smaller than the original
[`ZCban/kmboxNET`](https://github.com/ZCban/kmboxNET) client.

## Enabled functions

| Function | Availability |
| --- | --- |
| `kmNet_init` / `kmNet_close` | enabled |
| `kmNet_mouse_move` | enabled |
| `kmNet_mouse_left`, `kmNet_mouse_middle`, `kmNet_mouse_right` | enabled |
| `kmNet_mouse_wheel`, `kmNet_mouse_all` | enabled |
| `kmNet_keydown`, `kmNet_keyup` | enabled |
| `kmNet_monitor` and mouse/keyboard monitor queries | enabled |
| `kmNet_reboot` | enabled as a Bridge session reset |

Not all functions from other KM NET clients are enabled. Timed movement,
configuration, debug, LCD, masking, and every function not listed above are
not part of this library. Unsupported functions are omitted instead of
returning a false success.

The exact UDP contract is documented in
[`protocol/MAKXD_PROTOCOL.md`](../../protocol/MAKXD_PROTOCOL.md#makxd-net-bridge-protocol).

## Build

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Outputs:

```text
build/Release/makxd-net-cpp.lib
build/Release/makxd-net-example.exe
build/Release/makxd-net-tests.exe
```

## Use

```cpp
#include <makxd_net.h>

int result = kmNet_init("127.0.0.1", "8338", "12345678");
if (result == KMNET_SUCCESS) {
    kmNet_mouse_move(20, 0);
    kmNet_mouse_left(1);
    kmNet_mouse_left(0);
    kmNet_close();
}
```

The MAC/UUID string must be exactly eight hexadecimal characters. Its value is
an identifier echoed by the Bridge, not authentication. When the application
and Bridge run on different machines, replace `127.0.0.1` with the Bridge
host's IPv4 address. The command socket chooses its own local UDP source port;
only the Bridge's listening port is passed to `kmNet_init()`.

`kmNet_monitor(port)` opens a second local UDP socket for standard 20-byte
combined mouse/keyboard reports. Use a free UDP port. `kmNet_monitor(0)` stops
monitoring.

`kmNet_reboot()` resets the Bridge's KM Client session and closes this client's
command socket. Call `kmNet_init()` again before sending another command.
