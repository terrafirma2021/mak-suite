use aes::Aes128;
use ccm::{
    Ccm,
    aead::{AeadInPlace, KeyInit, generic_array::GenericArray},
    consts::{U13, U16},
};

use crate::error::{MakxdError, Result};

type Aes128Ccm = Ccm<Aes128, U16, U13>;

const FRAME_MAGIC: [u8; 2] = [0xDE, 0xAD];
const FRAME_COMMAND_ENCRYPTED: u8 = 0x03;
const FRAME_PAYLOAD_MAX: usize = 251;
const ENVELOPE_VERSION: u8 = 1;
const TRANSACTION_NONCE_BYTES: usize = 12;
const TAG_BYTES: usize = 16;
const ENVELOPE_BYTES: usize = 2 + TRANSACTION_NONCE_BYTES + TAG_BYTES;

#[derive(Clone)]
pub(crate) struct TransportEncryption {
    key: [u8; 16],
}

impl TransportEncryption {
    pub fn from_config(enabled: bool, key: Option<[u8; 16]>) -> Result<Option<Self>> {
        if !enabled {
            return Ok(None);
        }
        let key = key.ok_or_else(|| {
            MakxdError::Protocol("transport_encryption requires a 16-byte transport_key".into())
        })?;
        Ok(Some(Self { key }))
    }

    fn cipher(&self) -> Result<Aes128Ccm> {
        Aes128Ccm::new_from_slice(&self.key)
            .map_err(|_| MakxdError::Protocol("invalid AES-128 transport key".into()))
    }

    pub fn encode_command(&self, plaintext: &[u8]) -> Result<(Vec<u8>, [u8; 12])> {
        if plaintext.is_empty() {
            return Err(MakxdError::Protocol(
                "encrypted command payload cannot be empty".into(),
            ));
        }
        let mut transaction_nonce = [0u8; TRANSACTION_NONCE_BYTES];
        getrandom::fill(&mut transaction_nonce)
            .map_err(|error| MakxdError::Protocol(format!("nonce generation failed: {error}")))?;
        self.encode_command_with_nonce(plaintext, transaction_nonce)
    }

    fn encode_command_with_nonce(
        &self,
        plaintext: &[u8],
        transaction_nonce: [u8; 12],
    ) -> Result<(Vec<u8>, [u8; 12])> {
        let mut aad = [0u8; 14];
        aad[0] = ENVELOPE_VERSION;
        aad[1] = 0;
        aad[2..].copy_from_slice(&transaction_nonce);
        let mut nonce = [0u8; 13];
        nonce[1..].copy_from_slice(&transaction_nonce);
        let mut ciphertext = plaintext.to_vec();
        let tag = self
            .cipher()?
            .encrypt_in_place_detached(GenericArray::from_slice(&nonce), &aad, &mut ciphertext)
            .map_err(|_| MakxdError::Protocol("AES-CCM command encryption failed".into()))?;
        let payload_len = ENVELOPE_BYTES + ciphertext.len();
        if payload_len > FRAME_PAYLOAD_MAX {
            return Err(MakxdError::Protocol(
                "encrypted command exceeds the COM frame limit".into(),
            ));
        }
        let mut frame = Vec::with_capacity(5 + payload_len);
        frame.extend_from_slice(&FRAME_MAGIC);
        frame.extend_from_slice(&(payload_len as u16).to_le_bytes());
        frame.push(FRAME_COMMAND_ENCRYPTED);
        frame.extend_from_slice(&aad);
        frame.extend_from_slice(&tag);
        frame.extend_from_slice(&ciphertext);
        Ok((frame, transaction_nonce))
    }

    pub fn decode_response(
        &self,
        frame: &[u8],
        expected_nonce: Option<&[u8; 12]>,
    ) -> Result<(Vec<u8>, [u8; 12])> {
        if frame.len() < 5 + ENVELOPE_BYTES || frame[..2] != FRAME_MAGIC {
            return Err(MakxdError::Protocol(
                "encrypted response frame is malformed".into(),
            ));
        }
        let payload_len = u16::from_le_bytes([frame[2], frame[3]]) as usize;
        if payload_len > FRAME_PAYLOAD_MAX || frame.len() != 5 + payload_len {
            return Err(MakxdError::Protocol(
                "encrypted response frame length is invalid".into(),
            ));
        }
        if frame[4] != FRAME_COMMAND_ENCRYPTED || frame[5] != ENVELOPE_VERSION || frame[6] != 1 {
            return Err(MakxdError::Protocol(
                "encrypted response envelope is invalid".into(),
            ));
        }
        let mut transaction_nonce = [0u8; TRANSACTION_NONCE_BYTES];
        transaction_nonce.copy_from_slice(&frame[7..19]);
        if expected_nonce.is_some_and(|expected| expected != &transaction_nonce) {
            return Err(MakxdError::Protocol(
                "encrypted response transaction nonce does not match".into(),
            ));
        }
        let aad = &frame[5..19];
        let tag = GenericArray::from_slice(&frame[19..35]);
        let mut ciphertext = frame[35..].to_vec();
        let mut nonce = [0u8; 13];
        nonce[0] = 1;
        nonce[1..].copy_from_slice(&transaction_nonce);
        self.cipher()?
            .decrypt_in_place_detached(GenericArray::from_slice(&nonce), aad, &mut ciphertext, tag)
            .map_err(|_| MakxdError::Protocol("encrypted response authentication failed".into()))?;
        Ok((ciphertext, transaction_nonce))
    }
}

