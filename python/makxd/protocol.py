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


@dataclass(frozen=True)
class DeviceRoute:
    route_mask: int
    mouse_uframes: int
    keyboard_uframes: int
    controller_uframes: int
    generation: int = 0
    controller_family: int = 0
    controller_protocol: int = 0
    controller_layout: int = 0
    controller_supported_low: int = 0
    controller_supported_high: int = 0

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
    if len(payload) != 22:
        raise MakxdResponseError(
            f"Invalid MAK_API device response length: {len(payload)}"
        )
    return DeviceRoute(
        route_mask=payload[0],
        mouse_uframes=int.from_bytes(payload[1:3], "little"),
        keyboard_uframes=int.from_bytes(payload[3:5], "little"),
        controller_uframes=int.from_bytes(payload[5:7], "little"),
        generation=int.from_bytes(payload[7:11], "little"),
        controller_family=payload[11],
        controller_protocol=payload[12],
        controller_layout=payload[13],
        controller_supported_low=int.from_bytes(payload[14:18], "little"),
        controller_supported_high=int.from_bytes(payload[18:22], "little"),
    )


__all__ = [
    "ApiOpcode",
    "ApiProtocol",
    "ApiVerb",
    "DeviceRoute",
    "parse_device_route_mak_api",
    "parse_device_route_km",
]
