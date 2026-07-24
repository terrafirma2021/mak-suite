# makxd

[![Crates.io](https://img.shields.io/crates/v/makxd.svg?version=0.3.3)](https://crates.io/crates/makxd)
[![Docs.rs](https://img.shields.io/docsrs/makxd.svg?version=0.3.3)](https://docs.rs/makxd)
[![License](https://img.shields.io/crates/l/makxd.svg?version=0.3.3)](./LICENSE)

`makxd` is the Rust API in [`mak-suite`](https://github.com/terrafirma2021/mak-suite)
for controlling MAKXD devices over their serial interface. It provides the
firmware-native ASCII command surface, optional synchronous helpers, and the
binary multi-source input-stream codec.

## Install

The crate is published on crates.io:

```toml
[dependencies]
makxd = "0.3"
```

The crate currently requires Rust 1.85 or newer.

## Quick start

```rust
use makxd::{Button, Device, Result};

fn main() -> Result<()> {
    let device = Device::connect()?;

    device.move_xy(100, 50)?;
    device.button_down(Button::Left)?;
    device.button_up(Button::Left)?;
    device.wheel(-3)?;

    device.disconnect();
    Ok(())
}
```

`Device::connect()` auto-detects the available MAKXD serial port. Use
`Device::connect_port("COM31")` or `DeviceConfig` when the port or connection
policy must be explicit.

## Features

Features are opt-in; the default build is synchronous and minimal.

| Feature | Adds |
| --- | --- |
| `async` | `AsyncDevice` and async serial support through Tokio |
| `batch` | `BatchBuilder` for grouped command writes |
| `extras` | Software click, smooth movement, drag, patterns, and callbacks |
| `profile` | Per-command timing statistics |
| `mock` | In-process transport for tests without hardware |

```toml
[dependencies]
makxd = { version = "0.3", features = ["async", "batch", "extras"] }
```

## Device API

### Mouse and locks

```rust
use makxd::{Button, Device, LockTarget};

let device = Device::connect()?;
device.move_xy(100, -50)?;
device.silent_move(10, 10)?;
device.button_down(Button::Left)?;
device.button_up_force(Button::Left)?;
device.set_lock(LockTarget::X, true)?;
let locked = device.lock_state(LockTarget::X)?;
```

### Keyboard

```rust
device.keyboard_down("a")?;
device.keyboard_up(4u8)?;
device.keyboard_press_for("space", 25)?;
device.keyboard_string("hello")?;
device.keyboard_mask("a", true)?;
device.keyboard_remap("a", "b")?;
```

Keyboard names and numeric HID usages are accepted through `KeyboardKey`.

### Button events

The device button-event API listens to the firmware button stream:

```rust
device.enable_button_stream()?;
let events = device.button_events();

if let Ok(mask) = events.try_recv() {
    println!("left={} right={}", mask.left(), mask.right());
}

device.disable_button_stream()?;
```

## Binary input streaming

The public `makxd::stream` module encodes and decodes the shared binary input
stream for mouse, keyboard, and controller sources. Its frame is deliberately
small:

```text
DE AD + uint16 payload length (little-endian) + command byte + payload
```

The payload length excludes the command byte. Input-stream frames use command
`0x01`; timing is preserved as the raw 16-bit word, including `dt_uframes`,
baseline, and invalid flags.

```rust
use makxd::stream::{
    decode_stream_input_record, StreamFrameDecoder, StreamRequest,
};

let request = StreamRequest::mouse().encode();

let mut decoder = StreamFrameDecoder::new();
decoder.feed(&incoming_bytes);
while let Some(frame) = decoder.next() {
    if let Some(record) = decode_stream_input_record(&frame) {
        println!("kind={:?} sequence={} dt={}",
            record.kind, record.sequence, record.timing.dt_uframes);
    }
}
```

Use `StreamRequest::mouse()`, `keyboard()`, `controller()`, or `all()` to
select sources. `StreamRequest::stop()` and `status()` control the stream.
The complete wire contract is in [`protocol/MAKXD_PROTOCOL.md`](../protocol/MAKXD_PROTOCOL.md).

## Async, batching, and extras

```rust
use std::time::Duration;
use makxd::{AsyncDevice, Button};

let device = AsyncDevice::connect().await?;
device.move_xy(100, 50).await?;
device.click(Button::Left, Duration::from_millis(50)).await?;
device.batch()
    .move_xy(10, 0)
    .wheel(1)
    .execute()
    .await?;
```

The methods in this example require the `async`, `batch`, and `extras`
features as appropriate.

## KM response contract

Accepted SET/EXEC commands echo the command and prompt only. GET queries echo
the query, return the result line, and then emit the prompt:

In the examples below, `␠` denotes the required final ASCII space byte
(`0x20`) in the prompt.

```text
SET:
  input:  km.left(1)
  output: km.left(1)\r\n>>>␠

GET:
  input:  km.left()
  output: km.left()\r\n1\r\n>>>␠
```

## Mock transport

```rust
let (device, mock) = Device::mock();
mock.on_command(b"km.version()\r\n", b"km.MAKXD\r\n>>> ");
let response = device.version()?;
assert_eq!(response, "MAKXD");
```

The mock transport is available with the `mock` feature.

## Build and verify

From the `rust` directory:

```text
cargo test --all-features
cargo check
```

Examples requiring hardware:

```text
cargo run --example basic
cargo run --example comprehensive --features "async,batch,extras,profile"
```

## Raw commands

For a firmware command not wrapped by the typed API, use `send_raw` with the
command's complete CRLF-terminated byte sequence:

```rust
let response = device.send_raw(b"km.version()\r\n")?;
```

## License and attribution

This crate is MIT licensed. The API is maintained in the `mak-suite` project;
its command compatibility began from the open-source MAKCU API by
