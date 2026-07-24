import asyncio
import random
import time
from typing import Optional, Dict, Callable, Union, List
from concurrent.futures import ThreadPoolExecutor
from .mouse import Mouse
from .keyboard import Keyboard, KeyboardKey
from .connection import SerialTransport
from .errors import MakxdConnectionError
from .enums import MouseButton
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
    _BUTTON_LOCK_MAP = {
        MouseButton.LEFT: 'lock_left',
        MouseButton.RIGHT: 'lock_right',
        MouseButton.MIDDLE: 'lock_middle',
        MouseButton.MOUSE4: 'lock_side1',
        MouseButton.MOUSE5: 'lock_side2',
    }
    
    def __init__(self, fallback_com_port: str = "", debug: bool = False, 
                 send_init: bool = True, auto_reconnect: bool = True, 
                 override_port: bool = False) -> None:
        self.transport = SerialTransport(
            fallback_com_port, 
            debug=debug, 
            send_init=send_init,
            auto_reconnect=auto_reconnect,
            override_port=override_port
        )
        self.mouse = Mouse(self.transport)
        self.keyboard = Keyboard(self.transport)
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
    def click(self, button: MouseButton) -> None:
        self._check_connection()
        self.mouse.press(button)
        self.mouse.release(button)

    @maybe_async
    def double_click(self, button: MouseButton) -> None:
        self._check_connection()
        self.mouse.press(button)
        self.mouse.release(button)
        time.sleep(0.001)
        self.mouse.press(button)
        self.mouse.release(button)

    @maybe_async
    def move(self, dx: int, dy: int) -> None:
        self._check_connection()
        self.mouse.move(dx, dy)

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
    def batch_execute(self, actions: List[Callable[[], None]]) -> None:
        """Execute a batch of actions in sequence.
        
        Args:
            actions: List of callable functions to execute in order
            
        Example:
            makxd.batch_execute([
                lambda: makxd.move(50, 0),
                lambda: makxd.click(MouseButton.LEFT),
                lambda: makxd.move(-50, 0),
                lambda: makxd.click(MouseButton.RIGHT)
            ])
        """
        self._check_connection()
        
        for action in actions:
            try:
                action()
            except Exception as e:
                raise RuntimeError(f"Batch execution failed at action: {e}")

    @maybe_async
    def scroll(self, delta: int) -> None:
        self._check_connection()
        self.mouse.scroll(delta)

    @maybe_async
    def move_controls(self, dx: int, dy: int, segments: int,
                      ctrl_x1: int, ctrl_y1: int,
                      ctrl_x2: Optional[int] = None,
                      ctrl_y2: Optional[int] = None) -> None:
        self._check_connection()
        self.mouse.move_controls(dx, dy, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2)

    @maybe_async
    def move_to(self, x: int, y: int, segments: int = 1,
                ctrl_x1: Optional[int] = None, ctrl_y1: Optional[int] = None,
                ctrl_x2: Optional[int] = None, ctrl_y2: Optional[int] = None) -> None:
        self._check_connection()
        self.mouse.move_to(x, y, segments, ctrl_x1, ctrl_y1, ctrl_x2, ctrl_y2)

    @maybe_async
    def mouse_position(self) -> str:
        self._check_connection()
        return self.mouse.position()

    @maybe_async
    def mouse_screen(self, width: Optional[int] = None, height: Optional[int] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.screen(width, height)

    @maybe_async
    def click_count(self, button: MouseButton, count: int, delay_ms: int = 1) -> None:
        self._check_connection()
        self.mouse.click_count(button, count, delay_ms)

    @maybe_async
    def axis_stream(self, mode: Optional[str] = None, period_ms: Optional[int] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.axis_stream(mode, period_ms)

    @maybe_async
    def mouse_stream(self, mode: Optional[str] = None, period_ms: Optional[int] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.mouse_stream(mode, period_ms)

    @maybe_async
    def button_stream(self, mode: Optional[str] = None, period_ms: Optional[int] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.button_stream(mode, period_ms)

    @maybe_async
    def mouse_echo(self, enabled: Optional[bool] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.echo(enabled)

    @maybe_async
    def mouse_baud(self, rate: Optional[int] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.baud(rate)

    @maybe_async
    def mouse_catch(self, button: MouseButton, enabled: Optional[bool] = None) -> Optional[str]:
        self._check_connection()
        return self.mouse.catch(button, enabled)

    @maybe_async
    def press(self, button: MouseButton) -> None:
        self._check_connection()
        self.mouse.press(button)

    @maybe_async
    def release(self, button: MouseButton) -> None:
        self._check_connection()
        self.mouse.release(button)

    @maybe_async
    def keyboard_down(self, key: KeyboardKey) -> None:
        self._check_connection()
        self.keyboard.down(key)

    @maybe_async
    def keyboard_up(self, key: KeyboardKey) -> None:
        self._check_connection()
        self.keyboard.up(key)

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
    def keyboard_init(self) -> None:
        self._check_connection()
        self.keyboard.init()

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
    def move_smooth(self, dx: int, dy: int, segments: int = 10) -> None:
        self._check_connection()
        self.mouse.move_smooth(dx, dy, segments)

    @maybe_async
    def move_bezier(self, dx: int, dy: int, segments: int = 20,
                    ctrl_x: Optional[int] = None, ctrl_y: Optional[int] = None) -> None:
        self._check_connection()
        if ctrl_x is None:
            ctrl_x = dx // 2
        if ctrl_y is None:
            ctrl_y = dy // 2
        self.mouse.move_bezier(dx, dy, segments, ctrl_x, ctrl_y)

    @maybe_async
    def lock(self, target: Union[MouseButton, str]) -> None:
        self._check_connection()
        
        if isinstance(target, MouseButton):
            if target in self._BUTTON_LOCK_MAP:
                getattr(self.mouse, self._BUTTON_LOCK_MAP[target])(True)
            else:
                raise ValueError(f"Unsupported button: {target}")
        elif target.upper() in ['X', 'Y']:
            if target.upper() == 'X':
                self.mouse.lock_x(True)
            else:
                self.mouse.lock_y(True)
        else:
            raise ValueError(f"Invalid lock target: {target}")

    @maybe_async
    def unlock(self, target: Union[MouseButton, str]) -> None:
        self._check_connection()
        
        if isinstance(target, MouseButton):
            if target in self._BUTTON_LOCK_MAP:
                getattr(self.mouse, self._BUTTON_LOCK_MAP[target])(False)
            else:
                raise ValueError(f"Unsupported button: {target}")
        elif target.upper() in ['X', 'Y']:
            if target.upper() == 'X':
                self.mouse.lock_x(False)
            else:
                self.mouse.lock_y(False)
        else:
            raise ValueError(f"Invalid unlock target: {target}")

    @maybe_async
    def lock_direction(self, axis: str, positive: bool, lock: bool = True) -> None:
        self._check_connection()
        self.mouse.lock_direction(axis, positive, lock)

    @maybe_async
    def lock_left(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_left(lock)

    @maybe_async
    def lock_middle(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_middle(lock)

    @maybe_async
    def lock_right(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_right(lock)

    @maybe_async
    def lock_side1(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_side1(lock)

    @maybe_async
    def lock_side2(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_side2(lock)

    @maybe_async
    def lock_x(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_x(lock)

    @maybe_async
    def lock_y(self, lock: bool) -> None:
        self._check_connection()
        self.mouse.lock_y(lock)

    @maybe_async
    def lock_mouse_x(self, lock: bool) -> None:
        self.lock_x(lock)

    @maybe_async
    def lock_mouse_y(self, lock: bool) -> None:
        self.lock_y(lock)

    @maybe_async
    def is_locked(self, button: MouseButton) -> bool:
        self._check_connection()
        return self.mouse.is_locked(button)

    @maybe_async
    def get_all_lock_states(self) -> Dict[str, bool]:
        self._check_connection()
        return self.mouse.get_all_lock_states()

    @maybe_async
    def spoof_serial(self, serial: str) -> None:
        self._check_connection()
        self.mouse.spoof_serial(serial)

    @maybe_async
    def reset_serial(self) -> None:
        self._check_connection()
        self.mouse.reset_serial()

    @maybe_async
    def get_device_info(self) -> Dict[str, str]:
        self._check_connection()
        return self.mouse.get_device_info()

    @maybe_async
    def get_firmware_version(self) -> str:
        self._check_connection()
        return self.mouse.get_firmware_version()

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

    @maybe_async
    def click_human_like(self, button: MouseButton, count: int = 1,
                        profile: str = "normal", jitter: int = 0) -> None:
        self._check_connection()

        timing_profiles = {
            "normal": (60, 120, 100, 180),
            "fast": (30, 60, 50, 100),
            "slow": (100, 180, 150, 300),
            "variable": (40, 200, 80, 250),
            "gaming": (20, 40, 30, 60),
        }

        if profile not in timing_profiles:
            raise ValueError(f"Invalid profile: {profile}")

        min_down, max_down, min_wait, max_wait = timing_profiles[profile]

        for i in range(count):
            if jitter > 0:
                dx = random.randint(-jitter, jitter)
                dy = random.randint(-jitter, jitter)
                self.mouse.move(dx, dy)

            self.mouse.press(button)
            time.sleep(random.uniform(min_down, max_down) / 1000.0)
            self.mouse.release(button)
            
            if i < count - 1:
                time.sleep(random.uniform(min_wait, max_wait) / 1000.0)

    @maybe_async
    def drag(self, start_x: int, start_y: int, end_x: int, end_y: int,
             button: MouseButton = MouseButton.LEFT, duration: float = 1.0) -> None:
        self._check_connection()
        
        # Move to start position
        self.move(start_x, start_y)
        time.sleep(0.02)
        
        # Press button
        self.press(button)
        time.sleep(0.02)
        
        # Smooth move to end position
        segments = max(10, int(duration * 30))
        self.move_smooth(end_x - start_x, end_y - start_y, segments)
        
        # Release button
        time.sleep(0.02)
        self.release(button)

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

    # Legacy async methods for backward compatibility
    async def async_connect(self) -> None:
        """Legacy method - use connect() instead"""
        await self.connect()

    async def async_disconnect(self) -> None:
        """Legacy method - use disconnect() instead"""
        await self.disconnect()

    async def async_click(self, button: MouseButton) -> None:
        """Legacy method - use click() instead"""
        await self.click(button)

    async def async_move(self, dx: int, dy: int) -> None:
        """Legacy method - use move() instead"""
        await self.move(dx, dy)

    async def async_scroll(self, delta: int) -> None:
        """Legacy method - use scroll() instead"""
        await self.scroll(delta)

def create_controller(fallback_com_port: str = "", debug: bool = False, 
                     send_init: bool = True, auto_reconnect: bool = True, 
                     override_port: bool = False) -> MakxdController:
    """Create and connect a controller synchronously"""
    makxd = MakxdController(
        fallback_com_port, 
        debug=debug, 
        send_init=send_init,
        auto_reconnect=auto_reconnect,
        override_port=override_port
    )
    makxd.connect()
    return makxd


async def create_async_controller(fallback_com_port: str = "", debug: bool = False,
                                 send_init: bool = True, auto_reconnect: bool = True, 
                                 override_port: bool = False) -> MakxdController:
    """Create and connect a controller asynchronously"""
    makxd = MakxdController(
        fallback_com_port,
        debug=debug,
        send_init=send_init,
        auto_reconnect=auto_reconnect,
        override_port=override_port
    )
    await makxd.connect()
    return makxd
