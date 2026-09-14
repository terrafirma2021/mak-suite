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

BLE_BATCH_REQUEST_MAGIC = b"MBAT"
BLE_BATCH_RESPONSE_MAGIC = b"MBAR"
BLE_BATCH_VERSION = 1
BLE_BATCH_COMMANDS_MAX = 64
BLE_BATCH_PIPELINE_MAX = 5
BLE_BATCH_RESPONSE_HEADER_BYTES = 11
BLE_BATCH_STATUS_RESPONSE = 0
BLE_BATCH_STATUS_REJECTED = 2


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
            # Unsolicited framed events carry the subscription transaction.
            # They must not remove or require a pending query transaction.
            body = data[9:]
            if len(body) >= 5 and body[:2] == b"\xde\xad" and body[4] == 0x53:
                return body if len(body) == 5 + int.from_bytes(body[2:4], "little") else b""
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
            from bleak import BleakClient, BleakScanner
        except ImportError as error:
            raise RuntimeError(
                "BLE connections require the 'bleak' Python package"
            ) from error
        self._BleakClient = BleakClient
        self._BleakScanner = BleakScanner
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
        self._write_queue = None
        self._writer_task = None
        self._batch_write_bytes = 64
        self._batch_supported = False
        self._batch_id = 1
        self._batch_credits = BLE_BATCH_PIPELINE_MAX
        self._batch_credit_event = None
        self._batch_commands: dict[int, list[int]] = {}
        self._disconnected = threading.Event()
        self.port = f"ble://{config.ble_address or 'auto'}"
        self._wait(self._connect())

    def _wait(self, coroutine):
        future: Future = asyncio.run_coroutine_threadsafe(coroutine, self._loop)
        return future.result(timeout=self._timeout + 5.0)

    async def _connect(self) -> None:
        address = self._config.ble_address
        if not address:
            devices = await self._BleakScanner.discover(
                timeout=self._timeout,
                return_adv=True,
            )
            for found_address, (device, advertisement) in devices.items():
                services = [
                    service.lower()
                    for service in advertisement.service_uuids or []
                ]
                advertised_name = (
                    advertisement.local_name or device.name or ""
                )
                if (
                    BLE_SERVICE_UUID in services
                    or advertised_name == "Wireless"
                ):
                    address = found_address
                    break
            if not address:
                raise RuntimeError("MAKXD BLE device was not found")
        self.port = f"ble://{address}"
        self._disconnected.clear()
        self._client = self._BleakClient(
            address,
            disconnected_callback=lambda _client: self._disconnected.set(),
        )
        await self._client.connect()
        mtu_size = getattr(self._client, "mtu_size", 0)
        if mtu_size and mtu_size < 67:
            await self._client.disconnect()
            raise RuntimeError("MAKXD BLE requires ATT MTU 67 or greater")
        rx_characteristic = self._client.services.get_characteristic(BLE_RX_UUID)
        maximum_write = getattr(
            rx_characteristic,
            "max_write_without_response_size",
            0,
        )
        if mtu_size and maximum_write:
            self._batch_write_bytes = min(mtu_size - 3, maximum_write)
        self._batch_supported = (
            mtu_size >= 80 and self._batch_write_bytes >= 16
        )
        self._write_queue = asyncio.Queue()
        self._batch_credit_event = asyncio.Event()
        self._batch_credit_event.set()
        await self._client.start_notify(BLE_TX_UUID, self._notification)
        self._writer_task = asyncio.create_task(self._writer())
        self._open = True

    def _notification(self, _sender, data) -> None:
        packet = bytes(data)
        if not packet.startswith(BLE_BATCH_RESPONSE_MAGIC):
            normalized = _network_response_normalize(packet, False, True)
            if normalized:
                self._rx.put(normalized)
            return
        if len(packet) < BLE_BATCH_RESPONSE_HEADER_BYTES:
            return
        if packet[4] != BLE_BATCH_VERSION:
            return
        batch_id = int.from_bytes(packet[6:8], "little")
        commands = self._batch_commands.get(batch_id)
        if commands is None:
            return
        first = packet[8]
        count = packet[9]
        total = packet[10]
        if total != len(commands) or first + count > total:
            return
        offset = BLE_BATCH_RESPONSE_HEADER_BYTES
        for index in range(count):
            if offset + 2 > len(packet):
                return
            status = packet[offset]
            response_bytes = packet[offset + 1]
            offset += 2
            if offset + response_bytes > len(packet):
                return
            response = packet[offset:offset + response_bytes]
            offset += response_bytes
            if status == BLE_BATCH_STATUS_RESPONSE and response:
                normalized = _network_response_normalize(
                    response, False, True
                )
                if normalized:
                    self._rx.put(normalized)
            elif status == BLE_BATCH_STATUS_REJECTED:
                normalized = _network_response_normalize(
                    bytes((commands[first + index], 0xFF)), False, True
                )
                self._rx.put(normalized)
        if offset != len(packet):
            return
        if packet[5] & 0x01:
            credits = (packet[5] >> 1) & 0x07
            self._batch_commands.pop(batch_id, None)
            if credits:
                self._batch_credits = credits
                self._batch_credit_event.set()

    @staticmethod
    def _direct_record(data: bytes) -> bytes:
        wire = data[4:] if data[:2] == b"\xDE\xAD" else data
        if not wire or len(wire) > 64:
            raise ValueError("MAKXD BLE command length is invalid")
        return wire

    def _batch_request(self, records: list[bytes]) -> tuple[int, bytes]:
        batch_id = self._batch_id
        self._batch_id = (self._batch_id + 1) & 0xFFFF
        request = bytearray(BLE_BATCH_REQUEST_MAGIC)
        request.extend((BLE_BATCH_VERSION, 0))
        request.extend(batch_id.to_bytes(2, "little"))
        request.append(len(records))
        for record in records:
            request.append(len(record))
            request.extend(record)
        return batch_id, bytes(request)

    async def _batch_credit_take(self) -> None:
        while self._batch_credits == 0:
            self._batch_credit_event.clear()
            await self._batch_credit_event.wait()
        self._batch_credits -= 1
        if self._batch_credits == 0:
            self._batch_credit_event.clear()

    async def _writer(self) -> None:
        carry = None
        stop_after_batch = False
        while True:
            item = carry
            carry = None
            if item is None:
                item = await self._write_queue.get()
            if item is None:
                return
            items = [item]
            records = [self._direct_record(item[0])]
            request_bytes = 9 + 1 + len(records[0])

            # Yield once so commands already being submitted can share this
            # write. This is scheduling, not a timed batching delay.
            await asyncio.sleep(0)
            while len(records) < BLE_BATCH_COMMANDS_MAX:
                try:
                    candidate = self._write_queue.get_nowait()
                except asyncio.QueueEmpty:
                    break
                if candidate is None:
                    stop_after_batch = True
                    break
                candidate_record = self._direct_record(candidate[0])
                candidate_bytes = 1 + len(candidate_record)
                if (
                    not self._batch_supported
                    or request_bytes + candidate_bytes > self._batch_write_bytes
                ):
                    carry = candidate
                    break
                items.append(candidate)
                records.append(candidate_record)
                request_bytes += candidate_bytes

            try:
                if self._batch_supported and len(records) > 1:
                    await self._batch_credit_take()
                    batch_id, request = self._batch_request(records)
                    self._batch_commands[batch_id] = [
                        record[0] for record in records
                    ]
                    await self._client.write_gatt_char(
                        BLE_RX_UUID, request, response=False
                    )
                else:
                    await self._client.write_gatt_char(
                        BLE_RX_UUID,
                        records[0],
                        response=items[0][2],
                    )
                for original, completion, _response_expected in items:
                    if completion is not None and not completion.done():
                        completion.set_result(len(original))
            except Exception as error:
                for _original, completion, _response_expected in items:
                    if completion is not None and not completion.done():
                        completion.set_exception(error)
            if stop_after_batch:
                return

    async def _write_wait(self, data: bytes, response_expected: bool) -> int:
        completion = asyncio.get_running_loop().create_future()
        await self._write_queue.put((bytes(data), completion, response_expected))
        return await completion

    @property
    def is_open(self) -> bool:
        return self._open

    @property
    def in_waiting(self) -> int:
        return self._rx.qsize()

    def write(self, data: bytes) -> int:
        return self._wait(self._write_wait(data, True))

    def write_no_response(self, data: bytes) -> int:
        self._direct_record(data)
        self._loop.call_soon_threadsafe(
            self._write_queue.put_nowait,
            (bytes(data), None, False),
        )
        return len(data)

    def read(self, _size: int) -> bytes:
        if self._disconnected.is_set():
            raise OSError("BLE connection was lost")
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
            async def close_transport():
                await self._write_queue.put(None)
                if self._writer_task is not None:
                    await self._writer_task
                await self._client.stop_notify(BLE_TX_UUID)
                await self._client.disconnect()

            self._wait(close_transport())
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
