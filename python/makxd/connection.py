import serial
import threading
import time
from typing import Optional, Dict, Callable
from serial.tools import list_ports
from dataclasses import dataclass
from collections import deque
from concurrent.futures import Future
import logging
import asyncio
from .errors import (
    MakxdCommandError,
    MakxdConnectionError,
    MakxdResponseError,
    MakxdTimeoutError,
)
from .enums import MouseButton
from .protocol import (
    ApiOpcode,
    DeviceInfo,
    parse_device_info,
)
from .transport_encryption import EncryptedFrameDecoder, TransportEncryption
from .connection_config import ConnectionConfig, ConnectionMethod
from .wire_transport import BleWireTransport, UdpWireTransport

logger = logging.getLogger(__name__)

@dataclass
class PendingCommand:
    command_id: int
    command: str
    future: Future
    timestamp: float
    expect_response: bool = True
    timeout: float = 0.1
    transaction_nonce: bytes = b""
    mak_api_opcode: Optional[int] = None

class SerialTransport:

    SUPPORTED_USB_IDS = (
        (0x1A86, 0x55D3),
        (0x1A86, 0x7523),
    )
    BAUD_CANDIDATES = (115_200, 1_000_000, 4_000_000)
    BAUD_OPEN_SETTLE = 0.18
    BAUD_CLOSE_SETTLE = 0.12
    BAUD_PROBE_TIMEOUT = 0.75
    DEFAULT_TIMEOUT = 0.1
    MAX_RECONNECT_ATTEMPTS = 3
    RECONNECT_DELAY = 0.1
    

    BUTTON_MAP = (
        'left', 'right', 'middle', 'mouse4', 'mouse5'
    )
    
    BUTTON_ENUM_MAP = (
        MouseButton.LEFT,
        MouseButton.RIGHT,
        MouseButton.MIDDLE,
        MouseButton.MOUSE4,
        MouseButton.MOUSE5,
    )

    def __init__(self, com_port: str = "", debug: bool = False,
                 send_init: bool = True, auto_reconnect: bool = True,
                 override_port: bool = False,
                 encryption_enabled: bool = False,
                 encryption_key: str = "",
                 connection: ConnectionConfig | None = None) -> None:

        if connection is None:
            connection = ConnectionConfig.com(
                com_port,
                aes128_key=encryption_key if encryption_enabled else b"",
            )
        self.connection = connection.validated()
        self._com_port = self.connection.com_port
        self.debug = debug
        self.send_init = send_init
        self.auto_reconnect = auto_reconnect
        self.override_port = override_port
        self._transport_encryption = TransportEncryption(
            self.connection.encryption_enabled,
            self.connection.aes128_key,
        )
        self._encrypted_frame_decoder = EncryptedFrameDecoder(
            self._transport_encryption,
        )
        
        if not hasattr(SerialTransport, '_thread_counter'):
            SerialTransport._thread_counter = 0
            SerialTransport._thread_map = {}

        # Log version info during initialization
        try:
            from makxd import __version__
            version = __version__
            self._log(f"Makxd version: {version}")
        except ImportError:
            self._log("Makxd version info not available")
        
        self._log(f"Initializing SerialTransport with params: com_port='{com_port}', debug={debug}, send_init={send_init}, auto_reconnect={auto_reconnect}, override_port={override_port}")

        self._is_connected = False
        self._device_info: DeviceInfo | None = None
        self._probed_device_kinds: int | None = None
        self._device_info_lock = threading.Lock()
        self._reconnect_attempts = 0
        self.port: Optional[str] = None
        self.baudrate = self.BAUD_CANDIDATES[0]
        self.serial = None
        

        self._command_counter = 0
        self._pending_commands: Dict[int, PendingCommand] = {}
        self._command_lock = threading.Lock()
        

        self._parse_buffer = bytearray(1024)
        self._buffer_pos = 0
        self._response_queue = deque(maxlen=100)
        self._mak_api_frame_buffer = bytearray()
        

        self._button_callback: Optional[Callable[[MouseButton, bool], None]] = None
        self._last_button_mask = 0
        self._button_states = 0
        

        self._stop_event = threading.Event()
        self._listener_thread: Optional[threading.Thread] = None
                

        self._log("SerialTransport initialization completed")


    def _log(self, message: str, level: str = "INFO") -> None:
        if not self.debug:
            return
            
        timestamp = time.strftime("%H:%M:%S", time.localtime())
        thread_id = threading.get_ident()
        
        # Map thread ID to a simple number
        if thread_id not in SerialTransport._thread_map:
            SerialTransport._thread_counter += 1
            SerialTransport._thread_map[thread_id] = SerialTransport._thread_counter
        
        thread_num = SerialTransport._thread_map[thread_id]
        entry = f"[{timestamp}] [T:{thread_num}] [{level}] {message}"
        print(entry, flush=True)

    def _generate_command_id(self) -> int:
        old_counter = self._command_counter
        self._command_counter = (self._command_counter + 1) & 0x2710
        return self._command_counter

    def find_com_ports(self) -> list[str]:
        self._log("Starting COM port discovery")

        if self.override_port:
            self._log(f"Override port enabled, using: {self._com_port}")
            return [self._com_port] if self._com_port else []

        all_ports = list_ports.comports()
        self._log(f"Found {len(all_ports)} COM ports total")

        for i, port in enumerate(all_ports):
            self._log(f"Port {i}: {port.device} - HWID: {port.hwid}")

        candidates = [
            port.device
            for supported_id in self.SUPPORTED_USB_IDS
            for port in all_ports
            if (port.vid, port.pid) == supported_id
        ]
        if self._com_port and self._com_port not in candidates:
            candidates.append(self._com_port)
        self._log(f"Ordered candidate ports: {candidates}")
        return candidates

    def find_com_port(self) -> Optional[str]:
        candidates = self.find_com_ports()
        return candidates[0] if candidates else None

    def _handle_button_data(self, byte_val: int) -> None:
        if byte_val == self._last_button_mask:
            return

        changed_bits = byte_val ^ self._last_button_mask
        print("\n", end='')
        self._log(f"Button state changed: 0x{self._last_button_mask:02X} -> 0x{byte_val:02X}")

        for bit in range(8):
            if changed_bits & (1 << bit):
                is_pressed = bool(byte_val & (1 << bit))
                button_name = self.BUTTON_MAP[bit] if bit < len(self.BUTTON_MAP) else f"bit{bit}"
            
                self._log(f"Button {button_name}: {'PRESSED' if is_pressed else 'RELEASED'}")
                print(">>> ", end='', flush=True)
                if is_pressed:
                    self._button_states |= (1 << bit)
                else:
                    self._button_states &= ~(1 << bit)
            
                if self._button_callback and bit < len(self.BUTTON_ENUM_MAP):
                    try:
                        self._button_callback(self.BUTTON_ENUM_MAP[bit], is_pressed)
                    except Exception as e:
                        self._log(f"Button callback failed: {e}", "ERROR")

        self._last_button_mask = byte_val

    def _wire_mak_api(
        self,
        opcode: int,
        payload: bytes = b"",
    ) -> tuple[bytes, bytes]:
        record = bytes((opcode,)) + bytes(payload)
        if self._transport_encryption.enabled:
            if self.connection.method is not ConnectionMethod.COM:
                return self._transport_encryption.encode_record(record)
            return self._transport_encryption.encode_command(record)
        if len(payload) > 251:
            raise MakxdCommandError("MAK_API command exceeds the transport limit")
        if self.connection.method is ConnectionMethod.BLE:
            return record, b""
        return (
            b"\xDE\xAD"
            + len(payload).to_bytes(2, "little")
            + bytes((opcode,))
            + bytes(payload),
            b"",
        )

    def _process_mak_api_response(
        self,
        plaintext: bytes,
        transaction_nonce: bytes = b"",
    ) -> None:
        if len(plaintext) < 2:
            self._fail_oldest_pending(
                MakxdResponseError("Invalid MAK_API response")
            )
            return
        opcode = plaintext[0]
        result = bytes(plaintext[1:])
        with self._command_lock:
            matching_id = next(
                (
                    command_id
                    for command_id, pending in self._pending_commands.items()
                    if pending.mak_api_opcode == opcode
                    and (
                        not self._transport_encryption.enabled
                        or pending.transaction_nonce == transaction_nonce
                    )
                ),
                None,
            )
            if matching_id is None:
                return
            pending = self._pending_commands.pop(matching_id)
            if pending.future.done():
                return
            if result == b"\xFF":
                pending.future.set_exception(
                    MakxdResponseError(
                        f"MAK_API opcode 0x{opcode:02X} was rejected"
                    )
                )
                return
            pending.future.set_result(result)

    def _process_mak_api_frames(self, data: bytes) -> None:
        self._mak_api_frame_buffer.extend(data)
        while True:
            marker = self._mak_api_frame_buffer.find(b"\xDE\xAD")
            if marker < 0:
                self._mak_api_frame_buffer[:] = (
                    self._mak_api_frame_buffer[-1:]
                    if self._mak_api_frame_buffer[-1:] == b"\xDE"
                    else b""
                )
                return
            if marker:
                del self._mak_api_frame_buffer[:marker]
            if len(self._mak_api_frame_buffer) < 5:
                return
            payload_length = (
                self._mak_api_frame_buffer[2]
                | (self._mak_api_frame_buffer[3] << 8)
            )
            if payload_length == 0 or payload_length > 251:
                del self._mak_api_frame_buffer[0]
                continue
            frame_length = 5 + payload_length
            if len(self._mak_api_frame_buffer) < frame_length:
                return
            command = self._mak_api_frame_buffer[4]
            payload = bytes(self._mak_api_frame_buffer[5:frame_length])
            del self._mak_api_frame_buffer[:frame_length]
            self._process_mak_api_response(bytes((command,)) + payload)

    def _process_encrypted_frames(self, data: bytes) -> None:
        try:
            decoded_frames = self._encrypted_frame_decoder.feed(data)
        except Exception as error:
            self._fail_oldest_pending(
                MakxdConnectionError(f"Encrypted response rejected: {error}")
            )
            return
        for plaintext, transaction_nonce in decoded_frames:
            self._process_mak_api_response(plaintext, transaction_nonce)

    def _fail_oldest_pending(self, error: Exception) -> None:
        with self._command_lock:
            if not self._pending_commands:
                return
            oldest_id = next(iter(self._pending_commands))
            pending = self._pending_commands.pop(oldest_id)
            if not pending.future.done():
                pending.future.set_exception(error)

    def _cleanup_timed_out_commands(self) -> None:
        if not self._pending_commands:
            return
            
        current_time = time.time()
        
        with self._command_lock:
            timed_out = [
                (cmd_id, pending) 
                for cmd_id, pending in self._pending_commands.items()
                if current_time - pending.timestamp > pending.timeout
            ]

            for cmd_id, pending in timed_out:
                age = current_time - pending.timestamp
                self._log(f"Command '{pending.command}' timed out after {age:.3f}s", "ERROR")
                del self._pending_commands[cmd_id]
                if not pending.future.done():
                    pending.future.set_exception(
                        MakxdTimeoutError(f"Command #{cmd_id} timed out")
                    )

    def _listen(self) -> None:
        self._log("Starting listener thread")
        read_buffer = bytearray(4096)
        button_event_prefix = bytes((0x6B, 0x6D, 0x2E))
        button_event_prefix_matched = 0

        serial_read = self.serial.read
        serial_in_waiting = lambda: self.serial.in_waiting
        is_connected = lambda: self._is_connected
        stop_requested = self._stop_event.is_set

        last_cleanup = time.time()
        cleanup_interval = 0.05
        
        while is_connected() and not stop_requested():
            try:
                bytes_available = serial_in_waiting()
                if not bytes_available:
                    time.sleep(0.001)
                    continue
            
                bytes_read = serial_read(min(bytes_available, 4096))
                if self._transport_encryption.enabled:
                    self._process_encrypted_frames(bytes_read)
                    continue
                if self._mak_api_frame_buffer or bytes_read.startswith(b"\xDE"):
                    self._process_mak_api_frames(bytes_read)
                    continue
                for byte_val in bytes_read:
                    if button_event_prefix_matched == len(button_event_prefix):
                        if byte_val < 0x20:
                            self._handle_button_data(byte_val)
                        button_event_prefix_matched = (
                            1 if byte_val == button_event_prefix[0] else 0
                        )
                        continue
                    if byte_val == button_event_prefix[button_event_prefix_matched]:
                        button_event_prefix_matched += 1
                    else:
                        button_event_prefix_matched = (
                            1 if byte_val == button_event_prefix[0] else 0
                        )
                            
                current_time = time.time()
                if current_time - last_cleanup > cleanup_interval:
                    self._cleanup_timed_out_commands()
                    last_cleanup = current_time
                
            except serial.SerialException as e:
                self._log(f"Serial exception in listener: {e}", "ERROR")
                if self.auto_reconnect:
                    self._attempt_reconnect()
                else:
                    break
            except Exception as e:
                self._log(f"Unexpected exception in listener: {e}", "ERROR")

        self._log("Listener thread ending")

    def _attempt_reconnect(self) -> None:
        self._log(f"Attempting reconnect #{self._reconnect_attempts + 1}/{self.MAX_RECONNECT_ATTEMPTS}")
        
        if self._reconnect_attempts >= self.MAX_RECONNECT_ATTEMPTS:
            self._log("Max reconnect attempts reached, giving up", "ERROR")
            self._is_connected = False
            return
        
        self._reconnect_attempts += 1
        
        try:
            if self.serial and self.serial.is_open:
                self._log("Closing existing serial connection for reconnect")
                self.serial.close()
            
            time.sleep(self.RECONNECT_DELAY)
            
            self._log("Reconnecting with port and baud discovery")
            self._open_detected_port()
            with self._device_info_lock:
                self._device_info = None
            self.device_info()
            
            if (
                self.send_init
                and self.connection.method is ConnectionMethod.COM
            ):
                self._log("Sending init command during reconnect")
                init_bytes, _ = self._wire_mak_api(
                    ApiOpcode.BUTTONS, b"\x01"
                )
                self.serial.write(init_bytes)
                self.serial.flush()
            
            self._reconnect_attempts = 0
            self._log("Reconnect successful")
            
        except Exception as e:
            self._log(f"Reconnect attempt failed: {e}", "ERROR")
            time.sleep(self.RECONNECT_DELAY)

    def _device_probe(self, candidate: serial.Serial) -> int | None:
        candidate.reset_input_buffer()
        command_bytes, expected_nonce = self._wire_mak_api(ApiOpcode.DEVICE)
        candidate.write(command_bytes)
        candidate.flush()
        deadline = time.monotonic() + self.BAUD_PROBE_TIMEOUT
        response = bytearray()
        decoder = EncryptedFrameDecoder(self._transport_encryption)

        while time.monotonic() < deadline:
            waiting = candidate.in_waiting
            data = candidate.read(min(max(waiting, 1), 4096))
            if not data:
                continue
            if self._transport_encryption.enabled:
                try:
                    frames = decoder.feed(data)
                except Exception:
                    return None
                for plaintext, transaction_nonce in frames:
                    if (
                        transaction_nonce == expected_nonce
                        and len(plaintext) == 3
                        and plaintext[0] == int(ApiOpcode.DEVICE)
                        and plaintext[1] == 0x01
                    ):
                        return plaintext[2]
                continue

            response.extend(data)
            marker = response.find(b"\xDE\xAD")
            if marker >= 0 and len(response) >= marker + 7:
                if (
                    response[marker + 2:marker + 4] == b"\x02\x00"
                    and response[marker + 4] == int(ApiOpcode.DEVICE)
                    and response[marker + 5] == 0x01
                ):
                    return response[marker + 6]

        return None

    def _open_detected_baud(self, port_name: str) -> None:
        last_error: Exception | None = None
        for baudrate in self.BAUD_CANDIDATES:
            candidate: serial.Serial | None = None
            try:
                self._log(f"Probing {port_name} at {baudrate} baud")
                candidate = serial.Serial(
                    port_name,
                    baudrate,
                    timeout=0.05,
                    write_timeout=0.25,
                    xonxoff=False,
                    rtscts=False,
                    dsrdtr=False,
                )
                time.sleep(self.BAUD_OPEN_SETTLE)
                probed_kinds = self._device_probe(candidate)
                if probed_kinds is not None:
                    candidate.timeout = 0.001
                    candidate.write_timeout = 0.01
                    self.serial = candidate
                    self._probed_device_kinds = probed_kinds
                    self.baudrate = baudrate
                    self._log(f"Detected MAKXD on {port_name} at {baudrate} baud")
                    return
            except Exception as error:
                last_error = error
            if candidate is not None:
                candidate.close()
            time.sleep(self.BAUD_CLOSE_SETTLE)

        detail = f": {last_error}" if last_error is not None else ""
        raise MakxdConnectionError(
            f"Device identity probe failed at every supported baud{detail}"
        )

    def _open_detected_port(self) -> None:
        if self.connection.method is ConnectionMethod.UDP:
            candidate = UdpWireTransport(self.connection)
            probed_kinds = self._device_probe(candidate)
            if probed_kinds is None:
                candidate.close()
                raise MakxdConnectionError(
                    "UDP device identity probe failed"
                )
            self.serial = candidate
            self._probed_device_kinds = probed_kinds
            self.port = candidate.port
            self.baudrate = 0
            return
        if self.connection.method is ConnectionMethod.BLE:
            candidate = BleWireTransport(self.connection)
            probed_kinds = self._device_probe(candidate)
            if probed_kinds is None:
                candidate.close()
                raise MakxdConnectionError(
                    "BLE device identity probe failed"
                )
            self.serial = candidate
            self._probed_device_kinds = probed_kinds
            self.port = candidate.port
            self.baudrate = 0
            return
        candidates = self.find_com_ports()
        if not candidates:
            raise MakxdConnectionError("Makxd device not found")

        failures = []
        for port_name in candidates:
            try:
                self._open_detected_baud(port_name)
                self.port = port_name
                return
            except MakxdConnectionError as error:
                failures.append(f"{port_name}: {error}")

        raise MakxdConnectionError(
            "No supported MAKXD CH343/CH340 device was found; " +
            "; ".join(failures)
        )

    def connect(self) -> None:
        connection_start = time.time()
        self._log("Starting connection process")
        
        if self._is_connected:
            self._log("Already connected")
            return
        
        try:
            self._open_detected_port()
            self._log(f"Connected to {self.port}")
            
            self._is_connected = True
            self._reconnect_attempts = 0
            
            connection_time = time.time() - connection_start
            self._log(f"Connection established in {connection_time:.3f}s")
            
            self._stop_event.clear()
            self._listener_thread = threading.Thread(
                target=self._listen, 
                daemon=True,
                name="MakxdListener"
            )
            self._listener_thread.start()
            self._log(f"Listener thread started: {self._listener_thread.name}")

            self.device_info()

            if (
                self.send_init
                and self.connection.method is ConnectionMethod.COM
            ):
                self._log("Sending initialization command")
                self.send_mak_api(
                    ApiOpcode.BUTTONS, b"\x01", wait_response=False
                )
            
        except Exception as e:
            self._log(f"Connection failed: {e}", "ERROR")
            self._is_connected = False
            with self._device_info_lock:
                self._device_info = None
                self._probed_device_kinds = None
            self._stop_event.set()
            if self.serial:
                try:
                    self.serial.close()
                except:
                    pass
            raise MakxdConnectionError(f"Failed to connect: {e}")

    def disconnect(self) -> None:
        self._log("Starting disconnection process")
        
        self._is_connected = False
        with self._device_info_lock:
            self._device_info = None
            self._probed_device_kinds = None
        
        if self.send_init:
            self._stop_event.set()
            if self._listener_thread and self._listener_thread.is_alive():
                self._listener_thread.join(timeout=0.1)
                if self._listener_thread.is_alive():
                    self._log("Listener thread did not join within timeout")
                else:
                    self._log("Listener thread stopped")
        
        pending_count = len(self._pending_commands)
        if pending_count > 0:
            self._log(f"Cancelling {pending_count} pending commands")
        
        with self._command_lock:
            for cmd_id, pending in self._pending_commands.items():
                if not pending.future.done():
                    pending.future.cancel()
            self._pending_commands.clear()
        
        if self.serial and self.serial.is_open:
            self._log(f"Closing serial port: {self.serial.port}")
            self.serial.close()
            
        self.serial = None
        self._log("Disconnection completed")

    def send_mak_api(
        self,
        opcode: int | ApiOpcode,
        payload: bytes = b"",
        timeout: float = DEFAULT_TIMEOUT,
        *,
        wait_response: bool = True,
    ) -> bytes:
        if not self._is_connected or not self.serial or not self.serial.is_open:
            raise MakxdConnectionError("Not connected")
        opcode_value = int(opcode)
        command_bytes, transaction_nonce = self._wire_mak_api(
            opcode_value, payload
        )
        if not wait_response:
            write_no_response = getattr(
                self.serial, "write_no_response", self.serial.write
            )
            write_no_response(command_bytes)
            self.serial.flush()
            return b""
        command_id = self._generate_command_id()
        future = Future()
        with self._command_lock:
            self._pending_commands[command_id] = PendingCommand(
                command_id=command_id,
                command=f"mak_api:0x{opcode_value:02X}",
                future=future,
                timestamp=time.time(),
                timeout=timeout,
                transaction_nonce=transaction_nonce,
                mak_api_opcode=opcode_value,
            )
        try:
            self.serial.write(command_bytes)
            self.serial.flush()
            return future.result(timeout=timeout)
        except TimeoutError:
            raise MakxdTimeoutError(
                f"MAK_API opcode 0x{opcode_value:02X} timed out"
            )
        finally:
            with self._command_lock:
                self._pending_commands.pop(command_id, None)

    def device_info(self) -> DeviceInfo:
        with self._device_info_lock:
            if self._device_info is None:
                if self._probed_device_kinds is None:
                    learned = parse_device_info(
                        self.send_mak_api(ApiOpcode.DEVICE, timeout=0.1)
                    )
                else:
                    learned = parse_device_info(
                        bytes((self._probed_device_kinds,))
                    )
                self._device_info = learned
            return self._device_info

    def is_connected(self) -> bool:
        connected = self._is_connected and self.serial is not None and self.serial.is_open
        return connected

    def set_button_callback(self, callback: Optional[Callable[[MouseButton, bool], None]]) -> None:
        self._log(f"Setting button callback: {callback is not None}")
        self._button_callback = callback

    def get_button_states(self) -> Dict[str, bool]:
        states = {
            self.BUTTON_MAP[i]: bool(self._button_states & (1 << i))
            for i in range(5)
        }
        return states

    def get_button_mask(self) -> int:
        return self._last_button_mask

    def enable_button_monitoring(self, enable: bool = True) -> None:
        if self.connection.method is not ConnectionMethod.COM:
            raise MakxdCommandError("API_BUTTONS is available only on COM")
        self._log(f"{'Enabling' if enable else 'Disabling'} button monitoring")
        self.send_mak_api(
            ApiOpcode.BUTTONS,
            bytes((1 if enable else 0,)),
            wait_response=False,
        )

    async def __aenter__(self):
        self._log("Async context manager enter")
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self.connect)
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        self._log("Async context manager exit")
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, self.disconnect)

    def __enter__(self):
        self._log("Sync context manager enter")
        self.connect()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self._log("Sync context manager exit")
        self.disconnect()
