use std::io::Write;
use std::time::Duration;

use serialport::SerialPort;

use crate::error::{MakxdError, Result};
use crate::protocol::constants::*;

/// Open a serial port, try 4M baud first, fall back to baud-change sequence.
/// Returns the opened port and the firmware version string.
pub fn establish_connection(
    port_name: &str,
    try_4m_first: bool,
) -> Result<(Box<dyn SerialPort>, String)> {
    if try_4m_first && let Ok(result) = try_connect(port_name, BAUD_4M) {
        return Ok(result);
    }

    // Open at 115200, send baud-change frame, close.
    {
        let mut port = serialport::new(port_name, BAUD_DEFAULT)
            .timeout(Duration::from_millis(100))
            .open()
            .map_err(MakxdError::Port)?;
        port.write_all(BAUD_FRAME_4M)?;
        port.flush()?;
        std::thread::sleep(Duration::from_millis(100));
    }

    // Re-open at 4M baud. Wait for the UART to stabilise and flush any
    // garbage bytes accumulated during the baud transition.
    {
        let mut port = serialport::new(port_name, BAUD_4M)
            .timeout(Duration::from_millis(200))
            .open()
            .map_err(MakxdError::Port)?;
        std::thread::sleep(Duration::from_millis(50));
        let _ = port.clear(serialport::ClearBuffer::Input);
        // Now send version query through this port directly.
        port.write_all(CMD_VERSION)?;
        port.flush()?;

        let raw = read_until_prompt(&mut *port, Duration::from_millis(500))?;
        let text = String::from_utf8_lossy(&raw);
        if text.contains("km.MAKCU") {
            let version = text
                .lines()
                .find(|l| l.contains("km.MAKCU"))
                .unwrap_or("km.MAKCU")
                .trim()
                .to_string();
            return Ok((port, version));
        }
    }

    Err(MakxdError::Protocol(
        "failed to connect after baud change".into(),
    ))
}

/// Find the first serial port matching the MAKXD VID/PID.
pub fn find_port() -> Result<String> {
    let ports = serialport::available_ports().map_err(MakxdError::Port)?;
    for port in ports {
        if let serialport::SerialPortType::UsbPort(info) = port.port_type
            && info.vid == USB_VID
            && info.pid == USB_PID
        {
            return Ok(port.port_name);
        }
    }
    Err(MakxdError::NotFound)
}

fn try_connect(port_name: &str, baud: u32) -> Result<(Box<dyn SerialPort>, String)> {
    let mut port = serialport::new(port_name, baud)
        .timeout(Duration::from_millis(200))
        .open()
        .map_err(MakxdError::Port)?;

    // Send version query and verify response.
    port.write_all(CMD_VERSION)?;
    port.flush()?;

    let raw = read_until_prompt(&mut *port, Duration::from_millis(500))?;
    let text = String::from_utf8_lossy(&raw);
    if text.contains("km.MAKCU") {
        // Extract version string.
        let version = text
            .lines()
            .find(|l| l.contains("km.MAKCU"))
            .unwrap_or("km.MAKCU")
            .trim()
            .to_string();
        Ok((port, version))
    } else {
        Err(MakxdError::Protocol(format!(
            "unexpected version response: {}",
            text.trim()
        )))
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
