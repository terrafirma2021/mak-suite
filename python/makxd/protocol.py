from dataclasses import dataclass
from enum import Enum, IntEnum
import re

from .errors import MakxdResponseError


class ApiProtocol(str, Enum):
    KM = "km"
    MAK_API = "mak_api"

    @classmethod
    def parse(cls, value: "ApiProtocol | str") -> "ApiProtocol":
        if isinstance(value, cls):
            return value
        try:
            return cls(str(value).lower())
        except ValueError as error:
            raise ValueError("api_protocol must be 'km' or 'mak_api'") from error


class ApiVerb(IntEnum):
    GET = 0x00
    SET = 0x01
    EXEC = 0x02


class ApiOpcode(IntEnum):
    VERSION = 0x01
    DEVICE = 0x02
    BUTTONS = 0x10
    LEFT = 0x11
    RIGHT = 0x12
    MIDDLE = 0x13
    SIDE1 = 0x14
    SIDE2 = 0x15
    MOVE = 0x18
    WHEEL = 0x19
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
    CONTROLLER_LT = 0x43
    CONTROLLER_RT = 0x44
    CONTROLLER_LEFT_STICK = 0x45
    CONTROLLER_RIGHT_STICK = 0x46
    CONTROLLER_AUX = 0x47
    CONTROLLER_HAT_LEFT = 0x48
    CONTROLLER_HAT_RIGHT = 0x49
    CONTROLLER_HAT_DOWN = 0x4A
    CONTROLLER_HAT_UP = 0x4B
    CONTROLLER_LT_MASK = 0x52
    CONTROLLER_RT_MASK = 0x53
    CONTROLLER_LEFT_STICK_MASK = 0x54
    CONTROLLER_RIGHT_STICK_MASK = 0x55
    CONTROLLER_AUX_MASK = 0x56
    CONTROLLER_HAT_LEFT_MASK = 0x58
    CONTROLLER_HAT_RIGHT_MASK = 0x59
    CONTROLLER_HAT_DOWN_MASK = 0x5A
    CONTROLLER_HAT_UP_MASK = 0x5B
    CONTROLLER_BUTTON1 = 0x60
    CONTROLLER_BUTTON2 = 0x61
    CONTROLLER_BUTTON3 = 0x62
    CONTROLLER_BUTTON4 = 0x63
    CONTROLLER_BUTTON5 = 0x64
    CONTROLLER_BUTTON6 = 0x65
    CONTROLLER_BUTTON7 = 0x66
    CONTROLLER_BUTTON8 = 0x67
    CONTROLLER_BUTTON9 = 0x68
    CONTROLLER_BUTTON10 = 0x69
    CONTROLLER_BUTTON11 = 0x6A
    CONTROLLER_BUTTON12 = 0x6B
    CONTROLLER_BUTTON13 = 0x6C
    CONTROLLER_BUTTON14 = 0x6D
    CONTROLLER_BUTTON15 = 0x6E
    CONTROLLER_BUTTON16 = 0x6F
    CONTROLLER_BUTTON17 = 0x70
    CONTROLLER_BUTTON18 = 0x71
    CONTROLLER_BUTTON19 = 0x72
    CONTROLLER_BUTTON20 = 0x73
    CONTROLLER_BUTTON21 = 0x74
    CONTROLLER_BUTTON22 = 0x75
    CONTROLLER_BUTTON23 = 0x76
    CONTROLLER_BUTTON24 = 0x77
    CONTROLLER_BUTTON25 = 0x78
    CONTROLLER_BUTTON26 = 0x79
    CONTROLLER_BUTTON27 = 0x7A
    CONTROLLER_BUTTON28 = 0x7B
    CONTROLLER_BUTTON29 = 0x7C
    CONTROLLER_BUTTON30 = 0x7D
    CONTROLLER_BUTTON31 = 0x7E
    CONTROLLER_BUTTON32 = 0x7F
    CONTROLLER_BUTTON1_MASK = 0x80
    CONTROLLER_BUTTON2_MASK = 0x81
    CONTROLLER_BUTTON3_MASK = 0x82
    CONTROLLER_BUTTON4_MASK = 0x83
    CONTROLLER_BUTTON5_MASK = 0x84
    CONTROLLER_BUTTON6_MASK = 0x85
    CONTROLLER_BUTTON7_MASK = 0x86
    CONTROLLER_BUTTON8_MASK = 0x87
    CONTROLLER_BUTTON9_MASK = 0x88
    CONTROLLER_BUTTON10_MASK = 0x89
    CONTROLLER_BUTTON11_MASK = 0x8A
    CONTROLLER_BUTTON12_MASK = 0x8B
    CONTROLLER_BUTTON13_MASK = 0x8C
    CONTROLLER_BUTTON14_MASK = 0x8D
    CONTROLLER_BUTTON15_MASK = 0x8E
    CONTROLLER_BUTTON16_MASK = 0x8F
    CONTROLLER_BUTTON17_MASK = 0x90
    CONTROLLER_BUTTON18_MASK = 0x91
    CONTROLLER_BUTTON19_MASK = 0x92
    CONTROLLER_BUTTON20_MASK = 0x93
    CONTROLLER_BUTTON21_MASK = 0x94
    CONTROLLER_BUTTON22_MASK = 0x95
    CONTROLLER_BUTTON23_MASK = 0x96
    CONTROLLER_BUTTON24_MASK = 0x97
    CONTROLLER_BUTTON25_MASK = 0x98
    CONTROLLER_BUTTON26_MASK = 0x99
    CONTROLLER_BUTTON27_MASK = 0x9A
    CONTROLLER_BUTTON28_MASK = 0x9B
    CONTROLLER_BUTTON29_MASK = 0x9C
    CONTROLLER_BUTTON30_MASK = 0x9D
    CONTROLLER_BUTTON31_MASK = 0x9E
    CONTROLLER_BUTTON32_MASK = 0x9F


