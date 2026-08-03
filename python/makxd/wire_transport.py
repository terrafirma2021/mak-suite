import asyncio
from collections import deque
import os
import queue
import socket
import threading
import secrets
from concurrent.futures import Future

from .connection_config import ConnectionConfig, UdpWireMode


BLE_SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
BLE_RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
BLE_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def _network_response_normalize(data: bytes, raw: bool, direct: bool) -> bytes:
    if raw and data[:1] == b"\x55":
        if len(data) < 10:
            return b""
        data = data[9:]
    if data[:2] == b"\xDE\xAD":
        return data
    if data and (direct or data[:1] == b"\x03"):
        return b"\xDE\xAD" + (len(data) - 1).to_bytes(2, "little") + data
    return data


class UdpWireTransport:
    def __init__(self, config: ConnectionConfig) -> None:
        self._config = config.validated()
        self._socket = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._socket.settimeout(0.05)
        if config.udp_interface:
            if os.name == "nt":
                if not config.udp_bind_address:
                    raise ValueError(
                        "Windows VLAN/interface selection requires udp_bind_address"
                    )
            else:
                self._socket.setsockopt(
                    socket.SOL_SOCKET,
                    socket.SO_BINDTODEVICE,
                    config.udp_interface.encode("utf-8") + b"\x00",
                )
        if config.udp_bind_address:
            self._socket.bind((config.udp_bind_address, 0))
        self._socket.connect((config.udp_host, config.udp_port))
        self._open = True
        self._raw_transactions: deque[bytes] = deque()
        self._raw_transactions_lock = threading.Lock()
        self.port = f"udp://{config.udp_host}:{config.udp_port}"

    @property
    def is_open(self) -> bool:
        return self._open

    @property
    def in_waiting(self) -> int:
        return 1 if self._open else 0

    def write(self, data: bytes) -> int:
        return self._write(data, True)

    def write_no_response(self, data: bytes) -> int:
        return self._write(data, False)

    def _write(self, data: bytes, response_expected: bool) -> int:
        wire = data
        if (
            self._config.udp_mode is UdpWireMode.RAW
            and data[:1] != b"\x03"
        ):
            transaction = secrets.token_bytes(8)
            if response_expected:
                with self._raw_transactions_lock:
                    self._raw_transactions.append(transaction)
            wire = b"\x55" + transaction + data
        sent = self._socket.send(wire)
        return len(data) if sent == len(wire) else 0

    def read(self, _size: int) -> bytes:
        try:
            data = self._socket.recv(65535)
        except socket.timeout:
            return b""
        if self._config.udp_mode is UdpWireMode.RAW and data[:1] == b"\x55":
            if len(data) < 10:
                return b""
            transaction = data[1:9]
            with self._raw_transactions_lock:
                try:
                    self._raw_transactions.remove(transaction)
                except ValueError:
                    return b""
        return _network_response_normalize(
            data, self._config.udp_mode is UdpWireMode.RAW, False
        )

    def flush(self) -> None:
        return

    def reset_input_buffer(self) -> None:
        self._socket.setblocking(False)
        try:
            while self._socket.recv(65535):
                pass
        except BlockingIOError:
            pass
        finally:
            self._socket.settimeout(0.05)

    def close(self) -> None:
        if self._open:
            self._open = False
            self._socket.close()


class BleWireTransport:
    def __init__(self, config: ConnectionConfig, timeout: float = 10.0) -> None:
        try:
            from bleak import BleakClient
        except ImportError as error:
            raise RuntimeError(
                "BLE connections require the 'bleak' Python package"
            ) from error
        self._BleakClient = BleakClient
        self._config = config.validated()
        self._timeout = timeout
        self._rx: queue.Queue[bytes] = queue.Queue()
        self._loop = asyncio.new_event_loop()
        self._thread = threading.Thread(
            target=self._loop.run_forever,
            daemon=True,
            name="MakxdBle",
        )
        self._thread.start()
        self._client = None
        self._open = False
        self.port = f"ble://{config.ble_address}"
        self._wait(self._connect())

    def _wait(self, coroutine):
        future: Future = asyncio.run_coroutine_threadsafe(coroutine, self._loop)
        return future.result(timeout=self._timeout)

    async def _connect(self) -> None:
        self._client = self._BleakClient(self._config.ble_address)
        await self._client.connect()
        await self._client.start_notify(
            BLE_TX_UUID,
            lambda _sender, data: self._rx.put(
                _network_response_normalize(bytes(data), False, True)
            ),
        )
        mtu_size = getattr(self._client, "mtu_size", 0)
        if mtu_size and mtu_size < 67:
            await self._client.disconnect()
            raise RuntimeError("MAKXD BLE requires ATT MTU 67 or greater")
        self._open = True

    @property
    def is_open(self) -> bool:
        return self._open

    @property
    def in_waiting(self) -> int:
        return self._rx.qsize()

    def write(self, data: bytes) -> int:
        wire = data[4:] if data[:2] == b"\xDE\xAD" else data
        if len(wire) > 64:
            raise ValueError("MAKXD BLE writes are limited to 64 bytes")
        self._wait(
            self._client.write_gatt_char(BLE_RX_UUID, wire, response=True)
        )
        return len(data)

    def read(self, _size: int) -> bytes:
        try:
            return self._rx.get(timeout=0.05)
        except queue.Empty:
            return b""

    def flush(self) -> None:
        return

    def reset_input_buffer(self) -> None:
        while True:
            try:
                self._rx.get_nowait()
            except queue.Empty:
                return

    def close(self) -> None:
        if not self._open:
            return
        self._open = False
        try:
            self._wait(self._client.disconnect())
        finally:
            self._loop.call_soon_threadsafe(self._loop.stop)
            self._thread.join(timeout=1.0)


__all__ = [
    "BLE_RX_UUID",
    "BLE_SERVICE_UUID",
    "BLE_TX_UUID",
    "BleWireTransport",
    "UdpWireTransport",
]
