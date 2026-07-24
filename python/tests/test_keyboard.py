import pytest

from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard


class FakeTransport:
    def __init__(self):
        self.commands = []

    def send_command(self, command, expect_response=False, timeout=None):
        self.commands.append(command)
        if expect_response and command.startswith("km.isdown"):
            return "1"
        if expect_response and command == "km.disable()":
            return "enter"
        return None


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
    keyboard.disable("a", True)
    keyboard.disable_keys(["b", 5])
    assert keyboard.disabled() == "enter"
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
        "km.disable('a',1)",
        "km.disable('b',5)",
        "km.disable()",
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
        keyboard.disable_keys([])
