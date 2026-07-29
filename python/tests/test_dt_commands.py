import pytest

from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.gamepad import ControllerButton, Gamepad
from makxd.mouse import Mouse
from makxd.protocol import ApiProtocol
from makxd.stream import (
    ControllerStreamState,
    StreamInputRecord,
    StreamKind,
    StreamTiming,
    decode_controller_stream,
)
import struct


class CommandTransport:
    def __init__(self) -> None:
        self.commands: list[str] = []
        self.api_protocol = ApiProtocol.KM

    def send_command(self, command: str, **_kwargs):
        self.commands.append(command)
        return None

    def send_api(
        self, km_command: str, _opcode, _verb, _payload=b"", **kwargs
    ):
        return self.send_command(km_command, **kwargs)


def test_mouse_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    mouse.press(MouseButton.LEFT)
    mouse.release(MouseButton.LEFT, 0)
    mouse.move(12, -7, 16383)
    mouse.scroll(-2, 9)

    assert transport.commands == [
        "km.left(1)",
        "km.left(0,0)",
        "km.move(12,-7,16383)",
        "km.wheel(-2,9)",
    ]


def test_keyboard_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    keyboard = Keyboard(transport)

    keyboard.down(4)
    keyboard.up(4, 0)
    keyboard.init()
    keyboard.init(16383)

    assert transport.commands == [
        "km.down(4)",
        "km.up(4,0)",
        "km.init()",
        "km.init(16383)",
    ]


def test_controller_full_single_mask_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    gamepad = Gamepad(transport)

    gamepad.state(3, 8, 10, 20, -1, 2, -3, 4, -5, 6)
    gamepad.button(ControllerButton.BUTTON7, True, 0)
    gamepad.left_stick(-8, 9, 16383)
    gamepad.button_mask(ControllerButton.BUTTON32, True)
    gamepad.hat_up_mask(True, 0)
    gamepad.right_stick_mask(True, False, True, False, 17)

    assert transport.commands == [
        "km.controller(3,8,10,20,-1,2,-3,4,-5,6)",
        "km.controller_button7(1,0)",
        "km.controller_left_stick(-8,9,16383)",
        "km.controller_button32_mask(1)",
        "km.controller_hat_up_mask(1,0)",
        "km.controller_right_stick_mask(1,0,1,0,17)",
    ]


def test_controller_stream_decode_canonical_tuple() -> None:
    values = struct.pack("<IBHHhhhhhh", 5, 2, 100, 200,
                         -1, 2, -3, 4, -5, 6)
    record = StreamInputRecord(
        StreamKind.CONTROLLER, 9, StreamTiming.from_raw(7), values
    )
    assert decode_controller_stream(record) == ControllerStreamState(
        5, 2, 100, 200, -1, 2, -3, 4, -5, 6
    )


@pytest.mark.parametrize("dt_uframes", [-1, 16384, True, 1.5])
def test_dt_range_is_rejected(dt_uframes) -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    with pytest.raises(MakxdCommandError):
        mouse.press(MouseButton.LEFT, dt_uframes)

    gamepad = Gamepad(transport)
    with pytest.raises(MakxdCommandError):
        gamepad.hat_up(True, dt_uframes)

    assert transport.commands == []
