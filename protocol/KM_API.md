# KM_API

KM_API is the legacy, lowercase ASCII command interface supported by MAKXD and MAKCU.
It provides mouse, keyboard, and controller injection plus a small
set of status and input-stream controls. New typed integrations should use
[MAK_API](MAK_API.md); KM_API remains available for compatibility and direct
serial use.

This document lists every accepted `km.*` command and the exact argument,
result, timing, and failure rules.

## Command record

A command is one ASCII record with this shape:

```text
km.name(arguments)
```

Command names are exact, lowercase ASCII. Parentheses are required. Do not add
arguments to a query or omit required mutation arguments. Decimal integers may
be signed only where the documented value range is signed. Spaces and tabs are
accepted around numeric and key arguments, but not inside the `km.` prefix or
command name. Trailing CR and LF bytes are removed before parsing.

For direct COM use, terminate each command with `\r\n`:

```text
km.version()\r\n
```

KM records can also be carried as a MAK_API-style record. The first ASCII byte
`k` is the command byte and the rest of the ASCII record is its payload:

```text
DE AD | LEN:u16 | 'k' | ASCII_AFTER_K[LEN]
```

For example, framed `km.version()` is:

```text
DE AD 0B 00 6B 6D 2E 76 65 72 73 69 6F 6E 28 29
```

Plaintext UDP carries the complete framed record. Raw UDP adds its transaction
header. BLE carries the KM record without the `DE AD` length header. Direct
ASCII COM commands are unavailable while COM transport encryption is enabled;
use the encrypted framed carrier instead.

`km.buttons`, `km.keys`, `km.controller`, and `km.stream` work through each
supported command carrier. All use the same framed input-change events.

## Responses

KM has queries, mutations, and errors. There is no standalone success byte or
`OK` line.

A successful query always echoes the command, returns one result line, and
ends with the prompt:

```text
km.left()\r\n
km.left()\r\n1\r\n>>>\x20
```

With echo disabled, which is the default, a successful mutation returns no
bytes:

```text
km.left(1)\r\n
<no response>
```

With `km.echo(1)`, a successful mutation echoes the command and prompt:

```text
km.left(1)\r\n
km.left(1)\r\n>>>\x20
```

An unknown, malformed, out-of-range, unsupported, or busy command
returns an error regardless of the echo setting:

```text
km.left(2)\r\n
km.left(2)\r\nERR\r\n>>>\x20
```

In these escaped transcripts, `\x20` is the prompt's required trailing ASCII
space. The echoed command excludes its incoming CR/LF terminator. A mutation
echo confirms that the device accepted the command; it does not
prove that a physical USB host has already consumed the resulting report.

## Complete command index

The public interface accepts these 34 command names:

| Area | Query | Mutation |
| --- | --- | --- |
| Status | `km.version()`, `km.device()`, `km.echo()` | `km.echo(enabled)` |
| Any input stream | `km.stream(kind)` | `km.stream(kind,enabled)` |
| Mouse stream | `km.buttons()` | `km.buttons(enabled)` |
| Mouse buttons | `km.left()`, `km.right()`, `km.middle()`, `km.side1()`, `km.side2()` | same names with `state` |
| Mouse motion | none | `km.move(x,y)`, `km.wheel(delta)` |
| Mouse masks | none | `km.left_mask(enabled)`, `km.right_mask(enabled)`, `km.middle_mask(enabled)`, `km.side1_mask(enabled)`, `km.side2_mask(enabled)`, `km.move_mask(left,right,down,up)`, `km.wheel_mask(down,up)` |
| Keyboard | `km.isdown(key)`, `km.keys()` | `km.down(key)`, `km.up(key)`, `km.init()`, `km.press(key[,hold_ms[,random_range]])`, `km.string("text")`, `km.multidown(keys...)`, `km.multiup(keys...)`, `km.multipress(keys...)`, `km.mask(key,enabled)`, `km.remap(source,target)`, `km.keys(enabled)` |
| Controller | `km.controller()`, `km.controller_state()` | `km.controller(enabled)`, `km.controller_mask(control,mode)`, `km.controller_state(low,high,lt,rt,lx,ly,rx,ry)` |

