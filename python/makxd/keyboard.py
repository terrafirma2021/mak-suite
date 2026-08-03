from typing import Sequence, Union

from .connection import SerialTransport
from .errors import MakxdCommandError
from .protocol import ApiOpcode


KeyboardKey = Union[int, str]

_NAMED_KEY_CODES = {
    "enter": 40, "return": 40, "escape": 41, "esc": 41,
    "backspace": 42, "back": 42, "tab": 43,
    "space": 44, "spacebar": 44, "minus": 45, "dash": 45,
    "hyphen": 45, "equals": 46, "equal": 46,
    "leftbracket": 47, "lbracket": 47, "openbracket": 47,
    "rightbracket": 48, "rbracket": 48, "closebracket": 48,
    "backslash": 49, "bslash": 49, "nonus_hash": 50,
    "semicolon": 51, "semi": 51, "quote": 52, "apostrophe": 52,
    "singlequote": 52, "grave": 53, "backtick": 53, "tilde": 53,
    "comma": 54, "period": 55, "dot": 55, "slash": 56,
    "forwardslash": 56, "fslash": 56, "capslock": 57, "caps": 57,
    "printscreen": 70, "prtsc": 70, "print": 70,
    "scrolllock": 71, "scroll": 71, "pause": 72, "break": 72,
    "insert": 73, "ins": 73, "home": 74, "pageup": 75,
    "pgup": 75, "delete": 76, "del": 76, "end": 77,
    "pagedown": 78, "pgdown": 78, "pgdn": 78,
    "right": 79, "rightarrow": 79, "left": 80, "leftarrow": 80,
    "down": 81, "downarrow": 81, "up": 82, "uparrow": 82,
    "numlock": 83, "num": 83, "kpdivide": 84, "npdivide": 84,
    "kpmultiply": 85, "npmultiply": 85, "kpminus": 86,
    "npminus": 86, "kpplus": 87, "npplus": 87, "kpenter": 88,
    "npenter": 88, "kpperiod": 99, "kpdot": 99,
    "npperiod": 99, "npdot": 99,
    "leftctrl": 224, "lctrl": 224, "leftcontrol": 224,
    "lcontrol": 224, "ctrl": 224, "control": 224,
    "leftshift": 225, "lshift": 225, "shift": 225,
    "leftalt": 226, "lalt": 226, "alt": 226,
    "leftgui": 227, "lgui": 227, "leftwin": 227, "lwin": 227,
    "gui": 227, "win": 227, "windows": 227, "super": 227,
    "meta": 227, "cmd": 227, "command": 227,
    "rightctrl": 228, "rctrl": 228, "rightcontrol": 228,
    "rcontrol": 228, "rightshift": 229, "rshift": 229,
    "rightalt": 230, "ralt": 230, "rightgui": 231, "rgui": 231,
    "rightwin": 231, "rwin": 231, "rightwindows": 231,
}


def _key_code(key: KeyboardKey) -> int:
    if isinstance(key, bool):
        raise MakxdCommandError("Keyboard HID code must be an integer")
    if isinstance(key, int):
        if 0 <= key <= 255:
            return key
        raise MakxdCommandError("Keyboard HID code must be in the range 0..255")
    if not isinstance(key, str) or not key or not key.isascii():
        raise MakxdCommandError("Keyboard key name must contain ASCII characters")
    name = key.lower()
    if len(name) == 1:
        if "a" <= name <= "z":
            return 4 + ord(name) - ord("a")
        if "1" <= name <= "9":
            return 30 + ord(name) - ord("1")
        if name == "0":
            return 39
    if name.startswith("f") and name[1:].isdigit():
        function = int(name[1:])
        if 1 <= function <= 12:
            return 57 + function
    if (
        len(name) == 3
        and name[:2] in ("kp", "np")
        and name[2].isdigit()
    ):
        return 98 if name[2] == "0" else 88 + int(name[2])
    try:
        return _NAMED_KEY_CODES[name]
    except KeyError as error:
        raise MakxdCommandError(f"Unknown keyboard key name: {key}") from error


def _dt_value(dt_uframes: int | None) -> str:
    if dt_uframes is None:
        return ""
    if not isinstance(dt_uframes, int) or isinstance(dt_uframes, bool):
        raise MakxdCommandError("DT must be an integer")
    if dt_uframes < 0 or dt_uframes > 0x3FFF:
        raise MakxdCommandError("DT must be in the range 0..16383")
    return str(dt_uframes)


