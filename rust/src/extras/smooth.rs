use std::time::Duration;

use crate::error::Result;
use crate::timed;
use crate::types::Button;

use crate::device::Device;

impl Device {
    /// Smooth movement via repeated `move_xy` calls.
    ///
    /// Total distance is divided evenly across `steps`, remainder on last step.
    pub fn move_smooth(&self, x: i32, y: i32, steps: u32, interval: Duration) -> Result<()> {
        timed!("move_smooth", {
            if steps == 0 {
                return self.move_xy(x, y);
            }

            let step_x = x / steps as i32;
            let step_y = y / steps as i32;
            let rem_x = x - step_x * steps as i32;
            let rem_y = y - step_y * steps as i32;

            for i in 0..steps {
                let dx = if i == steps - 1 {
                    step_x + rem_x
                } else {
                    step_x
                };
                let dy = if i == steps - 1 {
                    step_y + rem_y
                } else {
                    step_y
                };
                self.move_xy(dx, dy)?;
                if i + 1 < steps {
                    std::thread::sleep(interval);
                }
            }
            Ok(())
        })
    }

    /// Smooth move with the given button held throughout (drag).
    pub fn drag(
        &self,
        button: Button,
        x: i32,
        y: i32,
        steps: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("drag", {
            self.button_down(button)?;
            let move_result = self.move_smooth(x, y, steps, interval);
            let up_result = self.button_up(button);
            move_result.and(up_result)
        })
    }

    /// Navigate through a list of relative waypoints in sequence.
    pub fn move_pattern(
        &self,
        waypoints: &[(i32, i32)],
        steps: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("move_pattern", {
            for &(x, y) in waypoints {
                self.move_smooth(x, y, steps, interval)?;
            }
            Ok(())
        })
    }
}

#[cfg(feature = "async")]
use crate::device::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    /// Smooth movement via repeated `move_xy` calls (async).
    pub async fn move_smooth(&self, x: i32, y: i32, steps: u32, interval: Duration) -> Result<()> {
        timed!("move_smooth", {
            if steps == 0 {
                return self.move_xy(x, y).await;
            }

            let step_x = x / steps as i32;
            let step_y = y / steps as i32;
            let rem_x = x - step_x * steps as i32;
            let rem_y = y - step_y * steps as i32;

            for i in 0..steps {
                let dx = if i == steps - 1 {
                    step_x + rem_x
                } else {
                    step_x
                };
                let dy = if i == steps - 1 {
                    step_y + rem_y
                } else {
                    step_y
                };
                self.move_xy(dx, dy).await?;
                if i + 1 < steps {
                    tokio::time::sleep(interval).await;
                }
            }
            Ok(())
        })
    }

    /// Smooth move with the given button held throughout (async drag).
    pub async fn drag(
        &self,
        button: Button,
        x: i32,
        y: i32,
        steps: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("drag", {
            self.button_down(button).await?;
            let move_result = self.move_smooth(x, y, steps, interval).await;
            let up_result = self.button_up(button).await;
            move_result.and(up_result)
        })
    }

    /// Navigate through a list of relative waypoints in sequence (async).
    pub async fn move_pattern(
        &self,
        waypoints: &[(i32, i32)],
        steps: u32,
        interval: Duration,
    ) -> Result<()> {
        timed!("move_pattern", {
            for &(x, y) in waypoints {
                self.move_smooth(x, y, steps, interval).await?;
            }
            Ok(())
        })
    }
}
