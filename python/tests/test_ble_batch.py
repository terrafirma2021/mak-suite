import asyncio
import queue

from makxd.wire_transport import (
    BLE_BATCH_PIPELINE_MAX,
    BLE_RX_UUID,
    BleWireTransport,
)
from makxd.connection import SerialTransport


class FakeBleClient:
    def __init__(self, transport):
        self.transport = transport
        self.writes = []

    async def write_gatt_char(self, uuid, data, response):
        assert uuid == BLE_RX_UUID
        packet = bytes(data)
        self.writes.append((packet, response))
        if packet[:4] != b"MBAT":
            return
        batch_id = packet[6:8]
        count = packet[8]
        offset = 9
        records = []
        for _ in range(count):
            size = packet[offset]
            offset += 1
            records.append(packet[offset:offset + size])
            offset += size
        reply = bytearray(b"MBAR")
        reply.extend((1, 1 | (BLE_BATCH_PIPELINE_MAX << 1)))
        reply.extend(batch_id)
        reply.extend((0, count, count))
        for record in records:
            reply.extend((0, 2, record[0], 1))
        self.transport._notification(None, reply)


def test_queued_ble_commands_batch_without_a_timed_delay():
    async def run():
        transport = BleWireTransport.__new__(BleWireTransport)
        transport._rx = queue.Queue()
        transport._write_queue = asyncio.Queue()
        transport._batch_write_bytes = 514
        transport._batch_supported = True
        transport._batch_id = 1
        transport._batch_credits = BLE_BATCH_PIPELINE_MAX
        transport._batch_credit_event = asyncio.Event()
        transport._batch_credit_event.set()
        transport._batch_commands = {}
        transport._client = FakeBleClient(transport)
        writer = asyncio.create_task(transport._writer())

        records = [
            b"\xDE\xAD\x01\x00" + bytes((opcode, 2))
            for opcode in (0x10, 0x11, 0x12)
        ]
        results = await asyncio.gather(*(
            transport._write_wait(record, True) for record in records
        ))
        await transport._write_queue.put(None)
        await writer

        assert results == [len(record) for record in records]
        assert len(transport._client.writes) == 1
        packet, response = transport._client.writes[0]
        assert packet[:4] == b"MBAT"
        assert packet[8] == 3
        assert response is False
        replies = [transport._rx.get_nowait() for _ in records]
        assert [reply[4] for reply in replies] == [0x10, 0x11, 0x12]
        assert all(reply[-1] == 1 for reply in replies)

    asyncio.run(run())


def test_concurrent_command_ids_do_not_reuse_pending_entries():
    transport = SerialTransport.__new__(SerialTransport)
    transport._command_counter = 0
    transport._pending_commands = {}
    allocated = []
    for _ in range(10000):
        command_id = transport._generate_command_id()
        allocated.append(command_id)
        transport._pending_commands[command_id] = object()
    assert len(set(allocated)) == 10000
