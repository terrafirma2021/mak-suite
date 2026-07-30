# MAKXD Public API

This document contains only the public MAKXD connection, KM ASCII, `MAK_API`,
and event-stream contracts.

## Connection

Choose one connection and one command API before connecting.

| Setting | Public values |
| --- | --- |
| Connection | COM, UDP, BLE |
| Command API | KM, `MAK_API` |
| AES-128 key | Optional on COM and UDP; not used by BLE |

The connection that sends a command owns its reply and any stream it starts.
Commands and reports are never broadcast to the other connections.

KM and `MAK_API` expose the same public SDK functions. KM sends the exact ASCII
shown below. `MAK_API` sends the matching typed opcode.

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

`km.version()` returns exactly `km.MAKXD`.

`km.device()` reports the routed mouse, keyboard, and controller outputs and
their exact report cadences. Mouse true cadence is independent from keyboard
and controller cadence. A zero cadence means that output is not routed.

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

Button values are `0` or `1`. Movement and wheel values are signed 16-bit
integers.

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

The full state call is the controller streaming/snapshot form:

| Exact KM ASCII |
| --- |
| `km.controller(buttons,hat,lt,rt,x,y,rx,ry,z,rz[,dt])` |

Only this complete snapshot carries the `buttons:u32` bitfield and encoded
`hat:u8` value. Its field ranges are:

| Field | Range |
| --- | ---: |
| buttons | `0..4294967295` |
| hat | `0..8` |
| lt, rt | `0..65535` |
| x, y, rx, ry, z, rz | `-32768..32767` |

Each injected button has a named command. With no argument it returns injected
state. `1` presses it and `0` releases it:

| Exact KM ASCII |
| --- |
| `km.controller_button1()` |
| `km.controller_button1(value[,dt])` |
| `km.controller_button2()` |
| `km.controller_button2(value[,dt])` |
| `km.controller_button3()` |
| `km.controller_button3(value[,dt])` |
| `km.controller_button4()` |
| `km.controller_button4(value[,dt])` |
| `km.controller_button5()` |
| `km.controller_button5(value[,dt])` |
| `km.controller_button6()` |
| `km.controller_button6(value[,dt])` |
| `km.controller_button7()` |
| `km.controller_button7(value[,dt])` |
| `km.controller_button8()` |
| `km.controller_button8(value[,dt])` |
| `km.controller_button9()` |
| `km.controller_button9(value[,dt])` |
| `km.controller_button10()` |
| `km.controller_button10(value[,dt])` |
| `km.controller_button11()` |
| `km.controller_button11(value[,dt])` |
| `km.controller_button12()` |
| `km.controller_button12(value[,dt])` |
| `km.controller_button13()` |
| `km.controller_button13(value[,dt])` |
| `km.controller_button14()` |
| `km.controller_button14(value[,dt])` |
| `km.controller_button15()` |
| `km.controller_button15(value[,dt])` |
| `km.controller_button16()` |
| `km.controller_button16(value[,dt])` |
| `km.controller_button17()` |
| `km.controller_button17(value[,dt])` |
| `km.controller_button18()` |
| `km.controller_button18(value[,dt])` |
| `km.controller_button19()` |
| `km.controller_button19(value[,dt])` |
| `km.controller_button20()` |
| `km.controller_button20(value[,dt])` |
| `km.controller_button21()` |
| `km.controller_button21(value[,dt])` |
| `km.controller_button22()` |
| `km.controller_button22(value[,dt])` |
| `km.controller_button23()` |
| `km.controller_button23(value[,dt])` |
| `km.controller_button24()` |
| `km.controller_button24(value[,dt])` |
| `km.controller_button25()` |
| `km.controller_button25(value[,dt])` |
| `km.controller_button26()` |
| `km.controller_button26(value[,dt])` |
| `km.controller_button27()` |
| `km.controller_button27(value[,dt])` |
| `km.controller_button28()` |
| `km.controller_button28(value[,dt])` |
| `km.controller_button29()` |
| `km.controller_button29(value[,dt])` |
| `km.controller_button30()` |
| `km.controller_button30(value[,dt])` |
| `km.controller_button31()` |
| `km.controller_button31(value[,dt])` |
| `km.controller_button32()` |
| `km.controller_button32(value[,dt])` |

