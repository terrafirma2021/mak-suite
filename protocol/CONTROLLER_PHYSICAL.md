# Physical controller snapshot

Thanks to evol for reporting the controller-state bug: reading the software-commanded
state did not reveal the user's physical joystick position. The separate physical
query below addresses that report while preserving existing callers of `0x40`.

Request the latest physical controller input with:

```c
{ 0xDE, 0xAD, 0x00, 0x00, 0x54 }
```

Successful UART reply: `DE AD 20 00 54`, followed by 32 payload bytes.
All multibyte values are little-endian. Payload offsets:

| Offset | Type | Value |
| --- | --- | --- |
| 0 | u32 | Digital controls, IDs 0..31 |
| 4 | u32 | Digital controls, IDs 32..54 |
| 8 | u16 | Left trigger, 0..1023 |
| 10 | u16 | Right trigger, 0..1023 |
| 12 | i16 | Left stick X, -32768..32767 |
| 14 | i16 | Left stick Y, -32768..32767 |
| 16 | i16 | Right stick X, -32768..32767 |
| 18 | i16 | Right stick Y, -32768..32767 |
| 20 | u32 | Snapshot sequence |
| 24 | u32 | Latest report's USB timestamp, in 125-microsecond units |
| 28 | u16 | Report timing: DT in bits 0..13, baseline in bit 14, invalid in bit 15 |
| 30 | u16 | Configured report cadence, in 125-microsecond units |

`dt_uframes = timing & 0x3FFF`; multiply by 0.125 for milliseconds.
For example, DT 8 means 1 ms. Baseline (`0x4000`) marks a timing restart;
invalid (`0x8000`) means there is no usable report interval. Do not integrate
movement using baseline/invalid timing. DT describes the most recent report,
not elapsed time since your previous query; repeated polls repeat it, and
polling may skip intermediate reports. Cadence is the configured interval,
not the measured DT. Use sequence changes to recognize new publications.

The first 20 bytes use the same control layout as `0x40`. Digital IDs match
[the controller controls](MAK_API.md); D-pad up/down/left/right are bits
4/5/6/7. Bits 10..15 are zero because triggers and sticks have separate fields.
Unsupported controls are zero. Native device ranges are normalized with rounding;
an unsigned 8-bit stick value of 128 maps to +128, not exactly zero. Axis direction
follows the existing controller-state API's native-device convention.

This returns input reported by the controller before firmware masks, remapping,
curves, interpolation, and injection. It cannot bypass processing inside the
controller itself. It does not request a new USB report or consume the snapshot.
Repeated polls can return the same sequence and timestamp. Idle input retains the
last values; sequence can also change for firmware timing notifications. Both
counters can wrap, and reconnection can reset them.

If no parsed sample exists, the controller is detached, the route is changing,
or a concurrent publication prevents a coherent read, the response is
`DE AD 01 00 54 FF`. Retry on a later poll; do not interpret it as centered sticks.
The query accepts no payload and does not change injection or stream settings.

Text equivalent: `km.controller_physical()` followed by CR/LF. Direct and framed
KM return twelve comma-separated decimal values in the table's order, or `ERR` when
unavailable. `km.controller_state()` / `0x40` retains its software-commanded state.

Python payload decoding (after validating the frame opcode and length):

```python
digital0, digital1, lt, rt, lx, ly, rx, ry, sequence, usb_time, timing, cadence = struct.unpack(
    '<IIHHhhhhIIHH', payload
)
dt_ms = (timing & 0x3FFF) * 0.125
```

This command requires firmware built with this change; it is not available on
older flashed images. It returns all supported standard controller controls,
not opaque device-specific report bytes such as IMU or touchpad data.

## Platform timing and axes

MAKXD has measured DT at 125-microsecond resolution. MAKCU derives DT from USB
time at 1-millisecond resolution, encoded in the same 125-microsecond units
(1 ms = 8). Neither value is measured from API polling or synthetic output.

MAKXD currently returns zero for the absolute USB timestamp because its physical
queue carries DT rather than that clock. Its explicit timing field is valid
independently of that timestamp. For generic HID, MAKXD preserves the parsed
native stick values, matching its existing controller-state API; MAKCU normalizes
from descriptor ranges. Sony and Xbox stick encodings use the canonical signed
16-bit range on both products.

## SDK readers

- Python: Gamepad.physical_state()
- Rust: Device.controller_physical() and AsyncDevice.controller_physical()
- C++: Device::controllerPhysical()
- C: makxd_controller_physical_get()
- C#: Mouse.device.controller_physical()

Each returns a snapshot with a nested controller state, sequence, USB timestamp,
timing, and configured cadence. Unavailable/malformed replies produce the SDK's
normal error or empty-result path; they are never converted into a centered state.
