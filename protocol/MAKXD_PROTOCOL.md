# MAKXD protocol

This is the shared device-wire contract for the MAKXD firmware and the C++,
Rust, Python, and C# APIs.

The ASCII command inventory below follows the registered command aliases in
the command parser. A handler prototype that is not registered as an alias is
not an active public command. Language bindings must use these exact wire
names and argument forms.

## Framed input streaming

The production stream uses one small frame for commands, acknowledgements, and
reports. The command byte is outside the payload length. The payload plus that
command byte is limited by the 256-byte UART DMA cell:

```text
bytes 0..1   DE AD
bytes 2..3   payload length, little-endian, 1..251
byte 4       command: 0x00 generic command, 0x01 input stream
bytes 5..N   payload
```

The host requests any combination of sources with a two-byte payload:

```text
byte 0       operation: 1 START, 2 STOP, 3 STATUS
byte 1       source mask: bit 0 mouse, bit 1 keyboard, bit 2 controller
```

The acknowledgement payload is three bytes:

```text
byte 0       operation echoed by the device
byte 1       status: 0 success, non-zero failure
byte 2       active source mask
```

Every source uses the same report payload. The source kind is present on each
report so simultaneous streams remain self-describing:

```text
byte 0       source kind: 1 mouse, 2 keyboard, 3 controller
bytes 1..2   raw timing word, little-endian
bytes 3..6   report sequence, little-endian
bytes 7..N   complete source-kind report payload
```

The source-kind payload is the complete semantic report for that kind: mouse
values, keyboard state, or controller state. It is not split into separate
metadata, type, or packed-mouse frames. The APIs preserve the source kind,
sequence, complete values, and raw timing word.

Timing is always present. The timing word is:

```text
bits 0..13   dt_uframes
bit 14       timing baseline
bit 15       timing invalid
```

An invalid timing record carries the reset/epoch boundary. Consumers keep the
record and use `invalid` rather than inventing a wall-clock delta. APIs preserve
the raw timing word, decoded `dt_uframes`, baseline flag, invalid flag, sequence,
source kind, and complete values without quantizing or discarding `dt`.

## MAKXD NET Bridge protocol

This section defines the UDP contract between a KM NET caller and
`kmNetMakxdBridge.exe`. The caller does not connect to the MAKXD COM port.
The Bridge must already be running, own the MAKXD COM connection, translate
the supported UDP commands, and return their acknowledgements.

The lightweight client in [`net/cpp`](../net/cpp/README.md) implements only the
commands listed below. Other KM NET functions are not enabled and are omitted
instead of returning a false success.

### UDP header and acknowledgement

All integers are little-endian. Every request begins with this 16-byte header:

| Offset | Size | Field | Meaning |
| ---: | ---: | --- | --- |
| 0 | 4 | `mac` | value parsed from the caller's eight-hex-character MAC/UUID string |
| 4 | 4 | `rand` | random value or command-specific argument |
| 8 | 4 | `indexpts` | request sequence counter |
| 12 | 4 | `cmd` | command identifier |

There is no version, length, checksum, or separate status field. A successful
acknowledgement is a 16-byte header that echoes all four request fields. A
failure acknowledgement echoes `mac`, `rand`, and `indexpts`, but sets `cmd`
to zero. Requests shorter than 16 bytes cannot be correlated and are dropped.

### Enabled commands

| Command | ID | Exact request size | Bridge action |
| --- | ---: | ---: | --- |
| connect | `0xAF3C2828` | 16 | establish or reset the caller session if MAKXD is connected |
| mouse move | `0xAEDE7345` | 72 | reconcile buttons, then send relative movement |
| mouse left | `0x9823AE8D` | 72 | reconcile the complete mouse-button snapshot |
| mouse middle | `0x97A3AE8D` | 72 | reconcile the complete mouse-button snapshot |
| mouse right | `0x238D8212` | 72 | reconcile the complete mouse-button snapshot |
| mouse wheel/all | `0xFFEEAD38` | 72 | reconcile buttons and send any non-zero movement or wheel values |
| keyboard all | `0x123C2C2F` | 28 | diff and apply the complete keyboard snapshot |
| physical monitor | `0x27388020` | 16 | start or stop combined mouse and keyboard monitoring |
| reboot | `0xAA8855AA` | 16 | acknowledge and reset the Bridge caller session |

The Bridge returns failure for a recognized command with the wrong datagram
size or invalid values. It does not treat an unlisted command as successful.

### Mouse payload

Mouse commands append this 56-byte payload to the header:

