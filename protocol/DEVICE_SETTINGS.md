# Live device settings

The MCU owns the active settings. Both the WebUI sliders and the typed SDKs
change that same live state. No reboot is needed for processing changes.

| Action | Result |
| --- | --- |
| Read | Read the MCU's current live values and revision. |
| Apply | Validate and apply selected settings immediately. Does not persist settings. |
| Save | Persist the selected current live settings to NOR; wait for completion. |
| Export | Read current MCU values and create an encrypted `.makxd-settings` file. Does not save settings to the MCU. |
| Import | Authenticate and validate the complete file, then apply it live. Save separately to survive power-off. |

Export returns file bytes. The application writes those bytes to a file. A file
can therefore preserve a tuning session even when its settings were never saved
to the MCU. Export's cryptographic nonce allocator may reserve counters in NOR;
that does not persist the live tuning configuration.

Always start with Read and retain the snapshot returned by Apply or Import.
Save and Export reject an outdated revision. Editing the host snapshot alone
does not change the MCU: Apply it first. Export reads the MCU and never exports
unsent changes in a host object. An external edit or a changed device route
invalidates an old snapshot; read again, show the new values, and let the caller
decide which changes to retain. Do not blindly retry an outdated snapshot.

## Persistent controller presets

Use [controller presets](CONTROLLER_PRESETS.md) for hash-addressed controller
configurations and startup selection. These persist separately from the legacy
global Save and encrypted settings-file operations described here. An enabled
controller preset is restored at startup after global settings. WebUI controller
saves use named presets. Mouse spread continues to use global settings.

## Capabilities and parameters

`sections` reports the settings available on the connected unit. `kinds` reports
its physical local kinds: mouse `1`, keyboard `2`, controller `4`. A missing kind
does not acquire controller processing merely by requesting it. For paired
units, connect the SDK to the unit that owns the physical device being tuned.

| Section | Mask | Fields |
| --- | ---: | --- |
| Controller processing | `1` | Interpolation; buffer; timing variance; four independent channel behaviors and curves. |
| Mouse/keyboard translation | `2` | Enable, scale and input timeout for four translation channels. Requires a controller. |
| Mouse processing | `4` | Mouse spread percentage. |

Controller behavior order is **right stick, left stick, left trigger, right
trigger**. Use the SDK's `behavior(channel)` / `controllerBehavior` helper to
promote older policies to independent channels before changing one.

| Parameter | Allowed values |
| --- | --- |
| Interpolation | Off, Fixed, Auto |
| Buffer | 1–64 ms |
| Timing variance | 0–100% |
| Strength, inertia, micro, limit | 0–100% |
| Magnitude variance, angle variance | 0–100% |
| Curve enabled, advanced enabled | Boolean |
| Center deadzone, anti-deadzone, change deadband | 0–50% |
| Curve | Five `(input%, output%)` points, beginning `(0,0)` and ending `(100,100)`; input strictly increases, output never decreases. |
| Behavior name | 1–24 printable ASCII characters |
| Mouse spread | 0–100% |

Each behavior retains four curves. Edit the curve corresponding to that
behavior's channel. Translation order is **left stick/WASD, right stick/mouse,
left trigger, right trigger**; it differs from controller behavior order.
Translation scale is 1–512 for sticks and 0–100% for triggers. Timeout is
1–1000 ms. Enable and timeout changes are live; new input uses the new values.

Keyboard masks/remaps, mouse locks, controller masks/remaps, identity overrides,
USB descriptor interval, LEDs, network credentials and activation are separate
API operations and are not included in this tuning preset. USB identity and
descriptor interval changes require USB re-enumeration; do not present them as
ordinary processing sliders.

## Public SDK calls

| Language | Access and operations |
| --- | --- |
| Python | `device.settings.info/read/apply/save/export_preset/import_preset` |
| C++ | `device.deviceSettingsInfo/readDeviceSettings/applyDeviceSettings/saveDeviceSettings/exportDeviceSettings/importDeviceSettings` |
| C | `makxd_settings_info/read/apply/save/export/import`; `makxd_settings_controller_behavior` |
| Rust | `device.settings().info/read/apply/save/export_preset/import_preset`; async device provides the same operations with `.await` |
| C# | `Mouse.device.settings().Info/Read/Apply/Save/ExportPreset/ImportPreset` |

In C/C++/Rust a section mask of `0` selects all supported sections. Python and
C# omit the optional mask for that behavior. Prefer explicit masks when editing
one section. Every request validates the full typed candidate locally; firmware
also validates all selected sections before publishing any of them.

Python:

```python
from pathlib import Path
from makxd import ControllerChannel, SettingsSection

settings = device.settings
live = settings.read()
live.settings.controller.buffer_ms = 12
live.settings.controller.behavior(ControllerChannel.RIGHT_STICK).strength_percent = 65
live = settings.apply(live, SettingsSection.CONTROLLER)  # output changes now
Path("aim.makxd-settings").write_bytes(settings.export_preset(live))
# Optional explicit persistence:
settings.save(live, SettingsSection.CONTROLLER)
# Later, or in another application using any supported SDK:
live = settings.import_preset(Path("aim.makxd-settings").read_bytes())
```

C++:

