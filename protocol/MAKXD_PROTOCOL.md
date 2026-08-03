# MAKXD Public API

This document contains only the public MAKXD connection, KM ASCII, `MAK_API`,
and event-stream contracts.

## Connection

Choose one connection before connecting. Every typed public SDK method uses
`MAK_API`. KM remains available only as the explicit legacy/raw ASCII surface.

| Setting | Public values |
| --- | --- |
| Connection | COM, UDP, BLE |
| Typed command API | `MAK_API` |
| Legacy/raw command API | KM |
| AES-128 key | Optional on COM and UDP; not used by BLE |

The connection that sends a command owns its reply and any stream it starts.
Commands and reports are never broadcast to the other connections.

Typed methods never fall back to KM. A caller using the explicit legacy/raw
surface sends the exact KM ASCII shown below. `MAK_API` is required for typed
controller family, protocol, layout, capability bits, and route generation.

### COM

The port is optional. Automatic discovery probes all supported CH343 devices
before all supported CH340 devices. Each candidate is tried at 115200,
1000000, and 4000000 baud.

Plain COM discovery accepts only the exact `km.version()` reply `km.MAKXD`.
When an AES-128 key is supplied, discovery uses authenticated requests and
does not wait for a plaintext version reply.

### UDP

UDP supports Ethernet and Wi-Fi. Supply the device address and command port.
Public options include host/raw mode, local bind address or interface, VLAN ID
`1..4094`, and an optional AES-128 key.

### BLE

Supply the BLE address and the platform BLE callbacks required by the SDK.
BLE uses its connection security and does not accept a MAKXD AES key.

### SDK constructors

| SDK | Public constructor |
| --- | --- |
| Python | `ConnectionConfig.com(...)`, `.udp(...)`, `.ble(...)` |
| C++ | `makxd::ConnectionConfig::com(...)`, `::udp(...)`, `::ble(...)` |
| C | `makxd_connection_config_t` with `makxd_connect_config(...)` |
| C# | `ConnectionConfig.Com(...)`, `.Udp(...)`, `.Ble(...)` |
| Rust | `ConnectionConfig::com(...)`, `::udp(...)`, `::ble(...)` |

## KM API

KM commands are lowercase ASCII. `dt` is always optional where shown, uses
`0..16383` USB microframes, and is zero when omitted.

### Common KM API

| Exact KM ASCII |
| --- |
| `km.version()` |
| `km.device()` |
| `km.echo()` |
| `km.echo(0|1)` |

`km.version()` returns exactly `km.MAKXD`.

`km.device()` reports the routed mouse, keyboard, and controller outputs and
their exact report cadences. Mouse true cadence is independent from keyboard
and controller cadence. A zero cadence means that output is not routed.

Successful KM actions send no response by default. GET queries and rejected
commands still return their normal response through the `>>> ` prompt.
`km.echo(1)` persistently enables successful action echoes and returns its own
enabled acknowledgement. `km.echo(0)` persistently disables them and sends no
successful acknowledgement. Blank or invalid storage defaults to echo off.
Event streams are unaffected.

### Digital input merge

Mouse buttons, keyboard usages and modifiers, controller buttons, and
controller hat directions use per-input last-transition-wins state. A changed
physical input becomes final state. A changed injected input becomes final
state. If both change during the same input service pass, the injected
transition is applied last.

An unchanged state from either source does not overwrite the other source's
newer transition. For example, an injected release remains released while the
same physical button is still held; it becomes pressed again only after a new
physical release/press transition or a new injected transition.

An injection-only digital request that does not change final state is accepted
without building or submitting a USB report. Raw reports with movement,
wheel, axes, triggers, or other changed fields are still forwarded even when
their digital fields are unchanged.

Synthetic mouse and keyboard reports start from the descriptor-shaped neutral
report and apply final digital state. Synthetic controller reports start from
the latest successfully submitted controller report and apply final controller
state. Controller injection is rejected until a current routed controller
report has been submitted.

### Mouse KM API