class Keyboard:
    """MAKXD keyboard command surface matching the C++ API contract."""

    def __init__(self, transport: SerialTransport) -> None:
        self.transport = transport

    def down(self, key: KeyboardKey, dt_uframes: int | None = None) -> None:
        payload = bytes((_key_code(key),))
        if dt_uframes is not None:
            payload += int(_dt_value(dt_uframes)).to_bytes(2, "little")
        self.transport.send_mak_api(
            ApiOpcode.KEY_DOWN, payload, wait_response=False
        )

    def up(self, key: KeyboardKey, dt_uframes: int | None = None) -> None:
        payload = bytes((_key_code(key),))
        if dt_uframes is not None:
            payload += int(_dt_value(dt_uframes)).to_bytes(2, "little")
        self.transport.send_mak_api(
            ApiOpcode.KEY_UP, payload, wait_response=False
        )

    def press(
        self,
        key: KeyboardKey,
        hold_ms: int | None = None,
        rand_ms: int | None = None,
    ) -> None:
        if hold_ms is not None:
            if hold_ms < 0 or hold_ms > 0xFFFFFFFF:
                raise MakxdCommandError("Keyboard hold time must be in the range 0..4294967295")
            if rand_ms is not None:
                if rand_ms < 0 or rand_ms > 0xFFFFFFFF:
                    raise MakxdCommandError("Keyboard random time must be in the range 0..4294967295")
        elif rand_ms is not None:
            raise MakxdCommandError("Keyboard random time requires a hold time")
        payload = bytes((_key_code(key),))
        if hold_ms is not None:
            payload += hold_ms.to_bytes(4, "little")
            if rand_ms is not None:
                payload += rand_ms.to_bytes(4, "little")
        self.transport.send_mak_api(
            ApiOpcode.KEY_PRESS, payload, wait_response=False
        )

    def string(self, text: str) -> None:
        if len(text.encode("utf-8")) > 256:
            raise MakxdCommandError("Keyboard string cannot exceed 256 bytes")
        if not text.isascii():
            raise MakxdCommandError("Keyboard string must contain ASCII bytes")
        encoded = text.encode("ascii")
        if len(encoded) > 248:
            raise MakxdCommandError(
                "MAK_API keyboard string cannot exceed 248 bytes"
            )
        self.transport.send_mak_api(
            ApiOpcode.KEY_STRING, encoded, wait_response=False
        )

    def init(self, dt_uframes: int | None = None) -> None:
        payload = b""
        if dt_uframes is not None:
            payload = int(_dt_value(dt_uframes)).to_bytes(2, "little")
        self.transport.send_mak_api(
            ApiOpcode.KEY_INIT, payload, wait_response=False
        )

    def is_down(self, key: KeyboardKey) -> bool:
        response = self.transport.send_mak_api(
            ApiOpcode.KEY_IS_DOWN, bytes((_key_code(key),)), 0.1
        )
        return response == b"\x01"

    def mask(self, key: KeyboardKey, enable: bool) -> None:
        self.transport.send_mak_api(
            ApiOpcode.KEY_MASK,
            bytes((_key_code(key), 1 if enable else 0)),
            wait_response=False,
        )

    def remap(self, source: KeyboardKey, target: KeyboardKey) -> None:
        self.transport.send_mak_api(
            ApiOpcode.KEY_REMAP,
            bytes((_key_code(source), _key_code(target))),
            wait_response=False,
        )

    def _multi(self, opcode: ApiOpcode, keys: Sequence[KeyboardKey]) -> None:
        if not keys:
            raise MakxdCommandError("Keyboard key list cannot be empty")
        self.transport.send_mak_api(
            opcode,
            bytes(_key_code(key) for key in keys),
            wait_response=False,
        )

    def multi_down(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi(ApiOpcode.KEY_MULTI_DOWN, keys)

    def multi_up(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi(ApiOpcode.KEY_MULTI_UP, keys)

    def multi_press(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi(ApiOpcode.KEY_MULTI_PRESS, keys)

    def keys(self, enabled: bool | None = None) -> str | None:
        if enabled is None:
            response = self.transport.send_mak_api(ApiOpcode.KEY_KEYS)
            return str(response[0]) if len(response) == 1 else ""
        self.transport.send_mak_api(
            ApiOpcode.KEY_KEYS, bytes((1 if enabled else 0,)),
            wait_response=False,
        )
        return None