The stable mapping is direct: `button1` is HID Button usage 1 and snapshot bit
0; `button2` is usage 2 and bit 1; this continues through `button32`, usage 32
and bit 31. Product-specific A/B/X/Y labels are intentionally not assumed.

Hat injection uses independent horizontal and vertical components. There is no
public centre command. `1` presses a direction, `0` releases that component,
and `()` returns its injected state:

| Exact KM ASCII |
| --- |
| `km.controller_hat_left()` |
| `km.controller_hat_left(value[,dt])` |
| `km.controller_hat_right()` |
| `km.controller_hat_right(value[,dt])` |
| `km.controller_hat_down()` |
| `km.controller_hat_down(value[,dt])` |
| `km.controller_hat_up()` |
| `km.controller_hat_up(value[,dt])` |

Pressing left releases right; pressing right releases left. Pressing down
releases up; pressing up releases down. A diagonal is one horizontal plus one
vertical component.

The remaining injection commands are:

| Exact KM ASCII |
| --- |
| `km.controller_lt(value[,dt])` |
| `km.controller_rt(value[,dt])` |
| `km.controller_left_stick(x,y[,dt])` |
| `km.controller_right_stick(rx,ry[,dt])` |
| `km.controller_aux(z,rz[,dt])` |

Controller masks are raw-input locks. Setting a lock bit to `1` forces only
that physical component to zero before injection. Setting it to `0` unlocks
that physical component. Locks never block or alter injected controller
values. Lock changes publish immediately, have no `dt` argument, do not enter
the injection queue or mailbox, and do not submit a USB report by themselves.
For a paired peer output, the complete latest lock policy is coalesced in one
dedicated pending slot and retried until the peer link accepts it.

| Exact KM ASCII |
| --- |
| `km.controller_button1_mask(enabled)` through `km.controller_button32_mask(enabled)` |
| `km.controller_hat_left_mask(enabled)` |
| `km.controller_hat_right_mask(enabled)` |
| `km.controller_hat_down_mask(enabled)` |
| `km.controller_hat_up_mask(enabled)` |
| `km.controller_lt_mask(enabled)` |
| `km.controller_rt_mask(enabled)` |
| `km.controller_left_stick_mask(left,right,down,up)` |
| `km.controller_right_stick_mask(left,right,down,up)` |
| `km.controller_aux_mask(z_negative,z_positive,rz_negative,rz_positive)` |

Hat locks remove only the selected raw component. For example, locking raw up
from a physical up-right value leaves raw right. Stick direction locks zero
the axis only while its selected raw sign is active.

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

`MAK_API_ID` is `0x00`. Verbs are `GET=0x00`, `SET=0x01`, and `EXEC=0x02`.
Successful responses use status `0x01`; rejected calls use `0xFF`.

### Common

| Opcode | Value |
| --- | ---: |
| `API_VERSION` | `0x01` |
| `API_DEVICE` | `0x02` |

### Mouse

| Opcode | Value |
| --- | ---: |
| `API_BUTTONS` | `0x10` |
| `API_LEFT` | `0x11` |
| `API_RIGHT` | `0x12` |
| `API_MIDDLE` | `0x13` |
| `API_SIDE1` | `0x14` |
| `API_SIDE2` | `0x15` |
| `API_MOVE` | `0x18` |
| `API_WHEEL` | `0x19` |

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
| `API_CONTROLLER_LT` | `0x43` |
| `API_CONTROLLER_RT` | `0x44` |
| `API_CONTROLLER_LEFT_STICK` | `0x45` |
| `API_CONTROLLER_RIGHT_STICK` | `0x46` |
| `API_CONTROLLER_AUX` | `0x47` |
| `API_CONTROLLER_HAT_LEFT` | `0x48` |
| `API_CONTROLLER_HAT_RIGHT` | `0x49` |
| `API_CONTROLLER_HAT_DOWN` | `0x4A` |
| `API_CONTROLLER_HAT_UP` | `0x4B` |
| `API_CONTROLLER_LT_MASK` | `0x52` |
| `API_CONTROLLER_RT_MASK` | `0x53` |
| `API_CONTROLLER_LEFT_STICK_MASK` | `0x54` |
| `API_CONTROLLER_RIGHT_STICK_MASK` | `0x55` |
| `API_CONTROLLER_AUX_MASK` | `0x56` |
| `API_CONTROLLER_HAT_LEFT_MASK` | `0x58` |
| `API_CONTROLLER_HAT_RIGHT_MASK` | `0x59` |
| `API_CONTROLLER_HAT_DOWN_MASK` | `0x5A` |
| `API_CONTROLLER_HAT_UP_MASK` | `0x5B` |

