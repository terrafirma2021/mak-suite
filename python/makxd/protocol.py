from dataclasses import dataclass
from enum import IntEnum, IntFlag

from .errors import MakxdResponseError


class ApiOpcode(IntEnum):
    DEVICE = 0x02
    BUTTONS = 0x10
    LEFT = 0x11
    RIGHT = 0x12
    MIDDLE = 0x13
    SIDE1 = 0x14
    SIDE2 = 0x15
    MOVE_MASK = 0x16
    WHEEL_MASK = 0x17
    MOVE = 0x18
    WHEEL = 0x19
    LEFT_MASK = 0x1A
    RIGHT_MASK = 0x1B
    MIDDLE_MASK = 0x1C
    SIDE1_MASK = 0x1D
    SIDE2_MASK = 0x1E
    KEY_DOWN = 0x20
    KEY_UP = 0x21
    KEY_INIT = 0x22
    KEY_PRESS = 0x23
    KEY_STRING = 0x24
    KEY_IS_DOWN = 0x25
    KEY_MULTI_DOWN = 0x26
    KEY_MULTI_UP = 0x27
    KEY_MULTI_PRESS = 0x28
    KEY_MASK = 0x29
    KEY_REMAP = 0x2A
    KEY_KEYS = 0x2B
    CONTROLLER_STATE = 0x40
    CONTROLLER_CONTROL = 0x41
    CONTROLLER_MASK = 0x51


class DeviceKind(IntFlag):
    NONE = 0x00
    MOUSE = 0x01
    KEYBOARD = 0x02
    GENERIC_HID = 0x04
    DS4 = 0x08
    DUALSENSE_DS5 = 0x10
    DUALSENSE_EDGE = 0x20
    XBOX_GIP = 0x40
    XBOX_360_XINPUT = 0x80


@dataclass(frozen=True)
class DeviceInfo:
    kinds: DeviceKind

    def has(self, kind: DeviceKind) -> bool:
        return bool(self.kinds & kind)

    @property
    def mouse(self) -> bool:
        return self.has(DeviceKind.MOUSE)

    @property
    def keyboard(self) -> bool:
        return self.has(DeviceKind.KEYBOARD)

def parse_device_info(payload: bytes) -> DeviceInfo:
    if len(payload) != 1:
        raise MakxdResponseError(
            f"Invalid MAK_API device response length: {len(payload)}"
        )
    return DeviceInfo(DeviceKind(payload[0]))


__all__ = [
    "ApiOpcode",
    "DeviceKind",
    "DeviceInfo",
    "parse_device_info",
]
