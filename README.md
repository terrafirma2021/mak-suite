# MAKXD SDKs

Official Python, Rust, C++, C, and C# clients for MAKXD.

All SDKs use `MAK_API` for mouse, keyboard, and controller input, including
masks and complete-state operations.

Supported connection methods are:

- COM
- Ethernet host/client over UDP
- Wi-Fi station/client over UDP
- BLE

On connection, the SDK reads `DEVICE` once and caches the exact active
mouse, keyboard, and controller kinds. Typed calls use that cached result.

Controller calls use the same semantic controls in every language: `SOUTH`,
`EAST`, `WEST`, `NORTH`, D-pad, shoulder, trigger, stick, system, grip, and
`EXTRA_1..EXTRA_32`.

The complete wire contract, opcode table, payload layouts, values, examples,
and events are defined in
[`protocol/MAK_API.md`](protocol/MAK_API.md).

| SDK | Directory |
| --- | --- |
| Python | `python/` |
| Rust | `rust/` |
| C++ | `cpp/` |
| C | `cpp/` |
| C# | `csharp/` |
