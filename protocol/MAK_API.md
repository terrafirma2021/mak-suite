# MAK_API

MAK_API is the command interface used by the Python, Rust, C++, C, and C#
SDKs. Multibyte integers are little-endian.

## Frame

```text
DE AD | LEN:u16 | CMD:u8 | PAYLOAD[LEN]
```

`LEN` is the payload length and does not include `CMD`. Requests with no
payload use `LEN=0`.

There is no GET/SET field. `CMD` and the exact payload shape select the
operation. Do not treat every empty payload as GET: for example, empty
`KEY_INIT` is SET, while `INPUT_STREAM` GET carries `kind:u8`.
The `Operation` column below defines each valid form.

Successful GET replies use the same frame:

```text
DE AD | LEN:u16 | CMD:u8 | RESULT[LEN]
```

SET operations do not reply when accepted. A rejected GET or SET returns
`DE AD | 01 00 | CMD | FF`. No success-status byte is used.

COM and plaintext UDP carry the complete frame. Raw UDP prepends its
transaction header before the frame. Ethernet and Wi-Fi use the same UDP
payload, including when the selected interface is VLAN tagged.

BLE carries `CMD + PAYLOAD`; ATT supplies the length. SDKs automatically
combine commands that are already queued, use the negotiated write size, and
restore individual replies in request order. A lone command is sent
immediately. Applications use the normal MAK_API calls and do not select a
batch size or enable a batching mode.

AES-128 transport encryption is available on MAKXD COM and UDP; MAKCU
uses plaintext COM and supports encrypted UDP. Encryption wraps
the command record and authenticates replies with the request nonce. BLE uses
BLE link security and does not accept a MAKXD AES key.

## Connect and learn

Connect by issuing `DEVICE` once and cache its result for the connection.
Typed commands use this cached information; they do not query the device
before each call.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `DEVICE` | `0x02` | empty | `kinds:u8` |
| GET | `FIRMWARE_VERSION` | `0x04` | empty | `version:u32` |

`FIRMWARE_VERSION` returns the installed application firmware version.

`kinds` is a bitmask. Multiple device kinds are ORed together.
It reports the active routed kinds, including saved mouse/keyboard injection
assignments and linked-device kinds. Reconnect after changing routes to refresh
an SDK's cached result.

| Device kind | Bit |
| --- | ---: |
| mouse | `0x01` |
| keyboard | `0x02` |
| generic HID controller | `0x04` |
| DS4 | `0x08` |
| DualSense / DS5 | `0x10` |
| DualSense Edge | `0x20` |
| Xbox GIP | `0x40` |
| Xbox 360 / XInput | `0x80` |

## Timing

Mouse, keyboard, and controller commands do not accept caller-supplied `dt`.
Use the exact argument counts and payload lengths below. A legacy DT argument
or two-byte trailer is rejected, including an explicit zero.

Keyboard press durations (`hold_ms` and `random_range`) remain in milliseconds.
The polling intervals returned by `km.device()` use USB microframes.
Change events contain only kind, control ID, and value.

## Mouse

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `BUTTONS` | `0x10` | empty | `enabled:u8` |
| SET | `BUTTONS` | `0x10` | `enabled:u8` | none |
| GET | `LEFT` | `0x11` | empty | `state:u8` |
| GET | `RIGHT` | `0x12` | empty | `state:u8` |
| GET | `MIDDLE` | `0x13` | empty | `state:u8` |
| GET | `SIDE1` | `0x14` | empty | `state:u8` |
| GET | `SIDE2` | `0x15` | empty | `state:u8` |
| SET | `LEFT..SIDE2` | `0x11..0x15` | `state:u8` | none |
| SET | `MOVE_MASK` | `0x16` | `left:u8 right:u8 down:u8 up:u8` | none |
| SET | `WHEEL_MASK` | `0x17` | `down:u8 up:u8` | none |
| SET | `MOVE` | `0x18` | `x:i16 y:i16` | none |
| SET | `WHEEL` | `0x19` | `delta:i16` | none |
| SET | `LEFT_MASK` | `0x1A` | `enabled:u8` | none |
| SET | `RIGHT_MASK` | `0x1B` | `enabled:u8` | none |
| SET | `MIDDLE_MASK` | `0x1C` | `enabled:u8` | none |
| SET | `SIDE1_MASK` | `0x1D` | `enabled:u8` | none |
| SET | `SIDE2_MASK` | `0x1E` | `enabled:u8` | none |

