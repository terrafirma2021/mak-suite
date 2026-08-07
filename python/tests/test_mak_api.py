from collections import deque
from concurrent.futures import Future
import time

import pytest

from makxd.connection import PendingCommand, SerialTransport
from makxd.connection_config import ConnectionConfig, UdpWireMode
from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError, MakxdResponseError
from makxd.gamepad import (
    ControllerControl,
    ControllerMaskMode,
    Gamepad,
)
from makxd.keyboard import Keyboard
from makxd.mouse import Mouse
from makxd.protocol import (
    ApiOpcode,
    DeviceInfo,
    DeviceKind,
    parse_device_info,
)
from makxd.wire_transport import UdpWireTransport


class MakApiCaptureTransport:
    def __init__(self) -> None:
        self.calls: list[tuple[int, bytes, bool]] = []
        self.device_queries = 0

    def device_info(self) -> DeviceInfo:
        if self.device_queries == 0:
            self.device_queries = 1
        return DeviceInfo(DeviceKind.DUALSENSE_EDGE)

    def send_mak_api(
        self,
        opcode: int,
        payload: bytes = b"",
        _timeout: float = 0.1,
        **kwargs,
    ) -> bytes:
        self.calls.append(
            (int(opcode), payload, kwargs.get("wait_response", True))
        )
        if int(opcode) == int(ApiOpcode.CONTROLLER_CONTROL) and len(payload) == 1:
            return payload[:1] + b"\x01\x00\x00\x00"
        return b""


class WireCapture:
    def __init__(self) -> None:
        self.is_open = True
        self.writes: list[bytes] = []

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        return None


class NoResponseWireCapture(WireCapture):
    def __init__(self) -> None:
        super().__init__()
        self.no_response_writes: list[bytes] = []

    def write_no_response(self, data: bytes) -> int:
        self.no_response_writes.append(bytes(data))
        return len(data)


class UdpSocketCapture:
    def __init__(self) -> None:
        self.sent: list[bytes] = []
        self.received: deque[bytes] = deque()

    def settimeout(self, _timeout: float) -> None:
        return

    def connect(self, _address: tuple[str, int]) -> None:
        return

    def send(self, data: bytes) -> int:
        self.sent.append(bytes(data))
        return len(data)

    def recv(self, _size: int) -> bytes:
        return self.received.popleft()

    def close(self) -> None:
        return


def test_mak_api_serial_frame_is_command_and_payload() -> None:
    transport = SerialTransport()
    firmware_frame, firmware_nonce = transport._wire_mak_api(
        ApiOpcode.FIRMWARE_VERSION,
    )
    frame, nonce = transport._wire_mak_api(
        ApiOpcode.MOVE,
        b"\x01\x00\xFF\xFF",
    )
    assert firmware_nonce == b""
    assert firmware_frame == b"\xDE\xAD\x00\x00\x04"
    assert nonce == b""
    assert frame == (
        b"\xDE\xAD\x04\x00\x18\x01\x00\xFF\xFF"
    )


def test_mak_api_mouse_keyboard_and_controller_payloads() -> None:
    transport = MakApiCaptureTransport()
    mouse = Mouse(transport)
    keyboard = Keyboard(transport)
    gamepad = Gamepad(transport)

    mouse.press(MouseButton.LEFT, 7)
    mouse.move(-2, 3)
    keyboard.down("enter", 9)
    gamepad.control(ControllerControl.LEFT_STICK_X, -4, 11)
    gamepad.mask(ControllerControl.EXTRA_32, ControllerMaskMode.COMPLETE)

    assert transport.calls == [
        (0x11, b"\x01\x07\x00", False),
        (0x18, b"\xFE\xFF\x03\x00", False),
        (0x20, b"\x28\x09\x00", False),
        (0x41, b"\x0C\xFC\xFF\xFF\xFF\x0B\x00", False),
        (0x51, b"\x36\x01", False),
    ]


