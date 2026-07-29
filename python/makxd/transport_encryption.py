import secrets
from typing import Union

from cryptography.hazmat.primitives.ciphers.aead import AESCCM


TRANSPORT_FRAME_MAGIC = b"\xDE\xAD"
TRANSPORT_FRAME_COMMAND_ENCRYPTED = 0x03
TRANSPORT_FRAME_PAYLOAD_MAX = 251
TRANSPORT_ENVELOPE_VERSION = 1
TRANSPORT_TRANSACTION_NONCE_BYTES = 12
TRANSPORT_TAG_BYTES = 16
TRANSPORT_ENVELOPE_BYTES = (
    2 + TRANSPORT_TRANSACTION_NONCE_BYTES + TRANSPORT_TAG_BYTES
)


def _transport_key_decode(key: Union[str, bytes, bytearray]) -> bytes:
    if isinstance(key, str):
        if len(key) != 32:
            raise ValueError("encryption_key must contain exactly 32 hexadecimal characters")
        try:
            key_bytes = bytes.fromhex(key)
        except ValueError as error:
            raise ValueError("encryption_key must contain only hexadecimal characters") from error
    elif isinstance(key, (bytes, bytearray)):
        key_bytes = bytes(key)
    else:
        raise TypeError("encryption_key must be a hexadecimal string or 16 bytes")
    if len(key_bytes) != 16:
        raise ValueError("encryption_key must contain exactly 16 bytes")
    return key_bytes


class TransportEncryption:
    __slots__ = ("_enabled", "_cipher")

    def __init__(
        self,
        enabled: bool = False,
        key: Union[str, bytes, bytearray] = b"",
    ) -> None:
        self._enabled = bool(enabled)
        self._cipher = AESCCM(_transport_key_decode(key), tag_length=TRANSPORT_TAG_BYTES) \
            if self._enabled else None

    @property
    def enabled(self) -> bool:
        return self._enabled

    def encode_command(self, plaintext: bytes) -> tuple[bytes, bytes]:
        record, transaction_nonce = self.encode_record(plaintext)
        if not self.enabled:
            return record, transaction_nonce
        payload = record[1:]
        frame = bytearray(5 + len(payload))
        frame[0:2] = TRANSPORT_FRAME_MAGIC
        frame[2] = len(payload) & 0xFF
        frame[3] = (len(payload) >> 8) & 0xFF
        frame[4] = TRANSPORT_FRAME_COMMAND_ENCRYPTED
        frame[5:] = payload
        return bytes(frame), transaction_nonce

    def encode_record(self, plaintext: bytes) -> tuple[bytes, bytes]:
        if not self.enabled or self._cipher is None:
            return plaintext, b""
        if not plaintext:
            raise ValueError("encrypted command payload cannot be empty")
        transaction_nonce = secrets.token_bytes(TRANSPORT_TRANSACTION_NONCE_BYTES)
        aad = bytes((TRANSPORT_ENVELOPE_VERSION, 0)) + transaction_nonce
        nonce = b"\x00" + transaction_nonce
        sealed = self._cipher.encrypt(nonce, plaintext, aad)
        ciphertext = sealed[:-TRANSPORT_TAG_BYTES]
        tag = sealed[-TRANSPORT_TAG_BYTES:]
        record = bytes((TRANSPORT_FRAME_COMMAND_ENCRYPTED,)) + aad + tag + ciphertext
        if len(record) - 1 > TRANSPORT_FRAME_PAYLOAD_MAX:
            raise ValueError("encrypted command exceeds the COM frame limit")
        return record, transaction_nonce

    def decode_response(self, frame: bytes, expected_nonce: bytes = b"") -> tuple[bytes, bytes]:
        if not self.enabled or self._cipher is None:
            raise ValueError("transport encryption is disabled")
        if len(frame) < 5 + TRANSPORT_ENVELOPE_BYTES:
            raise ValueError("encrypted response frame is too short")
        if frame[0:2] != TRANSPORT_FRAME_MAGIC:
            raise ValueError("encrypted response frame has invalid magic")
        payload_length = frame[2] | (frame[3] << 8)
        if payload_length > TRANSPORT_FRAME_PAYLOAD_MAX or len(frame) != 5 + payload_length:
            raise ValueError("encrypted response frame has invalid length")
        if frame[4] != TRANSPORT_FRAME_COMMAND_ENCRYPTED:
            raise ValueError("encrypted response frame has invalid command type")
        payload = frame[5:]
        if payload[0] != TRANSPORT_ENVELOPE_VERSION or payload[1] != 1:
            raise ValueError("encrypted response frame has invalid envelope")
        transaction_nonce = payload[2:14]
        if expected_nonce and not secrets.compare_digest(transaction_nonce, expected_nonce):
            raise ValueError("encrypted response transaction nonce does not match")
        tag = payload[14:30]
        ciphertext = payload[30:]
        aad = payload[:14]
        plaintext = self._cipher.decrypt(
            b"\x01" + transaction_nonce,
            ciphertext + tag,
            aad,
        )
        return plaintext, transaction_nonce


class EncryptedFrameDecoder:
    __slots__ = ("_encryption", "_buffer")

    def __init__(self, encryption: TransportEncryption) -> None:
        self._encryption = encryption
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[tuple[bytes, bytes]]:
        self._buffer.extend(data)
        decoded: list[tuple[bytes, bytes]] = []
        while True:
            marker = self._buffer.find(TRANSPORT_FRAME_MAGIC)
            if marker < 0:
                self._buffer[:] = self._buffer[-1:] if self._buffer[-1:] == b"\xDE" else b""
                return decoded
            if marker:
                del self._buffer[:marker]
            if len(self._buffer) < 5:
                return decoded
            payload_length = self._buffer[2] | (self._buffer[3] << 8)
            if payload_length == 0 or payload_length > TRANSPORT_FRAME_PAYLOAD_MAX:
                del self._buffer[0]
                continue
            frame_length = 5 + payload_length
            if len(self._buffer) < frame_length:
                return decoded
            frame = bytes(self._buffer[:frame_length])
            del self._buffer[:frame_length]
            plaintext, transaction_nonce = self._encryption.decode_response(frame)
            decoded.append((plaintext, transaction_nonce))
