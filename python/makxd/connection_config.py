from dataclasses import dataclass
from enum import Enum
from typing import Union

from .protocol import ApiProtocol


class ConnectionMethod(str, Enum):
    COM = "com"
    UDP = "udp"
    BLE = "ble"


class UdpWireMode(str, Enum):
    HOST = "host"
    RAW = "raw"


@dataclass(frozen=True)
class ConnectionConfig:
    method: ConnectionMethod
    api_protocol: ApiProtocol = ApiProtocol.KM
    aes128_key: Union[str, bytes, bytearray] = b""
    com_port: str = ""
    udp_host: str = ""
    udp_port: int = 8080
    udp_mode: UdpWireMode = UdpWireMode.HOST
    udp_bind_address: str = ""
    udp_interface: str = ""
    vlan_id: int | None = None
    ble_address: str = ""

    @property
    def encryption_enabled(self) -> bool:
        return bool(self.aes128_key)

    @classmethod
    def com(
        cls,
        port: str = "",
        *,
        api_protocol: ApiProtocol | str = ApiProtocol.KM,
        aes128_key: Union[str, bytes, bytearray] = b"",
    ) -> "ConnectionConfig":
        return cls(
            method=ConnectionMethod.COM,
            api_protocol=ApiProtocol.parse(api_protocol),
            aes128_key=aes128_key,
            com_port=port,
        )

    @classmethod
    def udp(
        cls,
        host: str,
        *,
        port: int = 8080,
        mode: UdpWireMode | str = UdpWireMode.HOST,
        bind_address: str = "",
        interface: str = "",
        vlan_id: int | None = None,
        api_protocol: ApiProtocol | str = ApiProtocol.KM,
        aes128_key: Union[str, bytes, bytearray] = b"",
    ) -> "ConnectionConfig":
        return cls(
            method=ConnectionMethod.UDP,
            api_protocol=ApiProtocol.parse(api_protocol),
            aes128_key=aes128_key,
            udp_host=host,
            udp_port=port,
            udp_mode=UdpWireMode(mode),
            udp_bind_address=bind_address,
            udp_interface=interface,
            vlan_id=vlan_id,
        ).validated()

    @classmethod
    def ble(
        cls,
        address: str,
        *,
        api_protocol: ApiProtocol | str = ApiProtocol.KM,
    ) -> "ConnectionConfig":
        return cls(
            method=ConnectionMethod.BLE,
            api_protocol=ApiProtocol.parse(api_protocol),
            ble_address=address,
        ).validated()

    def validated(self) -> "ConnectionConfig":
        if self.method is ConnectionMethod.UDP:
            if not self.udp_host:
                raise ValueError("udp_host is required")
            if not 1 <= self.udp_port <= 65535:
                raise ValueError("udp_port must be in 1..65535")
            if self.vlan_id is not None:
                if not 1 <= self.vlan_id <= 4094:
                    raise ValueError("vlan_id must be in 1..4094")
                if not self.udp_interface and not self.udp_bind_address:
                    raise ValueError(
                        "vlan_id requires udp_interface or udp_bind_address"
                    )
        elif self.method is ConnectionMethod.BLE:
            if not self.ble_address:
                raise ValueError("ble_address is required")
            if self.aes128_key:
                raise ValueError("BLE does not use MAKXD AES transport encryption")
        return self


__all__ = [
    "ConnectionConfig",
    "ConnectionMethod",
    "UdpWireMode",
]
