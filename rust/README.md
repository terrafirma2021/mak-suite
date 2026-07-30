# makxd

[![Crates.io](https://img.shields.io/crates/v/makxd.svg?version=0.4.0)](https://crates.io/crates/makxd)
[![Docs.rs](https://img.shields.io/docsrs/makxd.svg?version=0.4.0)](https://docs.rs/makxd)
[![License](https://img.shields.io/crates/l/makxd.svg?version=0.4.0)](./LICENSE)

`makxd` is the Rust API in [`mak-suite`](https://github.com/terrafirma2021/mak-suite)
for controlling MAKXD devices over COM, UDP, or BLE. It provides the
firmware-native ASCII command surface, optional synchronous helpers, and the
binary multi-source input-stream codec.

## Install

The crate is published on crates.io:

```toml
[dependencies]
makxd = "0.4"
```

The crate currently requires Rust 1.85 or newer.

## Quick start

```rust
use makxd::{Button, Device, Result};

fn main() -> Result<()> {
    let device = Device::connect()?;

    device.move_xy(100, 50)?;
    device.move_xy_dt(100, 50, 250)?;
    device.button_down(Button::Left)?;
    device.button_down_dt(Button::Left, 250)?;
    device.button_up(Button::Left)?;
    device.wheel(-3)?;

    device.disconnect();
    Ok(())
}
```

`Device::connect()` probes all CH343 (`1A86:55D3`) ports before all CH340
(`1A86:7523`) ports, trying every supported baud rate on each port.
Use `Device::connect_port("COM31")` or `DeviceConfig` when the port or
connection policy must be explicit.

## COM, UDP, and BLE connections

```rust
use makxd::{ApiProtocol, ConnectionConfig, Device, DeviceConfig, UdpWireMode};

let connection = ConnectionConfig::udp(
    "192.168.7.1",
    8080,
    UdpWireMode::Raw,
    None,
    Some("eth0.120".into()),
    Some(120),
)?;
let device = Device::with_config(DeviceConfig {
    connection,
    api_protocol: ApiProtocol::MakApi,
    ..Default::default()
})?;
```

UDP serves Ethernet and Wi-Fi. VLAN IDs select the operating-system VLAN
interface/bind address and remain outside KM, `MAK_API`, and AES records. BLE
uses an application-owned `BleConnectionIo` for the fixed GATT UUIDs. The SDK
invokes its connect/write/notification-read/close lifecycle and does not
accept MAKXD AES transport encryption.

## Encrypted COM or UDP API

Configure the device for the matching 16-byte key, then provide that key in
the local `DeviceConfig`:

```rust
use makxd::{Device, DeviceConfig};

let device = Device::with_config(DeviceConfig {
    encryption_enabled: true,
    encryption_key: Some([
        0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77,
        0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff,
    ]),
    ..Default::default()
})?;
```

Every normal command is encrypted automatically. These fields configure only
the Rust client and cannot change the device security setting. BLE rejects
these fields.

## KM or MAK_API

Set the command encoding in `DeviceConfig`; the same typed methods are used in
both modes:

```rust
use makxd::{ApiProtocol, Device, DeviceConfig};

let device = Device::with_config(DeviceConfig {
    api_protocol: ApiProtocol::MakApi,
    ..Default::default()
})?;
let route = device.device()?;
println!("mouse={} {} Hz", route.mouse(), route.mouse_hz());
device.move_xy_dt(100, 50, 250)?;
```

Baud discovery remains KM-only. With encryption enabled, the probe is an
authenticated encrypted `km.version()` transaction and never falls back to
plaintext.

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
makxd = { version = "0.4", features = ["async", "batch", "extras"] }
```

## Device API

### Mouse and locks

```rust
use makxd::{Button, Device, LockTarget};

let device = Device::connect()?;
device.move_xy(100, -50)?;
device.move_xy_dt(100, -50, 250)?;
device.silent_move(10, 10)?;
device.button_down(Button::Left)?;
device.button_down_dt(Button::Left, 250)?;
device.button_up_force(Button::Left)?;
device.left_mask(true)?;
device.move_mask(true, false, false, true)?;
device.wheel_mask(true, false)?;
device.set_lock(LockTarget::X, true)?;
let locked = device.lock_state(LockTarget::X)?;
```

### Keyboard

```rust
device.keyboard_down("a")?;
device.keyboard_down_dt("a", 250)?;
device.keyboard_up(4u8)?;
device.keyboard_init_dt(250)?;
device.keyboard_press_for("space", 25)?;
device.keyboard_string("hello")?;
device.keyboard_mask("a", true)?;
device.keyboard_remap("a", "b")?;
```

Keyboard names and numeric HID usages are accepted through `KeyboardKey`.
The `_dt` methods accept `dt_uframes` in `0..16383`. Methods without `_dt`
remain the no-trailing-parameter command forms; `_dt(..., 0)` sends an explicit
trailing zero.

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

## Input streaming

The public `makxd::stream` module selects and decodes mouse, keyboard, and
controller input sources. Timing exposes `dt_uframes`, baseline, and invalid
flags.

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
