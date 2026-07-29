import pytest

from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.protocol import ApiProtocol


class FakeTransport:
    def __init__(self):
        self.commands = []
        self.api_protocol = ApiProtocol.KM

    def send_command(self, command, expect_response=False, timeout=None):
        self.commands.append(command)
        if expect_response and command.startswith("km.isdown"):
            return "1"
        return None

    def send_api(
        self, km_command, _opcode, _verb, _payload=b"",
        expect_response=True, timeout=None
    ):
        return self.send_command(km_command, expect_response, timeout)


def test_keyboard_commands_match_cpp_contract():
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

    assert transport.commands == [
        "km.down('enter')",
        "km.up(4)",
        "km.press('a')",
        "km.press('a',10)",
        "km.press('a',10,3)",
        'km.string("A\\\"B")',
        "km.init()",
        "km.isdown('a')",
        "km.mask('a',0)",
        "km.remap('a','b')",
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
