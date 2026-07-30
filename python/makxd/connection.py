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
from .protocol import ApiOpcode, ApiProtocol, ApiVerb
from .transport_encryption import EncryptedFrameDecoder, TransportEncryption
from .connection_config import ConnectionConfig, ConnectionMethod
from .wire_transport import BleWireTransport, UdpWireTransport

logger = logging.getLogger(__name__)

ASCII_PROMPT = b">>> "


def parse_ascii_response_body(body: bytes) -> str:
    """Return the value portion of one prompt-delimited KM response.

    Echo-enabled action responses contain only the command. GET responses
    contain the echoed query followed by one or more result lines. The prompt
    is removed by the stream collector before this function is called.
    """
    text = body.decode("ascii", "ignore")
    lines = [line.strip() for line in text.replace("\r\n", "\n").replace("\r", "\n").split("\n") if line.strip()]
    if not lines:
        return ""
    if len(lines) == 1:
        return lines[0]
    return "\n".join(lines[1:])

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

@dataclass
class ParsedResponse:
    command_id: Optional[int]
    content: str
    is_button_data: bool = False
    button_mask: Optional[int] = None

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

    def __init__(self, fallback: str = "", debug: bool = False, 
                 send_init: bool = True, auto_reconnect: bool = True, 
                 override_port: bool = False,
                 encryption_enabled: bool = False,
                 encryption_key: str = "",
                 api_protocol: ApiProtocol | str = ApiProtocol.KM,
                 connection: ConnectionConfig | None = None) -> None:

        if connection is None:
            connection = ConnectionConfig.com(
                fallback,
                api_protocol=api_protocol,
                aes128_key=encryption_key if encryption_enabled else b"",
            )
        self.connection = connection.validated()
        self._fallback_com_port = self.connection.com_port
        self.debug = debug
        self.send_init = send_init
        self.auto_reconnect = auto_reconnect
        self.override_port = override_port
        self.api_protocol = self.connection.api_protocol
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
        
        self._log(f"Initializing SerialTransport with params: fallback='{fallback}', debug={debug}, send_init={send_init}, auto_reconnect={auto_reconnect}, override_port={override_port}")

        self._is_connected = False
        self._reconnect_attempts = 0
        self.port: Optional[str] = None
        self.baudrate = self.BAUD_CANDIDATES[0]
        self.serial = None
        

        self._command_counter = 0
        self._pending_commands: Dict[int, PendingCommand] = {}
        self._command_lock = threading.Lock()
        self._km_echo_enabled = False
        

        self._parse_buffer = bytearray(1024)
        self._buffer_pos = 0
        self._response_queue = deque(maxlen=100)
        self._mak_api_frame_buffer = bytearray()
        

        self._button_callback: Optional[Callable[[MouseButton, bool], None]] = None
        self._last_button_mask = 0
        self._button_states = 0
        

        self._stop_event = threading.Event()
        self._listener_thread: Optional[threading.Thread] = None
                

        self._ascii_decode_table = bytes(range(128))
        
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
            self._log(f"Override port enabled, using: {self._fallback_com_port}")
            return [self._fallback_com_port] if self._fallback_com_port else []

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
        if self._fallback_com_port and self._fallback_com_port not in candidates:
            candidates.append(self._fallback_com_port)
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

    def _process_ascii_response(
        self,
        body: bytes,
        transaction_nonce: bytes = b"",
    ) -> None:
        content = parse_ascii_response_body(body)
        if content:
            self._process_pending_commands(content, transaction_nonce)

    def _process_pending_commands(
        self,
        content: str,
        transaction_nonce: bytes = b"",
    ) -> None:
        if not self._pending_commands:
            return

        with self._command_lock:
            if not self._pending_commands:
                return

            if self._transport_encryption.enabled:
                matching_id = next(
                    (
                        command_id
                        for command_id, pending_command in self._pending_commands.items()
                        if pending_command.transaction_nonce == transaction_nonce
                    ),
                    None,
                )
                if matching_id is None:
                    return
                pending = self._pending_commands[matching_id]
                if pending.future.done():
                    return
                result = pending.command if content == pending.command else content
                pending.future.set_result(result)
                del self._pending_commands[matching_id]
                return

            oldest_id = next(iter(self._pending_commands))
            pending = self._pending_commands[oldest_id]

            if pending.future.done():
                return

            result = pending.command if content == pending.command else content
            pending.future.set_result(result)
            del self._pending_commands[oldest_id]

    def _wire_command(self, command: str) -> tuple[bytes, bytes]:
        plaintext = f"{command}\r\n".encode("ascii")
        if self.connection.method is not ConnectionMethod.COM:
            return self._transport_encryption.encode_record(plaintext)
        return self._transport_encryption.encode_command(plaintext)

    def _wire_mak_api(
        self,
        opcode: int,
        verb: int,
        payload: bytes = b"",
    ) -> tuple[bytes, bytes]:
        identified = bytes((0x00, opcode, verb)) + bytes(payload)
        if self._transport_encryption.enabled:
            if self.connection.method is not ConnectionMethod.COM:
                return self._transport_encryption.encode_record(identified)
            return self._transport_encryption.encode_command(identified)
        if len(identified) > 251:
            raise MakxdCommandError("MAK_API command exceeds the transport limit")
        if self.connection.method is not ConnectionMethod.COM:
            return identified, b""
        return (
            b"\xDE\xAD"
            + len(identified).to_bytes(2, "little")
            + b"\x00"
            + identified,
            b"",
        )

    def _process_mak_api_response(
        self,
        plaintext: bytes,
        transaction_nonce: bytes = b"",
    ) -> None:
        if len(plaintext) < 3 or plaintext[0] != 0x00:
            self._fail_oldest_pending(
                MakxdResponseError("Invalid MAK_API response")
            )
            return
        opcode = plaintext[1]
        status = plaintext[2]
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
            if status != 0x01:
                pending.future.set_exception(
                    MakxdResponseError(
                        f"MAK_API opcode 0x{opcode:02X} returned "
                        f"status 0x{status:02X}"
                    )
                )
                return
            pending.future.set_result(bytes(plaintext[3:]))

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
            if command == 0x00:
                self._process_mak_api_response(payload)

    def _process_encrypted_frames(self, data: bytes) -> None:
        try:
            decoded_frames = self._encrypted_frame_decoder.feed(data)
        except Exception as error:
            self._fail_oldest_pending(
                MakxdConnectionError(f"Encrypted response rejected: {error}")
            )
            return
        for plaintext, transaction_nonce in decoded_frames:
            if self.api_protocol is ApiProtocol.MAK_API:
                self._process_mak_api_response(plaintext, transaction_nonce)
                continue
            if not plaintext.endswith(ASCII_PROMPT):
                self._fail_oldest_pending(
                    MakxdConnectionError("Encrypted response is missing the command prompt")
                )
                continue
            self._process_ascii_response(
                plaintext[:-len(ASCII_PROMPT)],
                transaction_nonce,
            )

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
        response_buffer = bytearray()
        button_prefix = bytearray()

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
                if self.api_protocol is ApiProtocol.MAK_API:
                    self._process_mak_api_frames(bytes_read)
                    continue
                for byte_val in bytes_read:
                    if not response_buffer and button_prefix:
                        expected_prefix = b"km."
                        prefix_pos = len(button_prefix)
                        if prefix_pos < len(expected_prefix) and byte_val == expected_prefix[prefix_pos]:
                            button_prefix.append(byte_val)
                            continue
                        if len(button_prefix) == len(expected_prefix) and byte_val < 0x20:
                            self._handle_button_data(byte_val)
                            button_prefix.clear()
                            continue
                        response_buffer.extend(button_prefix)
                        button_prefix.clear()

                    if not response_buffer and byte_val == ord("k"):
                        button_prefix.append(byte_val)
                        continue

                    response_buffer.append(byte_val)
                    if response_buffer.endswith(ASCII_PROMPT):
                        body = bytes(response_buffer[:-len(ASCII_PROMPT)])
                        response_buffer.clear()
                        self._process_ascii_response(body)
                            
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
            
            if (
                self.send_init
                and self.connection.method is ConnectionMethod.COM
            ):
                self._log("Sending init command during reconnect")
                if self.api_protocol is ApiProtocol.MAK_API:
                    init_bytes, _ = self._wire_mak_api(
                        ApiOpcode.BUTTONS, ApiVerb.SET, b"\x01"
                    )
                else:
                    init_bytes, _ = self._wire_command("km.buttons(1)")
                self.serial.write(init_bytes)
                self.serial.flush()
            
            self._reconnect_attempts = 0
            self._log("Reconnect successful")
            
        except Exception as e:
            self._log(f"Reconnect attempt failed: {e}", "ERROR")
            time.sleep(self.RECONNECT_DELAY)

    def _version_probe(self, candidate: serial.Serial) -> bool:
        candidate.reset_input_buffer()
        command_bytes, expected_nonce = self._wire_command("km.version()")
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
                    return False
                for plaintext, transaction_nonce in frames:
                    if transaction_nonce != expected_nonce:
                        continue
                    if not plaintext.endswith(ASCII_PROMPT):
                        return False
                    return parse_ascii_response_body(
                        plaintext[:-len(ASCII_PROMPT)]
                    ) == "km.MAKXD"
                continue

            response.extend(data)
            if response.endswith(ASCII_PROMPT):
                return parse_ascii_response_body(
                    bytes(response[:-len(ASCII_PROMPT)])
                ) == "km.MAKXD"

        return False

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
                if self._version_probe(candidate):
                    candidate.timeout = 0.001
                    candidate.write_timeout = 0.01
                    self.serial = candidate
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
            f"km.version() did not return km.MAKXD at any supported baud{detail}"
        )

    def _open_detected_port(self) -> None:
        if self.connection.method is ConnectionMethod.UDP:
            candidate = UdpWireTransport(self.connection)
            if not self._version_probe(candidate):
                candidate.close()
                raise MakxdConnectionError(
                    "UDP km.version() did not return km.MAKXD"
                )
            self.serial = candidate
            self.port = candidate.port
            self.baudrate = 0
            return
        if self.connection.method is ConnectionMethod.BLE:
            candidate = BleWireTransport(self.connection)
            if not self._version_probe(candidate):
                candidate.close()
                raise MakxdConnectionError(
                    "BLE km.version() did not return km.MAKXD"
                )
            self.serial = candidate
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
            "No supported CH343/CH340 port returned km.MAKXD; " +
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

            if self.api_protocol is ApiProtocol.KM:
                self._km_echo_enabled = (
                    self.send_command("km.echo()", expect_response=True) == "1"
                )

            if (
                self.send_init
                and self.connection.method is ConnectionMethod.COM
            ):
                self._log("Sending initialization command")
                if self.api_protocol is ApiProtocol.MAK_API:
                    self.send_mak_api(
                        ApiOpcode.BUTTONS, ApiVerb.SET, b"\x01"
                    )
                else:
                    self.send_km_action("km.buttons(1)")
            
        except Exception as e:
            self._log(f"Connection failed: {e}", "ERROR")
            if self.serial:
                try:
                    self.serial.close()
                except:
                    pass
            raise MakxdConnectionError(f"Failed to connect: {e}")

    def disconnect(self) -> None:
        self._log("Starting disconnection process")
        
        self._is_connected = False
        
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

    def send_command(self, command: str, expect_response: bool = True,
                timeout: float = DEFAULT_TIMEOUT) -> Optional[str]:
        if self.api_protocol is ApiProtocol.MAK_API:
            raise MakxdCommandError(
                f"{command.split('(', 1)[0]} has no MAK_API mapping"
            )
        command_start = time.time()
        
        if not self._is_connected or not self.serial or not self.serial.is_open:
            raise MakxdConnectionError("Not connected")
        
        if not expect_response:
            command_bytes, _ = self._wire_command(command)
            write_no_response = getattr(
                self.serial, "write_no_response", self.serial.write
            )
            write_no_response(command_bytes)
            self.serial.flush()
            send_time = time.time() - command_start
            self._log(f"Command '{command}' written in {send_time:.5f}s (Makxd echo not awaited)")
            return None
        
        cmd_id = self._generate_command_id()
        future = Future()
        
        command_bytes, transaction_nonce = self._wire_command(command)

        with self._command_lock:
            self._pending_commands[cmd_id] = PendingCommand(
                command_id=cmd_id,
                command=command,
                future=future,
                timestamp=time.time(),
                expect_response=expect_response,
                timeout=timeout,
                transaction_nonce=transaction_nonce,
            )
        
        try:
            self.serial.write(command_bytes)
            self.serial.flush()
            
            result = future.result(timeout=timeout)
            
            total_time = time.time() - command_start
            self._log(f"Command '{command}' completed in {total_time:.5f}s total")
            return result
            
        except TimeoutError:
            total_time = time.time() - command_start
            self._log(f"Command '{command}' timed out after {total_time:.3f}s", "ERROR")
            raise MakxdTimeoutError(f"Command timed out: {command}")
        except Exception as e:
            total_time = time.time() - command_start
            self._log(f"Command '{command}' failed after {total_time:.3f}s: {e}", "ERROR")
            with self._command_lock:
                self._pending_commands.pop(cmd_id, None)
            raise

    def send_mak_api(
        self,
        opcode: int | ApiOpcode,
        verb: int | ApiVerb,
        payload: bytes = b"",
        timeout: float = DEFAULT_TIMEOUT,
    ) -> bytes:
        if not self._is_connected or not self.serial or not self.serial.is_open:
            raise MakxdConnectionError("Not connected")
        opcode_value = int(opcode)
        verb_value = int(verb)
        command_bytes, transaction_nonce = self._wire_mak_api(
            opcode_value, verb_value, payload
        )
        if verb_value == int(ApiVerb.SET):
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

    def send_api(
        self,
        km_command: str,
        opcode: int | ApiOpcode,
        verb: int | ApiVerb,
        payload: bytes = b"",
        expect_response: bool = True,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> Optional[str] | bytes:
        if self.api_protocol is ApiProtocol.MAK_API:
            return self.send_mak_api(opcode, verb, payload, timeout)
        km_expect_response = (
            expect_response
            if int(verb) == int(ApiVerb.GET)
            else self._km_echo_enabled
        )
        return self.send_command(km_command, km_expect_response, timeout)

    @property
    def km_echo_enabled(self) -> bool:
        return self._km_echo_enabled

    def send_km_action(
        self,
        command: str,
        timeout: float = DEFAULT_TIMEOUT,
    ) -> Optional[str]:
        return self.send_command(command, self._km_echo_enabled, timeout)

    def set_km_echo(self, enabled: bool) -> None:
        self.send_command(
            f"km.echo({1 if enabled else 0})",
            expect_response=enabled,
        )
        self._km_echo_enabled = enabled

    async def async_send_command(self, command: str, expect_response: bool = True,
                               timeout: float = DEFAULT_TIMEOUT) -> Optional[str]:
        self._log(f"Async sending command: '{command}'")
        loop = asyncio.get_running_loop()
        return await loop.run_in_executor(
            None, self.send_command, command, expect_response, timeout
        )

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
        cmd = "km.buttons(1)" if enable else "km.buttons(0)"
        self._log(f"{'Enabling' if enable else 'Disabling'} button monitoring")
        self.send_api(
            cmd,
            ApiOpcode.BUTTONS,
            ApiVerb.SET,
            bytes((1 if enable else 0,)),
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
