from cryptography.hazmat.primitives.ciphers.aead import AESCCM

from makxd.transport_encryption import (
    EncryptedFrameDecoder,
    TransportEncryption,
)


KEY_HEX = "000102030405060708090a0b0c0d0e0f"


def test_encrypted_command_uses_authenticated_com_frame(monkeypatch):
    monkeypatch.setattr(
        "makxd.transport_encryption.secrets.token_bytes",
        lambda count: bytes(range(count)),
    )
    encryption = TransportEncryption(True, KEY_HEX)
    frame, transaction_nonce = encryption.encode_command(b"\x02")

    assert frame[:2] == b"\xDE\xAD"
    assert frame[4] == 0x03
    assert transaction_nonce == bytes(range(12))
    assert frame.hex() == (
        "dead1f00030100000102030405060708090a0b"
        "6fe3ec584a75cf7829d25ec19670507c66"
    )
    payload = frame[5:]
    assert payload[:14] == b"\x01\x00" + transaction_nonce

    cipher = AESCCM(bytes.fromhex(KEY_HEX), tag_length=16)
    plaintext = cipher.decrypt(
        b"\x00" + transaction_nonce,
        payload[30:] + payload[14:30],
        payload[:14],
    )
    assert plaintext == b"\x02"


def test_encrypted_response_is_incrementally_decoded():
    encryption = TransportEncryption(True, KEY_HEX)
    transaction_nonce = bytes(range(12))
    aad = b"\x01\x01" + transaction_nonce
    plaintext = b"\x02\x01\x04"
    cipher = AESCCM(bytes.fromhex(KEY_HEX), tag_length=16)
    sealed = cipher.encrypt(b"\x01" + transaction_nonce, plaintext, aad)
    payload = aad + sealed[-16:] + sealed[:-16]
    frame = (
        b"\xDE\xAD" +
        len(payload).to_bytes(2, "little") +
        b"\x03" +
        payload
    )

    decoder = EncryptedFrameDecoder(encryption)
    assert decoder.feed(frame[:7]) == []
    assert decoder.feed(frame[7:]) == [(plaintext, transaction_nonce)]


def test_disabled_transport_does_not_require_or_wrap_a_key():
    encryption = TransportEncryption(False)
    plaintext = b"\x02"
    assert encryption.encode_command(plaintext) == (plaintext, b"")
