from .controller import (
    MakxdController,
    create_controller,
    create_async_controller,
    maybe_async
)
from .enums import MouseButton
from .keyboard import Keyboard, KeyboardKey
from .gamepad import (
    ControllerControl,
    ControllerMaskMode,
    ControllerState,
    ControllerSnapshot,
    Gamepad,
)
from .errors import MakxdConnectionError
from .settings import (DeviceConfiguration, DeviceSettings, SettingsSnapshot, SettingsInfo,
    SettingsSection, SettingsError, ControllerSettings, ControllerBehavior,
    ControllerCurve, ControllerTranslation, ControllerChannel)
from .protocol import ApiOpcode, CONTROLLER_TRIGGER_MAX, DeviceInfo, DeviceKind
from .connection_config import ConnectionConfig, ConnectionMethod, UdpWireMode
from .stream import (StreamKind, StreamFrame, InputChange, StreamRequest,
    StreamFrameDecoder, STREAM_COMMAND, STREAM_EVENT, STREAM_TRIGGER_MAX, decode_input_change)

# Version info
__version__ = "3.0.0"
__author__ = "terrafirma2021"

# Main exports
__all__ = [
    'DeviceConfiguration', 'DeviceSettings', 'SettingsSnapshot', 'SettingsInfo',
    'SettingsSection', 'SettingsError', 'ControllerSettings', 'ControllerBehavior',
    'ControllerCurve', 'ControllerTranslation', 'ControllerChannel',
    'MakxdController',
    'MouseButton',
    'Keyboard',
    'KeyboardKey',
    'ControllerControl',
    'ControllerMaskMode',
    'ControllerState',
    'ControllerSnapshot',
    'CONTROLLER_TRIGGER_MAX',
    'Gamepad',
    'MakxdConnectionError',
    'ApiOpcode',
    'DeviceKind',
    'DeviceInfo',
    'ConnectionConfig',
    'ConnectionMethod',
    'UdpWireMode',
    'create_controller',
    'create_async_controller',
    'maybe_async',
    'StreamKind', 'StreamFrame', 'InputChange', 'StreamRequest', 'StreamFrameDecoder',
    'STREAM_COMMAND', 'STREAM_EVENT', 'STREAM_TRIGGER_MAX', 'decode_input_change',
]