| Exact KM ASCII |
| --- |
| `km.buttons()` |
| `km.buttons(1)` |
| `km.buttons(0)` |
| `km.left()` |
| `km.left(value[,dt])` |
| `km.right()` |
| `km.right(value[,dt])` |
| `km.middle()` |
| `km.middle(value[,dt])` |
| `km.side1()` |
| `km.side1(value[,dt])` |
| `km.side2()` |
| `km.side2(value[,dt])` |
| `km.move(x,y[,dt])` |
| `km.wheel(delta[,dt])` |
| `km.left_mask(enabled)` |
| `km.right_mask(enabled)` |
| `km.middle_mask(enabled)` |
| `km.side1_mask(enabled)` |
| `km.side2_mask(enabled)` |
| `km.move_mask(left,right,down,up)` |
| `km.wheel_mask(down,up)` |

Button values are `0` or `1`. Movement and wheel values are signed 16-bit
integers.

Mouse masks are raw-input locks. A button lock removes that physical button.
Movement locks zero only the selected raw direction: left is negative X, right
is positive X, down is positive Y, and up is negative Y. Wheel down is
negative and wheel up is positive. Injected mouse values bypass every lock.
Lock changes publish immediately, have no `dt`, do not use the injection queue
or mailbox, and do not submit a USB report by themselves.

### Keyboard KM API

| Exact KM ASCII |
| --- |
| `km.down(key[,dt])` |
| `km.up(key[,dt])` |
| `km.init([dt])` |
| `km.press(key[,hold_ms[,random_range]])` |
| `km.string("text")` |
| `km.isdown(key)` |
| `km.multidown(key1,key2,...)` |
| `km.multiup(key1,key2,...)` |
| `km.multipress(key1,key2,...)` |
| `km.mask(key,mode)` |
| `km.remap(source,target)` |
| `km.keys()` |
| `km.keys(1)` |
| `km.keys(0)` |

A key is a USB HID usage in `0..255` or a supported key name. Multi-key calls
accept 1..14 keys. `mode` is `0` or `1`.

`isdown` reads physical state before mask/remap. Keyboard masks and remaps
affect raw physical input only. Injected keys remain usable.

### Controller KM API

KM remains the legacy text transport. It uses the same semantic controller
owner and control IDs as MAK_API:

| Exact KM ASCII | Meaning |
| --- | --- |
| `km.controller(control)` | Read one injected semantic control |
| `km.controller(control,value[,dt])` | Set one injected semantic control |
| `km.controller_mask(control,mode)` | Set one physical-input mask |
| `km.controller_state()` | Read the complete injected semantic state |
| `km.controller_state(low,high,lt,rt,lx,ly,rx,ry,dt)` | Set the complete injected semantic state |

`control` is one of the stable names below. The names describe position, not
the product artwork printed on a controller.

| ID | Control | Value |
| ---: | --- | --- |
| 0 | `south` | 0 or 1 |
| 1 | `east` | 0 or 1 |
| 2 | `west` | 0 or 1 |
| 3 | `north` | 0 or 1 |
| 4 | `dpad_up` | 0 or 1 |
| 5 | `dpad_down` | 0 or 1 |
| 6 | `dpad_left` | 0 or 1 |
| 7 | `dpad_right` | 0 or 1 |
| 8 | `left_shoulder` | 0 or 1 |
| 9 | `right_shoulder` | 0 or 1 |
| 10 | `left_trigger` | 0..65535 |
| 11 | `right_trigger` | 0..65535 |
| 12 | `left_stick_x` | -32768..32767 |
| 13 | `left_stick_y` | -32768..32767 |
| 14 | `right_stick_x` | -32768..32767 |
| 15 | `right_stick_y` | -32768..32767 |
| 16 | `left_stick_button` | 0 or 1 |
| 17 | `right_stick_button` | 0 or 1 |
| 18 | `select` | 0 or 1 |
| 19 | `start` | 0 or 1 |
| 20 | `mode` | 0 or 1 |
| 21 | `grip_left` | 0 or 1 |
| 22 | `grip_right` | 0 or 1 |
| 23..54 | `extra_1`..`extra_32` | 0 or 1 |

The complete state is `digital_low:u32, digital_high:u32, left_trigger:u16,
right_trigger:u16, left_stick_x:i16, left_stick_y:i16, right_stick_x:i16,
right_stick_y:i16`. Digital bit N is semantic control ID N. Trigger and axis
controls are stored in their dedicated fields.

