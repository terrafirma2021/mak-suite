# Integration Examples

Quick integration guide for adding MAKXD to existing projects. Supports both C++ and C APIs for multi-language integration.

## CMake Integration

**1. Install MAKXD library:**

```bash
cd makxd-cpp && mkdir build && cd build
cmake .. && make && sudo make install
```

**2. Add to your existing CMakeLists.txt:**

```cmake
find_package(makxd-cpp REQUIRED)
target_link_libraries(your_existing_target PRIVATE makxd::makxd-cpp)
```

**3. Use in your code:**

### C++ API

```cpp
#include <makxd.h>

makxd::Device device;
device.connect();
device.mouseMove(100, 0);
```

### C API

```c
#include <makxd_c.h>

makxd_device_t* device = makxd_device_create();
makxd_connect(device, "");
makxd_mouse_move(device, 100, 0);
makxd_device_destroy(device);
```

## Manual Compilation

### C++ Programs

**Linux:**

```bash
g++ -std=c++23 -I/usr/local/include/makxd your_app.cpp -lmakxd-cpp -lpthread -ludev
```

**Windows:** Add include/lib directories and link `makxd-cpp.lib` + `setupapi.lib`

### C Programs

**Linux:**

```bash
gcc -std=c99 -I/usr/local/include your_app.c -lmakxd-cpp -lstdc++ -lpthread -ludev
```

**Windows:**

```bash
gcc -I"C:\Program Files\makxd-cpp\include" your_app.c -L"C:\Program Files\makxd-cpp\lib" -lmakxd-cpp -lstdc++
```

## Bundle with App

Copy library files to your project and link directly:

```cmake
target_link_libraries(your_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/libmakxd-cpp.so)
```

## Common Issues

- **"makxd.h not found"**: Run `sudo make install`
- **"cannot find -lmakxd-cpp"**: Run `sudo ldconfig`
- **DLL not found (Windows)**: Copy `makxd-cpp.dll` next to exe

## Examples

```bash
mkdir build && cd build
cmake .. && make        # Build all examples
./bin/demo              # C++ demo application
./bin/basic_usage       # C++ basic usage example
./bin/c_api_test        # C API comprehensive test
```

### Language Integration Examples

The C API enables easy integration with other languages:

**Python (ctypes):**

```python
from ctypes import *
lib = CDLL("libmakxd-cpp.so")
device = lib.makxd_device_create()
lib.makxd_connect(device, b"")
lib.makxd_mouse_move(device, 100, 0)
```

**Rust:**

```rust
use std::ffi::CString;
extern "C" {
    fn makxd_device_create() -> *mut std::ffi::c_void;
    fn makxd_connect(device: *mut std::ffi::c_void, port: *const i8) -> i32;
}
```

**Go:**

```go
/*
#cgo LDFLAGS: -lmakxd-cpp -lstdc++
#include <makxd_c.h>
*/
import "C"
device := C.makxd_device_create()
C.makxd_connect(device, C.CString(""))
```
