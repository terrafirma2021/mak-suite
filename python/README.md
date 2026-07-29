# makxd

[![PyPI](https://img.shields.io/pypi/v/makxd.svg?version=2.4.0)](https://pypi.org/project/makxd/)
[![Python](https://img.shields.io/badge/python-3.10%2B-3776AB?logo=python&logoColor=white)](https://pypi.org/project/makxd/)
[![License](https://img.shields.io/pypi/l/makxd.svg?version=2.4.0)](LICENSE)

`makxd` is the Python API in [`mak-suite`](https://github.com/terrafirma2021/mak-suite)
for controlling MAKXD devices over COM, UDP, or BLE. It provides the
firmware-native ASCII command surface, mouse and keyboard helpers, and the
shared binary input-stream codec.

## Install

The package is published on PyPI:

```text
python -m pip install makxd
```

The package requires Python 3.10 or newer.

## Quick start

```python
from makxd import MouseButton, create_controller

device = create_controller()
device.move(100, 50)
device.click(MouseButton.LEFT)
device.scroll(-3)
device.disconnect()
```

`create_controller()` probes all CH343 (`1A86:55D3`) ports before all CH340
(`1A86:7523`) ports. Each port is tried at all supported baud rates. Pass
`fallback_com_port="COM31"` when a specific port is required. Use the context
manager to close the connection automatically:

```python
from makxd import MouseButton, create_controller

with create_controller() as device:
    device.move(100, 50)
    device.click(MouseButton.LEFT)
```

The controller also supports asynchronous use:

```python
import asyncio

from makxd import MouseButton, create_async_controller


async def main() -> None:
    device = await create_async_controller()
    await device.move(100, 50)
    await device.click(MouseButton.LEFT)
    await device.disconnect()


asyncio.run(main())
```

## Connections

```python
from makxd import ApiProtocol, ConnectionConfig, UdpWireMode, create_controller

com = ConnectionConfig.com(api_protocol=ApiProtocol.MAK_API)
udp = ConnectionConfig.udp(
    "192.168.7.1",
    port=8080,
    mode=UdpWireMode.RAW,
    interface="eth0.120",
    vlan_id=120,
    api_protocol=ApiProtocol.MAK_API,
    aes128_key="00112233445566778899aabbccddeeff",
)
ble = ConnectionConfig.ble(
    "AA:BB:CC:DD:EE:FF",
    api_protocol=ApiProtocol.MAK_API,
)

device = create_controller(connection=udp)
```

UDP serves Ethernet and Wi-Fi. A VLAN ID requires the operating-system VLAN
interface or bind address; it is not inserted into the command payload. BLE
uses Bleak, the fixed MAKXD service/RX/TX UUIDs, and no MAKXD AES transport
encryption.

## Encrypted COM or UDP API

Configure the device for the matching 16-byte key, then provide that key once
when creating the local client:

```python
device = create_controller(
    encryption_enabled=True,
    encryption_key="00112233445566778899aabbccddeeff",
)
```

All normal API and raw `km.*` calls are encrypted automatically. These
arguments configure only this Python client and cannot change the device
security setting. A device that requires encrypted COM rejects plaintext
clients. BLE rejects this setting.

## KM or MAK_API

Select the wire API once; the same high-level methods then encode KM text or
public MAK_API opcodes:

```python
from makxd import ApiProtocol, create_controller

device = create_controller(api_protocol=ApiProtocol.MAK_API)
route = device.device()
print(route.mouse, route.keyboard, route.controller, route.mouse_hz)
device.move(100, 50)
device.move(100, 50, dt_uframes=250)
```

Baud discovery still uses `km.version()`. When encryption is enabled, that
probe is encrypted and requires the matching authenticated `km.MAKXD`
response; it never falls back to plaintext.

## Mouse API

```python
device.move(100, 50)
device.move(100, 50, dt_uframes=250)
device.move_smooth(200, 100, segments=20)
device.move_bezier(150, 150, segments=30, ctrl_x=75, ctrl_y=200)
device.press(MouseButton.LEFT)
device.press(MouseButton.LEFT, dt_uframes=250)
device.release(MouseButton.LEFT)
device.click_count(MouseButton.LEFT, count=2)
device.scroll(-5)
device.scroll(-5, dt_uframes=250)
```

Direct mouse and keyboard mutations accept an optional `dt_uframes` in
`0..16383`. Omitting it sends the original command without a trailing
parameter; explicitly passing `0` sends the trailing zero.

The controller also exposes movement, locking, button-state, device-info,
firmware-version, serial, and command-stream helpers matching the MAKXD
command surface.

## Keyboard API

```python
device.keyboard_down("a")
device.keyboard_down("a", dt_uframes=250)
device.keyboard_up(4)
device.keyboard_init(dt_uframes=250)
device.keyboard_press("space", hold_ms=25)
device.keyboard_string("hello")
device.keyboard_mask("a", True)
device.keyboard_remap("a", "b")
device.keyboard_multidown(["ctrl", "c"])
device.keyboard_multiup(["ctrl", "c"])
```

Keyboard names and numeric HID usages are accepted through `KeyboardKey`.

## Input streaming

The public `makxd.stream` module selects and decodes mouse, keyboard, and
controller input sources. Timing exposes `dt_uframes`, baseline, and invalid
flags.

```python
from makxd import (
    StreamFrameDecoder,
    StreamRequest,
    decode_stream_input_record,
)

request = StreamRequest.mouse().encode()

decoder = StreamFrameDecoder()
decoder.feed(incoming_bytes)
while frame := decoder.next():
    record = decode_stream_input_record(frame)
    if record is not None:
        print(
            record.kind,
            record.sequence,
            record.timing.dt_uframes,
        )
```

Use `StreamRequest.mouse()`, `keyboard()`, `controller()`, or `all()` to
select sources. `StreamRequest.stop()` and `status()` control the stream.

## ASCII response contract

Normal SET and EXEC calls wait for Makxd to echo the accepted command and
prompt. GET calls echo the query, return the result line, and then emit the
prompt. The final prompt byte is one ASCII space (`0x20`):

```text
SET:
  input:  km.left(1)
  output: km.left(1)\r\n>>>␠

GET:
  input:  km.left()
  output: km.left()\r\n1\r\n>>>␠
```

Here `␠` denotes the required final ASCII space byte in `>>> `.

## Command line

```text
python -m makxd --debug
python -m makxd --testPort COM31
python -m makxd --runtest
```

The debug command opens the interactive command console. The port check tests
one explicit serial port, and `--runtest` runs the package test entry point.

## Build and verify

From the `python` directory:

```text
python -m pytest
python -m build
python -m twine check dist/*
```

## License and attribution

This package is licensed under GPL-3.0-only. The Python API is maintained in
the `mak-suite` project; its command compatibility began from the open-source
[SleepyTotem/makcu-py-lib](https://github.com/SleepyTotem/makcu-py-lib) API by
[SleepyTotem](https://github.com/SleepyTotem).
