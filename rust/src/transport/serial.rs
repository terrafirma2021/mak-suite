use std::io::Write;
use std::time::Duration;

use serialport::SerialPort;

use super::encryption::{EncryptedFrameDecoder, TransportEncryption};
use super::wire::{SerialWirePort, WirePort};
use crate::error::{MakxdError, Result};
use crate::protocol::api::ApiOpcode;
use crate::protocol::constants::*;

/// Open a serial port at the active firmware baud.
/// Returns the opened port and the discovered device-kind mask.
pub fn establish_connection(
    port_name: &str,
    transport_encryption: Option<&TransportEncryption>,
) -> Result<(Box<dyn WirePort>, u8)> {
    for baud in BAUD_CANDIDATES {
        if let Ok(result) = try_connect(port_name, baud, transport_encryption) {
            return Ok(result);
        }
        std::thread::sleep(Duration::from_millis(120));
    }
    Err(MakxdError::Protocol(
        "device identity probe failed at every supported baud".into(),
    ))
}

/// Find the first serial port matching the MAKXD VID/PID.
pub fn find_ports() -> Result<Vec<String>> {
    let ports = serialport::available_ports().map_err(MakxdError::Port)?;
    let mut candidates = Vec::new();
    for supported_id in SUPPORTED_USB_IDS {
        for port in &ports {
            if let serialport::SerialPortType::UsbPort(info) = &port.port_type
                && (info.vid, info.pid) == supported_id
            {
                candidates.push(port.port_name.clone());
            }
        }
    }
    if candidates.is_empty() {
        Err(MakxdError::NotFound)
    } else {
        Ok(candidates)
    }
}

fn try_connect(
    port_name: &str,
    baud: u32,
    transport_encryption: Option<&TransportEncryption>,
) -> Result<(Box<dyn WirePort>, u8)> {
    let mut port = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(50))
        .open()
        .map_err(MakxdError::Port)?;

    std::thread::sleep(Duration::from_millis(180));
    let _ = port.clear(serialport::ClearBuffer::Input);
    let command = if transport_encryption.is_some() {
        vec![ApiOpcode::Device as u8]
    } else {
        vec![0xde, 0xad, 0, 0, ApiOpcode::Device as u8]
    };
    let (device_command, expected_nonce) = encode_command(&command, transport_encryption)?;
    port.write_all(&device_command)?;
    port.flush()?;

    let raw = read_response(
        &mut *port,
        Duration::from_millis(750),
        transport_encryption,
        expected_nonce.as_ref(),
    )?;
    if let Some(kinds) = device_response_kinds(&raw, transport_encryption.is_none()) {
        Ok((Box::new(SerialWirePort::new(port)), kinds))
    } else {
        Err(MakxdError::Protocol("device identity probe failed".into()))
    }
}

fn device_response_kinds(response: &[u8], framed: bool) -> Option<u8> {
    if framed {
        response
            .windows(6)
            .find(|value| value[..5] == [0xde, 0xad, 1, 0, ApiOpcode::Device as u8])
            .map(|value| value[5])
            .filter(|value| *value != 0xff)
    } else if response.len() == 2 && response[0] == ApiOpcode::Device as u8 {
        (response[1] != 0xff).then_some(response[1])
    } else {
        None
    }
}

fn encode_command(
    plaintext: &[u8],
    transport_encryption: Option<&TransportEncryption>,
) -> Result<(Vec<u8>, Option<[u8; 12]>)> {
    if let Some(encryption) = transport_encryption {
        let (frame, nonce) = encryption.encode_command(plaintext)?;
        Ok((frame, Some(nonce)))
    } else {
        Ok((plaintext.to_vec(), None))
    }
}

fn read_response(
    port: &mut dyn SerialPort,
    timeout: Duration,
    transport_encryption: Option<&TransportEncryption>,
    expected_nonce: Option<&[u8; 12]>,
) -> Result<Vec<u8>> {
    let Some(encryption) = transport_encryption else {
        return read_device_frame(port, timeout);
    };
    let deadline = std::time::Instant::now() + timeout;
    let mut decoder = EncryptedFrameDecoder::new();
    let mut tmp = [0u8; 64];
    loop {
        if std::time::Instant::now() > deadline {
            return Err(MakxdError::Timeout);
        }
        match port.read(&mut tmp) {
            Ok(n) => {
                for (plaintext, transaction_nonce) in decoder.feed(encryption, &tmp[..n])? {
                    if expected_nonce.is_some_and(|expected| expected != &transaction_nonce) {
                        return Err(MakxdError::Protocol(
                            "encrypted response transaction nonce does not match".into(),
                        ));
                    }
                    return Ok(plaintext);
                }
            }
            Err(error) if error.kind() == std::io::ErrorKind::TimedOut => {}
            Err(error) => return Err(error.into()),
        }
    }
}

fn read_device_frame(port: &mut dyn SerialPort, timeout: Duration) -> Result<Vec<u8>> {
    let deadline = std::time::Instant::now() + timeout;
    let mut buf = Vec::new();
    let mut tmp = [0u8; 64];
    loop {
        if std::time::Instant::now() > deadline {
            return Err(MakxdError::Timeout);
        }
        match port.read(&mut tmp) {
            Ok(n) => {
                buf.extend_from_slice(&tmp[..n]);
                if device_response_kinds(&buf, true).is_some() {
                    return Ok(buf);
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
            Err(e) => return Err(e.into()),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::device_response_kinds;
    use crate::protocol::constants::{BAUD_CANDIDATES, SUPPORTED_USB_IDS};

    #[test]
    fn baud_candidates_have_required_order() {
        assert_eq!(BAUD_CANDIDATES, [115_200, 1_000_000, 4_000_000]);
    }

    #[test]
    fn usb_candidates_prioritize_ch343_before_ch340() {
        assert_eq!(SUPPORTED_USB_IDS, [(0x1A86, 0x55D3), (0x1A86, 0x7523)]);
    }

    #[test]
    fn device_probe_reads_result_payload() {
        assert_eq!(
            device_response_kinds(&[0xde, 0xad, 1, 0, 2, 7], true),
            Some(7)
        );
        assert_eq!(
            device_response_kinds(&[0xde, 0xad, 1, 0, 2, 0xff], true),
            None
        );
    }
}
