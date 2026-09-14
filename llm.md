# Mak-suite: agent and integration guide

Use this guide to integrate the SDK from source and maintain a reproducible
application. [README](README.md) describes the repository.
[MAK_API](protocol/MAK_API.md) and [KM_API](protocol/KM_API.md) are the complete
command contracts. Keep command tables in those two files.

## Start with the actual source

Read the relevant contract and declarations before writing a call. Method names,
argument counts, return types, supported device kinds, and transports differ.
Do not infer one language's signature from another or copy an older firmware's
wire layout. Resolve source/documentation disagreements before implementing.

| Language | Public entry points | Controller and transport source |
| --- | --- | --- |
| Python | [exports](python/makxd/__init__.py), [device](python/makxd/controller.py) | [gamepad](python/makxd/gamepad.py), [connection config](python/makxd/connection_config.py), [protocol](python/makxd/protocol.py) |
| Rust | [crate](rust/src/lib.rs), [device](rust/src/device/mod.rs) | [controller](rust/src/device/controller.rs), [connection](rust/src/types/connection.rs), [protocol](rust/src/protocol/api.rs) |
| C++ | [public header](cpp/makxd-cpp/include/makxd.h) | [implementation](cpp/makxd-cpp/src/makxd.cpp), [protocol](cpp/makxd-cpp/include/makxd_protocol.h) |
| C | [public header](cpp/makxd-cpp/include/makxd_c.h) | [implementation](cpp/makxd-cpp/src/makxd_c.cpp) |
| C# | [API](csharp/mouse.cs) | [stream decoder](csharp/makxd_stream.cs) |

Typed SDK calls use MAK_API. Connect once and reuse the connection. The SDK reads
and caches routed device kinds during connection; reconnect after route changes.
Query the installed application version with the SDK's firmware-version method.
Only issue input operations for available routed kinds. Check errors and return
values, release any held input, and disconnect when finished.

