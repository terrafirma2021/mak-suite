import pytest

from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.protocol import ApiOpcode


class FakeTransport:
    def __init__(self):
        self.calls = []

    def send_mak_api(self, opcode, payload=b"", *_args, **kwargs):
        self.calls.append((opcode, payload, kwargs.get("wait_response", True)))
        if opcode == ApiOpcode.KEY_IS_DOWN:
            return b"\x01"
        return b""


def test_keyboard_commands_use_mak_api_payloads():
    transport = FakeTransport()
    keyboard = Keyboard(transport)

    keyboard.down("enter")
    keyboard.up(4)
    keyboard.press("a")
    keyboard.press("a", 10)
    keyboard.press("a", 10, 3)
    keyboard.string('A"B')
    keyboard.init()
    assert keyboard.is_down("a") is True
    keyboard.mask("a", False)
    keyboard.remap("a", "b")

    assert transport.calls == [
        (ApiOpcode.KEY_DOWN, b"\x28", False),
        (ApiOpcode.KEY_UP, b"\x04", False),
        (ApiOpcode.KEY_PRESS, b"\x04", False),
        (ApiOpcode.KEY_PRESS, b"\x04\x0a\x00\x00\x00", False),
        (ApiOpcode.KEY_PRESS, b"\x04\x0a\x00\x00\x00\x03\x00\x00\x00", False),
        (ApiOpcode.KEY_STRING, b'A"B', False),
        (ApiOpcode.KEY_INIT, b"", False),
        (ApiOpcode.KEY_IS_DOWN, b"\x04", True),
        (ApiOpcode.KEY_MASK, b"\x04\x00", False),
        (ApiOpcode.KEY_REMAP, b"\x04\x05", False),
    ]


def test_keyboard_validation():
    keyboard = Keyboard(FakeTransport())

    with pytest.raises(MakxdCommandError):
        keyboard.down(256)
    with pytest.raises(MakxdCommandError):
        keyboard.down("")
    with pytest.raises(MakxdCommandError):
        keyboard.string("é")
    with pytest.raises(MakxdCommandError):
        keyboard.multi_down([])
