use crate::error::Result;
use crate::protocol::api::{ApiOpcode, ApiVerb};
use crate::protocol::{builder, constants};
use crate::timed;
use crate::types::{DeviceInfo, DeviceRoute};

use super::Device;

/// Strip the "km." prefix that the firmware prepends to some responses.
/// Takes ownership to avoid allocating when no prefix is present.
fn strip_km_prefix(mut s: String) -> String {
    if s.starts_with("km.") {
        s.drain(..3);
    }
    s
}

impl Device {
    /// Query the firmware version string (with "km." prefix stripped).
    pub fn version(&self) -> Result<String> {
        timed!(
            "version",
            self.query(constants::CMD_VERSION).map(strip_km_prefix)
        )
    }

    pub fn device(&self) -> Result<DeviceRoute> {
        let value = self.query_api(b"km.device()\r\n", ApiOpcode::Device, ApiVerb::Get, &[])?;
        parse_device_route(&value)
    }

    /// Returns combined device info (port name + firmware version).
    pub fn device_info(&self) -> Result<DeviceInfo> {
        timed!("device_info", {
            let firmware = self.version()?;
            let port = self.port_name().to_string();
            Ok(DeviceInfo { port, firmware })
        })
    }

    /// Query the current serial number reported by the connected mouse.
    pub fn serial(&self) -> Result<String> {
        timed!(
            "serial",
            self.query(constants::CMD_SERIAL_GET).map(strip_km_prefix)
        )
    }

    /// Spoof the mouse serial number. Returns the device's response.
    ///
    /// The value must be at most 45 characters.
    pub fn set_serial(&self, value: &str) -> Result<String> {
        timed!("set_serial", {
            let cmd = builder::build_serial_set(value)?;
            self.query_dynamic(cmd.as_bytes()).map(strip_km_prefix)
        })
    }

    /// Reset the spoofed serial back to the factory value.
    pub fn reset_serial(&self) -> Result<String> {
        timed!(
            "reset_serial",
            self.query(constants::CMD_SERIAL_RESET).map(strip_km_prefix)
        )
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Query the firmware version string (with "km." prefix stripped).
    pub async fn version(&self) -> Result<String> {
        timed!("version", {
            self.query(constants::CMD_VERSION)
                .await
                .map(strip_km_prefix)
        })
    }

    pub async fn device(&self) -> Result<DeviceRoute> {
        let value = self
            .query_api(b"km.device()\r\n", ApiOpcode::Device, ApiVerb::Get, &[])
            .await?;
        parse_device_route(&value)
    }

    /// Returns combined device info (port name + firmware version).
    pub async fn device_info(&self) -> Result<DeviceInfo> {
        timed!("device_info", {
            let firmware = self.version().await?;
            let port = self.port_name().to_string();
            Ok(DeviceInfo { port, firmware })
        })
    }

    pub async fn serial(&self) -> Result<String> {
        timed!("serial", {
            self.query(constants::CMD_SERIAL_GET)
                .await
                .map(strip_km_prefix)
        })
    }

    /// Spoof the mouse serial number. Returns the device's response.
    ///
    /// The value must be at most 45 characters.
    pub async fn set_serial(&self, value: &str) -> Result<String> {
        timed!("set_serial", {
            let cmd = builder::build_serial_set(value)?;
            self.query_dynamic(cmd.as_bytes())
                .await
                .map(strip_km_prefix)
        })
    }

    pub async fn reset_serial(&self) -> Result<String> {
        timed!("reset_serial", {
            self.query(constants::CMD_SERIAL_RESET)
                .await
                .map(strip_km_prefix)
        })
    }
}

fn parse_device_route(value: &[u8]) -> Result<DeviceRoute> {
    if value.len() != 22 {
        return Err(crate::error::MakxdError::Protocol(
            "MAK_API device response length is invalid".into(),
        ));
    }
    Ok(DeviceRoute {
        route_mask: value[0],
        mouse_uframes: u16::from_le_bytes([value[1], value[2]]),
        keyboard_uframes: u16::from_le_bytes([value[3], value[4]]),
        controller_uframes: u16::from_le_bytes([value[5], value[6]]),
        generation: u32::from_le_bytes([value[7], value[8], value[9], value[10]]),
        controller_family: value[11],
        controller_protocol: value[12],
        controller_layout: value[13],
        controller_supported_low: u32::from_le_bytes([value[14], value[15], value[16], value[17]]),
        controller_supported_high: u32::from_le_bytes([value[18], value[19], value[20], value[21]]),
    })
}
