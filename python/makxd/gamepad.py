"""Controller state injection and physical-input masking."""

from dataclasses import dataclass
from enum import IntEnum

from .connection import SerialTransport
from .errors import MakxdCommandError
from .protocol import ApiOpcode, ApiVerb


def _integer(name: str, value: int, minimum: int, maximum: int) -> int:
    if not isinstance(value, int) or isinstance(value, bool):
        raise MakxdCommandError(f"{name} must be an integer")
    if value < minimum or value > maximum:
        raise MakxdCommandError(
            f"{name} must be in the range {minimum}..{maximum}"
        )
    return value


def _enabled(name: str, value: bool | int) -> int:
    if isinstance(value, bool):
        return int(value)
    return _integer(name, value, 0, 1)


def _dt_argument(dt_uframes: int | None) -> str:
    if dt_uframes is None:
        return ""
    return f",{_integer('DT', dt_uframes, 0, 0x3FFF)}"


def _dt_bytes(dt_uframes: int | None) -> bytes:
    if dt_uframes is None:
        return b""
    return _integer("DT", dt_uframes, 0, 0x3FFF).to_bytes(2, "little")


def _u16(value: int) -> bytes:
    return value.to_bytes(2, "little")


def _i16(value: int) -> bytes:
    return value.to_bytes(2, "little", signed=True)


def _u32(value: int) -> bytes:
    return value.to_bytes(4, "little")


@dataclass(frozen=True)
class ControllerState:
    buttons: int
    hat: int
    lt: int
    rt: int
    x: int
    y: int
    rx: int
    ry: int
    z: int
    rz: int


class ControllerButton(IntEnum):
    BUTTON1 = 1
    BUTTON2 = 2
    BUTTON3 = 3
    BUTTON4 = 4
    BUTTON5 = 5
    BUTTON6 = 6
    BUTTON7 = 7
    BUTTON8 = 8
    BUTTON9 = 9
    BUTTON10 = 10
    BUTTON11 = 11
    BUTTON12 = 12
    BUTTON13 = 13
    BUTTON14 = 14
    BUTTON15 = 15
    BUTTON16 = 16
    BUTTON17 = 17
    BUTTON18 = 18
    BUTTON19 = 19
    BUTTON20 = 20
    BUTTON21 = 21
    BUTTON22 = 22
    BUTTON23 = 23
    BUTTON24 = 24
    BUTTON25 = 25
    BUTTON26 = 26
    BUTTON27 = 27
    BUTTON28 = 28
    BUTTON29 = 29
    BUTTON30 = 30
    BUTTON31 = 31
    BUTTON32 = 32


