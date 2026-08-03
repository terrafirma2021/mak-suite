from typing import Dict, Union
from .enums import MouseButton
from .connection import SerialTransport
from .errors import MakxdCommandError
from .protocol import ApiOpcode
from serial.tools import list_ports
import ctypes
import time

def _validate_dt(dt_uframes: int | None) -> None:
    if dt_uframes is None:
        return
    if not isinstance(dt_uframes, int) or isinstance(dt_uframes, bool):
        raise MakxdCommandError("DT must be an integer")
    if dt_uframes < 0 or dt_uframes > 0x3FFF:
        raise MakxdCommandError("DT must be in the range 0..16383")
    return


class Mouse:
    _BUTTON_OPCODES = {
        MouseButton.LEFT: ApiOpcode.LEFT,
        MouseButton.RIGHT: ApiOpcode.RIGHT,
        MouseButton.MIDDLE: ApiOpcode.MIDDLE,
        MouseButton.MOUSE4: ApiOpcode.SIDE1,
        MouseButton.MOUSE5: ApiOpcode.SIDE2,
    }
    _BUTTON_MASK_OPCODES = {
        MouseButton.LEFT: ApiOpcode.LEFT_MASK,
        MouseButton.RIGHT: ApiOpcode.RIGHT_MASK,
        MouseButton.MIDDLE: ApiOpcode.MIDDLE_MASK,
        MouseButton.MOUSE4: ApiOpcode.SIDE1_MASK,
        MouseButton.MOUSE5: ApiOpcode.SIDE2_MASK,
    }

    def __init__(self, transport: SerialTransport) -> None:
        self.transport = transport

    def _send_button_command(
        self,
        button: MouseButton,
        state: int,
        dt_uframes: int | None = None,
    ) -> None:
        if button not in self._BUTTON_OPCODES:
            raise MakxdCommandError(f"Unsupported button: {button}")

        payload = bytes((state,))
        if dt_uframes is not None:
            _validate_dt(dt_uframes)
            payload += dt_uframes.to_bytes(2, "little")
        self.transport.send_mak_api(
            self._BUTTON_OPCODES[button], payload, wait_response=False
        )

    def press(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._send_button_command(button, 1, dt_uframes)

    def release(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._send_button_command(button, 0, dt_uframes)

    def button_mask(self, button: MouseButton, enabled: bool) -> None:
        if button not in self._BUTTON_MASK_OPCODES:
            raise MakxdCommandError(f"Unsupported button: {button}")
        state = 1 if enabled else 0
        self.transport.send_mak_api(
            self._BUTTON_MASK_OPCODES[button], bytes((state,)),
            wait_response=False,
        )

    def left_mask(self, enabled: bool) -> None:
        self.button_mask(MouseButton.LEFT, enabled)

    def right_mask(self, enabled: bool) -> None:
        self.button_mask(MouseButton.RIGHT, enabled)

    def middle_mask(self, enabled: bool) -> None:
        self.button_mask(MouseButton.MIDDLE, enabled)

    def side1_mask(self, enabled: bool) -> None:
        self.button_mask(MouseButton.MOUSE4, enabled)

    def side2_mask(self, enabled: bool) -> None:
        self.button_mask(MouseButton.MOUSE5, enabled)

    def move_mask(
        self,
        left: bool,
        right: bool,
        down: bool,
        up: bool,
    ) -> None:
        values = tuple(1 if value else 0 for value in (left, right, down, up))
        self.transport.send_mak_api(
            ApiOpcode.MOVE_MASK, bytes(values), wait_response=False
        )

    def wheel_mask(self, down: bool, up: bool) -> None:
        values = (1 if down else 0, 1 if up else 0)
        self.transport.send_mak_api(
            ApiOpcode.WHEEL_MASK, bytes(values), wait_response=False
        )

    def move(self, x: int, y: int, dt_uframes: int | None = None) -> None:
        if not isinstance(x, int) or isinstance(x, bool) or not -32768 <= x <= 32767:
            raise MakxdCommandError("Mouse X must be in the range -32768..32767")
        if not isinstance(y, int) or isinstance(y, bool) or not -32768 <= y <= 32767:
            raise MakxdCommandError("Mouse Y must be in the range -32768..32767")
        payload = x.to_bytes(2, "little", signed=True) + y.to_bytes(
            2, "little", signed=True
        )
        if dt_uframes is not None:
            _validate_dt(dt_uframes)
            payload += dt_uframes.to_bytes(2, "little")
        self.transport.send_mak_api(
            ApiOpcode.MOVE, payload, wait_response=False
        )

    def move_abs(self,
        target: tuple[int, int],
        speed: int = 1,
        wait_ms: int = 2) -> None:

        def get_mouse_speed_multiplier() -> float:
            """Return multiplier to convert pixels to mickeys based on Windows pointer speed."""
            SPI_GETMOUSESPEED = 0x0070
            speed = ctypes.c_uint()
            ctypes.windll.user32.SystemParametersInfoW(
                SPI_GETMOUSESPEED, 0, ctypes.byref(speed), 0
            )
            return speed.value / 10.0
        
        def get_cursor_pos():
            class POINT(ctypes.Structure):
                _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]
            pt = POINT()
            ctypes.windll.user32.GetCursorPos(ctypes.byref(pt))
            return pt.x, pt.y
        
        multiplier = get_mouse_speed_multiplier()
        end_x, end_y = target
        
        # clamp speed to be between 1-14. >15 creates an infinite overflick loop issue.
        speed = max(1, min(speed, 14))
        
        while True:
            cx, cy = get_cursor_pos()
            dx, dy = end_x - cx, end_y - cy

            if abs(dx) <= 1 and abs(dy) <= 1:
                break
            
            move_x = max(-speed, min(speed, int(dx / multiplier)))
            move_y = max(-speed, min(speed, int(dy / multiplier)))

            self.move(move_x, move_y)
            time.sleep(wait_ms / 1000)

    def click(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        if button not in self._BUTTON_OPCODES:
            raise MakxdCommandError(f"Unsupported button: {button}")
        self.press(button, dt_uframes)
        self.release(button, dt_uframes)

    def scroll(self, delta: int, dt_uframes: int | None = None) -> None:
        if (
            not isinstance(delta, int)
            or isinstance(delta, bool)
            or not -32768 <= delta <= 32767
        ):
            raise MakxdCommandError(
                "Mouse wheel delta must be in the range -32768..32767"
            )
        payload = delta.to_bytes(2, "little", signed=True)
        if dt_uframes is not None:
            _validate_dt(dt_uframes)
            payload += dt_uframes.to_bytes(2, "little")
        self.transport.send_mak_api(
            ApiOpcode.WHEEL, payload, wait_response=False
        )

    def get_device_info(self) -> Dict[str, Union[str, bool]]:
        port_name = self.transport.port
        is_connected = self.transport.is_connected()
        
        if not is_connected or not port_name:
            return {
                "port": port_name or "Unknown",
                "description": "Disconnected",
                "vid": "Unknown", 
                "pid": "Unknown",
                "isConnected": False
            }
        
        info = {
            "port": port_name,
            "description": "Connected Device",
            "vid": "Unknown", 
            "pid": "Unknown",
            "isConnected": True
        }
        
        try:
            for port in list_ports.comports():
                if port.device == port_name:
                    info["description"] = port.description or "Connected Device"
                    if port.vid is not None:
                        info["vid"] = f"0x{port.vid:04x}"
                    if port.pid is not None:
                        info["pid"] = f"0x{port.pid:04x}"
                    break
        except Exception:
            pass
        
        return info
