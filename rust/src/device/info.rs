use crate::error::{MakxdError, Result};
use crate::protocol::api::ApiOpcode;
use crate::types::DeviceKinds;

use super::Device;

impl Device {
    pub fn device(&self) -> Result<DeviceKinds> {
        self.transport.device_kinds(self.config.command_timeout)
    }

    pub fn firmware_version(&self) -> Result<u32> {
        let response = self.query_api(ApiOpcode::FirmwareVersion, &[])?;
        let bytes: [u8; 4] = response.try_into().map_err(|_| {
            MakxdError::Protocol("MAK_API firmware version response length is invalid".into())
        })?;
        Ok(u32::from_le_bytes(bytes))
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn device(&self) -> Result<DeviceKinds> {
        let transport = self.transport.clone();
        let timeout = self.config.command_timeout;
        tokio::task::spawn_blocking(move || transport.device_kinds(timeout))
            .await
            .map_err(|error| {
                crate::error::MakxdError::Protocol(format!("tokio join error: {error}"))
            })?
    }

    pub async fn firmware_version(&self) -> Result<u32> {
        let response = self.query_api(ApiOpcode::FirmwareVersion, &[]).await?;
        let bytes: [u8; 4] = response.try_into().map_err(|_| {
            MakxdError::Protocol("MAK_API firmware version response length is invalid".into())
        })?;
        Ok(u32::from_le_bytes(bytes))
    }
}