### Controller buttons

| Opcode | Value |
| --- | ---: |
| `API_CONTROLLER_BUTTON1` | `0x60` |
| `API_CONTROLLER_BUTTON2` | `0x61` |
| `API_CONTROLLER_BUTTON3` | `0x62` |
| `API_CONTROLLER_BUTTON4` | `0x63` |
| `API_CONTROLLER_BUTTON5` | `0x64` |
| `API_CONTROLLER_BUTTON6` | `0x65` |
| `API_CONTROLLER_BUTTON7` | `0x66` |
| `API_CONTROLLER_BUTTON8` | `0x67` |
| `API_CONTROLLER_BUTTON9` | `0x68` |
| `API_CONTROLLER_BUTTON10` | `0x69` |
| `API_CONTROLLER_BUTTON11` | `0x6A` |
| `API_CONTROLLER_BUTTON12` | `0x6B` |
| `API_CONTROLLER_BUTTON13` | `0x6C` |
| `API_CONTROLLER_BUTTON14` | `0x6D` |
| `API_CONTROLLER_BUTTON15` | `0x6E` |
| `API_CONTROLLER_BUTTON16` | `0x6F` |
| `API_CONTROLLER_BUTTON17` | `0x70` |
| `API_CONTROLLER_BUTTON18` | `0x71` |
| `API_CONTROLLER_BUTTON19` | `0x72` |
| `API_CONTROLLER_BUTTON20` | `0x73` |
| `API_CONTROLLER_BUTTON21` | `0x74` |
| `API_CONTROLLER_BUTTON22` | `0x75` |
| `API_CONTROLLER_BUTTON23` | `0x76` |
| `API_CONTROLLER_BUTTON24` | `0x77` |
| `API_CONTROLLER_BUTTON25` | `0x78` |
| `API_CONTROLLER_BUTTON26` | `0x79` |
| `API_CONTROLLER_BUTTON27` | `0x7A` |
| `API_CONTROLLER_BUTTON28` | `0x7B` |
| `API_CONTROLLER_BUTTON29` | `0x7C` |
| `API_CONTROLLER_BUTTON30` | `0x7D` |
| `API_CONTROLLER_BUTTON31` | `0x7E` |
| `API_CONTROLLER_BUTTON32` | `0x7F` |

### Controller button raw locks

