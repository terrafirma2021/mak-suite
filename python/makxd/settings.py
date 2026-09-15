"""Typed live device tuning and authenticated, portable presets.

Apply/import do not save or reboot. Save is explicit. Completing/cancelling an
injected movement still requires stick (0, 0) or trigger 0; tuning is independent.
"""
from copy import deepcopy
from dataclasses import dataclass, field
from enum import IntEnum, IntFlag
import hashlib
import secrets
import struct
import threading
import time


class SettingsSection(IntFlag):
    CONTROLLER = 1
    TRANSLATION = 2
    MOUSE = 4


class ControllerChannel(IntEnum):
    RIGHT_STICK = 0
    LEFT_STICK = 1
    LEFT_TRIGGER = 2
    RIGHT_TRIGGER = 3


class SettingsError(RuntimeError):
    def __init__(self, status: int, message: str = "Device settings request failed"):
        self.status = status
        super().__init__(f"{message} (status {status})")


def _integer(value, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"Expected an integer in {low}..{high}")
    return value


@dataclass
class ControllerCurve:
    center_deadzone_percent: int = 0
    anti_deadzone_percent: int = 0
    change_deadband_percent: int = 0
    points: list[tuple[int, int]] = field(default_factory=lambda: [(n, n) for n in (0, 25, 50, 75, 100)])

    def _encode(self):
        values = [_integer(v, 0, 50) for v in (self.center_deadzone_percent, self.anti_deadzone_percent, self.change_deadband_percent)]
        if len(self.points) != 5 or self.points[0] != (0, 0) or self.points[-1] != (100, 100):
            raise ValueError("Curve requires five points from (0, 0) to (100, 100)")
        for i, (x, y) in enumerate(self.points):
            _integer(x, 0, 100); _integer(y, 0, 100)
            if i and (x <= self.points[i-1][0] or y < self.points[i-1][1]):
                raise ValueError("Curve input must increase and output must not decrease")
        return bytes(values + [5] + [v for point in self.points for v in point])

    @classmethod
    def _decode(cls, p):
        if len(p) != 14 or p[3] != 5:
            raise ValueError("Invalid curve")
        result = cls(*p[:3], [tuple(p[i:i+2]) for i in range(4, 14, 2)])
        result._encode()
        return result


@dataclass
class ControllerBehavior:
    name: str = "Linear"
    strength_percent: int | None = 0
    curve_enabled: bool = False
    advanced_enabled: bool = False
    inertia_percent: int = 0
    micro_percent: int = 0
    limit_percent: int = 0
    magnitude_variance_percent: int = 0
    angle_variance_percent: int = 0
    curves: list[ControllerCurve] = field(default_factory=lambda: [ControllerCurve() for _ in range(4)])

    def _encode(self):
        name = self.name.encode("ascii")
        if not 1 <= len(name) <= 24 or any(c < 32 or c > 126 for c in name) or len(self.curves) != 4:
            raise ValueError("Behavior requires a printable ASCII name of 1..24 bytes and four curves")
        if type(self.curve_enabled) is not bool or type(self.advanced_enabled) is not bool:
            raise ValueError("Behavior switches must be boolean")
        flags = 1 | (_integer(self.inertia_percent, 0, 100) << 1) | (_integer(self.micro_percent, 0, 100) << 8) | (_integer(self.limit_percent, 0, 100) << 15)
        flags |= int(self.advanced_enabled) << 22 | int(self.curve_enabled) << 23
        strength = 15 if self.strength_percent is None else 128 | _integer(self.strength_percent, 0, 100)
        return struct.pack("<IBBBB24s", flags, len(name), strength,
            _integer(self.magnitude_variance_percent, 0, 100), _integer(self.angle_variance_percent, 0, 100), name) + b"".join(c._encode() for c in self.curves)

    @classmethod
    def _decode(cls, p):
        flags, length, strength, magnitude, angle, name = struct.unpack("<IBBBB24s", p[:32])
        if not flags:
            if any(p):
                raise ValueError("Invalid empty behavior")
            return None
        if flags & ~0xffffff or not flags & 1 or not 1 <= length <= 24 or (strength != 15 and not strength & 128):
            raise ValueError("Invalid behavior flags")
        result = cls(name[:length].decode("ascii"), None if strength == 15 else strength & 127,
            bool(flags & (1 << 23)), bool(flags & (1 << 22)), flags >> 1 & 127, flags >> 8 & 127,
            flags >> 15 & 127, magnitude, angle, [ControllerCurve._decode(p[i:i+14]) for i in range(32, 88, 14)])
        result._encode()
        return result