## Status and settings

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| GET | `km.version()` | none | literal `km.MAKXD` |
| GET | `km.device()` | none | route and report-period string |
| GET | `km.echo()` | none | `0` or `1` |
| SET | `km.echo(enabled)` | `enabled`: `0` or `1` | none |

`km.version()` identifies the KM-compatible device family. It does not return
the installed application firmware number; use MAK_API `FIRMWARE_VERSION` for
that value.

`km.device()` returns:

```text
R:<routes>;M:<mouse_period>uf;K:<keyboard_period>uf;C:<controller_period>uf
```

`<routes>` is `-` when no output route is active. Otherwise it contains `M`
for mouse, `K` for keyboard, and `C` for controller in that order. Periods are
decimal USB microframes. For example:

```text
R:MK;M:8uf;K:8uf;C:0uf
```

The echo setting is stored persistently. Setting echo to its current value is
valid. `km.echo(0)` can be silent because the new setting applies to that same
response; `km.echo(1)` returns its mutation echo and prompt.

## Timing

Mouse, keyboard, and controller commands do not accept caller-supplied `dt`.
Use the exact argument counts and payload lengths below. A legacy DT argument
or two-byte trailer is rejected, including an explicit zero.

Keyboard press durations (`hold_ms` and `random_range`) remain in milliseconds.
The polling intervals returned by `km.device()` use USB microframes.
Change events contain only kind, control ID, and value.

## Mouse

### Buttons and movement

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| GET | `km.left()` | none | injected state, `0` or `1` |
| GET | `km.right()` | none | injected state, `0` or `1` |
| GET | `km.middle()` | none | injected state, `0` or `1` |
| GET | `km.side1()` | none | injected state, `0` or `1` |
| GET | `km.side2()` | none | injected state, `0` or `1` |
| SET | `km.left(state)` | `state`: `0` or `1` | none |
| SET | `km.right(state)` | `state`: `0` or `1` | none |
| SET | `km.middle(state)` | `state`: `0` or `1` | none |
| SET | `km.side1(state)` | `state`: `0` or `1` | none |
| SET | `km.side2(state)` | `state`: `0` or `1` | none |
| SET | `km.move(x,y)` | `x`, `y`: `-32768..32767` | none |
| SET | `km.wheel(delta)` | `delta`: `-32768..32767` | none |

Button queries report the KM injected state tracked by the device, not the live
physical mouse state. A nonzero button state presses the button; zero releases
it. Movement and wheel values are signed relative deltas.

### Physical-input masks

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| SET | `km.left_mask(enabled)` | `enabled`: `0` or `1` | none |
| SET | `km.right_mask(enabled)` | `enabled`: `0` or `1` | none |
| SET | `km.middle_mask(enabled)` | `enabled`: `0` or `1` | none |
| SET | `km.side1_mask(enabled)` | `enabled`: `0` or `1` | none |
| SET | `km.side2_mask(enabled)` | `enabled`: `0` or `1` | none |
| SET | `km.move_mask(left,right,down,up)` | four values, each `0` or `1` | none |
| SET | `km.wheel_mask(down,up)` | two values, each `0` or `1` | none |

For a named button, `1` blocks that physical button and `0` allows it.
Directional values independently block negative X (`left`), positive X
(`right`), positive Y (`down`), negative Y (`up`), negative wheel (`down`),
or positive wheel (`up`). A mask affects physical input only; values injected
through KM_API or MAK_API bypass it.

### Mouse event stream