| Payload offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | button bitmask |
| 4 | 4 | signed relative X |
| 8 | 4 | signed relative Y |
| 12 | 4 | signed wheel delta |
| 16 | 40 | reserved; ignored by the Bridge and zeroed by the lightweight client |

Button bits 0 through 4 are left, right, middle, side 1, and side 2. Although
the UDP fields are signed 32-bit values, MAKXD accepts X, Y, and wheel values
only in the signed 16-bit range. The Bridge rejects values outside that range.

`mouse wheel/all` is shared by `kmNet_mouse_wheel()` and
`kmNet_mouse_all()`. The Bridge therefore processes the complete packet:
button transitions are reconciled, non-zero X/Y is moved, and a non-zero
wheel value is sent.

### Keyboard payload

The keyboard command appends this 12-byte complete-state snapshot:

| Payload offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | modifier bitmap for HID usages `0xE0` through `0xE7` |
| 1 | 1 | reserved, zero |
| 2 | 10 | currently held non-modifier HID usages, zero-padded |

The Bridge diffs each snapshot against its caller-session state, releases
removed usages, and presses added usages. Zero entries are empty.

### Combined monitor datagram

`kmNet_monitor(port)` sends a header-only monitor request. Its `rand` field is
`0xAA55PPPP` to enable monitoring, where `PPPP` is the unsigned 16-bit local
UDP monitor port, or zero to disable it. The destination is the request source
IPv4 address plus `PPPP`, not the caller's command-source port.

The Bridge enables MAKXD's production mouse and keyboard stream together and
translates each event into this exact 20-byte little-endian datagram:

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | mouse report ID, zero |
| 1 | 1 | latest mouse button bitmap |
| 2 | 2 | current event's signed relative X, or zero for a keyboard event |
| 4 | 2 | current event's signed relative Y, or zero for a keyboard event |
| 6 | 2 | current event's signed wheel, or zero for a keyboard event |
| 8 | 1 | keyboard report ID, zero |
| 9 | 1 | latest keyboard modifier bitmap |
| 10 | 10 | latest keyboard HID usages, zero-padded |

Mouse deltas are not retained. Mouse buttons and keyboard fields are retained
until the next event of their type. The Bridge's internal MAKXD stream framing
is not exposed to the NET caller.

### Reboot compatibility

The reboot command does not reboot MAKXD and does not terminate the Bridge.
The Bridge sends the normal header-only success acknowledgement, stops
monitoring, releases the caller session's held inputs, and clears that session.
The lightweight client closes its local socket after the acknowledgement, so
the caller must run `kmNet_init()` again before sending another command.

## ASCII command framing

ASCII commands are sent inside the `KM_COMMAND` carrier and use the `km.`
namespace. Commands are terminated by the existing command-line terminator.
The parser accepts loose separators where stated by the command handler, but
the canonical forms below use commas and parentheses.

The other command-carrier values are:

```text
0xA1  HELLO
0x2E  KM_COMMAND
0xA4  GET_BAUD_RATE
0xA5  SET_BAUD_RATE
```

`SET_BAUD_RATE` carries one little-endian unsigned 32-bit baud value after the
carrier byte. `GET_BAUD_RATE` reads the stored value. These binary commands are
separate from the ASCII `km.baud(...)` command.

### ASCII response contract

Accepted `SET` and `EXEC` commands echo the accepted command and prompt only;
they do not emit a result line. `GET` queries echo the query, emit the result
line, and then emit the prompt:

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

## Mouse commands

### Button state and button actions

```text
km.left()       GET;   km.left(value)       SET
km.right()      GET;   km.right(value)      SET
km.middle()     GET;   km.middle(value)     SET
km.side1()      GET;   km.side1(value)      SET
km.side2()      GET;   km.side2(value)      SET

km.click(button,count)
km.click(button,count,delay_ms)
```

The no-argument button form queries state. The value form sends the button
state (`0` released, `1` pressed). `click` uses a one-based button number,
click count, and optional delay.

### Relative and absolute movement

```text
km.move(x,y)
km.move(x,y,segments)
km.move(x,y,segments,cx1,cy1)
km.move(x,y,segments,cx1,cy1,cx2,cy2)

km.moveto(x,y)
km.moveto(x,y,segments)
km.moveto(x,y,segments,cx1,cy1)
km.moveto(x,y,segments,cx1,cy1,cx2,cy2)
km.getpos()

km.wheel(delta)
```

Movement values are signed. `move` is relative; `moveto` is absolute and is
clamped to the configured screen. A single control pair is duplicated as both
control points. `moveto(0,0)` with no segment or control arguments is the
calibration-to-zero form.

### Screen and stream configuration