Boolean values are 0 or 1. X, Y, and wheel use the signed 16-bit range.
Masks affect physical input only; injected values bypass them.

## Keyboard

Keys are USB HID usages `0..255`. SDK key names are converted before framing.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| SET | `KEY_DOWN` | `0x20` | `key:u8` | none |
| SET | `KEY_UP` | `0x21` | `key:u8` | none |
| SET | `KEY_INIT` | `0x22` | empty | none |
| SET | `KEY_PRESS` | `0x23` | `key:u8 [hold_ms:u32 [random_range:u32]]` | none |
| SET | `KEY_STRING` | `0x24` | `text:ASCII[0..248]` | none |
| GET | `KEY_IS_DOWN` | `0x25` | `key:u8` | `state:u8` |
| SET | `KEY_MULTI_DOWN` | `0x26` | `keys:u8[1..14]` | none |
| SET | `KEY_MULTI_UP` | `0x27` | `keys:u8[1..14]` | none |
| SET | `KEY_MULTI_PRESS` | `0x28` | `keys:u8[1..14]` | none |
| SET | `KEY_MASK` | `0x29` | `key:u8 mode:u8` | none |
| SET | `KEY_REMAP` | `0x2A` | `source:u8 target:u8` | none |
| GET | `KEY_KEYS` | `0x2B` | empty | `enabled:u8` |
| SET | `KEY_KEYS` | `0x2B` | `enabled:u8` | none |

Keyboard masks and remaps affect physical input only. `KEY_INIT` clears
injected keyboard state and keyboard policies.

## Controller

Names describe physical position, not product artwork.

| ID | Control | Value |
| ---: | --- | --- |
| 0 | `SOUTH` | 0 or 1 |
| 1 | `EAST` | 0 or 1 |
| 2 | `WEST` | 0 or 1 |
| 3 | `NORTH` | 0 or 1 |
| 4 | `DPAD_UP` | 0 or 1 |
| 5 | `DPAD_DOWN` | 0 or 1 |
| 6 | `DPAD_LEFT` | 0 or 1 |
| 7 | `DPAD_RIGHT` | 0 or 1 |
| 8 | `LEFT_SHOULDER` | 0 or 1 |
| 9 | `RIGHT_SHOULDER` | 0 or 1 |
| 10 | `LEFT_TRIGGER` | 0..1023 |
| 11 | `RIGHT_TRIGGER` | 0..1023 |
| 12 | `LEFT_STICK_X` | -32768..32767 |
| 13 | `LEFT_STICK_Y` | -32768..32767 |
| 14 | `RIGHT_STICK_X` | -32768..32767 |
| 15 | `RIGHT_STICK_Y` | -32768..32767 |
| 16 | `LEFT_STICK_BUTTON` | 0 or 1 |
| 17 | `RIGHT_STICK_BUTTON` | 0 or 1 |
| 18 | `SELECT` | 0 or 1 |
| 19 | `START` | 0 or 1 |
| 20 | `MODE` | 0 or 1 |
| 21 | `GRIP_LEFT` | 0 or 1 |
| 22 | `GRIP_RIGHT` | 0 or 1 |
| 23..54 | `EXTRA_1..EXTRA_32` | 0 or 1 |

Complete controller state is:

```text
digital_low:u32
digital_high:u32
left_trigger:u16
right_trigger:u16
left_stick_x:i16
left_stick_y:i16
right_stick_x:i16
right_stick_y:i16
```

