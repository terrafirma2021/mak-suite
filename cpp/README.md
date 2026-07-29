# MAKXD C++ Library

High-performance C++ library for MAKXD mouse controllers. Sub-millisecond response times, cross-platform support with C ABI for multi-language integration.

## Prerequisites

- C++23 compiler (for C++ API)
- C99 compiler (for C API only)
- CMake 3.20+
- MAKXD Device (CH343 `1A86:55D3` or CH340 `1A86:7523`)
- Linux: `libudev-dev`, `pkg-config`

## Building

```bash
git clone https://github.com/terrafirma2021/mak-suite
cd mak-suite/cpp
mkdir build && cd build
cmake ..
make -j$(nproc)  # Linux
# OR cmake --build . --config Release  # Windows
sudo make install  # Install system-wide
```

## Integration

### C++ API

Add to your CMakeLists.txt:

```cmake
find_package(makxd-cpp REQUIRED)
target_link_libraries(your_app PRIVATE makxd::makxd-cpp)
```

Basic C++ usage:

```cpp
#include <makxd.h>

makxd::Device device;
device.connect();
device.mouseMove(100, 0);
device.mouseMove(100, 0, 250);
device.mouseDown(makxd::MouseButton::LEFT, 250);
device.keyboardDown(makxd::KeyboardKey{static_cast<uint8_t>(4)}, 250);
device.click(makxd::MouseButton::LEFT);
```

The timed overloads accept `dt_uframes` in `0..16383`. Calling the original
overload sends no trailing DT parameter; passing `0` explicitly sends a
trailing zero.

### COM, UDP, and BLE connections

```cpp
auto udp = makxd::ConnectionConfig::udp(
    "192.168.7.1",
    8080,
    makxd::UdpWireMode::RAW,
    makxd::ApiProtocol::MAK_API,
    "00112233445566778899aabbccddeeff",
    {},
    "eth0.120",
    120);
makxd::Device device(udp);
device.connect();
```

UDP serves Ethernet and Wi-Fi. VLAN IDs select an operating-system VLAN
interface or bind address; the 802.1Q tag is never part of a KM, `MAK_API`, or
AES record. `ConnectionConfig::ble` accepts the application-owned GATT
connect/write/notification-read/close functions for the fixed MAKXD UUIDs.
The SDK invokes them in lifecycle order. BLE does
not accept a MAKXD AES transport key.

### Encrypted COM or UDP API

Configure the device for the matching 16-byte key, then provide that key when
creating the local client:

```cpp
makxd::Device device(
    true,
    "00112233445566778899aabbccddeeff");
device.connect();
device.mouseMove(100, 0);
```

Every normal command is encrypted automatically. Constructor arguments
configure only the C++ client and cannot change the device security setting.
BLE carries plain application records instead.

## KM or MAK_API

Select the API in the constructor; high-level method names do not change:

```cpp
makxd::Device device(false, {}, makxd::ApiProtocol::MAK_API);
device.connect();
const auto route = device.device();
device.move(100, 50, 250);
```

Baud discovery remains `km.version()`. An encryption-enabled client encrypts
that probe and requires the authenticated matching-nonce `km.MAKXD` response;
it never probes plaintext.

### C API (for other languages)

The library includes a complete C ABI for easy integration with Python, Rust, Go, C#, and other languages:

```c
#include <makxd_c.h>

makxd_device_t* device = makxd_device_create();
makxd_connect(device, "");
makxd_mouse_move(device, 100, 0);
makxd_mouse_move_dt(device, 100, 0, 250);
makxd_mouse_down_dt(device, MAKXD_MOUSE_LEFT, 250);
makxd_keyboard_down_dt(device, 4, 250);
makxd_mouse_click(device, MAKXD_MOUSE_LEFT);
makxd_device_destroy(device);
```

The C ABI equivalent is:

```c
makxd_device_t* device = makxd_device_create_with_transport(
    true, "00112233445566778899aabbccddeeff");
```

For COM, UDP, or BLE, populate `makxd_connection_config_t` and call
`makxd_connect_config`. A non-empty `aes128_key_hex` is rejected for BLE.

See `examples/` for complete integration examples.

## ASCII response contract

The serial collector consumes the complete response through the `>>> ` prompt.
Normal SET and EXEC calls wait for Makxd to echo the accepted command and
prompt. GET calls echo the query, return the result line, and then emit the
prompt:

```text
SET:
  input:  km.left(1)
  output: km.left(1)\r\n>>>[space]

GET:
  input:  km.left()
  output: km.left()\r\n1\r\n>>>[space]
```

`[space]` is the required final ASCII byte `0x20`.

## Examples

```bash
cd examples && ./build.sh  # Build examples
./build/bin/demo           # Run demo
```

## Performance

- Mouse Movement: ~0.04ms (40 us)
- Button Click: ~0.04ms (40 us)
- 28x faster than Python implementation

## Troubleshooting

**Linux permissions:**

```bash
sudo usermod -a -G dialout $USER
```

**Windows:** Check Device Manager for a CH343 `1A86:55D3` or CH340
`1A86:7523` COM port.

## License

GNU GPLv3

## Acknowledgements

- Shared protocol contract: [`protocol/MAKXD_PROTOCOL.md`](../protocol/MAKXD_PROTOCOL.md)
- [Makxd Discord Server](https://discord.gg/frvh3P4Qeg) community
- [Original Python library](https://github.com/SleepyTotem/makcu-py-lib) by [SleepyTotem](https://github.com/SleepyTotem)
- [Joonal Salmi](https://github.com/josal52) for his fix to a long-standing bug
