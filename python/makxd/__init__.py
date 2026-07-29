from .controller import (
    MakxdController,
    create_controller,
    create_async_controller,
    maybe_async
)
from .enums import MouseButton
from .keyboard import Keyboard, KeyboardKey
from .gamepad import ControllerButton, ControllerState, Gamepad
from .errors import MakxdConnectionError
from .protocol import ApiOpcode, ApiProtocol, ApiVerb, DeviceRoute
from .connection_config import ConnectionConfig, ConnectionMethod, UdpWireMode
from .stream import (
    StreamKind,
    StreamOperation,
    StreamTiming,
    StreamFrame,
    StreamControl,
    StreamInputRecord,
    ControllerStreamState,
    StreamRequest,
    StreamFrameDecoder,
    decode_stream_control,
    decode_stream_input_record,
    decode_controller_stream,
    STREAM_MASK_MOUSE,
    STREAM_MASK_KEYBOARD,
    STREAM_MASK_CONTROLLER,
    STREAM_MASK_ALL,
    STREAM_COMMAND_INPUT,
    STREAM_MAX_BODY_BYTES,
    STREAM_MAX_PAYLOAD_BYTES,
)

# Version info
__version__ = "2.4.0"
__author__ = "terrafirma2021"

# Main exports
__all__ = [
    'MakxdController',
    'MouseButton',
    'Keyboard',
    'KeyboardKey',
    'ControllerButton',
    'ControllerState',
    'Gamepad',
    'MakxdConnectionError',
    'ApiOpcode',
    'ApiProtocol',
    'ApiVerb',
    'DeviceRoute',
    'ConnectionConfig',
    'ConnectionMethod',
    'UdpWireMode',
    'create_controller',
    'create_async_controller',
    'maybe_async',
    'StreamKind', 'StreamOperation', 'StreamTiming',
    'StreamFrame', 'StreamControl', 'StreamInputRecord',
    'ControllerStreamState', 'StreamRequest',
    'StreamFrameDecoder', 'decode_stream_control',
    'decode_stream_input_record', 'decode_controller_stream',
    'STREAM_MASK_MOUSE',
    'STREAM_MASK_KEYBOARD', 'STREAM_MASK_CONTROLLER', 'STREAM_MASK_ALL',
    'STREAM_COMMAND_INPUT', 'STREAM_MAX_BODY_BYTES',
    'STREAM_MAX_PAYLOAD_BYTES'
]