pub(crate) struct EncryptedFrameDecoder {
    buffer: Vec<u8>,
}

impl EncryptedFrameDecoder {
    pub fn new() -> Self {
        Self {
            buffer: Vec::with_capacity(512),
        }
    }

    pub fn feed(
        &mut self,
        encryption: &TransportEncryption,
        data: &[u8],
    ) -> Result<Vec<(Vec<u8>, [u8; 12])>> {
        self.buffer.extend_from_slice(data);
        let mut decoded = Vec::new();
        loop {
            let marker = self
                .buffer
                .windows(2)
                .position(|window| window == FRAME_MAGIC);
            let Some(marker) = marker else {
                let keep_de = self.buffer.last() == Some(&FRAME_MAGIC[0]);
                self.buffer.clear();
                if keep_de {
                    self.buffer.push(FRAME_MAGIC[0]);
                }
                return Ok(decoded);
            };
            if marker != 0 {
                self.buffer.drain(..marker);
            }
            if self.buffer.len() < 5 {
                return Ok(decoded);
            }
            let payload_len = u16::from_le_bytes([self.buffer[2], self.buffer[3]]) as usize;
            if payload_len == 0 || payload_len > FRAME_PAYLOAD_MAX {
                self.buffer.remove(0);
                continue;
            }
            let frame_len = 5 + payload_len;
            if self.buffer.len() < frame_len {
                return Ok(decoded);
            }
            let frame: Vec<u8> = self.buffer.drain(..frame_len).collect();
            decoded.push(encryption.decode_response(&frame, None)?);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn command_frame_round_trips_with_standard_ccm() {
        let encryption = TransportEncryption {
            key: [
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                0x0e, 0x0f,
            ],
        };
        let transaction_nonce = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
        let (frame, nonce) = encryption
            .encode_command_with_nonce(b"\x02", transaction_nonce)
            .unwrap();
        assert_eq!(nonce, transaction_nonce);
        assert_eq!(&frame[..2], &FRAME_MAGIC);
        assert_eq!(frame[4], FRAME_COMMAND_ENCRYPTED);
        assert_eq!(&frame[5..19], &[1, 0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]);
        assert_eq!(
            &frame[19..],
            &[
                0x6f, 0xe3, 0xec, 0x58, 0x4a, 0x75, 0xcf, 0x78, 0x29, 0xd2, 0x5e, 0xc1, 0x96, 0x70,
                0x50, 0x7c, 0x66,
            ]
        );
    }

    #[test]
    fn enabled_mode_requires_key() {
        assert!(TransportEncryption::from_config(true, None).is_err());
        assert!(
            TransportEncryption::from_config(false, None)
                .unwrap()
                .is_none()
        );
    }

    #[test]
    fn authenticated_response_requires_matching_transaction_nonce() {
        let encryption = TransportEncryption {
            key: [
                0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
                0x0e, 0x0f,
            ],
        };
        let transaction_nonce = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
        let mut aad = [0u8; 14];
        aad[0] = ENVELOPE_VERSION;
        aad[1] = 1;
        aad[2..].copy_from_slice(&transaction_nonce);
        let mut nonce = [0u8; 13];
        nonce[0] = 1;
        nonce[1..].copy_from_slice(&transaction_nonce);
        let mut ciphertext = b"\x02\x04".to_vec();
        let tag = encryption
            .cipher()
            .unwrap()
            .encrypt_in_place_detached(GenericArray::from_slice(&nonce), &aad, &mut ciphertext)
            .unwrap();
        let payload_len = ENVELOPE_BYTES + ciphertext.len();
        let mut frame = Vec::with_capacity(5 + payload_len);
        frame.extend_from_slice(&FRAME_MAGIC);
        frame.extend_from_slice(&(payload_len as u16).to_le_bytes());
        frame.push(FRAME_COMMAND_ENCRYPTED);
        frame.extend_from_slice(&aad);
        frame.extend_from_slice(&tag);
        frame.extend_from_slice(&ciphertext);

        let (plaintext, returned_nonce) = encryption
            .decode_response(&frame, Some(&transaction_nonce))
            .unwrap();
        assert_eq!(plaintext, b"\x02\x04");
        assert_eq!(returned_nonce, transaction_nonce);

        let different_nonce = [0xff; 12];
        assert!(
            encryption
                .decode_response(&frame, Some(&different_nonce))
                .is_err()
        );
    }
}
