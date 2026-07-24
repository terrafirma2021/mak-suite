from typing import Sequence, Union

from .connection import SerialTransport
from .errors import MakxdCommandError


KeyboardKey = Union[int, str]


def _key_argument(key: KeyboardKey) -> str:
    if isinstance(key, bool):
        raise MakxdCommandError("Keyboard HID code must be an integer")
    if isinstance(key, int):
        if not 0 <= key <= 255:
            raise MakxdCommandError("Keyboard HID code must be in the range 0..255")
        return str(key)
    if not isinstance(key, str) or not key:
        raise MakxdCommandError("Keyboard key name cannot be empty")
    if not key.isascii():
        raise MakxdCommandError("Keyboard key name must contain ASCII characters")
    return "'" + _escape_single_quoted(key) + "'"


def _escape_single_quoted(value: str) -> str:
    escaped = []
    for byte in value.encode("utf-8"):
        if byte == ord("\\"):
            escaped.append("\\\\")
        elif byte == ord("'"):
            escaped.append("\\'")
        elif byte == 0x0A:
            escaped.append("\\n")
        elif byte == 0x0D:
            escaped.append("\\r")
        elif byte == 0x09:
            escaped.append("\\t")
        elif byte < 0x20 or byte == 0x7F:
            escaped.append(f"\\x{byte:02X}")
        else:
            escaped.append(chr(byte))
    return "".join(escaped)


def _escape_double_quoted(value: str) -> str:
    escaped = []
    for byte in value.encode("ascii"):
        if byte == ord("\\"):
            escaped.append("\\\\")
        elif byte == ord('"'):
            escaped.append('\\"')
        elif byte == 0x0A:
            escaped.append("\\n")
        elif byte == 0x0D:
            escaped.append("\\r")
        elif byte == 0x09:
            escaped.append("\\t")
        elif byte < 0x20 or byte == 0x7F:
            escaped.append(f"\\x{byte:02X}")
        else:
            escaped.append(chr(byte))
    return "".join(escaped)


class Keyboard:
    """MAKXD keyboard command surface matching the C++ API contract."""

    def __init__(self, transport: SerialTransport) -> None:
        self.transport = transport

    def down(self, key: KeyboardKey) -> None:
        self.transport.send_command(f"km.down({_key_argument(key)})")

    def up(self, key: KeyboardKey) -> None:
        self.transport.send_command(f"km.up({_key_argument(key)})")

    def press(
        self,
        key: KeyboardKey,
        hold_ms: int | None = None,
        rand_ms: int | None = None,
    ) -> None:
        command = f"km.press({_key_argument(key)}"
        if hold_ms is not None:
            if hold_ms < 0 or hold_ms > 0xFFFFFFFF:
                raise MakxdCommandError("Keyboard hold time must be in the range 0..4294967295")
            command += f",{hold_ms}"
            if rand_ms is not None:
                if rand_ms < 0 or rand_ms > 0xFFFFFFFF:
                    raise MakxdCommandError("Keyboard random time must be in the range 0..4294967295")
                command += f",{rand_ms}"
        elif rand_ms is not None:
            raise MakxdCommandError("Keyboard random time requires a hold time")
        self.transport.send_command(command + ")")

    def string(self, text: str) -> None:
        if len(text.encode("utf-8")) > 256:
            raise MakxdCommandError("Keyboard string cannot exceed 256 bytes")
        if not text.isascii():
            raise MakxdCommandError("Keyboard string must contain ASCII bytes")
        self.transport.send_command(f'km.string("{_escape_double_quoted(text)}")')

    def init(self) -> None:
        self.transport.send_command("km.init()")

    def is_down(self, key: KeyboardKey) -> bool:
        response = self.transport.send_command(
            f"km.isdown({_key_argument(key)})",
            expect_response=True,
            timeout=0.1,
        )
        return bool(response and response.strip() == "1")

    def mask(self, key: KeyboardKey, enable: bool) -> None:
        self.transport.send_command(
            f"km.mask({_key_argument(key)},{1 if enable else 0})"
        )

    def remap(self, source: KeyboardKey, target: KeyboardKey) -> None:
        self.transport.send_command(
            f"km.remap({_key_argument(source)},{_key_argument(target)})"
        )

    def _multi(self, command: str, keys: Sequence[KeyboardKey]) -> None:
        if not keys:
            raise MakxdCommandError("Keyboard key list cannot be empty")
        self.transport.send_command(
            f"km.{command}(" + ",".join(_key_argument(key) for key in keys) + ")"
        )

    def multi_down(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi("multidown", keys)

    def multi_up(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi("multiup", keys)

    def multi_press(self, keys: Sequence[KeyboardKey]) -> None:
        self._multi("multipress", keys)

    def keys(self, enabled: bool | None = None) -> str | None:
        if enabled is None:
            return self.transport.send_command("km.keys()", expect_response=True)
        self.transport.send_command(f"km.keys({1 if enabled else 0})")
        return None
