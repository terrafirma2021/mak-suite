"""Unified semantic controller API."""

from dataclasses import dataclass
from enum import IntEnum

from .connection import SerialTransport
from .errors import MakxdCommandError, MakxdResponseError
from .protocol import (
    ApiOpcode,
    ApiVerb,
    DeviceRoute,
    parse_device_route_mak_api,
)


class ControllerControl(IntEnum):
    SOUTH = 0
    EAST = 1
    WEST = 2
    NORTH = 3
    DPAD_UP = 4
    DPAD_DOWN = 5
    DPAD_LEFT = 6
    DPAD_RIGHT = 7
    LEFT_SHOULDER = 8
    RIGHT_SHOULDER = 9
    LEFT_TRIGGER = 10
    RIGHT_TRIGGER = 11
    LEFT_STICK_X = 12
    LEFT_STICK_Y = 13
    RIGHT_STICK_X = 14
    RIGHT_STICK_Y = 15
    LEFT_STICK_BUTTON = 16
    RIGHT_STICK_BUTTON = 17
    SELECT = 18
    START = 19
    MODE = 20
    GRIP_LEFT = 21
    GRIP_RIGHT = 22
    EXTRA_1 = 23
    EXTRA_2 = 24
    EXTRA_3 = 25
    EXTRA_4 = 26
    EXTRA_5 = 27
    EXTRA_6 = 28
    EXTRA_7 = 29
    EXTRA_8 = 30
    EXTRA_9 = 31
    EXTRA_10 = 32
    EXTRA_11 = 33
    EXTRA_12 = 34
    EXTRA_13 = 35
    EXTRA_14 = 36
    EXTRA_15 = 37
    EXTRA_16 = 38
    EXTRA_17 = 39
    EXTRA_18 = 40
    EXTRA_19 = 41
    EXTRA_20 = 42
    EXTRA_21 = 43
    EXTRA_22 = 44
    EXTRA_23 = 45
    EXTRA_24 = 46
    EXTRA_25 = 47
    EXTRA_26 = 48
    EXTRA_27 = 49
    EXTRA_28 = 50
    EXTRA_29 = 51
    EXTRA_30 = 52
    EXTRA_31 = 53
    EXTRA_32 = 54


class ControllerFamily(IntEnum):
    NONE = 0
    GENERIC_HID = 1
    DS4 = 2
    DUALSENSE = 3
    DS5 = 3
    DUALSENSE_EDGE = 4
    XBOX_GIP = 5
    XBOX_360 = 6
    X_INPUT = 6


class ControllerProtocol(IntEnum):
    NONE = 0
    HID = 1
    GIP = 2
    XINPUT = 3
    X_INPUT = 3


class ControllerMaskMode(IntEnum):
    DISABLED = 0
    COMPLETE = 1
    NEGATIVE = 2
    POSITIVE = 3
    BOTH = 4


@dataclass(frozen=True)
class ControllerState:
    digital_low: int = 0
    digital_high: int = 0
    left_trigger: int = 0
    right_trigger: int = 0
    left_stick_x: int = 0
    left_stick_y: int = 0
    right_stick_x: int = 0
    right_stick_y: int = 0


