"""Framed, on-change physical input: independent mouse/keyboard/controller kinds."""
from dataclasses import dataclass
from enum import IntEnum
from typing import Optional

class StreamKind(IntEnum):
    MOUSE = 1
    KEYBOARD = 2
    CONTROLLER = 3

STREAM_COMMAND = 0x52
STREAM_EVENT = 0x53
STREAM_TRIGGER_MAX = 1023

@dataclass(frozen=True)
class StreamFrame:
    command: int
    payload: bytes

@dataclass(frozen=True)
class InputChange:
    kind: StreamKind
    control: int
    value: int
    overflow: bool = False

    @property
    def trigger(self) -> bool:
        return self.kind == StreamKind.CONTROLLER and self.control in (10, 11)

@dataclass(frozen=True)
class StreamRequest:
    kind: StreamKind
    enabled: Optional[bool] = None

    def __post_init__(self):
        object.__setattr__(self, "kind", StreamKind(self.kind))
        if self.enabled is not None and not isinstance(self.enabled, bool):
            raise ValueError("enabled must be True, False or None (query)")

    def encode(self) -> bytes:
        payload = bytes((self.kind,))
        if self.enabled is not None:
            payload += bytes((self.enabled,))
        return b"\xDE\xAD" + len(payload).to_bytes(2, "little") + bytes((STREAM_COMMAND,)) + payload

    @classmethod
    def mouse(cls, enabled: Optional[bool] = True):
        return cls(StreamKind.MOUSE, enabled)
    @classmethod
    def keyboard(cls, enabled: Optional[bool] = True):
        return cls(StreamKind.KEYBOARD, enabled)
    @classmethod
    def controller(cls, enabled: Optional[bool] = True):
        return cls(StreamKind.CONTROLLER, enabled)

class StreamFrameDecoder:
    def __init__(self):
        self._buffer = bytearray()
    def feed(self, data: bytes):
        self._buffer.extend(data)
    def next(self) -> Optional[StreamFrame]:
        while len(self._buffer) >= 2:
            if self._buffer[:2] != b"\xDE\xAD":
                del self._buffer[0]
                continue
            if len(self._buffer) < 5:
                return None
            length = int.from_bytes(self._buffer[2:4], "little")
            if length > 251:
                del self._buffer[0]
                continue
            if len(self._buffer) < 5 + length:
                return None
            frame = StreamFrame(self._buffer[4], bytes(self._buffer[5:5+length]))
            del self._buffer[:5+length]
            return frame
        return None

def decode_input_change(frame: StreamFrame) -> Optional[InputChange]:
    p = frame.payload
    if frame.command != STREAM_EVENT or len(p) not in (3, 4):
        return None
    try:
        kind = StreamKind(p[0])
    except ValueError:
        return None
    control = p[1]
    if len(p) == 3 and control == 255 and p[2] == 255:
        return InputChange(kind, control, 255, True)
    if kind == StreamKind.CONTROLLER and control in (10, 11):
        value = int.from_bytes(p[2:], "little")
        return InputChange(kind, control, value) if len(p) == 4 and value <= STREAM_TRIGGER_MAX else None
    if len(p) != 3 or p[2] > 1:
        return None
    if kind == StreamKind.MOUSE and control > 31:
        return None
    if kind == StreamKind.CONTROLLER and (control > 54 or 12 <= control <= 15):
        return None
    return InputChange(kind, control, p[2])

__all__ = ["StreamKind", "StreamFrame", "InputChange", "StreamRequest", "StreamFrameDecoder",
           "STREAM_COMMAND", "STREAM_EVENT", "STREAM_TRIGGER_MAX", "decode_input_change"]
