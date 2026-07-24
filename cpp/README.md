# MAKXD C++ Library

High-performance C++ library for MAKXD mouse controllers. Sub-millisecond response times, cross-platform support with C ABI for multi-language integration.

## Prerequisites

- C++23 compiler (for C++ API)
- C99 compiler (for C API only)
- CMake 3.20+
- MAKXD Device (VID:PID = 1A86:55D3)
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
device.click(makxd::MouseButton::LEFT);
```

### C API (for other languages)

The library includes a complete C ABI for easy integration with Python, Rust, Go, C#, and other languages:

```c
#include <makxd_c.h>

makxd_device_t* device = makxd_device_create();
makxd_connect(device, "");
makxd_mouse_move(device, 100, 0);
makxd_mouse_click(device, MAKXD_MOUSE_LEFT);
makxd_device_destroy(device);
```

See `examples/` for complete integration examples.

## ASCII response contract

The serial collector consumes the complete response through the `>>> ` prompt.
Normal SET and EXEC calls wait for Makxd to echo the accepted command and
prompt. GET calls echo the query, return the result line, and then emit the
prompt:

```text
SET:
  input:  km.left(1)
  output: km.left(1)\r\n>>>␠

GET:
  input:  km.left()
  output: km.left()\r\n1\r\n>>>␠
```

Here `␠` denotes the required final ASCII space byte (`0x20`).

## Examples

```bash
cd examples && ./build.sh  # Build examples
./build/bin/demo           # Run demo
```

## Performance

- Mouse Movement: ~0.04ms (40μs)
- Button Click: ~0.04ms (40μs)
- 28x faster than Python implementation

## Troubleshooting

**Linux permissions:**

```bash
sudo usermod -a -G dialout $USER
```

**Windows:** Check Device Manager for COM port with VID:PID = 1A86:55D3

## License

GNU GPLv3

## Acknowledgements

- Shared protocol contract: [`protocol/MAKXD_PROTOCOL.md`](../protocol/MAKXD_PROTOCOL.md)
- [Makxd Discord Server](https://discord.gg/frvh3P4Qeg) community
- [Original Python library](https://github.com/SleepyTotem/makcu-py-lib) by [SleepyTotem](https://github.com/SleepyTotem)
- [Joonal Salmi](https://github.com/josal52) for his fix to a long-standing bug
