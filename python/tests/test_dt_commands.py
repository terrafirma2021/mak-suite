import pytest

from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.gamepad import (
    ControllerControl,
    ControllerMaskMode,
    ControllerState,
    Gamepad,
)
from makxd.mouse import Mouse
from makxd.protocol import ApiOpcode, ApiProtocol, ApiVerb
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
        self.api_calls: list[tuple[ApiOpcode, ApiVerb, bytes]] = []
        self.api_protocol = ApiProtocol.MAK_API

    def send_command(self, command: str, **_kwargs):
        self.commands.append(command)
        return None

    def send_api(
        self, km_command: str, _opcode, _verb, _payload=b"", **kwargs
    ):
        if _opcode == ApiOpcode.DEVICE:
            return (
                b"\x04\x00\x00\x00\x00\x08\x00\x11\x00\x00\x00"
                b"\x05\x02\x00\xFF\xFF\xFF\xFF\xFF\xFF\x7F\x00"
            )
        self.api_calls.append((_opcode, _verb, _payload))
        return b""


def test_mouse_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    mouse.press(MouseButton.LEFT)
    mouse.release(MouseButton.LEFT, 0)
    mouse.move(12, -7, 16383)
    mouse.scroll(-2, 9)

    assert transport.api_calls == [
        (ApiOpcode.LEFT, ApiVerb.SET, b"\x01"),
        (ApiOpcode.LEFT, ApiVerb.SET, b"\x00\x00\x00"),
        (ApiOpcode.MOVE, ApiVerb.EXEC, b"\x0c\x00\xf9\xff\xff\x3f"),
        (ApiOpcode.WHEEL, ApiVerb.EXEC, b"\xfe\xff\x09\x00"),
    ]


def test_keyboard_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    keyboard = Keyboard(transport)

    keyboard.down(4)
    keyboard.up(4, 0)
    keyboard.init()
    keyboard.init(16383)

    assert transport.api_calls == [
        (ApiOpcode.KEY_DOWN, ApiVerb.EXEC, b"\x04"),
        (ApiOpcode.KEY_UP, ApiVerb.EXEC, b"\x04\x00\x00"),
        (ApiOpcode.KEY_INIT, ApiVerb.EXEC, b""),
        (ApiOpcode.KEY_INIT, ApiVerb.EXEC, b"\xff\x3f"),
    ]


def test_mouse_immediate_mask_commands_and_binary_payloads() -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    mouse.left_mask(True)
    mouse.right_mask(False)
    mouse.middle_mask(True)
    mouse.side1_mask(False)
    mouse.side2_mask(True)
    mouse.move_mask(True, False, True, False)
    mouse.wheel_mask(True, False)

    assert transport.api_calls == [
        (ApiOpcode.LEFT_MASK, ApiVerb.SET, b"\x01"),
        (ApiOpcode.RIGHT_MASK, ApiVerb.SET, b"\x00"),
        (ApiOpcode.MIDDLE_MASK, ApiVerb.SET, b"\x01"),
        (ApiOpcode.SIDE1_MASK, ApiVerb.SET, b"\x00"),
        (ApiOpcode.SIDE2_MASK, ApiVerb.SET, b"\x01"),
        (ApiOpcode.MOVE_MASK, ApiVerb.SET, b"\x01\x00\x01\x00"),
        (ApiOpcode.WHEEL_MASK, ApiVerb.SET, b"\x01\x00"),
    ]


def test_controller_full_single_and_immediate_mask_commands() -> None:
    transport = CommandTransport()
    gamepad = Gamepad(transport)

    gamepad.state(ControllerState(3, 0, 10, 20, -1, 2, -3, 4))
    gamepad.control(ControllerControl.SOUTH, 1)
    gamepad.control(ControllerControl.LEFT_STICK_X, -8, 16383)
    gamepad.mask(ControllerControl.EXTRA_32, ControllerMaskMode.COMPLETE)
    gamepad.mask(ControllerControl.DPAD_UP, ControllerMaskMode.COMPLETE)
    gamepad.mask(ControllerControl.RIGHT_STICK_X, ControllerMaskMode.BOTH)

    assert [call[:2] for call in transport.api_calls] == [
        (ApiOpcode.CONTROLLER_STATE, ApiVerb.SET),
        (ApiOpcode.CONTROLLER_CONTROL, ApiVerb.SET),
        (ApiOpcode.CONTROLLER_CONTROL, ApiVerb.SET),
        (ApiOpcode.CONTROLLER_MASK, ApiVerb.SET),
        (ApiOpcode.CONTROLLER_MASK, ApiVerb.SET),
        (ApiOpcode.CONTROLLER_MASK, ApiVerb.SET),
    ]
    assert all(call[2][-4:] == b"\x11\x00\x00\x00" for call in transport.api_calls)


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
        gamepad.control(ControllerControl.DPAD_UP, 1, dt_uframes)

    assert transport.commands == []
