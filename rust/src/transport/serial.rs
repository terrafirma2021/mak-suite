use std::io::Write;
use std::time::Duration;

use serialport::SerialPort;

use super::encryption::{EncryptedFrameDecoder, TransportEncryption};
use super::wire::{SerialWirePort, WirePort};
use crate::error::{MakxdError, Result};
use crate::protocol::constants::*;

/// Open a serial port at the active firmware baud.
/// Returns the opened port and the firmware version string.
pub fn establish_connection(
    port_name: &str,
    transport_encryption: Option<&TransportEncryption>,
) -> Result<(Box<dyn WirePort>, String)> {
    for baud in BAUD_CANDIDATES {
        if let Ok(result) = try_connect(port_name, baud, transport_encryption) {
            return Ok(result);
        }
        std::thread::sleep(Duration::from_millis(120));
    }
    Err(MakxdError::Protocol(
        "km.version() did not return km.MAKXD at any supported baud".into(),
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
) -> Result<(Box<dyn WirePort>, String)> {
    let mut port = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(50))
        .open()
        .map_err(MakxdError::Port)?;

    std::thread::sleep(Duration::from_millis(180));
    let _ = port.clear(serialport::ClearBuffer::Input);
    let (version_command, expected_nonce) = encode_command(CMD_VERSION, transport_encryption)?;
    port.write_all(&version_command)?;
    port.flush()?;

    let raw = read_response(
        &mut *port,
        Duration::from_millis(750),
        transport_encryption,
        expected_nonce.as_ref(),
    )?;
    let text = String::from_utf8_lossy(&raw);
    if let Some(version) = version_response(&text) {
        let version = version.to_string();
        Ok((Box::new(SerialWirePort::new(port)), version))
    } else {
        Err(MakxdError::Protocol(format!(
            "unexpected version response: {}",
            text.trim()
        )))
    }
}

fn version_response(response: &str) -> Option<&str> {
    response
        .lines()
        .map(str::trim)
        .find(|line| *line == "km.MAKXD")
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
        return read_until_prompt(port, timeout);
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

/// Read from port until `>>> ` prompt is found or timeout elapses.
fn read_until_prompt(port: &mut dyn SerialPort, timeout: Duration) -> Result<Vec<u8>> {
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
                if buf.windows(PROMPT.len()).any(|w| w == PROMPT) {
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
    use super::version_response;
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
    fn version_probe_requires_exact_makxd_line() {
        assert_eq!(
            version_response("km.version()\r\nkm.MAKXD\r\n>>> "),
            Some("km.MAKXD")
        );
        assert_eq!(version_response("km.version()\r\nERR\r\n>>> "), None);
        assert_eq!(version_response("km.MAKXD-old\r\n>>> "), None);
    }
}
