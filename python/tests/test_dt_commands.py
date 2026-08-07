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
from makxd.protocol import ApiOpcode, DeviceInfo, DeviceKind
from makxd.controller import MakxdController
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
        self.api_calls: list[tuple[ApiOpcode, bytes, bool]] = []
        self.device_queries = 0

    def device_info(self) -> DeviceInfo:
        if self.device_queries == 0:
            self.device_queries = 1
        return DeviceInfo(DeviceKind.XBOX_GIP)

    def send_mak_api(self, _opcode, _payload=b"", *_args, **kwargs):
        self.api_calls.append(
            (_opcode, _payload, kwargs.get("wait_response", True))
        )
        return b""


def test_firmware_version_uses_mak_api_get() -> None:
    transport = CommandTransport()

    def firmware_version_response(opcode, payload=b"", *_args, **_kwargs):
        transport.api_calls.append((opcode, payload, True))
        return b"\x01\x00\x00\x00"

    transport.send_mak_api = firmware_version_response
    controller = MakxdController.__new__(MakxdController)
    controller.transport = transport
    controller._connected = True

    assert controller.firmware_version() == 1
    assert transport.api_calls == [(ApiOpcode.FIRMWARE_VERSION, b"", True)]


def test_mouse_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    mouse.press(MouseButton.LEFT)
    mouse.release(MouseButton.LEFT, 0)
    mouse.move(12, -7, 16383)
    mouse.scroll(-2, 9)

    assert transport.api_calls == [
        (ApiOpcode.LEFT, b"\x01", False),
        (ApiOpcode.LEFT, b"\x00\x00\x00", False),
        (ApiOpcode.MOVE, b"\x0c\x00\xf9\xff\xff\x3f", False),
        (ApiOpcode.WHEEL, b"\xfe\xff\x09\x00", False),
    ]


def test_keyboard_single_and_explicit_dt_commands() -> None:
    transport = CommandTransport()
    keyboard = Keyboard(transport)

    keyboard.down(4)
    keyboard.up(4, 0)
    keyboard.init()
    keyboard.init(16383)

    assert transport.api_calls == [
        (ApiOpcode.KEY_DOWN, b"\x04", False),
        (ApiOpcode.KEY_UP, b"\x04\x00\x00", False),
        (ApiOpcode.KEY_INIT, b"", False),
        (ApiOpcode.KEY_INIT, b"\xff\x3f", False),
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
        (ApiOpcode.LEFT_MASK, b"\x01", False),
        (ApiOpcode.RIGHT_MASK, b"\x00", False),
        (ApiOpcode.MIDDLE_MASK, b"\x01", False),
        (ApiOpcode.SIDE1_MASK, b"\x00", False),
        (ApiOpcode.SIDE2_MASK, b"\x01", False),
        (ApiOpcode.MOVE_MASK, b"\x01\x00\x01\x00", False),
        (ApiOpcode.WHEEL_MASK, b"\x01\x00", False),
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

    assert [call[0] for call in transport.api_calls] == [
        ApiOpcode.CONTROLLER_STATE,
        ApiOpcode.CONTROLLER_CONTROL,
        ApiOpcode.CONTROLLER_CONTROL,
        ApiOpcode.CONTROLLER_MASK,
        ApiOpcode.CONTROLLER_MASK,
        ApiOpcode.CONTROLLER_MASK,
    ]
    assert all(call[2] is False for call in transport.api_calls)
    assert len(transport.api_calls[0][1]) == 22
    assert len(transport.api_calls[1][1]) == 7
    assert len(transport.api_calls[3][1]) == 2
    assert transport.device_queries == 0


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

    assert transport.api_calls == []
