use crate::error::Result;
use crate::protocol::{builder, constants};
use crate::timed;
use crate::types::DeviceInfo;

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
            let v = self.query(constants::CMD_VERSION).await?;
            Ok(strip_km_prefix(v))
        })
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