@dataclass
class ControllerSettings:
    interpolation: str = "auto"
    buffer_ms: int = 8
    timing_variance_percent: int = 0
    behaviors: list[ControllerBehavior | None] = field(default_factory=lambda: [ControllerBehavior(name=n) for n in ("Right stick", "Left stick", "Left trigger", "Right trigger")])
    # Retained when reading older policies; behavior() promotes them losslessly.
    curve_enabled: bool = False
    legacy_strengths: tuple[int, int, int] = (0, 0, 0)
    selected_profile: int = 0
    profile_count: int = 4

    def behavior(self, channel: ControllerChannel) -> ControllerBehavior:
        index = int(channel)
        _integer(index, 0, 3)
        if self.profile_count < 4:
            original = deepcopy(self.behaviors[0])
            if original is None:
                raise ValueError("Missing controller behavior")
            for i in range(4):
                source = self.behaviors[i] or deepcopy(self.behaviors[2] if i == 3 and self.behaviors[2] else original)
                source.curves[i] = deepcopy(original.curves[i])
                source.strength_percent = self.legacy_strengths[min(i, 2)] if source.strength_percent is None else source.strength_percent
                source.curve_enabled = source.curve_enabled or self.curve_enabled
                self.behaviors[i] = source
            self.profile_count = 4
            self.selected_profile = 0
        return self.behaviors[index]

    def _encode(self):
        if self.interpolation not in ("off", "fixed", "auto") or len(self.behaviors) != 4 or len(self.legacy_strengths) != 3:
            raise ValueError("Invalid controller policy")
        count = _integer(self.profile_count, 1, 4)
        selected = _integer(self.selected_profile, 0, count-1)
        if any(p is None for p in self.behaviors[:count]):
            raise ValueError("Missing controller behavior")
        flags = int(self.curve_enabled) | (_integer(self.timing_variance_percent, 0, 100) << 22)
        for v, shift in zip(self.legacy_strengths, (1, 8, 15)):
            flags |= _integer(v, 0, 100) << shift
        return struct.pack("<5I", flags, ("off", "fixed", "auto").index(self.interpolation),
            _integer(self.buffer_ms, 1, 64), selected, count) + b"".join(p._encode() if p else bytes(88) for p in self.behaviors)

    @classmethod
    def _decode(cls, p):
        if len(p) != 372:
            raise ValueError("Invalid controller policy length")
        flags, mode, buffer, selected, count = struct.unpack("<5I", p[:20])
        if flags & ~0x1fffffff or mode > 2:
            raise ValueError("Invalid controller policy flags")
        result = cls(("off", "fixed", "auto")[mode], buffer, flags >> 22 & 127,
            [ControllerBehavior._decode(p[i:i+88]) for i in range(20, 372, 88)], bool(flags & 1),
            tuple(flags >> s & 127 for s in (1, 8, 15)), selected, count)
        result._encode()
        return result


@dataclass
class ControllerTranslation:
    enabled: bool = True
    scale: int = 1
    timeout_ms: int = 50

    def _encode(self, index):
        if type(self.enabled) is not bool:
            raise ValueError("Translation enabled must be boolean")
        return struct.pack("<3H", self.enabled, _integer(self.scale, 1 if index < 2 else 0, 512 if index < 2 else 100), _integer(self.timeout_ms, 1, 1000))


@dataclass
class DeviceSettings:
    controller: ControllerSettings = field(default_factory=ControllerSettings)
    # Translation order: LEFT stick/WASD, RIGHT stick/mouse, LT, RT.
    translation: list[ControllerTranslation] = field(default_factory=lambda: [ControllerTranslation(scale=1 if i < 2 else 100) for i in range(4)])
    mouse_spread_percent: int = 50

    def _encode(self):
        if len(self.translation) != 4:
            raise ValueError("Exactly four translation channels are required")
        return self.controller._encode() + b"".join(v._encode(i) for i, v in enumerate(self.translation)) + bytes([_integer(self.mouse_spread_percent, 0, 100), 0, 0, 0])

    @classmethod
    def _decode(cls, p):
        if len(p) != 400 or any(p[397:]):
            raise ValueError("Invalid device settings image")
        mappings = []
        for i in range(372, 396, 6):
            enabled, scale, timeout = struct.unpack("<3H", p[i:i+6])
            if enabled > 1:
                raise ValueError("Invalid translation switch")
            mappings.append(ControllerTranslation(bool(enabled), scale, timeout))
        result = cls(ControllerSettings._decode(p[:372]), mappings, p[396])
        result._encode()
        return result


@dataclass(frozen=True)
class SettingsInfo:
    sections: SettingsSection
    kinds: int  # mouse=1, keyboard=2, controller=4; physical devices on this unit.
    save_state: int
    revision: int


