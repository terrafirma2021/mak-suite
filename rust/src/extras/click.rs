use std::time::Duration;

use crate::error::Result;
use crate::timed;
use crate::types::Button;

use crate::device::Device;

impl Device {
    /// Press + hold + release. `hold` is the delay between down and up.
    pub fn click(&self, button: Button, hold: Duration) -> Result<()> {
        timed!("click", {
            self.button_down(button)?;
            std::thread::sleep(hold);
            self.button_up(button)
        })
    }

    /// Repeated clicks with a delay between each press+release cycle.
    pub fn click_sequence(
        &self,
        button: Button,
        hold: Duration,
        count: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("click_sequence", {
            for i in 0..count {
                self.click(button, hold)?;
                if i + 1 < count {
                    std::thread::sleep(interval);
                }
            }
            Ok(())
        })
    }
}

#[cfg(feature = "async")]
use crate::device::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Press + hold + release (async — uses `tokio::time::sleep`).
    pub async fn click(&self, button: Button, hold: Duration) -> Result<()> {
        timed!("click", {
            self.button_down(button).await?;
            tokio::time::sleep(hold).await;
            self.button_up(button).await
        })
    }

    /// Repeated clicks with a delay between each press+release cycle (async).
    pub async fn click_sequence(
        &self,
        button: Button,
        hold: Duration,
        count: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("click_sequence", {
            for i in 0..count {
                self.click(button, hold).await?;
                if i + 1 < count {
                    tokio::time::sleep(interval).await;
                }
            }
            Ok(())
        })
    }
}