def _integer(name: str, value: int, minimum: int, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise MakxdCommandError(f"{name} must be an integer")
    if value < minimum or value > maximum:
        raise MakxdCommandError(f"{name} must be in {minimum}..{maximum}")
    return value


def _control(value: ControllerControl | int) -> ControllerControl:
    try:
        return ControllerControl(int(value))
    except (TypeError, ValueError) as error:
        raise MakxdCommandError("invalid controller control") from error


def _control_value(control: ControllerControl, value: int) -> int:
    if control in (ControllerControl.LEFT_TRIGGER, ControllerControl.RIGHT_TRIGGER):
        return _integer("value", value, 0, 0xFFFF)
    if ControllerControl.LEFT_STICK_X <= control <= ControllerControl.RIGHT_STICK_Y:
        return _integer("value", value, -0x8000, 0x7FFF)
    return _integer("value", value, 0, 1)


def _state_validate(state: ControllerState) -> ControllerState:
    return ControllerState(
        _integer("digital_low", state.digital_low, 0, 0xFFFFFFFF),
        _integer("digital_high", state.digital_high, 0, 0xFFFFFFFF),
        _integer("left_trigger", state.left_trigger, 0, 0xFFFF),
        _integer("right_trigger", state.right_trigger, 0, 0xFFFF),
        _integer("left_stick_x", state.left_stick_x, -0x8000, 0x7FFF),
        _integer("left_stick_y", state.left_stick_y, -0x8000, 0x7FFF),
        _integer("right_stick_x", state.right_stick_x, -0x8000, 0x7FFF),
        _integer("right_stick_y", state.right_stick_y, -0x8000, 0x7FFF),
    )


class Gamepad:
    def __init__(self, transport: SerialTransport) -> None:
        self.transport = transport

    def _route(self) -> DeviceRoute:
        response = self.transport.send_api(
            "km.device()", ApiOpcode.DEVICE, ApiVerb.GET, expect_response=True
        )
        if not isinstance(response, bytes):
            raise MakxdResponseError("MAK_API device response is invalid")
        return parse_device_route_mak_api(response)

    def device(self) -> DeviceRoute:
        return self._route()

    def family(self) -> ControllerFamily:
        return ControllerFamily(self._route().controller_family)

    def protocol(self) -> ControllerProtocol:
        return ControllerProtocol(self._route().controller_protocol)

    def supports(self, control: ControllerControl | int) -> bool:
        control_value = int(_control(control))
        route = self._route()
        mask = (
            route.controller_supported_low
            if control_value < 32
            else route.controller_supported_high
        )
        return bool(mask & (1 << (control_value if control_value < 32 else control_value - 32)))

    def control(
        self,
        control: ControllerControl | int,
        value: int | None = None,
        dt_uframes: int = 0,
    ) -> int | None:
        semantic = _control(control)
        name = semantic.name.lower()
        if value is None:
            if dt_uframes != 0:
                raise MakxdCommandError("DT is valid only for SET")
            response = self.transport.send_api(
                f"km.controller({name})",
                ApiOpcode.CONTROLLER_CONTROL,
                ApiVerb.GET,
                bytes((semantic,)),
            )
            if isinstance(response, bytes):
                if len(response) != 9 or response[0] != semantic:
                    raise MakxdResponseError("invalid controller control response")
                return int.from_bytes(response[1:5], "little", signed=True)
            return int((response or "").strip())

        checked = _control_value(semantic, value)
        dt = _integer("dt_uframes", dt_uframes, 0, 0x3FFF)
        generation = self._route().generation
        payload = (
            bytes((semantic,))
            + checked.to_bytes(4, "little", signed=True)
            + dt.to_bytes(2, "little")
            + generation.to_bytes(4, "little")
        )
        km = f"km.controller({name},{checked}" + (f",{dt}" if dt else "") + ")"
        self.transport.send_api(km, ApiOpcode.CONTROLLER_CONTROL, ApiVerb.SET, payload)
        return None

    def mask(
        self,
        control: ControllerControl | int,
        mode: ControllerMaskMode | int,
    ) -> None:
        semantic = _control(control)
        try:
            mask_mode = ControllerMaskMode(int(mode))
        except (TypeError, ValueError) as error:
            raise MakxdCommandError("invalid controller mask mode") from error
        generation = self._route().generation
        payload = bytes((semantic, mask_mode)) + generation.to_bytes(4, "little")
        self.transport.send_api(
            f"km.controller_mask({semantic.name.lower()},{int(mask_mode)})",
            ApiOpcode.CONTROLLER_MASK,
            ApiVerb.SET,
            payload,
        )

    def state(
        self,
        value: ControllerState | None = None,
        dt_uframes: int = 0,
    ) -> ControllerState | None:
        if value is None:
            if dt_uframes != 0:
                raise MakxdCommandError("DT is valid only for SET")
            response = self.transport.send_api(
                "km.controller_state()",
                ApiOpcode.CONTROLLER_STATE,
                ApiVerb.GET,
            )
            if isinstance(response, bytes):
                if len(response) != 24:
                    raise MakxdResponseError("invalid controller state response")
                return ControllerState(
                    int.from_bytes(response[0:4], "little"),
                    int.from_bytes(response[4:8], "little"),
                    int.from_bytes(response[8:10], "little"),
                    int.from_bytes(response[10:12], "little"),
                    int.from_bytes(response[12:14], "little", signed=True),
                    int.from_bytes(response[14:16], "little", signed=True),
                    int.from_bytes(response[16:18], "little", signed=True),
                    int.from_bytes(response[18:20], "little", signed=True),
                )
            parts = [int(part) for part in (response or "").split(",")]
            if len(parts) != 8:
                raise MakxdResponseError("invalid km.controller_state() response")
            return ControllerState(*parts)

        state = _state_validate(value)
        dt = _integer("dt_uframes", dt_uframes, 0, 0x3FFF)
        generation = self._route().generation
        fields = (
            state.digital_low,
            state.digital_high,
            state.left_trigger,
            state.right_trigger,
            state.left_stick_x,
            state.left_stick_y,
            state.right_stick_x,
            state.right_stick_y,
        )
        payload = (
            state.digital_low.to_bytes(4, "little")
            + state.digital_high.to_bytes(4, "little")
            + state.left_trigger.to_bytes(2, "little")
            + state.right_trigger.to_bytes(2, "little")
            + state.left_stick_x.to_bytes(2, "little", signed=True)
            + state.left_stick_y.to_bytes(2, "little", signed=True)
            + state.right_stick_x.to_bytes(2, "little", signed=True)
            + state.right_stick_y.to_bytes(2, "little", signed=True)
            + dt.to_bytes(2, "little")
            + generation.to_bytes(4, "little")
        )
        km = "km.controller_state(" + ",".join(str(field) for field in fields + (dt,)) + ")"
        self.transport.send_api(km, ApiOpcode.CONTROLLER_STATE, ApiVerb.SET, payload)
        return None


__all__ = [
    "ControllerControl",
    "ControllerFamily",
    "ControllerMaskMode",
    "ControllerProtocol",
    "ControllerState",
    "Gamepad",
]
