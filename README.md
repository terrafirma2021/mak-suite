# MAKXD API Suite

Unified public APIs for MAKXD devices across C++, Rust, Python, and C#.
The suite keeps device commands, keyboard and mouse control, controller support,
and kinded input streaming consistent across languages.

The single shared protocol contract is
[`protocol/MAKXD_PROTOCOL.md`](protocol/MAKXD_PROTOCOL.md), with
language-specific implementations in `cpp/`, `rust/`, `python/`, and `csharp/`.
The separate lightweight [NET C++ client](net/cpp/README.md) is in `net/cpp/`.
It requires `kmNetMakxdBridge.exe` to be running and exposes only the supported
Bridge subset documented there.

## Repository

[`terrafirma2021/mak-suite`](https://github.com/terrafirma2021/mak-suite)

## Original project attribution

| API | Original project | Original author or maintainer |
| --- | --- | --- |
| Python | [SleepyTotem/makcu-py-lib](https://github.com/SleepyTotem/makcu-py-lib) | [SleepyTotem](https://github.com/SleepyTotem) |
| C# | [1claim-gh/makcu-csharp](https://github.com/1claim-gh/makcu-csharp) | [1claim-gh](https://github.com/1claim-gh) |
| NET C++ interoperability reference | [ZCban/kmboxNET](https://github.com/ZCban/kmboxNET) | [ZCban](https://github.com/ZCban) |
