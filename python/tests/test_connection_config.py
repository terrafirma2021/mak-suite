import pytest

from makxd import ConnectionConfig, ConnectionMethod, UdpWireMode
from makxd.protocol import ApiProtocol


def test_com_connection_selects_mak_api_and_aes_key() -> None:
    config = ConnectionConfig.com(
        "COM5",
        api_protocol=ApiProtocol.MAK_API,
        aes128_key="00112233445566778899aabbccddeeff",
    )
    assert config.method is ConnectionMethod.COM
    assert config.com_port == "COM5"
    assert config.api_protocol is ApiProtocol.MAK_API
    assert config.encryption_enabled


def test_udp_connection_keeps_vlan_at_interface_boundary() -> None:
    config = ConnectionConfig.udp(
        "192.168.7.1",
        port=8080,
        mode=UdpWireMode.RAW,
        interface="eth0.120",
        vlan_id=120,
        api_protocol=ApiProtocol.MAK_API,
    )
    assert config.method is ConnectionMethod.UDP
    assert config.udp_mode is UdpWireMode.RAW
    assert config.vlan_id == 120


def test_vlan_requires_selected_vlan_interface_or_address() -> None:
    with pytest.raises(ValueError):
        ConnectionConfig.udp("192.168.7.1", vlan_id=120)


def test_ble_connection_uses_fixed_public_service_owner() -> None:
    config = ConnectionConfig.ble(
        "AA:BB:CC:DD:EE:FF",
        api_protocol="mak_api",
    )
    assert config.method is ConnectionMethod.BLE
    assert config.api_protocol is ApiProtocol.MAK_API


def test_ble_rejects_makxd_aes_transport_key() -> None:
    with pytest.raises(ValueError, match="BLE does not use"):
        ConnectionConfig(
            method=ConnectionMethod.BLE,
            ble_address="AA:BB:CC:DD:EE:FF",
            aes128_key="00112233445566778899aabbccddeeff",
        ).validated()
