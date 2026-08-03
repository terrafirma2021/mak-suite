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
`KEY_INIT` is SET, while a `CONTROLLER_CONTROL` GET carries `control:u8`.
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

BLE carries `CMD + PAYLOAD`; ATT supplies the length. The receiver passes the
known ATT length and command to the same MAK_API executor used by COM and UDP.

AES-128 transport encryption is available on COM and UDP. Encryption wraps
the command record and authenticates replies with the request nonce. BLE uses
BLE link security and does not accept a MAKXD AES key.

## Connect and learn

Connect by issuing `DEVICE` once and cache its result for the connection.
Typed commands use this cached information; they do not query the device
before each call.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `DEVICE` | `0x02` | empty | `kinds:u8` |

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

`dt` is an optional `u16` measured in USB microframes and must be `0..16383`.
One microframe is 125 us. Omitting `dt` selects zero.

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
| SET | `LEFT..SIDE2` | `0x11..0x15` | `state:u8 [dt:u16]` | none |
| SET | `MOVE_MASK` | `0x16` | `left:u8 right:u8 down:u8 up:u8` | none |
| SET | `WHEEL_MASK` | `0x17` | `down:u8 up:u8` | none |
| SET | `MOVE` | `0x18` | `x:i16 y:i16 [dt:u16]` | none |
| SET | `WHEEL` | `0x19` | `delta:i16 [dt:u16]` | none |
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
| SET | `KEY_DOWN` | `0x20` | `key:u8 [dt:u16]` | none |
| SET | `KEY_UP` | `0x21` | `key:u8 [dt:u16]` | none |
| SET | `KEY_INIT` | `0x22` | `[dt:u16]` | none |
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
| 10 | `LEFT_TRIGGER` | 0..65535 |
| 11 | `RIGHT_TRIGGER` | 0..65535 |
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
disabled or complete. Axes accept all five modes.

| Operation | Command | Value | Payload | Returned data |
| --- | --- | ---: | --- | --- |
| GET | `CONTROLLER_STATE` | `0x40` | empty | complete state |
| SET | `CONTROLLER_STATE` | `0x40` | complete state + `dt:u16` | none |
| GET | `CONTROLLER_CONTROL` | `0x41` | `control:u8` | `control:u8 value:i32` |
| SET | `CONTROLLER_CONTROL` | `0x41` | `control:u8 value:i32 dt:u16` | none |
| SET | `CONTROLLER_MASK` | `0x51` | `control:u8 mode:u8` | none |

The firmware rejects unsupported controls, invalid values or modes, and
`dt > 16383`. Controller injection requires a routed controller with a
successfully parsed current report.

## SDK surface

| SDK | Device kinds | Controller |
| --- | --- | --- |
| Python | `device.device()` | `device.gamepad.control/mask/state` |
| Rust | `device()` | `controller_control[_dt]`, `controller_mask`, state methods |
| C++ | `device()` | `controllerControl`, `controllerMask`, state methods |
| C | `makxd_get_device_kinds` | `makxd_controller_*` |
| C# | `device.device_kinds()` | `device.controller_*` |

## Examples

Read device kinds:

```text
request:  DE AD 00 00 02
response: DE AD 01 00 02 43
```

`0x43` is mouse + keyboard + Xbox GIP.

Set `SOUTH=1` with `dt=250`:

```text
DE AD 07 00 41 00 01 00 00 00 FA 00
```

Read `SOUTH`:

```text
request:  DE AD 01 00 41 00
response: DE AD 05 00 41 00 01 00 00 00
```

## COM event streams

`BUTTONS` and `KEY_KEYS` control COM-only physical event streams. Mouse button
changes emit `6B 6D 2E mask:u8`. Key changes emit
`6B 6D 2E key:u8 state:u8`.

## HPM KM compatibility

HPM also accepts lowercase ASCII KM commands. This compatibility parser is
independent of the SDK command path.

```text
km.version()
km.device()
km.echo([0|1])
km.buttons([0|1])
km.left([value[,dt]])
km.right([value[,dt]])
km.middle([value[,dt]])
km.side1([value[,dt]])
km.side2([value[,dt]])
km.move(x,y[,dt])
km.wheel(delta[,dt])
km.left_mask(enabled)
km.right_mask(enabled)
km.middle_mask(enabled)
km.side1_mask(enabled)
km.side2_mask(enabled)
km.move_mask(left,right,down,up)
km.wheel_mask(down,up)
km.down(key[,dt])
km.up(key[,dt])
km.init([dt])
km.press(key[,hold_ms[,random_range]])
km.string("text")
km.isdown(key)
km.multidown(key1,key2,...)
km.multiup(key1,key2,...)
km.multipress(key1,key2,...)
km.mask(key,mode)
km.remap(source,target)
km.keys([0|1])
km.controller(control[,value[,dt]])
km.controller_mask(control,mode)
km.controller_state([low,high,lt,rt,lx,ly,rx,ry,dt])
```

Controller names are lowercase forms of the semantic names above. HPM KM
queries return through the `>>> ` prompt; successful mutations are silent
unless KM echo is enabled.