```cpp
auto live = device.readDeviceSettings();
live.settings.controller.buffer_ms = 12;
makxd::Device::controllerBehavior(live.settings, MAKXD_TUNE_RIGHT_STICK).strength_percent = 65;
live = device.applyDeviceSettings(live, MAKXD_SETTINGS_CONTROLLER);
auto file = device.exportDeviceSettings(live); // write these bytes to a file
device.saveDeviceSettings(live, MAKXD_SETTINGS_CONTROLLER); // optional NOR save
```

C:

```c
makxd_settings_snapshot_t live;
if (makxd_settings_read(device, &live) != 0) return;
live.settings.controller.buffer_ms = 12;
if (makxd_settings_apply(device, &live, MAKXD_SETTINGS_CONTROLLER, &live) != 0) return;
uint8_t file[MAKXD_SETTINGS_PRESET_MAX_BYTES]; size_t written = 0;
if (makxd_settings_export(device, &live, 0, file, sizeof file, &written) != 0) return;
/* Write file[0..written) to disk. Save only when requested by the user. */
if (makxd_settings_save(device, &live, MAKXD_SETTINGS_CONTROLLER) != 0) return;
```

Rust (synchronous; async uses the same operations and owned snapshot/file):

```rust
use makxd::settings::{ControllerChannel, CONTROLLER};
let settings = device.settings();
let mut live = settings.read()?;
live.settings.controller.behavior(ControllerChannel::RightStick)?.strength_percent = Some(65);
live = settings.apply(&live, CONTROLLER)?;
let file = settings.export_preset(&live, 0)?;
settings.save(&live, CONTROLLER)?; // optional
```

C#:

```csharp
var settings = Mouse.device.settings();
var live = settings.Read();
live.Settings.Controller.Behavior(Makxd.ControllerChannel.RightStick).StrengthPercent = 65;
live = settings.Apply(live, Makxd.SettingsSection.Controller);
System.IO.File.WriteAllBytes("aim.makxd-settings", settings.ExportPreset(live));
settings.Save(live); // optional NOR persistence
```

Include `makxd_settings.cs` alongside `mouse.cs` and `makxd_stream.cs`.

## Slider and movement lifecycle

A slider handler changes the current snapshot and calls Apply. Permit one
configuration transaction at a time; while it is in flight retain the newest
desired slider value, then apply that value after the reply. Do not queue every
intermediate drag value, debounce until pointer-up, automatically Save, or reboot.
Save/Export must wait for the final slider Apply to finish.

Tuning does not finish an injected movement. Send stick `(0,0)` when movement
finishes **or is cancelled**, and trigger `0` when a trigger finishes. Release
starts the firmware's configured handoff to current physical input. Continue
to send updated movement while active, and put the release in cancellation/error
cleanup. The tuning API does not run a host timer or guess movement duration.

## Wire contract

These management operations are an explicit exception to silent ordinary SETs.
They use `CONNECTION` opcode `0x3E` with payload record `0x1D`, and return the same
opcode with `[record, operation, status, ...]`. Status is `0` success, `1` saving,
`2` busy, `3` stale, `4` invalid, `5` unsupported, `6` storage error. C adds `255`
for a host/transport failure. Do not interpret `1` as completed persistence.

| Operation | Request after `[1D, op]` | Reply after `[1D, op, status]` |
| --- | --- | --- |
| INFO `0` | empty | `version=1:u8, sections:u8, kinds:u8, saveState:u8, zero:u8, revision:u32, imageBytes=400:u16` |
| READ `1` | `revision:u32, offset:u16, length:u8` | `revision:u32, offset:u16, data[length]` |
| BEGIN `2` | `revision:u32, sections:u8` | `token:u32` |
| WRITE `3` | `token:u32, offset:u16, data[1..96]` | `received:u16` |
| APPLY `4` | `token:u32` | empty |
| SAVE `5` | `revision:u32, sections:u8` | empty; poll INFO until saveState is terminal |
| DISCARD `6` | `token:u32` | empty |

Image bytes `0..371` are the controller policy, `372..395` contain four
`{enabled:u16, scale:u16, timeoutMs:u16}` mappings, byte `396` is mouse spread,
and bytes `397..399` are zero. READ chunks are at most 96 bytes. BEGIN captures
the revision and creates a token. WRITE fills all 400 bytes in order; identical
already-received chunks are accepted. APPLY requires complete coverage and an
unchanged revision/route. A new BEGIN supersedes an abandoned transaction.
DISCARD drops only that token's staging. If an APPLY response is lost, read the
MCU before deciding whether to resend; a consumed token is not reusable.

Preset plaintext is `MKDS`, version `1`, section mask, kind mask, zero, 16 random
bytes, and the 400-byte image (424 bytes total). Existing firmware settings-file
record `0x1B` seals/opens it as five authenticated MKSE packets (789 file bytes).
The key stays in firmware. Validate packet framing, every authentication tag,
the whole plaintext digest, schema and all values before BEGIN. SDKs implement
this; applications should use the typed import/export methods.

Asynchronous CONNECTION topology notices have a 13-byte payload beginning
`0x12`. They are not tuning replies. Transport readers must demultiplex them,
just as they demultiplex input-change events.