class Gamepad:
    def __init__(self, transport: SerialTransport) -> None:
        self.transport = transport

    def state(
        self,
        buttons: int,
        hat: int,
        lt: int,
        rt: int,
        x: int,
        y: int,
        rx: int,
        ry: int,
        z: int,
        rz: int,
        dt_uframes: int | None = None,
    ) -> None:
        values = ControllerState(
            _integer("buttons", buttons, 0, 0xFFFFFFFF),
            _integer("hat", hat, 0, 8),
            _integer("lt", lt, 0, 0xFFFF),
            _integer("rt", rt, 0, 0xFFFF),
            _integer("x", x, -0x8000, 0x7FFF),
            _integer("y", y, -0x8000, 0x7FFF),
            _integer("rx", rx, -0x8000, 0x7FFF),
            _integer("ry", ry, -0x8000, 0x7FFF),
            _integer("z", z, -0x8000, 0x7FFF),
            _integer("rz", rz, -0x8000, 0x7FFF),
        )
        command_values = (
            values.buttons, values.hat, values.lt, values.rt, values.x,
            values.y, values.rx, values.ry, values.z, values.rz,
        )
        payload = (
            _u32(values.buttons)
            + bytes((values.hat,))
            + _u16(values.lt)
            + _u16(values.rt)
            + _i16(values.x)
            + _i16(values.y)
            + _i16(values.rx)
            + _i16(values.ry)
            + _i16(values.z)
            + _i16(values.rz)
            + _dt_bytes(dt_uframes)
        )
        self.transport.send_api(
            "km.controller(" + ",".join(map(str, command_values))
            + _dt_argument(dt_uframes) + ")",
            ApiOpcode.CONTROLLER_STATE,
            ApiVerb.SET,
            payload,
        )

    def button(
        self,
        button: ControllerButton | int,
        pressed: bool | int | None = None,
        dt_uframes: int | None = None,
    ) -> bool | None:
        button_value = _integer("button", int(button), 1, 32)
        command = f"controller_button{button_value}"
        opcode = ApiOpcode(0x5F + button_value)
        if pressed is None:
            if dt_uframes is not None:
                raise MakxdCommandError("DT is valid only when setting a button")
            response = self.transport.send_api(
                f"km.{command}()",
                opcode,
                ApiVerb.GET,
                b"",
            )
            if isinstance(response, bytes):
                return response == b"\x01"
            return bool(response and response.strip() == "1")
        pressed_value = _enabled("pressed", pressed)
        self.transport.send_api(
            f"km.{command}({pressed_value}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            bytes((pressed_value,)) + _dt_bytes(dt_uframes),
        )
        return None

    def hat_left(
        self, pressed: bool | int | None = None,
        dt_uframes: int | None = None,
    ) -> bool | None:
        return self._hat_direction(
            "left", ApiOpcode.CONTROLLER_HAT_LEFT, pressed, dt_uframes
        )

    def hat_right(
        self, pressed: bool | int | None = None,
        dt_uframes: int | None = None,
    ) -> bool | None:
        return self._hat_direction(
            "right", ApiOpcode.CONTROLLER_HAT_RIGHT, pressed, dt_uframes
        )

    def hat_down(
        self, pressed: bool | int | None = None,
        dt_uframes: int | None = None,
    ) -> bool | None:
        return self._hat_direction(
            "down", ApiOpcode.CONTROLLER_HAT_DOWN, pressed, dt_uframes
        )

    def hat_up(
        self, pressed: bool | int | None = None,
        dt_uframes: int | None = None,
    ) -> bool | None:
        return self._hat_direction(
            "up", ApiOpcode.CONTROLLER_HAT_UP, pressed, dt_uframes
        )

    def left_trigger(self, value: int, dt_uframes: int | None = None) -> None:
        self._single(
            "controller_lt", ApiOpcode.CONTROLLER_LT,
            "lt", value, 0, 0xFFFF, 2, dt_uframes
        )

    def right_trigger(self, value: int, dt_uframes: int | None = None) -> None:
        self._single(
            "controller_rt", ApiOpcode.CONTROLLER_RT,
            "rt", value, 0, 0xFFFF, 2, dt_uframes
        )

    def left_stick(
        self, x: int, y: int, dt_uframes: int | None = None
    ) -> None:
        self._axis_pair(
            "controller_left_stick", ApiOpcode.CONTROLLER_LEFT_STICK,
            "x", x, "y", y, dt_uframes
        )

    def right_stick(
        self, rx: int, ry: int, dt_uframes: int | None = None
    ) -> None:
        self._axis_pair(
            "controller_right_stick", ApiOpcode.CONTROLLER_RIGHT_STICK,
            "rx", rx, "ry", ry, dt_uframes
        )

    def aux(self, z: int, rz: int, dt_uframes: int | None = None) -> None:
        self._axis_pair(
            "controller_aux", ApiOpcode.CONTROLLER_AUX,
            "z", z, "rz", rz, dt_uframes
        )

    def button_mask(
        self,
        button: ControllerButton | int,
        enabled: bool | int,
        dt_uframes: int | None = None,
    ) -> None:
        button_value = _integer("button", int(button), 1, 32)
        enabled_value = _enabled("enabled", enabled)
        opcode = ApiOpcode(0x7F + button_value)
        self.transport.send_api(
            f"km.controller_button{button_value}_mask("
            f"{enabled_value}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            bytes((enabled_value,)) + _dt_bytes(dt_uframes),
        )

    def hat_left_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_hat_left_mask", ApiOpcode.CONTROLLER_HAT_LEFT_MASK,
            "hat left", enabled, dt_uframes
        )

    def hat_right_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_hat_right_mask", ApiOpcode.CONTROLLER_HAT_RIGHT_MASK,
            "hat right", enabled, dt_uframes
        )

    def hat_down_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_hat_down_mask", ApiOpcode.CONTROLLER_HAT_DOWN_MASK,
            "hat down", enabled, dt_uframes
        )

    def hat_up_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_hat_up_mask", ApiOpcode.CONTROLLER_HAT_UP_MASK,
            "hat up", enabled, dt_uframes
        )

    def left_trigger_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_lt_mask", ApiOpcode.CONTROLLER_LT_MASK,
            "lt", enabled, dt_uframes
        )

    def right_trigger_mask(
        self, enabled: bool | int, dt_uframes: int | None = None
    ) -> None:
        self._mask_single(
            "controller_rt_mask", ApiOpcode.CONTROLLER_RT_MASK,
            "rt", enabled, dt_uframes
        )

    def left_stick_mask(
        self,
        left: bool | int,
        right: bool | int,
        down: bool | int,
        up: bool | int,
        dt_uframes: int | None = None,
    ) -> None:
        self._mask_directions(
            "controller_left_stick_mask",
            ApiOpcode.CONTROLLER_LEFT_STICK_MASK,
            ("left", left, "right", right, "down", down, "up", up),
            dt_uframes,
        )

    def right_stick_mask(
        self,
        left: bool | int,
        right: bool | int,
        down: bool | int,
        up: bool | int,
        dt_uframes: int | None = None,
    ) -> None:
        self._mask_directions(
            "controller_right_stick_mask",
            ApiOpcode.CONTROLLER_RIGHT_STICK_MASK,
            ("left", left, "right", right, "down", down, "up", up),
            dt_uframes,
        )

    def aux_mask(
        self,
        z_negative: bool | int,
        z_positive: bool | int,
        rz_negative: bool | int,
        rz_positive: bool | int,
        dt_uframes: int | None = None,
    ) -> None:
        self._mask_directions(
            "controller_aux_mask",
            ApiOpcode.CONTROLLER_AUX_MASK,
            (
                "z negative", z_negative, "z positive", z_positive,
                "rz negative", rz_negative, "rz positive", rz_positive,
            ),
            dt_uframes,
        )

    def _hat_direction(
        self,
        name: str,
        opcode: ApiOpcode,
        pressed: bool | int | None,
        dt_uframes: int | None,
    ) -> bool | None:
        command = f"controller_hat_{name}"
        if pressed is None:
            if dt_uframes is not None:
                raise MakxdCommandError("DT is valid only when setting the hat")
            response = self.transport.send_api(
                f"km.{command}()", opcode, ApiVerb.GET
            )
            if isinstance(response, bytes):
                return response == b"\x01"
            return bool(response and response.strip() == "1")
        pressed_value = _enabled(name, pressed)
        self.transport.send_api(
            f"km.{command}({pressed_value}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            bytes((pressed_value,)) + _dt_bytes(dt_uframes),
        )
        return None

    def _single(
        self,
        command: str,
        opcode: ApiOpcode,
        name: str,
        value: int,
        minimum: int,
        maximum: int,
        width: int,
        dt_uframes: int | None,
    ) -> None:
        value = _integer(name, value, minimum, maximum)
        self.transport.send_api(
            f"km.{command}({value}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            value.to_bytes(width, "little") + _dt_bytes(dt_uframes),
        )

    def _axis_pair(
        self,
        command: str,
        opcode: ApiOpcode,
        first_name: str,
        first: int,
        second_name: str,
        second: int,
        dt_uframes: int | None,
    ) -> None:
        first = _integer(first_name, first, -0x8000, 0x7FFF)
        second = _integer(second_name, second, -0x8000, 0x7FFF)
        self.transport.send_api(
            f"km.{command}({first},{second}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            _i16(first) + _i16(second) + _dt_bytes(dt_uframes),
        )

    def _mask_single(
        self,
        command: str,
        opcode: ApiOpcode,
        name: str,
        enabled: bool | int,
        dt_uframes: int | None,
    ) -> None:
        enabled_value = _enabled(name, enabled)
        self.transport.send_api(
            f"km.{command}({enabled_value}{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            bytes((enabled_value,)) + _dt_bytes(dt_uframes),
        )

    def _mask_directions(
        self,
        command: str,
        opcode: ApiOpcode,
        values: tuple[
            str, bool | int, str, bool | int,
            str, bool | int, str, bool | int,
        ],
        dt_uframes: int | None,
    ) -> None:
        enabled_values = (
            _enabled(values[0], values[1]),
            _enabled(values[2], values[3]),
            _enabled(values[4], values[5]),
            _enabled(values[6], values[7]),
        )
        self.transport.send_api(
            f"km.{command}(" + ",".join(map(str, enabled_values))
            + f"{_dt_argument(dt_uframes)})",
            opcode,
            ApiVerb.SET,
            bytes(enabled_values) + _dt_bytes(dt_uframes),
        )


__all__ = ["ControllerButton", "ControllerState", "Gamepad"]