| Opcode | Value |
| --- | ---: |
| `API_CONTROLLER_BUTTON1_MASK` | `0x80` |
| `API_CONTROLLER_BUTTON2_MASK` | `0x81` |
| `API_CONTROLLER_BUTTON3_MASK` | `0x82` |
| `API_CONTROLLER_BUTTON4_MASK` | `0x83` |
| `API_CONTROLLER_BUTTON5_MASK` | `0x84` |
| `API_CONTROLLER_BUTTON6_MASK` | `0x85` |
| `API_CONTROLLER_BUTTON7_MASK` | `0x86` |
| `API_CONTROLLER_BUTTON8_MASK` | `0x87` |
| `API_CONTROLLER_BUTTON9_MASK` | `0x88` |
| `API_CONTROLLER_BUTTON10_MASK` | `0x89` |
| `API_CONTROLLER_BUTTON11_MASK` | `0x8A` |
| `API_CONTROLLER_BUTTON12_MASK` | `0x8B` |
| `API_CONTROLLER_BUTTON13_MASK` | `0x8C` |
| `API_CONTROLLER_BUTTON14_MASK` | `0x8D` |
| `API_CONTROLLER_BUTTON15_MASK` | `0x8E` |
| `API_CONTROLLER_BUTTON16_MASK` | `0x8F` |
| `API_CONTROLLER_BUTTON17_MASK` | `0x90` |
| `API_CONTROLLER_BUTTON18_MASK` | `0x91` |
| `API_CONTROLLER_BUTTON19_MASK` | `0x92` |
| `API_CONTROLLER_BUTTON20_MASK` | `0x93` |
| `API_CONTROLLER_BUTTON21_MASK` | `0x94` |
| `API_CONTROLLER_BUTTON22_MASK` | `0x95` |
| `API_CONTROLLER_BUTTON23_MASK` | `0x96` |
| `API_CONTROLLER_BUTTON24_MASK` | `0x97` |
| `API_CONTROLLER_BUTTON25_MASK` | `0x98` |
| `API_CONTROLLER_BUTTON26_MASK` | `0x99` |
| `API_CONTROLLER_BUTTON27_MASK` | `0x9A` |
| `API_CONTROLLER_BUTTON28_MASK` | `0x9B` |
| `API_CONTROLLER_BUTTON29_MASK` | `0x9C` |
| `API_CONTROLLER_BUTTON30_MASK` | `0x9D` |
| `API_CONTROLLER_BUTTON31_MASK` | `0x9E` |
| `API_CONTROLLER_BUTTON32_MASK` | `0x9F` |

Controller payloads use little-endian integers:

- `API_CONTROLLER_STATE SET`: `buttons:u32, hat:u8, lt:u16, rt:u16,
  x:i16, y:i16, rx:i16, ry:i16, z:i16, rz:i16 [,dt:u16]`.
- Each named controller button uses no GET payload; SET uses `state:u8
  [,dt:u16]`.
- Each hat direction uses no GET payload; SET uses `state:u8 [,dt:u16]`.
- Trigger SET uses `value:u16 [,dt:u16]`.
- Stick/aux SET uses `first:i16, second:i16 [,dt:u16]`.
- Each named controller button raw lock SET uses exactly `enabled:u8`.
- Hat/trigger lock SET uses exactly `enabled:u8`.
- Stick/aux lock SET uses four directional `u8` flags in the same order as
  its KM command.

Unlisted opcodes are not part of the public API.

## Constructing MAK_API frames

### COM send

A plaintext COM request is:

```text
DE AD | payload_length:u16le | 00 | 00 opcode verb payload...
```

The first `00` is the generic frame type. The second `00` is `MAK_API_ID`.
`payload_length` counts from `MAK_API_ID` through the final payload byte; it
does not count `DE AD`, the length field, or the generic frame type.

`API_VERSION GET` has no command payload:

```text
DE AD 03 00 00 00 01 00
```

Press controller button 1 without `dt`:

```text
DE AD 04 00 00 00 60 01 01
```

Press controller button 1 with `dt=250` (`FA 00`):

```text
DE AD 06 00 00 00 60 01 01 FA 00
```

### COM receive

A plaintext COM response is:

```text
DE AD | payload_length:u16le | 00 | 00 opcode status result...
```

Successful `API_VERSION GET`:

```text
DE AD 08 00 00 00 01 01 4D 41 4B 58 44
```

Successful controller button 1 SET:

```text
DE AD 03 00 00 00 60 01
```

Successful controller button 1 GET while pressed:

```text
DE AD 04 00 00 00 60 01 01
```

Encrypted COM uses the SDK encryption option; callers still supply the same
opcode, verb, and payload.

### UDP and BLE records

The public identified request record is:

```text
00 opcode verb payload...
```

The matching response record is:

```text
00 opcode status result...
```

For example, controller button 1 SET is sent as:

```text
00 60 01 01
```

and a successful reply is:

```text
00 60 01
```
