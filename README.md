# MAKXD SDKs

Official Python, Rust, C++, C, and C# clients for MAKXD.

All SDKs use `MAK_API` for mouse, keyboard, and controller input, including
masks and complete-state operations.

Supported connection methods are:

- COM
- Ethernet host/client over UDP
- Wi-Fi station/client over UDP
- BLE

BLE batching is automatic. Existing typed and low-level API calls do not need
a batching option: commands that are already queued together are combined to
use the negotiated link payload, and each result is returned to its original
caller. A single command is sent immediately, without a batching delay.
Python can discover a nearby MAKXD by service when no BLE address is supplied.
Callback-based SDK adapters receive an empty address as the same request to
discover by service.

On connection, the SDK reads `DEVICE` once and caches the exact active
mouse, keyboard, and controller kinds. Typed calls use that cached result.
`FIRMWARE_VERSION` reads the installed application firmware version.

Controller calls use the same semantic controls in every language: `SOUTH`,
`EAST`, `WEST`, `NORTH`, D-pad, shoulder, trigger, stick, system, grip, and
`EXTRA_1..EXTRA_32`.

The complete wire contract, opcode table, payload layouts, values, examples,
and events are defined in
[`protocol/MAK_API.md`](protocol/MAK_API.md).

The complete legacy ASCII `km.*` command contract is defined in
[`protocol/KM_API.md`](protocol/KM_API.md).

| SDK | Directory |
| --- | --- |
| Python | `python/` |
| Rust | `rust/` |
| C++ | `cpp/` |
| C | `cpp/` |
| C# | `csharp/` |