`km.buttons(1)` enables physical mouse-button changes. `km.buttons(0)` disables
them. `km.buttons()` returns `0` or `1`. This is the mouse alias for
`km.stream(mouse,enabled)`; keyboard and controller subscriptions are independent.
See [Input change streams](#input-change-streams) for the common event format.

## Keyboard

### Key arguments

A `key` is either an unquoted decimal USB HID keyboard usage in `0..255` or a
single-quoted name. Names are case-insensitive. These compact forms are also
accepted:

- `'a'..'z'`, `'0'..'9'`
- `'f1'..'f12'`
- `'kp0'..'kp9'` and `'np0'..'np9'`

All other accepted names and aliases are:

| Usage | Names |
| ---: | --- |
| 40 | `enter`, `return` |
| 41 | `escape`, `esc` |
| 42 | `backspace`, `back` |
| 43 | `tab` |
| 44 | `space`, `spacebar` |
| 45 | `minus`, `dash`, `hyphen` |
| 46 | `equals`, `equal` |
| 47 | `leftbracket`, `lbracket`, `openbracket` |
| 48 | `rightbracket`, `rbracket`, `closebracket` |
| 49 | `backslash`, `bslash` |
| 50 | `nonus_hash` |
| 51 | `semicolon`, `semi` |
| 52 | `quote`, `apostrophe`, `singlequote` |
| 53 | `grave`, `backtick`, `tilde` |
| 54 | `comma` |
| 55 | `period`, `dot` |
| 56 | `slash`, `forwardslash`, `fslash` |
| 57 | `capslock`, `caps` |
| 70 | `printscreen`, `prtsc`, `print` |
| 71 | `scrolllock`, `scroll` |
| 72 | `pause`, `break` |
| 73 | `insert`, `ins` |
| 74 | `home` |
| 75 | `pageup`, `pgup` |
| 76 | `delete`, `del` |
| 77 | `end` |
| 78 | `pagedown`, `pgdown`, `pgdn` |
| 79 | `right`, `rightarrow` |
| 80 | `left`, `leftarrow` |
| 81 | `down`, `downarrow` |
| 82 | `up`, `uparrow` |
| 83 | `numlock`, `num` |
| 84 | `kpdivide`, `npdivide` |
| 85 | `kpmultiply`, `npmultiply` |
| 86 | `kpminus`, `npminus` |
| 87 | `kpplus`, `npplus` |
| 88 | `kpenter`, `npenter` |
| 99 | `kpperiod`, `kpdot`, `npperiod`, `npdot` |
| 224 | `leftctrl`, `lctrl`, `leftcontrol`, `lcontrol`, `ctrl`, `control` |
| 225 | `leftshift`, `lshift`, `shift` |
| 226 | `leftalt`, `lalt`, `alt` |
| 227 | `leftgui`, `lgui`, `leftwin`, `lwin`, `gui`, `win`, `windows`, `super`, `meta`, `cmd`, `command` |
| 228 | `rightctrl`, `rctrl`, `rightcontrol`, `rcontrol` |
| 229 | `rightshift`, `rshift` |
| 230 | `rightalt`, `ralt` |
| 231 | `rightgui`, `rgui`, `rightwin`, `rwin`, `rightwindows` |

Inside a quoted key name, `\\`, `\'`, `\n`, `\r`, `\t`, and `\xNN` escapes
are parsed before name lookup. Numeric usages are the unambiguous form for any
key not in the name table.

### Key commands

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| SET | `km.down(key)` | key | none |
| SET | `km.up(key)` | key | none |
| SET | `km.init()` | none | none |
| SET | `km.press(key[,hold_ms[,random_range]])` | key; optional unsigned millisecond values | none |
| SET | `km.string("text")` | double-quoted ASCII, `0..256` bytes | none |
| GET | `km.isdown(key)` | key | physical state, `0` or `1` |
| SET | `km.multidown(key1,key2,...)` | `1..14` keys | none |
| SET | `km.multiup(key1,key2,...)` | `1..14` keys | none |
| SET | `km.multipress(key1,key2,...)` | `1..14` keys | none |
| SET | `km.mask(key,enabled)` | key; `enabled`: `0` or `1` | none |
| SET | `km.remap(source,target)` | two keys | none |

`km.down` and `km.up` modify the device's current injected keyboard state.
Modifier usages `224..231` update modifier bits; other nonzero usages occupy
the keyboard key list. `km.init()` releases every injected key, cancels an
active press or string action, and clears keyboard masks and remaps.

`km.press` presses one key, then releases it. `hold_ms` defaults to 10.
`random_range` defaults to 0; when nonzero, the device adds a pseudorandom value
from `0..random_range` milliseconds, saturating at the unsigned 32-bit maximum.
Only one timed press or string action can be active at a time.

`km.multipress` uses a 10 ms hold and no random range. Multi-key commands
accept at most 14 arguments, but the resulting non-modifier state must also fit
the routed keyboard report. Duplicate keys do not consume additional slots.

`km.string` uses a US-keyboard ASCII mapping and accepts standard ASCII bytes
only. String values support `\\`, `\"`, `\n`, `\r`, `\t`, and `\xNN` escapes.
Each character is scheduled as a press/release action; another press, string,
down, up, multidown, or multiup request can return `ERR` while that action is
active.

`km.isdown` reads the last valid physical keyboard state. It does not report
the KM injected state. `km.mask` and `km.remap` also affect physical keyboard
input only; injected values bypass both policies. A remap replaces `source`
with `target` in the physical input path.

### Keyboard event stream

`km.keys(1)` enables physical keyboard changes. `km.keys(0)` disables them.
`km.keys()` returns `0` or `1`. This is the keyboard alias for
`km.stream(keyboard,enabled)`.

## Input change streams

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| GET | `km.stream(kind)` | `mouse`, `keyboard`, or `controller` | `0` or `1` |
| SET | `km.stream(kind,enabled)` | kind; `enabled`: `0` or `1` | none |
| GET | `km.controller()` | none | `0` or `1` |
| SET | `km.controller(enabled)` | `enabled`: `0` or `1` | none |

Use unquoted, lowercase kind names. `km.controller` is only the controller
stream switch/query. Named-control arguments are rejected.

Kinds are mouse `1`, keyboard `2`, controller `3`. Each subscription is
independent. Events contain only changed buttons/keys and controller triggers;
no mouse motion, wheel, or controller stick-axis events are emitted.

```text
DE AD 03 00 53 kind id state:u8       # digital state 0/1
DE AD 04 00 53 03   id trigger:u16le  # trigger IDs 10/11, 0..1023
DE AD 03 00 53 kind FF FF            # overflow disables this kind
```

Mouse IDs are button positions `0..31`; keyboard IDs are HID usages `0..255`.
Controller IDs use the table below, excluding stick axes `12..15`. Trigger
values are normalized to `0..1023`. Events are binary framed records even when
the subscription was enabled through ASCII KM. They have no prompt or CR/LF.

For enable baselines, detach, overflow recovery, destination ownership, and
transport parsing, see the complete [MAK_API streaming contract](MAK_API.md#input-change-streams).

## Controller

Controller injection and masks require an active compatible controller route. A control
must be supported by that routed controller.
Names describe physical position rather than product artwork and are exact,
lowercase ASCII.

| ID | Control name | Value |
| ---: | --- | --- |
| 0 | `south` | `0` or `1` |
| 1 | `east` | `0` or `1` |
| 2 | `west` | `0` or `1` |
| 3 | `north` | `0` or `1` |
| 4 | `dpad_up` | `0` or `1` |
| 5 | `dpad_down` | `0` or `1` |
| 6 | `dpad_left` | `0` or `1` |
| 7 | `dpad_right` | `0` or `1` |
| 8 | `left_shoulder` | `0` or `1` |
| 9 | `right_shoulder` | `0` or `1` |
| 10 | `left_trigger` | `0..1023` |
| 11 | `right_trigger` | `0..1023` |
| 12 | `left_stick_x` | `-32768..32767` |
| 13 | `left_stick_y` | `-32768..32767` |
| 14 | `right_stick_x` | `-32768..32767` |
| 15 | `right_stick_y` | `-32768..32767` |
| 16 | `left_stick_button` | `0` or `1` |
| 17 | `right_stick_button` | `0` or `1` |
| 18 | `select` | `0` or `1` |
| 19 | `start` | `0` or `1` |
| 20 | `mode` | `0` or `1` |
| 21 | `grip_left` | `0` or `1` |
| 22 | `grip_right` | `0` or `1` |
| 23..54 | `extra_1`..`extra_32` | `0` or `1` |

Not every controller family supports every semantic control. Unsupported
mask or state mutations return `ERR`.

### Physical-input masks

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| SET | `km.controller_mask(control,mode)` | supported control name and mask mode | none |

For MAKCU zero-state handoff firmware, follow the
[completion/cancel contract](MAK_API.md#makcu-controller-handoff): send a final
zero stick pair / trigger when done, preserving other active controls.

Use `km.controller_state` for controller injection. The former
`km.controller(control[,value])` overload is removed.

Mask modes are:

| Mode | Name | Valid for |
| ---: | --- | --- |
| 0 | disabled | every control |
| 1 | complete | digital buttons, D-pad, triggers |
| 2 | negative | stick axes only |
| 3 | positive | stick axes only |
| 4 | both | stick axes only |

Controller masks affect physical input only. Injected controller values bypass
them.

### Complete state

The complete state is:

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

Digital bit N is control ID N. `digital_low` contains IDs 0..31 and
`digital_high` contains IDs 32..54 at bit positions 0..22.

| Operation | Command | Arguments | Returned data |
| --- | --- | --- | --- |
| GET | `km.controller_state()` | none | `low,high,lt,rt,lx,ly,rx,ry` |
| SET | `km.controller_state(low,high,lt,rt,lx,ly,rx,ry)` | all eight decimal values are required | none |

The two digital words are unsigned decimal values. Trigger and stick ranges
match the control table. A complete-state mutation takes exactly eight values.

The trigger contract is canonical 10-bit for both injection and input
streaming. MAKXD maps `0..1023` to and from the selected controller's native
trigger width. Stick axes remain signed 16-bit.

MAKXD rejects contradictory D-pad pairs (`up` with `down`, or `left`
with `right`), set bits for unsupported digital controls, and nonzero analog
values for unsupported controls.

## Command rejection

A syntactically valid command can still return `ERR` when the device is busy,
a required controller route is unavailable, a timed keyboard action is already
active, or the requested value is unsupported by the routed device.

Treat `ERR` as rejection of that command. Treat a silent mutation as accepted
only when echo is disabled by design. For applications that require a normal
request/response contract across COM, UDP, BLE, and the SDKs, use MAK_API.

## Examples

Read the route:

```text
request:  km.device()\r\n
response: km.device()\r\nR:MK;M:8uf;K:8uf;C:0uf\r\n>>>\x20
```

Move left 120 counts:

```text
km.move(-120,0)\r\n
```

Press Enter for the default 10 ms:

```text
km.press('enter')\r\n
```

Type a line containing a quoted word:

```text
km.string("say \"hello\"\n")\r\n
```

Enable controller buttons and triggers, then query the switch:

```text
request:  km.controller(1)\r\n
response with echo disabled: <no response>
request:  km.controller()\r\n
response: km.controller()\r\n1\r\n>>>\x20
```

Enable keyboard changes alongside controller changes:

```text
km.stream(keyboard,1)\r\n
```

Set a complete neutral controller state immediately:

```text
km.controller_state(0,0,0,0,0,0,0,0)\r\n
```