For MAKCU controller actions, read the firmware requirements and completion
example in [Controller handoff](protocol/MAK_API.md#makcu-controller-handoff).
Include an explicit final zero stick pair / trigger in the completion and cancel
paths; a setter returning or the caller stopping transmission is not handoff.
Preserve other active controls, and handle failures to send the final state.

COM can be auto-detected. Ethernet and Wi-Fi use UDP. Python can discover BLE by
service; callback-based adapters receive an empty address for service discovery.
Queued BLE operations are batched automatically; a single operation is sent
immediately. Use the ordinary methods without adding a batching option.
For encryption, reply framing, masks, timing, and event-stream restrictions, read
the protocol documents rather than adding assumptions to application code.

## Use a submodule

From the application's Git repository, add the SDK once:

```sh
git submodule add https://github.com/terrafirma2021/mak-suite.git vendor/mak-suite
git submodule update --init --recursive
git add .gitmodules vendor/mak-suite
git commit -m "Add Mak-suite SDK"
```

For an existing application checkout:

```sh
git submodule update --init --recursive
git submodule status --recursive
git -C vendor/mak-suite rev-parse HEAD
git -C vendor/mak-suite status --short
```

The parent repository pins the SDK commit. A detached submodule HEAD is normal;
the submodule does not update itself. Commit `.gitmodules` and the gitlink, not
a copied SDK tree. In CI, initialise recursive submodules before building.
Point your coding agent to `vendor/mak-suite/llm.md`, the relevant protocol, and
your language's declarations. Load those files for the task instead of pasting
the entire repository into every prompt.

## Install and build

Run builds and tests in your project's isolated container or VM. Commands below
assume the application's root and the submodule path above. For a standalone
Mak-suite checkout, replace `vendor/mak-suite` with `.`.

Python requires Python 3.10 or newer. Install the pinned source in your environment:

```sh
python -m pip install ./vendor/mak-suite/python
```

Rust requires Rust 1.85 or newer. Use a path dependency in the application's Cargo.toml:

```toml
[dependencies]
makxd = { path = "vendor/mak-suite/rust" }
```

The path is relative to that Cargo.toml. Enable the crate's `async` feature only
when using its asynchronous API. Linux serial builds need `libudev` development files.

C++ and C use CMake 3.20 or newer and a C++23 compiler. Linux needs pkg-config,
libudev development files, and OpenSSL development files; macOS needs OpenSSL.

```sh
cmake -S vendor/mak-suite/cpp -B build/mak-suite -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/mak-suite --config Release
cmake --install build/mak-suite --prefix build/mak-suite-install --config Release
```

Add the install prefix to your application's `CMAKE_PREFIX_PATH`, then use:

```cmake
find_package(makxd-cpp REQUIRED)
target_link_libraries(your_target PRIVATE makxd::makxd-cpp)
```

C++ includes `<makxd.h>`; C includes `<makxd_c.h>` and links the same library.
Keep the full SDK checkout when building so packaging can read the root README.

For C#, compile `csharp/mouse.cs` and `csharp/makxd_stream.cs` into your application
or a library and reference `System.IO.Ports` for your target framework. The
repository's controller check is a .NET 8 project and provides a working example
of the source and package references.

Use independent `input_stream(kind, enabled)` subscriptions (language spelling
varies below). Events are `0x53` frames with kind, control ID, and changed value.
Buttons are 0/1; triggers are 0..1023. Do not use the removed named-controller
command or raw `km.` event parser. See [the event contract](protocol/MAK_API.md#input-change-streams).

## Typed call examples

These are SDK calls, not raw wire commands. See their declarations for exact
results and failures. Use only the row for your chosen language.

| SDK | Mouse movement | Keyboard input | Controller stream |
| --- | --- | --- | --- |
| Python | `device.move(x, y)` | `device.keyboard_press("A")` | `device.gamepad.stream(True)` |
| Rust | `device.move_xy(x, y)` | `device.keyboard_press(KeyboardKey::from("A"))` | `device.controller_stream(true)` |
| C++ | `device.mouseMove(x, y)` | `device.keyboardPress(std::string{"A"})` | `device.controllerStream(true)` |
| C | `makxd_mouse_move(device, x, y)` | `makxd_keyboard_down(device, 0x04)` / `makxd_keyboard_up(device, 0x04)` | `makxd_controller_stream(device, true)` |
| C# | `device.move(x, y)` | `device.keyboard_press(new KeyboardKey("A"))` | `device.controller_stream(true)` |

Minimal Python connection and optional mouse movement:

```python
from makxd import DeviceKind, create_controller

device = create_controller()
try:
    kinds = device.device()
    print(device.firmware_version())
    if kinds.has(DeviceKind.MOUSE):
        device.move(40, 0)
finally:
    device.disconnect()
```

To choose an explicit Python transport, import `ConnectionConfig` and pass
`connection=ConnectionConfig.com(...)`, `.udp(...)`, or `.ble(...)` to
`create_controller`. Read the linked connection-config declarations for the
required host, port, interface, address, and encryption arguments.

## Checks before accepting a change

Run the checks for the SDKs you changed. These verify host-side behaviour and
packaging; they do not prove a device's physical USB output.

```sh
python -m pip install pytest build twine ./vendor/mak-suite/python
python -m pytest vendor/mak-suite/python/tests
python -m build vendor/mak-suite/python
python -m twine check vendor/mak-suite/python/dist/*

cargo test --manifest-path vendor/mak-suite/rust/Cargo.toml --all-features
cargo package --manifest-path vendor/mak-suite/rust/Cargo.toml

ctest --test-dir build/mak-suite -C Release --output-on-failure

dotnet run --project vendor/mak-suite/csharp/tests/ControlWidth.csproj -c Release
```

When changing the command contract, compare the complete opcode/name tables with
the SDK implementations, verify payload lengths and signedness, query/mutation
replies, invalid values, and supported transport behaviour. Check Markdown links
and build examples against the current declarations. Do not claim hardware proof
from a host test; report the transport, firmware version, returned bytes, and
observed physical behaviour separately when a connected-device test is needed.

## Update a pinned SDK

First confirm the submodule is clean and preserve any local changes. Fetch and
review the proposed revision before changing the parent project's pointer:

```sh
git -C vendor/mak-suite fetch origin main
git -C vendor/mak-suite log --oneline HEAD..origin/main
git -C vendor/mak-suite diff HEAD..origin/main -- protocol python rust cpp csharp llm.md
git -C vendor/mak-suite checkout --detach origin/main
```

Reinstall or rebuild from the new source, run the applicable checks above and
your application's compatibility tests, then commit the updated gitlink:

```sh
git add vendor/mak-suite
git commit -m "Update Mak-suite SDK"
```

When changing SDK code itself, make and push the SDK commit first, then update
the application's submodule pointer. Do not publish firmware, packages, or changes
to another repository as a side effect of running local build/test commands.

## KM NET compatibility client

`net/cpp` is a separate Windows UDP client. `kmNetMakxdBridge.exe` must already be
running; this client does not discover a device or open COM. Supported functions
are init/close, mouse move/left/middle/right/wheel/all, keydown/keyup, monitor and
mouse/keyboard monitor queries, and reboot as a bridge-session reset. Read
[the header](net/cpp/include/makxd_net.h) for the complete callable surface.
Unsupported configuration, debug, LCD, and masking functions are omitted.

Build with `cmake -S vendor/mak-suite/net/cpp -B build/mak-suite-net -A x64`,
then `cmake --build build/mak-suite-net --config Release` and
`ctest --test-dir build/mak-suite-net -C Release --output-on-failure`.
Use `kmNet_init("127.0.0.1", "8338", "12345678")` for a local bridge example;
replace the host and port for your bridge. The eight-hex-digit identifier is not
authentication. Monitor uses a second local UDP port; `kmNet_monitor(0)` stops it.
After `kmNet_reboot()`, reconnect with `kmNet_init()` before sending commands.
