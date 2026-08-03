use crate::error::Result;
use crate::types::DeviceKinds;

use super::Device;

impl Device {
    pub fn device(&self) -> Result<DeviceKinds> {
        self.transport.device_kinds(self.config.command_timeout)
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
}
