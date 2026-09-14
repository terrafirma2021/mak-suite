import pytest

from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.gamepad import (
    CONTROLLER_TRIGGER_MAX,
    ControllerControl,
    ControllerMaskMode,
    ControllerState,
    Gamepad,
)
from makxd.mouse import Mouse
from makxd.protocol import ApiOpcode, DeviceInfo, DeviceKind
from makxd.controller import MakxdController
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


def test_mouse_commands_have_exact_payloads() -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    mouse.press(MouseButton.LEFT)
    mouse.release(MouseButton.LEFT)
    mouse.move(12, -7)
    mouse.scroll(-2)

    assert transport.api_calls == [
        (ApiOpcode.LEFT, b"\x01", False),
        (ApiOpcode.LEFT, b"\x00", False),
        (ApiOpcode.MOVE, b"\x0c\x00\xf9\xff", False),
        (ApiOpcode.WHEEL, b"\xfe\xff", False),
    ]


def test_keyboard_commands_have_exact_payloads() -> None:
    transport = CommandTransport()
    keyboard = Keyboard(transport)

    keyboard.down(4)
    keyboard.up(4)
    keyboard.init()

    assert transport.api_calls == [
        (ApiOpcode.KEY_DOWN, b"\x04", False),
        (ApiOpcode.KEY_UP, b"\x04", False),
        (ApiOpcode.KEY_INIT, b"", False),
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
    gamepad.stream(True)
    gamepad.stream(False)
    gamepad.mask(ControllerControl.EXTRA_32, ControllerMaskMode.COMPLETE)
    gamepad.mask(ControllerControl.DPAD_UP, ControllerMaskMode.COMPLETE)
    gamepad.mask(ControllerControl.RIGHT_STICK_X, ControllerMaskMode.BOTH)

    assert [call[0] for call in transport.api_calls] == [
        ApiOpcode.CONTROLLER_STATE,
        ApiOpcode.CONTROLLER_STREAM,
        ApiOpcode.CONTROLLER_STREAM,
        ApiOpcode.CONTROLLER_MASK,
        ApiOpcode.CONTROLLER_MASK,
        ApiOpcode.CONTROLLER_MASK,
    ]
    assert all(call[2] is False for call in transport.api_calls)
    assert len(transport.api_calls[0][1]) == 20
    assert transport.api_calls[1][1] == b"\x01"
    assert len(transport.api_calls[3][1]) == 2
    assert transport.device_queries == 0


def test_controller_trigger_contract_is_10_bit_and_sticks_i16():
    transport = CommandTransport()
    gamepad = Gamepad(transport)
    gamepad.state(ControllerState(left_trigger=1023, right_trigger=1023,
                                  left_stick_x=-32768, right_stick_y=32767))
    assert transport.api_calls[-1][1][8:12] == b"\xff\x03" * 2
    with pytest.raises(MakxdCommandError):
        gamepad.state(ControllerState(left_trigger=1024))
    with pytest.raises(MakxdCommandError):
        gamepad.stream("south")


@pytest.mark.parametrize("dt_uframes", [0, 8, 16383])
@pytest.mark.parametrize("kind,method,args", [
    (Mouse, "press", (MouseButton.LEFT,)),
    (Mouse, "release", (MouseButton.LEFT,)),
    (Mouse, "move", (12, -7)),
    (Mouse, "scroll", (-2,)),
    (Mouse, "click", (MouseButton.LEFT,)),
    (Keyboard, "down", (4,)),
    (Keyboard, "up", (4,)),
    (Keyboard, "init", ()),
    (Gamepad, "stream", (True,)),
    (Gamepad, "state", (ControllerState(),)),
])
def test_removed_dt_argument_is_rejected_before_sending(kind, method, args, dt_uframes):
    transport = CommandTransport()
    command = getattr(kind(transport), method)
    with pytest.raises(TypeError):
        command(*args, dt_uframes)
    with pytest.raises(TypeError):
        command(*args, dt_uframes=dt_uframes)
    assert transport.api_calls == []
