import asyncio
import time
from typing import Optional, Dict, Callable, List
from concurrent.futures import ThreadPoolExecutor
from .mouse import Mouse
from .keyboard import Keyboard, KeyboardKey
from .gamepad import Gamepad
from .connection import SerialTransport
from .connection_config import ConnectionConfig
from .errors import MakxdConnectionError
from .enums import MouseButton
from .protocol import DeviceInfo
from functools import wraps

def maybe_async(func):
    @wraps(func)
    def wrapper(self, *args, **kwargs):
        try:
            loop = asyncio.get_running_loop()
            async def async_wrapper():
                def execute_sync():
                    return func(self, *args, **kwargs)
                executor = getattr(self, '_executor', None)
                return await loop.run_in_executor(executor, execute_sync)
            return async_wrapper()
        except RuntimeError:
            return func(self, *args, **kwargs)
    
    return wrapper

class MakxdController:
    def __init__(self, com_port: str = "", debug: bool = False,
                 send_init: bool = True, auto_reconnect: bool = True,
                 override_port: bool = False,
                 encryption_enabled: bool = False,
                 encryption_key: str = "",
                 connection: ConnectionConfig | None = None) -> None:
        self.transport = SerialTransport(
            com_port,
            debug=debug, 
            send_init=send_init,
            auto_reconnect=auto_reconnect,
            override_port=override_port,
            encryption_enabled=encryption_enabled,
            encryption_key=encryption_key,
            connection=connection,
        )
        self.mouse = Mouse(self.transport)
        self.keyboard = Keyboard(self.transport)
        self.gamepad = Gamepad(self.transport)
        self._executor = ThreadPoolExecutor(max_workers=1)
        self._connection_callbacks: List[Callable[[bool], None]] = []
        self._connected = False

    def _check_connection(self) -> None:
        if not self._connected:
            raise MakxdConnectionError("Not connected")

    def _notify_connection_change(self, connected: bool) -> None:
        for callback in self._connection_callbacks:
            try:
                callback(connected)
            except Exception:
                pass

    @maybe_async
    def connect(self) -> None:
        self.transport.connect()
        self._connected = True
        self._notify_connection_change(True)

    @maybe_async
    def disconnect(self) -> None:
        self.transport.disconnect()
        self._connected = False
        self._notify_connection_change(False)
        self._executor.shutdown(wait=False)

    @maybe_async
    def is_connected(self) -> bool:
        return self._connected and self.transport.is_connected()

    @maybe_async
    def click(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.press(button, dt_uframes)
        self.mouse.release(button, dt_uframes)

    @maybe_async
    def double_click(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.press(button, dt_uframes)
        self.mouse.release(button, dt_uframes)
        time.sleep(0.001)
        self.mouse.press(button, dt_uframes)
        self.mouse.release(button, dt_uframes)

    @maybe_async
    def move(self, dx: int, dy: int, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.move(dx, dy, dt_uframes)

    @maybe_async
    def move_abs(
        self,
        target: tuple[int, int],
        speed: int = 1,
        wait_ms: int = 2,
        debug: bool = False,
    ) -> None:
        self._check_connection()
        self.mouse.move_abs(target, speed=speed, wait_ms=wait_ms, debug=debug)

        if debug:
            print(f"[DEBUG] Moving mouse to {target} with speed={speed}, wait_ms={wait_ms}")


    @maybe_async
    def scroll(self, delta: int, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.scroll(delta, dt_uframes)

    @maybe_async
    def press(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.press(button, dt_uframes)

    @maybe_async
    def release(self, button: MouseButton, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.mouse.release(button, dt_uframes)

    @maybe_async
    def mouse_button_mask(self, button: MouseButton, enabled: bool) -> None:
        self._check_connection()
        self.mouse.button_mask(button, enabled)

    @maybe_async
    def mouse_move_mask(
        self,
        left: bool,
        right: bool,
        down: bool,
        up: bool,
    ) -> None:
        self._check_connection()
        self.mouse.move_mask(left, right, down, up)

    @maybe_async
    def mouse_wheel_mask(self, down: bool, up: bool) -> None:
        self._check_connection()
        self.mouse.wheel_mask(down, up)

    @maybe_async
    def keyboard_down(self, key: KeyboardKey, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.keyboard.down(key, dt_uframes)

    @maybe_async
    def keyboard_up(self, key: KeyboardKey, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.keyboard.up(key, dt_uframes)

    @maybe_async
    def keyboard_press(
        self,
        key: KeyboardKey,
        hold_ms: Optional[int] = None,
        rand_ms: Optional[int] = None,
    ) -> None:
        self._check_connection()
        self.keyboard.press(key, hold_ms, rand_ms)

    @maybe_async
    def keyboard_string(self, text: str) -> None:
        self._check_connection()
        self.keyboard.string(text)

    @maybe_async
    def keyboard_init(self, dt_uframes: int | None = None) -> None:
        self._check_connection()
        self.keyboard.init(dt_uframes)

    @maybe_async
    def keyboard_is_down(self, key: KeyboardKey) -> bool:
        self._check_connection()
        return self.keyboard.is_down(key)

    @maybe_async
    def keyboard_mask(self, key: KeyboardKey, enable: bool) -> None:
        self._check_connection()
        self.keyboard.mask(key, enable)

    @maybe_async
    def keyboard_remap(self, source: KeyboardKey, target: KeyboardKey) -> None:
        self._check_connection()
        self.keyboard.remap(source, target)

    @maybe_async
    def keyboard_multidown(self, keys: List[KeyboardKey]) -> None:
        self._check_connection()
        self.keyboard.multi_down(keys)

    @maybe_async
    def keyboard_multiup(self, keys: List[KeyboardKey]) -> None:
        self._check_connection()
        self.keyboard.multi_up(keys)

    @maybe_async
    def keyboard_multipress(self, keys: List[KeyboardKey]) -> None:
        self._check_connection()
        self.keyboard.multi_press(keys)

    @maybe_async
    def keyboard_keys(self, enabled: Optional[bool] = None) -> Optional[str]:
        self._check_connection()
        return self.keyboard.keys(enabled)

    @maybe_async
    def get_device_info(self) -> Dict[str, str]:
        self._check_connection()
        return self.mouse.get_device_info()

    @maybe_async
    def device(self) -> DeviceInfo:
        self._check_connection()
        return self.transport.device_info()

    @maybe_async
    def get_button_mask(self) -> int:
        self._check_connection()
        return self.transport.get_button_mask()

    @maybe_async
    def get_button_states(self) -> Dict[str, bool]:
        self._check_connection()
        return self.transport.get_button_states()

    @maybe_async
    def is_pressed(self, button: MouseButton) -> bool:
        self._check_connection()
        return self.transport.get_button_states().get(button.name.lower(), False)

    @maybe_async
    def enable_button_monitoring(self, enable: bool = True) -> None:
        self._check_connection()
        self.transport.enable_button_monitoring(enable)

    @maybe_async
    def set_button_callback(self, callback: Optional[Callable[[MouseButton, bool], None]]) -> None:
        self._check_connection()
        self.transport.set_button_callback(callback)

    @maybe_async
    def on_connection_change(self, callback: Callable[[bool], None]) -> None:
        self._connection_callbacks.append(callback)

    @maybe_async
    def remove_connection_callback(self, callback: Callable[[bool], None]) -> None:
        if callback in self._connection_callbacks:
            self._connection_callbacks.remove(callback)

    # Context managers for both sync and async
    def __enter__(self):
        if not self.is_connected():
            self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.disconnect()

    async def __aenter__(self):
        if not await self.is_connected():
            await self.connect()
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self.disconnect()

def create_controller(com_port: str = "", debug: bool = False,
                     send_init: bool = True, auto_reconnect: bool = True,
                     override_port: bool = False,
                     encryption_enabled: bool = False,
                     encryption_key: str = "",
                     connection: ConnectionConfig | None = None,
                     ) -> MakxdController:
    """Create and connect a controller synchronously"""
    makxd = MakxdController(
        com_port,
        debug=debug, 
        send_init=send_init,
        auto_reconnect=auto_reconnect,
        override_port=override_port,
        encryption_enabled=encryption_enabled,
        encryption_key=encryption_key,
        connection=connection,
    )
    makxd.connect()
    return makxd


async def create_async_controller(com_port: str = "", debug: bool = False,
                                 send_init: bool = True, auto_reconnect: bool = True, 
                                 override_port: bool = False,
                                 encryption_enabled: bool = False,
                                 encryption_key: str = "",
                                 connection: ConnectionConfig | None = None,
                                 ) -> MakxdController:
    """Create and connect a controller asynchronously"""
    makxd = MakxdController(
        com_port,
        debug=debug,
        send_init=send_init,
        auto_reconnect=auto_reconnect,
        override_port=override_port,
        encryption_enabled=encryption_enabled,
        encryption_key=encryption_key,
        connection=connection,
    )
    await makxd.connect()
    return makxd
