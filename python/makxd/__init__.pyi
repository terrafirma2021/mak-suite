from typing import List, Optional, Sequence, Union
from .controller import MakxdController
from .keyboard import Keyboard, KeyboardKey
from .gamepad import (
    ControllerControl, ControllerFamily, ControllerMaskMode,
    ControllerProtocol, ControllerState, Gamepad,
)
from .protocol import ApiOpcode, ApiProtocol, ApiVerb, DeviceRoute
from .connection_config import ConnectionConfig, ConnectionMethod, UdpWireMode
from .stream import (
    StreamKind, StreamOperation, StreamTiming, StreamFrame,
    StreamControl, StreamInputRecord, ControllerStreamState, StreamRequest,
    StreamFrameDecoder, decode_stream_control, decode_stream_input_record,
    decode_controller_stream, STREAM_MASK_MOUSE,
    STREAM_MASK_KEYBOARD, STREAM_MASK_CONTROLLER, STREAM_MASK_ALL,
    STREAM_COMMAND_INPUT, STREAM_MAX_BODY_BYTES,
    STREAM_MAX_PAYLOAD_BYTES,
)

KeyboardKey = Union[int, str]

class Keyboard:
    def down(self, key: KeyboardKey, dt_uframes: Optional[int] = ...) -> None: ...
    def up(self, key: KeyboardKey, dt_uframes: Optional[int] = ...) -> None: ...
    def press(self, key: KeyboardKey, hold_ms: Optional[int] = ..., rand_ms: Optional[int] = ...) -> None: ...
    def string(self, text: str) -> None: ...
    def init(self, dt_uframes: Optional[int] = ...) -> None: ...
    def is_down(self, key: KeyboardKey) -> bool: ...
    def mask(self, key: KeyboardKey, enable: bool) -> None: ...
    def remap(self, source: KeyboardKey, target: KeyboardKey) -> None: ...
    def multi_down(self, keys: Sequence[KeyboardKey]) -> None: ...
    def multi_up(self, keys: Sequence[KeyboardKey]) -> None: ...
    def multi_press(self, keys: Sequence[KeyboardKey]) -> None: ...
    def keys(self, enabled: Optional[bool] = ...) -> Optional[str]: ...

__version__: str
__all__: List[str]

def create_controller(
    fallback_com_port: str = "", 
    debug: bool = False, 
    send_init: bool = True,
    auto_reconnect: bool = True,
    override_port: bool = False,
    encryption_enabled: bool = False,
    encryption_key: str = "",
    api_protocol: Union[ApiProtocol, str] = ...,
    connection: Optional[ConnectionConfig] = ...,
) -> MakxdController: ...