Mask modes are `disabled=0`, `complete=1`, `negative=2`, `positive=3`,
and `both=4`. Digital and trigger controls accept disabled or complete.
Axis controls accept disabled, negative, positive, or both. Masks affect
physical input only; injected values remain usable.

`dt` is optional on individual control writes and required in the complete
state write. It is `0..16383` in USB microframes; one unit is 125 us.
## COM event streams

Button and key events are COM-only, disabled after reset, and mutually
exclusive with each other and the general input stream.

### Button events

`km.buttons()` returns the current event state. `km.buttons(1)` enables it and
`km.buttons(0)` disables it.

Each changed physical mouse-button snapshot emits exactly four bytes:

```text
6b 6d 2e mask:u8
```

| Bit | Button |
| ---: | --- |
| 0 | left |
| 1 | right |
| 2 | middle |
| 3 | side 1 |
| 4 | side 2 |

`0x00` means all released. Repeated snapshots emit nothing.

### Key events

`km.keys()` returns the current event state. `km.keys(1)` enables it and
`km.keys(0)` disables it.

Each physical key transition emits exactly five bytes:

```text
6b 6d 2e key:u8 state:u8
```

`state` is `1` for down and `0` for up. Events contain the physical HID usage
before mask/remap. Repeated states emit nothing.

## MAK_API

`MAK_API_ID` is `0x00`. Verbs are `GET=0x00`, `SET=0x01`, and
`EXEC=0x02`. Successful SET calls send no response. GET and EXEC responses
use status `0x01`; rejected calls, including rejected SET calls, respond with
`0xFF`. Event streams are unaffected. All multibyte integers are
little-endian.

There is no binary API version opcode. The contract below is the sole
pre-production MAK_API contract.

### Common

| Opcode | Value |
| --- | ---: |
| `API_DEVICE` | `0x02` |

`API_DEVICE GET` has no request payload. Its 22-byte result is:

```text
route_mask:u8
mouse_uframes:u16
keyboard_uframes:u16
controller_uframes:u16
route_generation:u32
controller_family:u8
controller_protocol:u8
controller_layout:u8
controller_supported_low:u32
controller_supported_high:u32
```

Controller families are none 0, generic HID 1, DS4 2, DualSense 3,
DualSense Edge 4, Xbox GIP 5, and Xbox 360/XInput 6. Protocols are none 0,
HID 1, GIP 2, and XInput 3. Support bit N corresponds to semantic control ID
N. A controller SET must carry the current route generation.

### Mouse

| Opcode | Value |
| --- | ---: |
| `API_BUTTONS` | `0x10` |
| `API_LEFT` | `0x11` |
| `API_RIGHT` | `0x12` |
| `API_MIDDLE` | `0x13` |
| `API_SIDE1` | `0x14` |
| `API_SIDE2` | `0x15` |
| `API_MOVE_MASK` | `0x16` |
| `API_WHEEL_MASK` | `0x17` |
| `API_MOVE` | `0x18` |
| `API_WHEEL` | `0x19` |
| `API_LEFT_MASK` | `0x1A` |
| `API_RIGHT_MASK` | `0x1B` |
| `API_MIDDLE_MASK` | `0x1C` |
| `API_SIDE1_MASK` | `0x1D` |
| `API_SIDE2_MASK` | `0x1E` |

### Keyboard

| Opcode | Value |
| --- | ---: |
| `API_KEY_DOWN` | `0x20` |
| `API_KEY_UP` | `0x21` |
| `API_KEY_INIT` | `0x22` |
| `API_KEY_PRESS` | `0x23` |
| `API_KEY_STRING` | `0x24` |
| `API_KEY_IS_DOWN` | `0x25` |
| `API_KEY_MULTI_DOWN` | `0x26` |
| `API_KEY_MULTI_UP` | `0x27` |
| `API_KEY_MULTI_PRESS` | `0x28` |
| `API_KEY_MASK` | `0x29` |
| `API_KEY_REMAP` | `0x2A` |
| `API_KEY_KEYS` | `0x2B` |

### Controller

