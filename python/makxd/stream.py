"""MAKXD lightweight multi-source input streaming protocol."""

from dataclasses import dataclass
from enum import IntEnum
from typing import Optional
import struct


class StreamKind(IntEnum):
    MOUSE = 1
    KEYBOARD = 2
    CONTROLLER = 3


STREAM_MASK_MOUSE = 1 << 0
STREAM_MASK_KEYBOARD = 1 << 1
STREAM_MASK_CONTROLLER = 1 << 2
STREAM_MASK_ALL = (STREAM_MASK_MOUSE | STREAM_MASK_KEYBOARD |
                   STREAM_MASK_CONTROLLER)
STREAM_COMMAND_INPUT = 0x01
STREAM_MAX_BODY_BYTES = 252
STREAM_MAX_PAYLOAD_BYTES = STREAM_MAX_BODY_BYTES - 1


class StreamOperation(IntEnum):
    START = 1
    STOP = 2
    STATUS = 3


@dataclass(frozen=True)
class StreamTiming:
    raw: int
    dt_uframes: int
    baseline: bool
    invalid: bool

    @classmethod
    def from_raw(cls, raw: int) -> "StreamTiming":
        return cls(raw, raw & 0x3FFF, bool(raw & 0x4000),
                   bool(raw & 0x8000))


@dataclass(frozen=True)
class StreamFrame:
    command: int
    payload: bytes


@dataclass(frozen=True)
class StreamControl:
    operation: int
    status: int
    active_mask: int


@dataclass(frozen=True)
class StreamInputRecord:
    kind: StreamKind
    sequence: int
    timing: StreamTiming
    values: bytes


@dataclass(frozen=True)
class StreamRequest:
    operation: StreamOperation
    source_mask: int = 0

    @classmethod
    def start(cls, source_mask: int = STREAM_MASK_ALL) -> "StreamRequest":
        return cls(StreamOperation.START, source_mask & STREAM_MASK_ALL)

    @classmethod
    def mouse(cls) -> "StreamRequest":
        return cls.start(STREAM_MASK_MOUSE)

    @classmethod
    def keyboard(cls) -> "StreamRequest":
        return cls.start(STREAM_MASK_KEYBOARD)

    @classmethod
    def controller(cls) -> "StreamRequest":
        return cls.start(STREAM_MASK_CONTROLLER)

    @classmethod
    def all(cls) -> "StreamRequest":
        return cls.start(STREAM_MASK_ALL)

    @classmethod
    def stop(cls) -> "StreamRequest":
        return cls(StreamOperation.STOP)

    @classmethod
    def status(cls) -> "StreamRequest":
        return cls(StreamOperation.STATUS)

    def encode(self) -> bytes:
        return _encode_frame(
            STREAM_COMMAND_INPUT,
            bytes((int(self.operation), self.source_mask & STREAM_MASK_ALL)),
        )


class StreamFrameDecoder:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> None:
        self._buffer.extend(data)

    def next(self) -> Optional[StreamFrame]:
        while len(self._buffer) >= 2:
            if self._buffer[:2] != b"\xDE\xAD":
                del self._buffer[0]
                continue
            if len(self._buffer) < 4:
                return None
            payload_len = struct.unpack_from("<H", self._buffer, 2)[0]
            if payload_len < 1 or payload_len > STREAM_MAX_PAYLOAD_BYTES:
                del self._buffer[0]
                continue
            frame_len = 5 + payload_len
            if len(self._buffer) < frame_len:
                return None
            command = self._buffer[4]
            payload = bytes(self._buffer[5:frame_len])
            del self._buffer[:frame_len]
            return StreamFrame(command, payload)
        return None


def decode_stream_control(frame: StreamFrame) -> Optional[StreamControl]:
    if frame.command != STREAM_COMMAND_INPUT or len(frame.payload) != 3:
        return None
    return StreamControl(frame.payload[0], frame.payload[1], frame.payload[2])


def decode_stream_input_record(
    frame: StreamFrame,
) -> Optional[StreamInputRecord]:
    if frame.command != STREAM_COMMAND_INPUT or len(frame.payload) < 8:
        return None
    try:
        kind = StreamKind(frame.payload[0])
    except ValueError:
        return None
    sequence = struct.unpack_from("<I", frame.payload, 3)[0]
    timing = StreamTiming.from_raw(struct.unpack_from("<H", frame.payload, 1)[0])
    return StreamInputRecord(kind, sequence, timing, frame.payload[7:])


def _encode_frame(command: int, payload: bytes) -> bytes:
    if not 0 < len(payload) <= STREAM_MAX_PAYLOAD_BYTES:
        raise ValueError("stream payload must be between 1 and 251 bytes")
    return (b"\xDE\xAD" + struct.pack("<H", len(payload)) +
            bytes((command,)) + payload)


__all__ = [
    "StreamKind", "StreamOperation", "StreamTiming", "StreamFrame",
    "StreamControl", "StreamInputRecord", "StreamRequest",
    "StreamFrameDecoder", "decode_stream_control",
    "decode_stream_input_record", "STREAM_MASK_MOUSE", "STREAM_MASK_KEYBOARD",
    "STREAM_MASK_CONTROLLER", "STREAM_MASK_ALL", "STREAM_COMMAND_INPUT",
    "STREAM_MAX_BODY_BYTES", "STREAM_MAX_PAYLOAD_BYTES",
]
