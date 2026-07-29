import pytest

from makxd.enums import MouseButton
from makxd.errors import MakxdCommandError
from makxd.keyboard import Keyboard
from makxd.mouse import Mouse


class CommandTransport:
    def __init__(self) -> None:
        self.commands: list[str] = []

    def send_command(self, command: str, **_kwargs):
        self.commands.append(command)
        return None


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


@pytest.mark.parametrize("dt_uframes", [-1, 16384, True, 1.5])
def test_dt_range_is_rejected(dt_uframes) -> None:
    transport = CommandTransport()
    mouse = Mouse(transport)

    with pytest.raises(MakxdCommandError):
        mouse.press(MouseButton.LEFT, dt_uframes)

    assert transport.commands == []
