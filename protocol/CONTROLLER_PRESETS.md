# Controller presets in NOR

MAKCU and MAKXD/HPM own 15 controller preset slots. A preset contains controller
processing and mouse/keyboard-to-controller translation settings only. Mouse
spread, device identity and serial strings are excluded.

## API presets

Supply the same nonzero 16-byte opaque hash to read, save or load a preset.
The hash is a caller-selected identifier, not a checksum of the settings. Saving
with an existing hash overwrites that preset. A different hash creates a new
entry. Unknown hashes return not-found. Hashes are not enumerated by the API.
Keep hashes private if they are used as access tokens; this is possession-based
access, not caller authentication. Live settings remain readable and tuneable.

Load enables the stored settings in SRAM and persists the startup selection.
Save captures the current live controller settings and selects that preset for
startup. Wait for completion before disconnecting power. Editing live settings
does not write NOR or change any stored preset.

| SDK | Methods |
| --- | --- |
| Python | `read_controller_preset`, `save_controller_preset`, `load_controller_preset` |
| C# | `ReadControllerPreset`, `SaveControllerPreset`, `LoadControllerPreset` |
| C++ | `readControllerPreset`, `saveControllerPreset`, `loadControllerPreset` |
| C | `makxd_controller_preset_read`, `makxd_controller_preset_save`, `makxd_controller_preset_load` |
| Rust, sync and async | `read_controller_preset`, `save_controller_preset`, `load_controller_preset` |

Read returns controller and translation settings. Load returns the resulting live
settings snapshot. Save optionally accepts the current snapshot to reject stale
edits; without one the SDK reads the live revision first.

## WebUI presets

WebUI users can name (or leave blank), save, load, cycle, import and export their
own controller presets. API entries, hashes and physical slots are hidden.
When an API preset is active, WebUI tuning changes only live settings; Save makes
a separate WebUI preset. Export reads a selected saved WebUI preset. Import
applies controller settings live; Save separately to retain them on the device.
The JSON export contains `type`, `version`, `name` and 396 settings bytes only.

Both namespaces share 15 entries. When full, a new save replaces the oldest saved
entry in its own namespace. It never evicts the other namespace. If all entries
belong to the other namespace, save returns full. Loading does not change age.
Factory reset clears both namespaces and their startup selection.

## Wire contract

Connection opcode `0x3e`, record `0x1e`; all integers are little-endian.
Replies start `[record, operation, status]`. Status: 0 complete, 1 pending,
2 busy, 3 stale, 4 invalid, 5 unsupported, 6 storage error, 7 not-found, 8 full.

A key is 17 bytes: WebUI `[1, id:u32, zero:12]` (ID zero creates), or API
`[2, hash:16]`. API hashes must be nonzero; API names must be empty.

| Operation | Request after record and operation | Reply after status |
| --- | --- | --- |
| 0 status | empty | version:u8=1, capacity:u8=15, webCount:u8, activeWebID:u32, revision:u32 |
| 1 list WebUI | index:u8 | webID:u32, nameLength:u8, UTF-8 name |
| 2 read | key:17, offset:u16, length:u8 | revision:u32, offset:u16, settings chunk |
| 3 save live | key:17, liveRevision:u32, nameLength:u8, UTF-8 name | token:u32, webID:u32 |
| 4 load and enable | key:17 | token:u32, webID:u32 |
| 6 completion | token:u32 | empty |

API replies use webID zero. Status exposes no API entry count or key; activeWebID
is zero when an API preset is active. Names are valid UTF-8, maximum 32 bytes.
Read chunks are at most 96 bytes across a 396-byte image: controller policy at
0..371, four six-byte translation records at 372..395. Retry the complete read
if catalog revisions differ between chunks. Live save revision uses the existing
400-byte tuning snapshot and route generation, not the catalog revision.

Save/load return pending with a token. Poll operation 6 until complete; a new
transaction invalidates an old token. SDKs wait up to 30 seconds. Busy and stale
responses require reading current state before deciding whether to retry.

## Storage and upgrade behavior

The MCU uses two 8 KiB banks with a separately committed, verified catalog.
Writes and erases advance through the existing asynchronous NOR owner. HPM uses
its authenticated page codec. A committed bank survives interruption of a later
save. Firmware update and factory reset exclude in-progress preset mutations.
A nonblank unknown region is preserved and reports storage error rather than
being erased automatically. This includes legacy HPM log pages in the reserved
region; explicit factory reset clears the region. An interrupted first-ever save
without a committed bank may also require factory reset.

Serial management reads return only `none`, `original` or `spoofed` state (0, 1,
2); writes can update the serial. No serial string is included in these presets.
