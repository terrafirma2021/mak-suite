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

def test_authenticated_event_nonce_does_not_consume_query():
    from makxd.connection import SerialTransport, PendingCommand
    from concurrent.futures import Future
    import time
    import pytest
    encryption = TransportEncryption(True, KEY_HEX)
    decoder = EncryptedFrameDecoder(encryption)
    transport = SerialTransport()
    pending_nonce = bytes(range(12))
    event_nonce = bytes(range(12,24))
    future = Future()
    transport._pending_commands[1] = PendingCommand(1,"query",future,time.time(),mak_api_opcode=0x52,transaction_nonce=pending_nonce)
    def seal(plaintext, nonce):
        aad = b"\x01\x01" + nonce
        sealed = AESCCM(bytes.fromhex(KEY_HEX),tag_length=16).encrypt(b"\x01"+nonce,plaintext,aad)
        payload = aad + sealed[-16:] + sealed[:-16]
        return b"\xde\xad" + len(payload).to_bytes(2,"little") + b"\x03" + payload
    wire = seal(b"\xde\xad\x04\x00\x53\x03\x0a\xff\x03",event_nonce)
    for plain, nonce in decoder.feed(wire):
        transport._process_mak_api_response(plain, nonce)
    assert transport.read_input_change(0).value == 1023
    assert not future.done()
    for plain, nonce in decoder.feed(seal(b"\x52\x01",pending_nonce)):
        transport._process_mak_api_response(plain, nonce)
    assert future.result() == b"\x01"
    corrupt = bytearray(wire); corrupt[-1] ^= 1
    with pytest.raises(Exception):
        EncryptedFrameDecoder(encryption).feed(bytes(corrupt))
