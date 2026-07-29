use crate::error::Result;
use crate::protocol::constants;
use crate::timed;
use crate::types::{LockStates, LockTarget};

use super::Device;

impl Device {
    /// Lock or unlock a mouse input.
    pub fn set_lock(&self, target: LockTarget, locked: bool) -> Result<()> {
        timed!(
            "set_lock",
            self.exec(constants::lock_set_cmd(target, locked))
        )
    }

    /// Query whether a lock is currently active.
    pub fn lock_state(&self, target: LockTarget) -> Result<bool> {
        timed!("lock_state", {
            let value = self.query(constants::lock_query_cmd(target))?;
            Ok(value.trim() == "1")
        })
    }

    /// Query all seven lock states in one call.
    pub fn lock_states_all(&self) -> Result<LockStates> {
        Ok(LockStates {
            x: self.lock_state(LockTarget::X)?,
            y: self.lock_state(LockTarget::Y)?,
            left: self.lock_state(LockTarget::Left)?,
            right: self.lock_state(LockTarget::Right)?,
            middle: self.lock_state(LockTarget::Middle)?,
            side1: self.lock_state(LockTarget::Side1)?,
            side2: self.lock_state(LockTarget::Side2)?,
        })
    }

    pub fn set_directional_lock(&self, axis: char, positive: bool, locked: bool) -> Result<()> {
        let axis = match axis.to_ascii_lowercase() {
            'x' => 'x',
            'y' => 'y',
            _ => return Err(crate::error::MakxdError::Protocol("axis must be x or y".into())),
        };
        let sign = if positive { '+' } else { '-' };
        timed!("set_directional_lock", {
            let command = format!("km.lock_m{axis}{sign}({})\r\n", if locked { 1 } else { 0 });
            self.exec_dynamic(command.as_bytes())
        })
    }
}

// -- Async --

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn set_lock(&self, target: LockTarget, locked: bool) -> Result<()> {
        timed!(
            "set_lock",
            self.exec(constants::lock_set_cmd(target, locked)).await
        )
    }

    pub async fn lock_state(&self, target: LockTarget) -> Result<bool> {
        timed!("lock_state", {
            let value = self.query(constants::lock_query_cmd(target)).await?;
            Ok(value.trim() == "1")
        })
    }

    pub async fn lock_states_all(&self) -> Result<LockStates> {
        Ok(LockStates {
            x: self.lock_state(LockTarget::X).await?,
            y: self.lock_state(LockTarget::Y).await?,
            left: self.lock_state(LockTarget::Left).await?,
            right: self.lock_state(LockTarget::Right).await?,
            middle: self.lock_state(LockTarget::Middle).await?,
            side1: self.lock_state(LockTarget::Side1).await?,
            side2: self.lock_state(LockTarget::Side2).await?,
        })
    }

    pub async fn set_directional_lock(&self, axis: char, positive: bool, locked: bool) -> Result<()> {
        let axis = match axis.to_ascii_lowercase() {
            'x' => 'x',
            'y' => 'y',
            _ => return Err(crate::error::MakxdError::Protocol("axis must be x or y".into())),
        };
        let sign = if positive { '+' } else { '-' };
        timed!("set_directional_lock", {
            let command = format!("km.lock_m{axis}{sign}({})\r\n", if locked { 1 } else { 0 });
            self.exec_dynamic(command.as_bytes()).await
        })
    }
}
