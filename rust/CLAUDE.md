# CLAUDE.md — makxd

## What is this?

`makxd` is a Rust library for controlling MAKXD USB HID interceptor devices. These are small USB dongles (CH340/CH343 chip, VID=0x1A86 PID=0x55D3) that sit between a mouse and a host PC, allowing programmatic mouse control via serial commands.

## Build & test

```bash
# Check (no hardware needed)
cargo check
cargo check --all-features

# Run examples (requires real MAKXD device plugged in)
cargo run --example basic
cargo run --example comprehensive --features "async,batch,extras,profile"
cargo run --example benchmark --features "batch,extras"

# Run mock example (no hardware needed)
cargo run --example mock --features "mock"
```

## Architecture

### Module layout

- `types/` — Data types (Button, LockTarget, ButtonMask, LockStates, DeviceInfo, ConnectionState)
- `error/` — MakxdError enum, Result alias
- `protocol/` — Wire format only, no I/O. Constants, stack-allocated command builders, response parser
- `transport/` — **Internal only** (`pub(crate)`). Reader/writer/monitor threads, serial port handling, mock backend
- `device/` — Public Device and AsyncDevice APIs, split by concern (buttons, movement, locks, stream, info)
- `batch/` — BatchBuilder fluent API (feature-gated)
- `extras/` — Software-implemented operations: click, smooth move, drag, event callbacks (feature-gated)
- `profiler/` — Per-command timing, zero-cost when disabled (feature-gated)

### Threading model

The transport layer spawns 3 threads per connection:
1. **Writer thread** — receives commands via channel, coalesces pending payloads into single `write_all()` calls
2. **Reader thread** — tight read loop with `StreamParser` state machine, routes responses to oneshot channels, fans out button events
3. **Monitor thread** — sleeps while connected, handles reconnection with exponential backoff on disconnect

All Device methods take `&self` (not `&mut self`). I/O goes through channels. Device is `Send + Sync`.

### Feature flags

| Flag | What it enables |
|------|----------------|
| `async` | AsyncDevice + async batch/extras (pulls in tokio, tokio-serial) |
| `batch` | BatchBuilder fluent command sequencing |
| `extras` | Software-implemented click, smooth move, drag, event callbacks |
| `profile` | Per-command timing profiler (zero-cost when off) |
| `mock` | MockTransport for testing without hardware |

### Protocol details

- Serial at 4 Mbaud (auto-upgrades from 115200 if needed)
- Commands are `km.foo(args)\r\n`. Accepted SET/EXEC responses echo the
  command and the `>>> ` prompt only; GET responses echo the query, result,
  and the `>>> ` prompt. The prompt's final byte is ASCII space (`0x20`).
- Button stream events use `km.` prefix followed by a raw button mask byte
- Static commands use compile-time `&[u8]` constants; parametric commands use stack-allocated `CommandBuf` (no heap on hot paths)

## Conventions

- Sync and async impls live in the same files, async gated by `#[cfg(feature = "async")]`
- Fire-and-forget mode accessed via `device.ff()` wrapper, not a flag on each method
- The `timed!` macro wraps Device methods when the `profile` feature is enabled
- Broken firmware commands (km.click, km.smooth, km.bezier) are intentionally not exposed; `extras` provides correct software implementations
- `km.catch_m*()` works but requires button locking first — see `discovery/CATCH.md` for full protocol details
- Query commands always hit the device — no cached state that can go stale