def test_mak_api_named_controller_button_hat_and_direction_locks() -> None:
    transport = MakApiCaptureTransport()
    gamepad = Gamepad(transport)

    assert gamepad.control(ControllerControl.SOUTH) == 1
    gamepad.control(ControllerControl.EXTRA_32, 0, 250)
    assert gamepad.control(ControllerControl.DPAD_LEFT) == 1
    gamepad.control(ControllerControl.DPAD_LEFT, 1)
    gamepad.mask(ControllerControl.DPAD_RIGHT, ControllerMaskMode.COMPLETE)
    gamepad.mask(ControllerControl.LEFT_STICK_X, ControllerMaskMode.BOTH)

    assert transport.calls == [
        (0x41, b"\x00", True),
        (0x41, b"\x36\x00\x00\x00\x00\xFA\x00", False),
        (0x41, b"\x06", True),
        (0x41, b"\x06\x01\x00\x00\x00\x00\x00", False),
        (0x51, b"\x07\x01", False),
        (0x51, b"\x0C\x04", False),
    ]
    assert transport.device_queries == 0


def test_mak_api_set_writes_without_registering_a_pending_response() -> None:
    transport = SerialTransport()
    wire = NoResponseWireCapture()
    transport.serial = wire
    transport._is_connected = True

    assert transport.send_mak_api(
        ApiOpcode.LEFT,
        b"\x01",
        wait_response=False,
    ) == b""
    assert wire.writes == []
    assert wire.no_response_writes == [
        b"\xDE\xAD\x01\x00\x11\x01"
    ]
    assert transport._pending_commands == {}


def test_mak_api_get_returns_result_without_success_status() -> None:
    transport = SerialTransport()
    future = Future()
    transport._pending_commands[1] = PendingCommand(
        1, "device", future, time.time(), mak_api_opcode=ApiOpcode.DEVICE
    )

    transport._process_mak_api_response(b"\x02\x43")

    assert future.result() == b"\x43"


def test_mak_api_error_is_the_single_ff_payload() -> None:
    transport = SerialTransport()
    future = Future()
    transport._pending_commands[1] = PendingCommand(
        1, "device", future, time.time(), mak_api_opcode=ApiOpcode.DEVICE
    )

    transport._process_mak_api_response(b"\x02\xFF")

    with pytest.raises(MakxdResponseError):
        future.result()


def test_raw_udp_silent_set_does_not_own_next_get_transaction(
    monkeypatch,
) -> None:
    socket_capture = UdpSocketCapture()
    transactions = iter((b"SET_____", b"GET_____"))
    monkeypatch.setattr(
        "makxd.wire_transport.socket.socket",
        lambda *_args, **_kwargs: socket_capture,
    )
    monkeypatch.setattr(
        "makxd.wire_transport.secrets.token_bytes",
        lambda _size: next(transactions),
    )
    transport = UdpWireTransport(
        ConnectionConfig.udp("192.0.2.1", mode=UdpWireMode.RAW)
    )

    assert transport.write_no_response(b"\xDE\xAD\x01\x00\x11\x01") == 6
    assert transport.write(b"\xDE\xAD\x00\x00\x11") == 5
    assert list(transport._raw_transactions) == [b"GET_____"]

    socket_capture.received.extend(
        (
            b"\x55SET_____\xDE\xAD\x01\x00\x11\xFF",
            b"\x55GET_____\xDE\xAD\x01\x00\x11\x01",
        )
    )
    assert transport.read(64) == b""
    assert transport.read(64) == (
        b"\xDE\xAD\x01\x00\x11\x01"
    )
    assert list(transport._raw_transactions) == []


def test_mak_api_controller_has_one_semantic_opcode_set() -> None:
    assert int(ApiOpcode.CONTROLLER_STATE) == 0x40
    assert int(ApiOpcode.CONTROLLER_CONTROL) == 0x41
    assert int(ApiOpcode.CONTROLLER_MASK) == 0x51


def test_controller_names_are_complete() -> None:
    controls = list(ControllerControl)
    assert len(controls) == 55
    assert controls[0] is ControllerControl.SOUTH
    assert controls[22] is ControllerControl.GRIP_RIGHT
    assert controls[-1] is ControllerControl.EXTRA_32
    assert [control.value for control in controls] == list(range(55))

def test_device_learning_is_one_exact_kind_mask() -> None:
    kinds = DeviceKind.MOUSE | DeviceKind.KEYBOARD | DeviceKind.XBOX_GIP
    info = parse_device_info(bytes((int(kinds),)))
    assert info == DeviceInfo(kinds)
    assert info.mouse
    assert info.keyboard
    assert info.has(DeviceKind.XBOX_GIP)