Digital bit N is control ID N. Mask modes are `DISABLED=0`, `COMPLETE=1`,
`NEGATIVE=2`, `POSITIVE=3`, and `BOTH=4`. Digital and trigger controls accept
disabled or complete. Axes accept disabled, negative, positive, or both;
complete is not valid for an axis.

Trigger fields remain `u16` on the wire but their canonical value range is
`0..1023` for both injection and input streaming. MAKXD maps that 10-bit value
to and from the selected controller's native trigger width. Stick axes remain
signed `-32768..32767`.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `CONTROLLER_STATE` | `0x40` | empty | complete state |
| SET | `CONTROLLER_STATE` | `0x40` | complete state (20 bytes) | none |
| GET | `CONTROLLER_STREAM` | `0x41` | empty | `enabled:u8` |
| SET | `CONTROLLER_STREAM` | `0x41` | `enabled:u8` | none |
| SET | `CONTROLLER_MASK` | `0x51` | `control:u8 mode:u8` | none |

`CONTROLLER_STREAM` accepts only on/off (`1`/`0`) or an empty query.
The previous named/individual-control command and its 3- or 5-byte payloads
are removed. Use `CONTROLLER_STATE` for injection and `CONTROLLER_MASK` for
physical-input masks. Update firmware and SDKs together.

MAKXD rejects unsupported controls, invalid values or modes, and incorrect
payload lengths. Controller injection requires a routed controller with a
successfully parsed current report.

## SDK surface

| SDK | Device kinds | Firmware version | Controller |
| --- | --- | --- | --- |
| Python | `device.device()` | `device.firmware_version()` | `device.gamepad.stream/mask/state` |
| Rust | `device()` | `firmware_version()` | `controller_stream`, `controller_mask`, state methods |
| C++ | `device()` | `firmwareVersion()` | `controllerStream`, `controllerMask`, state methods |
| C | `makxd_get_device_kinds` | `makxd_firmware_version` | `makxd_controller_*` |
| C# | `device.device_kinds()` | `device.firmware_version()` | `device.controller_*` |

## Examples

Read device kinds:

```text
request:  DE AD 00 00 02
response: DE AD 01 00 02 43
```

`0x43` is mouse + keyboard + Xbox GIP.

Read firmware version:

```text
request:  DE AD 00 00 04
response: DE AD 04 00 04 01 00 00 00
```

## Input change streams

Mouse, keyboard, and controller subscriptions are independent. Set one kind
on/off; query one kind. Enabling a kind does not disable the others.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `INPUT_STREAM` | `0x52` | `kind:u8` | `enabled:u8` |
| SET | `INPUT_STREAM` | `0x52` | `kind:u8 enabled:u8` | none |
| EVENT | `INPUT_CHANGE` | `0x53` | `kind:u8 id:u8 value` | unsolicited |

| Kind | ID | On/off alias | Changed controls |
| --- | ---: | --- | --- |
| mouse | 1 | `BUTTONS` (`0x10`) | Button IDs 0..31; 0 released, 1 pressed |
| keyboard | 2 | `KEY_KEYS` (`0x2B`) | HID usages 0..255, including modifiers; 0 released, 1 pressed |
| controller | 3 | `CONTROLLER_STREAM` (`0x41`) | Canonical button IDs and triggers 10/11 |

Controller IDs use the table above. IDs 12..15 (stick axes) are not emitted.
D-pad directions and stick clicks are buttons. Extras are emitted where the
connected controller supports them. Mouse motion and wheel are not emitted.

Every event is a complete MAK frame:

```text
DE AD 03 00 53 kind id state:u8          # 8 bytes, digital state 0 or 1
DE AD 04 00 53 03   id trigger:u16le     # 9 bytes, IDs 10/11, 0..1023
DE AD 03 00 53 kind FF FF               # overflow for this kind
```

`LEN` determines the frame boundary. Dispatch `0x53` separately from command
replies, then read kind and ID. A trigger value above 1023 is invalid. There
are no event bitmaps, timestamps, CR/LF, or prompts.

