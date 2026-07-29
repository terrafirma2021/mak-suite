import pytest
from types import SimpleNamespace

import makxd.connection as connection
from makxd.connection import SerialTransport
from makxd.errors import MakxdConnectionError


class ProbeSerial:
    def __init__(self, port, baudrate, response, **_kwargs):
        self.port = port
        self.baudrate = baudrate
        self.timeout = 0.05
        self.write_timeout = 0.25
        self.is_open = True
        self.closed = False
        self.writes = []
        self._response = bytearray(response)

    @property
    def in_waiting(self):
        return len(self._response)

    def reset_input_buffer(self):
        return None

    def write(self, data):
        self.writes.append(bytes(data))
        return len(data)

    def flush(self):
        return None

    def read(self, size):
        data = bytes(self._response[:size])
        del self._response[:size]
        return data

    def close(self):
        self.closed = True
        self.is_open = False


@pytest.mark.parametrize("product_id", [0x55D3, 0x7523])
def test_port_discovery_uses_supported_wch_vid_pid(monkeypatch, product_id):
    ports = [
        SimpleNamespace(
            device="COM2",
            vid=0x1234,
            pid=0x5678,
            hwid="USB VID:PID=1234:5678",
        ),
        SimpleNamespace(
            device="COM15",
            vid=0x1A86,
            pid=product_id,
            hwid=f"USB VID:PID=1A86:{product_id:04X}",
        ),
    ]
    monkeypatch.setattr(connection.list_ports, "comports", lambda: ports)

    assert SerialTransport().find_com_ports() == ["COM15"]


def test_port_discovery_prioritizes_all_ch343_before_ch340(monkeypatch):
    ports = [
        SimpleNamespace(device="COM20", vid=0x1A86, pid=0x7523, hwid="CH340"),
        SimpleNamespace(device="COM8", vid=0x1A86, pid=0x55D3, hwid="CH343"),
        SimpleNamespace(device="COM9", vid=0x1A86, pid=0x55D3, hwid="CH343"),
        SimpleNamespace(device="COM21", vid=0x1A86, pid=0x7523, hwid="CH340"),
    ]
    monkeypatch.setattr(connection.list_ports, "comports", lambda: ports)

    assert SerialTransport().find_com_ports() == [
        "COM8",
        "COM9",
        "COM20",
        "COM21",
    ]


def test_port_discovery_probes_each_candidate_until_makxd(monkeypatch):
    ports = [
        SimpleNamespace(device="COM8", vid=0x1A86, pid=0x55D3, hwid="CH343"),
        SimpleNamespace(device="COM15", vid=0x1A86, pid=0x7523, hwid="CH340"),
    ]
    monkeypatch.setattr(connection.list_ports, "comports", lambda: ports)
    transport = SerialTransport()
    attempts = []

    def probe(port_name):
        attempts.append(port_name)
        if port_name == "COM8":
            raise MakxdConnectionError("wrong device")

    monkeypatch.setattr(transport, "_open_detected_baud", probe)
    transport._open_detected_port()

    assert attempts == ["COM8", "COM15"]
    assert transport.port == "COM15"


def test_baud_discovery_uses_version_and_keeps_detected_port(monkeypatch):
    responses = {
        115_200: b"km.version()\r\nERR\r\n>>> ",
        1_000_000: b"km.version()\r\nkm.MAKXD\r\n>>> ",
        4_000_000: b"km.version()\r\nkm.MAKXD\r\n>>> ",
    }
    opened = []

    def open_probe(port, baudrate, **kwargs):
        candidate = ProbeSerial(port, baudrate, responses[baudrate], **kwargs)
        opened.append(candidate)
        return candidate

    monkeypatch.setattr(connection.serial, "Serial", open_probe)
    monkeypatch.setattr(connection.time, "sleep", lambda _seconds: None)

    transport = SerialTransport()
    transport._open_detected_baud("COM7")

    assert [candidate.baudrate for candidate in opened] == [115_200, 1_000_000]
    assert opened[0].closed
    assert transport.serial is opened[1]
    assert transport.baudrate == 1_000_000
    assert opened[1].writes == [b"km.version()\r\n"]


def test_baud_discovery_rejects_every_non_makxd_response(monkeypatch):
    opened = []

    def open_probe(port, baudrate, **kwargs):
        candidate = ProbeSerial(
            port,
            baudrate,
            b"km.version()\r\nERR\r\n>>> ",
            **kwargs,
        )
        opened.append(candidate)
        return candidate

    monkeypatch.setattr(connection.serial, "Serial", open_probe)
    monkeypatch.setattr(connection.time, "sleep", lambda _seconds: None)

    transport = SerialTransport()
    with pytest.raises(MakxdConnectionError, match="did not return km.MAKXD"):
        transport._open_detected_baud("COM7")

    assert [candidate.baudrate for candidate in opened] == [
        115_200,
        1_000_000,
        4_000_000,
    ]
    assert all(candidate.closed for candidate in opened)