@dataclass
class SettingsSnapshot:
    info: SettingsInfo
    settings: DeviceSettings


@dataclass
class ControllerPreset:
    controller: ControllerSettings
    translation: list[ControllerTranslation]


class DeviceConfiguration:
    def __init__(self, transport):
        self._transport = transport
        self._lock = getattr(transport, "configuration_lock", threading.RLock())

    def _request(self, record, operation, data=b"", pending=False):
        p = self._transport.send_mak_api(0x3e, bytes([record, operation]) + data, timeout=2.0)
        if len(p) < 3 or p[:2] != bytes([record, operation]):
            raise SettingsError(5, "Firmware does not support this settings operation")
        if p[2] and not (pending and p[2] == 1):
            raise SettingsError(p[2])
        return p

    def info(self) -> SettingsInfo:
        with self._lock:
            p = self._request(0x1d, 0)
            if len(p) != 14 or p[3] != 1 or p[4] & ~7 or p[5] & ~7 or p[7] or struct.unpack_from("<H", p, 12)[0] != 400:
                raise SettingsError(4, "Invalid settings capability response")
            return SettingsInfo(SettingsSection(p[4]), p[5], p[6], struct.unpack_from("<I", p, 8)[0])

    def read(self) -> SettingsSnapshot:
        with self._lock:
            info = self.info()
            image = bytearray()
            for offset in range(0, 400, 96):
                length = min(96, 400-offset)
                p = self._request(0x1d, 1, struct.pack("<IHB", info.revision, offset, length))
                if len(p) != 9+length or struct.unpack_from("<IH", p, 3) != (info.revision, offset):
                    raise SettingsError(4, "Invalid settings readback")
                image.extend(p[9:])
            return SettingsSnapshot(info, DeviceSettings._decode(bytes(image)))

    def apply(self, snapshot: SettingsSnapshot, sections: SettingsSection | None = None) -> SettingsSnapshot:
        """Validate the complete candidate and apply live, rejecting stale reads.

        Does not persist, reboot, release sticks, or cancel injected movement.
        """
        image = snapshot.settings._encode()  # No request before full local validation.
        mask = int(snapshot.info.sections if sections is None else sections)
        if not mask or mask & ~int(snapshot.info.sections):
            raise SettingsError(5, "Requested settings are unsupported on this unit")
        with self._lock:
            begin = self._request(0x1d, 2, struct.pack("<IB", snapshot.info.revision, mask))
            if len(begin) != 7:
                raise SettingsError(4, "Invalid settings transaction")
            token = begin[3:7]
            try:
                for offset in range(0, 400, 96):
                    chunk = image[offset:offset+96]
                    p = self._request(0x1d, 3, token + struct.pack("<H", offset) + chunk)
                    if len(p) != 5 or struct.unpack_from("<H", p, 3)[0] != offset+len(chunk):
                        raise SettingsError(4, "Invalid settings progress")
                self._request(0x1d, 4, token)
            except Exception:
                try:
                    self._request(0x1d, 6, token)
                except Exception:
                    pass
                raise
            return self.read()

    def save(self, snapshot: SettingsSnapshot, sections: SettingsSection | None = None) -> None:
        """Persist current live settings; fails if the supplied snapshot is stale."""
        mask = int(snapshot.info.sections if sections is None else sections)
        if not mask or mask & ~int(snapshot.info.sections):
            raise SettingsError(5)
        with self._lock:
            self._request(0x1d, 5, struct.pack("<IB", snapshot.info.revision, mask), pending=True)
            deadline = time.monotonic()+15
            while True:
                state = self.info().save_state
                if state == 0:
                    return
                if state != 1:
                    raise SettingsError(state, "Settings were not saved")
                if time.monotonic() >= deadline:
                    raise TimeoutError("Settings save is still pending; read info before retrying")
                time.sleep(.02)

    @staticmethod
    def _preset_key(hash_id):
        if not isinstance(hash_id, (bytes, bytearray)) or len(hash_id) != 16 or not any(hash_id):
            raise ValueError("Preset hash must be 16 nonzero identifier bytes")
        return b"\x02" + bytes(hash_id)

    def _preset_complete(self, response):
        if len(response) != 11:
            raise SettingsError(4, "Invalid preset transaction")
        deadline = time.monotonic() + 30
        while True:
            status = self._request(0x1e, 6, response[3:7], pending=True)[2]
            if not status:
                return
            if time.monotonic() >= deadline:
                raise TimeoutError("Preset operation is still pending")
            time.sleep(.02)

    def read_controller_preset(self, hash_id: bytes) -> ControllerPreset:
        """Read a saved controller preset by its hash; does not enable it."""
        key = self._preset_key(hash_id)
        with self._lock:
            image, revision = bytearray(), None
            for offset in range(0, 396, 96):
                length = min(96, 396-offset)
                p = self._request(0x1e, 2, key + struct.pack("<HB", offset, length))
                if len(p) != 9+length or struct.unpack_from("<H", p, 7)[0] != offset:
                    raise SettingsError(4)
                current = struct.unpack_from("<I", p, 3)[0]
                if revision is not None and current != revision:
                    raise SettingsError(3, "Preset changed during read")
                revision = current
                image.extend(p[9:])
            value = DeviceSettings._decode(bytes(image) + bytes(4))
            return ControllerPreset(value.controller, value.translation)

    def save_controller_preset(self, hash_id: bytes, snapshot: SettingsSnapshot | None = None) -> None:
        """Save current controller tuning to NOR. Same hash overwrites its preset."""
        key = self._preset_key(hash_id)
        with self._lock:
            snapshot = snapshot or self.read()
            self._preset_complete(self._request(0x1e, 3,
                key + struct.pack("<IB", snapshot.info.revision, 0), pending=True))

    def load_controller_preset(self, hash_id: bytes) -> SettingsSnapshot:
        """Enable the hashed preset and remember it for startup; unknown hash errors."""
        key = self._preset_key(hash_id)
        with self._lock:
            self._preset_complete(self._request(0x1e, 4, key, pending=True))
            return self.read()

    def _seal(self, plain):
        digest = hashlib.sha256(plain).digest()
        packets = []
        for index, offset in enumerate(range(0, len(plain), 96)):
            chunk = plain[offset:offset+96]
            body = digest+struct.pack("<HHH", len(plain), index, len(chunk))+chunk
            for attempt in range(100):
                try:
                    packet = self._request(0x1b, 1, body)[3:]
                    break
                except SettingsError as error:
                    if error.status != 2 or attempt == 99:
                        raise
                    time.sleep(.01)
            if len(packet) != 73+len(chunk) or packet[:6] != b"MKSE\x01\x01" or packet[19:57] != body[:38]:
                raise SettingsError(4, "Invalid authenticated settings packet")
            packets.append(packet)
        return b"".join(packets)

    def _open(self, data):
        if not 74 <= len(data) <= 30000:
            raise ValueError("Invalid settings file size")
        total = struct.unpack_from("<H", data, 51)[0]
        if not 1 <= total <= 16384:
            raise ValueError("Invalid settings file length")
        digest = data[19:51]
        packets, offset = [], 0
        for index, start in enumerate(range(0, total, 96)):
            length = min(96, total-start)
            p = data[offset:offset+73+length]
            if len(p) != 73+length or p[:6] != b"MKSE\x01\x01" or p[19:51] != digest or struct.unpack_from("<HHH", p, 51) != (total, index, length):
                raise ValueError("Invalid settings file framing")
            packets.append(p); offset += len(p)
        if offset != len(data):
            raise ValueError("Unexpected settings file data")
        chunks = []
        for p in packets:
            chunk = self._request(0x1b, 2, p)[3:]
            if len(chunk) != struct.unpack_from("<H", p, 55)[0]:
                raise ValueError("Invalid authenticated settings chunk")
            chunks.append(chunk)
        plain = b"".join(chunks)
        if not secrets.compare_digest(hashlib.sha256(plain).digest(), digest):
            raise ValueError("Settings digest mismatch")
        return plain

    def export_preset(self, snapshot: SettingsSnapshot, sections: SettingsSection | None = None) -> bytes:
        """Encrypted .makxd-settings file; the key remains in firmware."""
        mask = int(snapshot.info.sections if sections is None else sections)
        if not mask or mask & ~int(snapshot.info.sections):
            raise SettingsError(5)
        with self._lock:
            current = self.read()
            if current.info.revision != snapshot.info.revision:
                raise SettingsError(3, "Settings changed before export; read again")
            return self._seal(b"MKDS" + bytes([1, mask, current.info.kinds, 0]) + secrets.token_bytes(16) + current.settings._encode())

    def import_preset(self, data: bytes) -> SettingsSnapshot:
        """Authenticate/validate everything, then apply live. Call save separately."""
        with self._lock:
            # Capture revision before decrypting to reject an intervening edit.
            current = self.read()
            plain = self._open(data)
            if len(plain) != 424 or plain[:5] != b"MKDS\x01" or plain[7] or not plain[5] or plain[5] & ~7 or plain[6] & ~7:
                raise ValueError("Unsupported device preset")
            if plain[5] & ~int(current.info.sections):
                raise SettingsError(5, "Preset needs settings unavailable on this device")
            current.settings = DeviceSettings._decode(plain[24:])
            return self.apply(current, SettingsSection(plain[5]))