```text
km.screen()
km.screen(width,height)

km.axis()
km.axis(mode)
km.axis(mode,period_ms)

km.mouse()
km.mouse(mode)
km.mouse(mode,period_ms)

km.buttons()
km.buttons(mode)
km.buttons(mode,period_ms)
```

`axis` modes are `0`, `abs`, `rel`, and `act`. `mouse` modes are `0`, `raw`,
and `mut`. `buttons` modes are `0`, `raw`, and `mut`. The no-argument forms
return the current mode and period.

### Axis and button locks

```text
km.lock_mx()     GET; km.lock_mx(value)     SET
km.lock_my()     GET; km.lock_my(value)     SET
km.lock_mx+()    km.lock_mx+(value)
km.lock_mx-()    km.lock_mx-(value)
km.lock_my+()    km.lock_my+(value)
km.lock_my-()    km.lock_my-(value)

km.lock_ml()     GET; km.lock_ml(value)    SET
km.lock_mr()     GET; km.lock_mr(value)    SET
km.lock_mm()     GET; km.lock_mm(value)    SET
km.lock_ms1()    GET; km.lock_ms1(value)   SET
km.lock_ms2()    GET; km.lock_ms2(value)   SET
```

No-argument forms query state. Value `1` locks and value `0` unlocks. The
directional axis forms lock or query only the positive or negative direction.

### Button catch and device settings

```text
km.catch_ml()    km.catch_ml(value)
km.catch_mr()    km.catch_mr(value)
km.catch_mm()    km.catch_mm(value)
km.catch_ms1()   km.catch_ms1(value)
km.catch_ms2()   km.catch_ms2(value)

km.version()
km.baud()
km.baud(rate)
km.serial()
km.serial('serial')
km.serial(0)

km.silent(x,y)
km.echo()
km.echo(value)
```

The no-argument catch forms query state. `km.baud()` queries the current
speed. The parser accepts `115200` and `4000000` as the supported baud values;
other values resolve to `115200`. Serial values are limited to 20 characters.
`echo` queries or sets command acknowledgements with `0` or `1`. `silent`
performs the parser's silent movement/click operation with an `x,y` pair.

## Keyboard commands

```text
km.down(key)
km.up(key)
km.press(key)
km.press(key,hold_ms)
km.press(key,hold_ms,random_range)
km.string("text")
km.isdown(key)

km.multidown(key1,key2,...)
km.multiup(key1,key2,...)
km.multipress(key1,key2,...)

km.mask(key,mode)
km.remap(source,target)
km.init()
km.keys()
km.keys(value)
```

Keys accept unsigned HID codes (`0` through `255`) or recognized names. Named
keys are single-quoted and escaped. Keyboard strings are double-quoted,
ASCII-only, and limited to 256 bytes. `mode` and `value` use `0` to disable
and `1` to enable. `km.keys()` queries the keyboard callback state.

### Key names

Single-character letters are case-sensitive. Multi-character names and aliases
are case-insensitive. Generic modifier names resolve to the left-side variant.

| HID range | Names |
| --- | --- |
| 4-29 | `a` through `z` |
| 30-39 | `1` through `0` |
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
| 58-69 | `f1` through `f12` |
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
| 89-98 | `kp1` through `kp0`, with `np1` through `np0` aliases |
| 99 | `kpperiod`, `kpdot`, `npperiod`, `npdot` |
| 224 | `leftctrl`, `lctrl`, `leftcontrol`, `lcontrol`, `ctrl`, `control` |
| 225 | `leftshift`, `lshift`, `shift` |
| 226 | `leftalt`, `lalt`, `alt` |
| 227 | `leftgui`, `lgui`, `leftwin`, `lwin`, `gui`, `win`, `windows`, `super`, `meta`, `cmd`, `command` |
| 228 | `rightctrl`, `rctrl`, `rightcontrol`, `rcontrol` |
| 229 | `rightshift`, `rshift` |
| 230 | `rightalt`, `ralt` |
| 231 | `rightgui`, `rgui`, `rightwin`, `rwin`, `rightwindows` |

## API alignment gates

Before adding or changing language bindings, reconcile these source-level
differences explicitly:

- The parser registers `side1` and `side2`; bindings must not silently replace
  them with `ms1` and `ms2`.
- The parser registers `multidown`, `multiup`, `multipress`, `keys`, `silent`,
  `moveto`, `getpos`, `screen`, `axis`, `mouse`, `echo`, and `baud`.
- `km.disable(...)` is not registered in this command table. A binding must not
  advertise it as supported until a parser alias is intentionally added.
- Keyboard catch handler symbols exist, but no `catch_kb` alias is registered;
  it is not part of the active ASCII contract.