Enable controller changes, observe a full left-trigger press, then disable:

```text
DE AD 01 00 41 01
DE AD 04 00 53 03 0A FF 03
DE AD 01 00 41 00
```

Enable keyboard independently and query controller subscription state:

```text
request:  DE AD 02 00 52 02 01
request:  DE AD 01 00 52 03
response: DE AD 01 00 52 01
```

Events describe changed physical controls before masks, remaps, or injection.
Unchanged values emit nothing. Each enable starts from released/zero; the next
physical report emits held buttons and nonzero triggers. Detach releases known
active controls. Trigger native ranges are normalized to 0..1023, rounded to the
nearest integer. An overflow disables only its kind and invalidates its queued
changes; discard that kind's cached state and explicitly enable it again.

All enabled kinds share one destination: the caller of the last successful
subscription change. Queries and disabling an already-disabled kind do not
transfer ownership. Disconnect invalidates the destination. No automatic
fallback to another transport occurs.

COM, UDP, BLE Command TX, and WebSocket carry the same full event frame.
BLE command records still omit the length header; event notifications retain it.
Raw UDP retains the subscription transaction header; events do not consume a
pending query transaction. WebSocket uses the unsolicited request ID `0xffff`.
Encrypted MAKXD COM and encrypted UDP events are authenticated using their carried event nonce, which
is distinct from a command reply nonce. Authenticate before decoding the event;
continue matching normal replies to the requested opcode and transaction nonce.

| SDK | Set kind | Query kind | Receive changes |
| --- | --- | --- | --- |
| Python | `device.input_stream(StreamKind.CONTROLLER, True)` | `device.input_stream(StreamKind.CONTROLLER)` | `set_input_callback(fn)` or `read_input_change(timeout)` |
| Rust | `device.input_stream(StreamKind::Controller, true)` | `input_stream_state(kind)` | `input_changes()` channel |
| C++ | `device.inputStream(StreamKind::Controller, true)` | `inputStream(kind)` | `setInputCallback(fn)` |
| C | `makxd_input_stream(device, MAKXD_STREAM_CONTROLLER, true)` | `makxd_input_stream_get(...)` | `makxd_set_input_callback(...)` |
| C# | `device.input_stream(StreamKind.Controller, true)` | `device.input_stream(kind)` | `device.read_input_change()` |

Callbacks run on the reader thread; keep them short and send synchronous
queries from another thread. C# polling uses the configured transport timeout.
The standalone `StreamFrameDecoder` helpers handle fragmented or concatenated
frames. The former raw `km.` events and general full-report stream helpers are
not part of this public subscription contract.

## KM_API compatibility

MAKXD also accepts lowercase ASCII KM commands. See [KM_API](KM_API.md) for
the complete command, argument, response, event-stream, and failure contract.

```text
km.version()
km.device()
km.echo([0|1])
km.buttons([0|1])
km.left([value])
km.right([value])
km.middle([value])
km.side1([value])
km.side2([value])
km.move(x,y)
km.wheel(delta)
km.left_mask(enabled)
km.right_mask(enabled)
km.middle_mask(enabled)
km.side1_mask(enabled)
km.side2_mask(enabled)
km.move_mask(left,right,down,up)
km.wheel_mask(down,up)
km.down(key)
km.up(key)
km.init()
km.press(key[,hold_ms[,random_range]])
km.string("text")
km.isdown(key)
km.multidown(key1,key2,...)
km.multiup(key1,key2,...)
km.multipress(key1,key2,...)
km.mask(key,mode)
km.remap(source,target)
km.keys([0|1])
km.controller([0|1])
km.stream(mouse|keyboard|controller[,0|1])
km.controller_mask(control,mode)
km.controller_state([low,high,lt,rt,lx,ly,rx,ry])
```

Controller mask names are lowercase forms of the semantic names above. KM_API
queries return through the `>>> ` prompt; successful mutations are silent
unless KM echo is enabled.
