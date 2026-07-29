# MAKXD API Suite

Unified public APIs for MAKXD devices across C++, Rust, Python, and C#.
The suite keeps device commands, keyboard and mouse control, controller
support, and kinded input streaming consistent across languages.

The single shared protocol contract is
[`protocol/MAKXD_PROTOCOL.md`](protocol/MAKXD_PROTOCOL.md), with
language-specific implementations in `cpp/`, `rust/`, `python/`, and
`csharp/`. The separate lightweight [NET C++ client](net/cpp/README.md) is in
`net/cpp/`. It requires `kmNetMakxdBridge.exe` and exposes only the Bridge
functions documented there.

All four APIs select exactly one COM, UDP, or BLE connection owner and either
KM or `MAK_API` command encoding. COM and UDP support optional AES-128-CCM.
BLE carries plain application records as-is and rejects a MAKXD AES transport
key. See the
[public protocol contract](protocol/MAKXD_PROTOCOL.md#connection).

Automatic serial discovery probes all `1A86:55D3` CH343 ports first, then all
`1A86:7523` CH340 ports. Each port is tried at `115200`, `1000000`, and
`4000000` until `km.version()` returns exactly `km.MAKXD`.

UDP supports Ethernet and Wi-Fi in host or raw transaction mode. VLAN IDs are
bound through the selected operating-system VLAN interface/address so the
802.1Q tag stays outside KM, `MAK_API`, and AES records. BLE uses the fixed
MAKXD GATT UUIDs and enables notifications before the version proof.

## MAKXD firmware

The current MAKXD firmware release is stored under
[`/makxd_firmware/`](makxd_firmware/). The release catalog is
[`/makxd_firmware/firmware.json`](makxd_firmware/firmware.json), and every
catalog `file` is resolved relative to that directory. Installable `.mkafw`
firmware files are stored beside the JSON catalog. The catalog contains only
the current release.

## Repository

[`terrafirma2021/mak-suite`](https://github.com/terrafirma2021/mak-suite)

## Original project attribution

| API | Original project | Original author or maintainer |
| --- | --- | --- |
| Python | [SleepyTotem/makcu-py-lib](https://github.com/SleepyTotem/makcu-py-lib) | [SleepyTotem](https://github.com/SleepyTotem) |
| C# | [1claim-gh/makcu-csharp](https://github.com/1claim-gh/makcu-csharp) | [1claim-gh](https://github.com/1claim-gh) |
| NET C++ interoperability reference | [ZCban/kmboxNET](https://github.com/ZCban/kmboxNET) | [ZCban](https://github.com/ZCban) |