| Opcode | Value |
| --- | ---: |
| `API_CONTROLLER_STATE` | `0x40` |
| `API_CONTROLLER_CONTROL` | `0x41` |
| `API_CONTROLLER_MASK` | `0x51` |

The semantic control IDs and values are identical to the Controller KM API
table above.

`API_CONTROLLER_CONTROL GET` request:

```text
control:u8
```

Successful result:

```text
control:u8 value:i32 route_generation:u32
```

`API_CONTROLLER_CONTROL SET` request:

```text
control:u8 value:i32 dt:u16 route_generation:u32
```

`API_CONTROLLER_MASK SET` request:

```text
control:u8 mode:u8 route_generation:u32
```

`API_CONTROLLER_STATE GET` has no request payload. Its 24-byte result is:

```text
digital_low:u32 digital_high:u32
left_trigger:u16 right_trigger:u16
left_stick_x:i16 left_stick_y:i16
right_stick_x:i16 right_stick_y:i16
route_generation:u32
```

`API_CONTROLLER_STATE SET` uses the same first 20 bytes, followed by:

```text
dt:u16 route_generation:u32
```

The firmware rejects unsupported controls, invalid values, invalid mask modes,
`dt > 16383`, and stale route generations. Unlisted opcodes are not part of
the public API.

## Public SDK Controller Surface

Every language uses the same `ControllerControl`, `ControllerMaskMode`,
controller-family, controller-protocol, complete-state, and device-route
values defined above. Artwork names such as Cross, Circle, A, B, X, and Y are
not separate public controls.

| SDK | Read/write control | Mask | Complete state | Route identity |
| --- | --- | --- | --- | --- |
| Python | `device.gamepad.control(...)` | `device.gamepad.mask(...)` | `device.gamepad.state(...)` | `device.device()` |
| Rust | `controller_control_state`, `controller_control[_dt]` | `controller_mask` | `controller_state`, `set_controller_state[_dt]` | `device()` |
| C++ | `controllerControl(...)` | `controllerMask(...)` | `controllerState`, `setControllerState(...)` | `device()` |
| C | `makxd_controller_control_get`, `makxd_controller_control[_dt]` | `makxd_controller_mask` | `makxd_controller_state_get`, `makxd_controller_state_set[_dt]` | `makxd_get_output_device` |
| C# | `device.controller_control(...)` | `device.controller_mask(...)` | `device.controller_state(...)` | `device.device_route()` |

The non-DT overloads use `dt_uframes=0`. A route capability bit must be set
before that control can be accepted. KM remains callable through the explicit
legacy/raw surface, but is never a backend or fallback for these typed methods.

## Constructing MAK_API frames

### COM send

A plaintext COM request is:

```text
DE AD | payload_length:u16le | 00 | 00 opcode verb payload...
```

The first `00` is the generic frame type. The second `00` is `MAK_API_ID`.
`payload_length` counts from `MAK_API_ID` through the final payload byte; it
does not count `DE AD`, the length field, or the generic frame type.

`API_DEVICE GET` has no command payload:

```text
DE AD 03 00 00 00 02 00
```

Set controller `south=1`, `dt=250`, and route generation 7:

```text
DE AD 0E 00 00 00 41 01 00 01 00 00 00 FA 00 07 00 00 00
```

### COM receive

A plaintext COM response is:

```text
DE AD | payload_length:u16le | 00 | 00 opcode status result...
```

Successful controller SET sends no response frame.

Rejected controller SET:

```text
DE AD 03 00 00 00 41 FF
```

Read controller `south`:

```text
DE AD 04 00 00 00 41 00 00
```

Successful result `south=1`, route generation 7:

```text
DE AD 0C 00 00 00 41 01 00 01 00 00 00 07 00 00 00
```

Encrypted COM uses the SDK encryption option; callers still supply the same
opcode, verb, and payload.

### UDP and BLE records

The public identified request record is:

```text
00 opcode verb payload...
```

GET, EXEC, and rejected-call response records are:

```text
00 opcode status result...
```

For example, controller button 1 SET is sent as:

```text
00 60 01 01
```

and a successful SET sends no response record. A rejected SET returns:

```text
00 60 FF
```
