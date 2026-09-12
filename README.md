# Mak-suite

Official Python, Rust, C++, C, and C# clients for MAKCU and MAKXD. Use the typed
SDKs to connect over COM, Ethernet/Wi-Fi UDP, or BLE and work with mouse,
keyboard, and controller input, physical-input masks, and complete controller state.
The device's routed kinds determine which operations are available.

## Documentation

- [MAK_API](protocol/MAK_API.md): every public binary command, opcode, payload,
  reply, value range, transport rule, and event format used by the SDKs.
- [KM_API](protocol/KM_API.md): every accepted legacy ASCII `km.*` command,
  arguments, query/mutation behaviour, echo, errors, and COM events.
- [Agent and integration guide](llm.md): SDK entry points, installation,
  submodules, examples, builds, compatibility checks, and updating safely.

## Repository contents

| Directory | Contents |
| --- | --- |
| `python/` | `makxd` Python package, synchronous/asynchronous device API and tests |
| `rust/` | `makxd` Rust crate, synchronous/asynchronous clients and tests |
| `cpp/` | C++ library, C interface, CMake packaging, examples and tests |
| `csharp/` | C# API and stream decoder source, controller wire-format check |
| `net/cpp/` | Windows KM NET compatibility client for the separate MAKXD Bridge |
| `protocol/` | The two complete public API contracts |

New typed integrations use MAK_API. KM_API is the legacy compatibility interface.
Keep the SDK revision and installed firmware contract compatible; use the linked
references for the current argument counts and wire layouts.
