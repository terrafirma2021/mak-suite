from concurrent.futures import Future
import time
import pytest
from makxd.connection import SerialTransport, PendingCommand
from makxd.stream import StreamKind, StreamFrame, StreamFrameDecoder, StreamRequest, decode_input_change

def frame(payload, opcode=0x53):
    return b"\xde\xad" + len(payload).to_bytes(2, "little") + bytes([opcode]) + bytes(payload)

def test_fragmented_mixed_events_and_reply():
    t = SerialTransport()
    future = Future()
    t._pending_commands[1] = PendingCommand(1, "stream", future, time.time(), mak_api_opcode=0x52)
    seen = []
    t.set_input_callback(seen.append)
    data = frame([1,31,1]) + frame([3,10,255,3]) + frame([2,224,0])
    for b in data:
        t._process_mak_api_frames(bytes([b]))
    assert not future.done()
    t._process_mak_api_frames(frame([1], 0x52))
    assert future.result() == b"\x01"
    assert [(e.kind,e.control,e.value) for e in seen] == [(1,31,1),(3,10,1023),(2,224,0)]
    assert [t.read_input_change(0) for _ in range(3)] == seen
    assert t.read_input_change(0) is None

def test_event_without_pending_and_framed_authenticated_body():
    t = SerialTransport()
    t._process_mak_api_response(frame([3,11,0,2]), transaction_nonce=b"nonce1234567")
    assert t.read_input_change(0).value == 512
    t._process_mak_api_frames(frame([3,255,255]))
    assert t.read_input_change(0).overflow

@pytest.mark.parametrize("payload", [[3,10,0,4],[3,12,1],[3,10,1],[1,32,1],[2,4,2],[4,0,1],[3,55,1],[1,0,1,0]])
def test_invalid_event(payload):
    assert decode_input_change(StreamFrame(0x53, bytes(payload))) is None

def test_requests_and_decoder():
    assert StreamRequest.controller(False).encode() == frame([3,0],0x52)
    assert StreamRequest(StreamKind.KEYBOARD).encode() == frame([2],0x52)
    d = StreamFrameDecoder()
    frames = []
    for b in b"noise" + frame([2,255,1]) + frame([3,10,0,0]):
        d.feed(bytes([b]))
        while (f := d.next()) is not None:
            frames.append(f)
    assert [decode_input_change(f).value for f in frames] == [1,0]