@dataclass(frozen=True)
class DeviceRoute:
    route_mask: int
    mouse_uframes: int
    keyboard_uframes: int
    controller_uframes: int
    generation: int = 0

    @property
    def mouse(self) -> bool:
        return bool(self.route_mask & 0x01)

    @property
    def keyboard(self) -> bool:
        return bool(self.route_mask & 0x02)

    @property
    def controller(self) -> bool:
        return bool(self.route_mask & 0x04)

    @staticmethod
    def _hz(uframes: int) -> float:
        return 0.0 if uframes == 0 else 8000.0 / uframes

    @property
    def mouse_hz(self) -> float:
        return self._hz(self.mouse_uframes)

    @property
    def keyboard_hz(self) -> float:
        return self._hz(self.keyboard_uframes)

    @property
    def controller_hz(self) -> float:
        return self._hz(self.controller_uframes)


_DEVICE_KM_PATTERN = re.compile(
    r"^R:(?P<route>-|[MKC]+);M:(?P<mouse>\d+)uf;"
    r"K:(?P<keyboard>\d+)uf;C:(?P<controller>\d+)uf$"
)


def parse_device_route_km(response: str) -> DeviceRoute:
    match = _DEVICE_KM_PATTERN.fullmatch(response.strip())
    if match is None:
        raise MakxdResponseError(f"Invalid km.device() response: {response!r}")
    route = match.group("route")
    route_mask = (
        (0x01 if "M" in route else 0)
        | (0x02 if "K" in route else 0)
        | (0x04 if "C" in route else 0)
    )
    return DeviceRoute(
        route_mask=route_mask,
        mouse_uframes=int(match.group("mouse")),
        keyboard_uframes=int(match.group("keyboard")),
        controller_uframes=int(match.group("controller")),
    )


def parse_device_route_mak_api(payload: bytes) -> DeviceRoute:
    if len(payload) != 11:
        raise MakxdResponseError(
            f"Invalid MAK_API device response length: {len(payload)}"
        )
    return DeviceRoute(
        route_mask=payload[0],
        mouse_uframes=int.from_bytes(payload[1:3], "little"),
        keyboard_uframes=int.from_bytes(payload[3:5], "little"),
        controller_uframes=int.from_bytes(payload[5:7], "little"),
        generation=int.from_bytes(payload[7:11], "little"),
    )


__all__ = [
    "ApiOpcode",
    "ApiProtocol",
    "ApiVerb",
    "DeviceRoute",
    "parse_device_route_mak_api",
    "parse_device_route_km",
]
