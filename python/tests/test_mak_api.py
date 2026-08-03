from collections import deque

import pytest

from makxd.connection import SerialTransport
from makxd.connection_config import ConnectionConfig, UdpWireMode
from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError
from makxd.gamepad import (
    ControllerControl,
    ControllerMaskMode,
    Gamepad,
)
from makxd.keyboard import Keyboard
from makxd.mouse import Mouse
from makxd.protocol import (
    ApiOpcode,
    ApiProtocol,
    ApiVerb,
    DeviceRoute,
    parse_device_route_mak_api,
    parse_device_route_km,
)
from makxd.wire_transport import UdpWireTransport


class MakApiCaptureTransport:
    def __init__(self) -> None:
        self.api_protocol = ApiProtocol.MAK_API
        self.calls: list[tuple[int, int, bytes]] = []

    def send_api(
        self,
        _km_command: str,
        opcode: int,
        verb: int,
        payload: bytes = b"",
        **_kwargs,
    ) -> bytes:
        if int(opcode) == int(ApiOpcode.DEVICE):
            return (
                b"\x04\x00\x00\x00\x00\x08\x00\x11\x00\x00\x00"
                b"\x04\x01\x08\xFF\xFF\xFF\xFF\xFF\xFF\x7F\x00"
            )
        self.calls.append((int(opcode), int(verb), payload))
        if int(opcode) == int(ApiOpcode.CONTROLLER_CONTROL) and int(verb) == 0:
            return payload[:1] + b"\x01\x00\x00\x00\x11\x00\x00\x00"
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


def test_mak_api_serial_frame_has_generic_command_and_public_identifier() -> None:
    transport = SerialTransport(api_protocol=ApiProtocol.MAK_API)
    frame, nonce = transport._wire_mak_api(
        ApiOpcode.MOVE,
        ApiVerb.EXEC,
        b"\x01\x00\xFF\xFF",
    )
    assert nonce == b""
    assert frame == (
        b"\xDE\xAD\x07\x00\x00"
        b"\x00\x18\x02\x01\x00\xFF\xFF"
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
        (0x11, 0x01, b"\x01\x07\x00"),
        (0x18, 0x02, b"\xFE\xFF\x03\x00"),
        (0x20, 0x02, b"\x28\x09\x00"),
        (0x41, 0x01, b"\x0C\xFC\xFF\xFF\xFF\x0B\x00\x11\x00\x00\x00"),
        (0x51, 0x01, b"\x36\x01\x11\x00\x00\x00"),
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
        (0x41, 0x00, b"\x00"),
        (0x41, 0x01, b"\x36\x00\x00\x00\x00\xFA\x00\x11\x00\x00\x00"),
        (0x41, 0x00, b"\x06"),
        (0x41, 0x01, b"\x06\x01\x00\x00\x00\x00\x00\x11\x00\x00\x00"),
        (0x51, 0x01, b"\x07\x01\x11\x00\x00\x00"),
        (0x51, 0x01, b"\x0C\x04\x11\x00\x00\x00"),
    ]


def test_mak_api_set_writes_without_registering_a_pending_response() -> None:
    transport = SerialTransport(api_protocol=ApiProtocol.MAK_API)
    wire = NoResponseWireCapture()
    transport.serial = wire
    transport._is_connected = True

    assert transport.send_mak_api(
        ApiOpcode.LEFT,
        ApiVerb.SET,
        b"\x01",
    ) == b""
    assert wire.writes == []
    assert wire.no_response_writes == [
        b"\xDE\xAD\x04\x00\x00\x00\x11\x01\x01"
    ]
    assert transport._pending_commands == {}


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

    assert transport.write_no_response(b"\x00\x11\x01\x01") == 4
    assert transport.write(b"\x00\x11\x00") == 3
    assert list(transport._raw_transactions) == [b"GET_____"]

    socket_capture.received.extend(
        (
            b"\x55SET_____\x00\x11\xFF",
            b"\x55GET_____\x00\x11\x01\x01",
        )
    )
    assert transport.read(64) == b""
    assert transport.read(64) == (
        b"\xDE\xAD\x04\x00\x00\x00\x11\x01\x01"
    )
    assert list(transport._raw_transactions) == []


def test_typed_api_never_falls_back_to_km() -> None:
    transport = SerialTransport(api_protocol=ApiProtocol.KM)
    calls: list[tuple[str, bool]] = []
    transport.send_command = lambda command, expect_response=True, timeout=0: (
        calls.append((command, expect_response)) or None
    )

    with pytest.raises(MakxdCommandError, match="require MAK_API"):
        transport.send_api(
            "km.left(1)", ApiOpcode.LEFT, ApiVerb.SET, b"\x01"
        )
    transport.send_km_action("km.left(1)")

    assert calls == [
        ("km.left(1)", False),
    ]


def test_mak_api_controller_has_one_semantic_opcode_set() -> None:
    assert int(ApiOpcode.CONTROLLER_STATE) == 0x40
    assert int(ApiOpcode.CONTROLLER_CONTROL) == 0x41
    assert int(ApiOpcode.CONTROLLER_MASK) == 0x51


def test_public_defaults_to_mak_api_and_km_remains_explicit() -> None:
    assert ConnectionConfig.com().api_protocol is ApiProtocol.MAK_API
    assert ConnectionConfig.udp("192.0.2.1").api_protocol is ApiProtocol.MAK_API
    assert ConnectionConfig.ble("00:11:22:33:44:55").api_protocol is ApiProtocol.MAK_API
    assert ConnectionConfig.com(api_protocol=ApiProtocol.KM).api_protocol is ApiProtocol.KM


def test_controller_names_are_complete() -> None:
    controls = list(ControllerControl)
    assert len(controls) == 55
    assert controls[0] is ControllerControl.SOUTH
    assert controls[22] is ControllerControl.GRIP_RIGHT
    assert controls[-1] is ControllerControl.EXTRA_32
    assert [control.value for control in controls] == list(range(55))

def test_device_route_parsers_preserve_exact_uframe_cadence() -> None:
    expected = DeviceRoute(0x03, 1, 8, 0, 17, 4, 1, 8, 0xFFFFFFFF, 0x7FFFFF)
    assert parse_device_route_mak_api(
        b"\x03\x01\x00\x08\x00\x00\x00\x11\x00\x00\x00"
        b"\x04\x01\x08\xFF\xFF\xFF\xFF\xFF\xFF\x7F\x00"
    ) == expected
    km = parse_device_route_km("R:MK;M:1uf;K:8uf;C:0uf")
    assert km == DeviceRoute(0x03, 1, 8, 0, 0)
    assert km.mouse_hz == 8000.0
    assert km.keyboard_hz == 1000.0
    assert km.controller_hz == 0.0
