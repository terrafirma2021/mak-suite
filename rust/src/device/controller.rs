use crate::error::{MakxdError, Result};
use crate::protocol::api::ApiOpcode;
use crate::types::{
    CONTROLLER_TRIGGER_MAX, ControllerControl, ControllerMaskMode, ControllerState,
};

use super::Device;

fn controller_value_check(control: ControllerControl, value: i32) -> Result<()> {
    let (min, max) = match control {
        ControllerControl::LeftTrigger | ControllerControl::RightTrigger => {
            (0, i32::from(CONTROLLER_TRIGGER_MAX))
        }
        ControllerControl::LeftStickX
        | ControllerControl::LeftStickY
        | ControllerControl::RightStickX
        | ControllerControl::RightStickY => (-32768, 32767),
        _ => (0, 1),
    };
    if (min..=max).contains(&value) {
        Ok(())
    } else {
        Err(MakxdError::OutOfRange {
            value: value as i64,
            min: min as i64,
            max: max as i64,
        })
    }
}

fn controller_state_check(state: ControllerState) -> Result<()> {
    controller_value_check(
        ControllerControl::LeftTrigger,
        i32::from(state.left_trigger),
    )?;
    controller_value_check(
        ControllerControl::RightTrigger,
        i32::from(state.right_trigger),
    )
}

fn controller_state_payload(state: ControllerState, dt_uframes: u16) -> Vec<u8> {
    let mut payload = Vec::with_capacity(22);
    payload.extend_from_slice(&state.digital_low.to_le_bytes());
    payload.extend_from_slice(&state.digital_high.to_le_bytes());
    payload.extend_from_slice(&state.left_trigger.to_le_bytes());
    payload.extend_from_slice(&state.right_trigger.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_y.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_y.to_le_bytes());
    payload.extend_from_slice(&dt_uframes.to_le_bytes());
    payload
}

fn controller_state_parse(value: &[u8]) -> Result<ControllerState> {
    if value.len() != 20 {
        return Err(MakxdError::Protocol(
            "controller state response length is invalid".into(),
        ));
    }
    let state = ControllerState {
        digital_low: u32::from_le_bytes(value[0..4].try_into().unwrap()),
        digital_high: u32::from_le_bytes(value[4..8].try_into().unwrap()),
        left_trigger: u16::from_le_bytes(value[8..10].try_into().unwrap()),
        right_trigger: u16::from_le_bytes(value[10..12].try_into().unwrap()),
        left_stick_x: i16::from_le_bytes(value[12..14].try_into().unwrap()),
        left_stick_y: i16::from_le_bytes(value[14..16].try_into().unwrap()),
        right_stick_x: i16::from_le_bytes(value[16..18].try_into().unwrap()),
        right_stick_y: i16::from_le_bytes(value[18..20].try_into().unwrap()),
    };
    controller_state_check(state).map_err(|_| {
        MakxdError::Protocol("controller trigger response is outside 0..1023".into())
    })?;
    Ok(state)
}

impl Device {
    pub fn controller_control_state(&self, control: ControllerControl) -> Result<i32> {
        let value = self.query_api(ApiOpcode::ControllerControl, &[control as u8])?;
        if value.len() != 5 || value[0] != control as u8 {
            return Err(MakxdError::Protocol(
                "controller control response is invalid".into(),
            ));
        }
        Ok(i32::from_le_bytes(value[1..5].try_into().unwrap()))
    }

    pub fn controller_control(&self, control: ControllerControl, value: i32) -> Result<()> {
        self.controller_control_dt(control, value, 0)
    }

    pub fn controller_control_dt(
        &self,
        control: ControllerControl,
        value: i32,
        dt_uframes: u16,
    ) -> Result<()> {
        controller_value_check(control, value)?;
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        let mut payload = vec![control as u8];
        payload.extend_from_slice(&value.to_le_bytes());
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.write_api(ApiOpcode::ControllerControl, &payload)
    }

    pub fn controller_mask(
        &self,
        control: ControllerControl,
        mode: ControllerMaskMode,
    ) -> Result<()> {
        let payload = vec![control as u8, mode as u8];
        self.write_api(ApiOpcode::ControllerMask, &payload)
    }

    pub fn controller_state(&self) -> Result<ControllerState> {
        let value = self.query_api(ApiOpcode::ControllerState, &[])?;
        controller_state_parse(&value)
    }

    pub fn set_controller_state(&self, state: ControllerState) -> Result<()> {
        self.set_controller_state_dt(state, 0)
    }

    pub fn set_controller_state_dt(&self, state: ControllerState, dt_uframes: u16) -> Result<()> {
        controller_state_check(state)?;
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        self.write_api(
            ApiOpcode::ControllerState,
            &controller_state_payload(state, dt_uframes),
        )
    }
}

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn controller_control_state(&self, control: ControllerControl) -> Result<i32> {
        let value = self
            .query_api(ApiOpcode::ControllerControl, &[control as u8])
            .await?;
        if value.len() != 5 || value[0] != control as u8 {
            return Err(MakxdError::Protocol(
                "controller control response is invalid".into(),
            ));
        }
        Ok(i32::from_le_bytes(value[1..5].try_into().unwrap()))
    }

    pub async fn controller_control(&self, control: ControllerControl, value: i32) -> Result<()> {
        self.controller_control_dt(control, value, 0).await
    }

    pub async fn controller_control_dt(
        &self,
        control: ControllerControl,
        value: i32,
        dt_uframes: u16,
    ) -> Result<()> {
        controller_value_check(control, value)?;
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        let mut payload = vec![control as u8];
        payload.extend_from_slice(&value.to_le_bytes());
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.write_api(ApiOpcode::ControllerControl, &payload).await
    }

    pub async fn controller_mask(
        &self,
        control: ControllerControl,
        mode: ControllerMaskMode,
    ) -> Result<()> {
        let payload = vec![control as u8, mode as u8];
        self.write_api(ApiOpcode::ControllerMask, &payload).await
    }

    pub async fn controller_state(&self) -> Result<ControllerState> {
        let value = self.query_api(ApiOpcode::ControllerState, &[]).await?;
        controller_state_parse(&value)
    }

    pub async fn set_controller_state(&self, state: ControllerState) -> Result<()> {
        self.set_controller_state_dt(state, 0).await
    }

    pub async fn set_controller_state_dt(
        &self,
        state: ControllerState,
        dt_uframes: u16,
    ) -> Result<()> {
        controller_state_check(state)?;
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        self.write_api(
            ApiOpcode::ControllerState,
            &controller_state_payload(state, dt_uframes),
        )
        .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn trigger_is_10_bit_and_stick_is_signed_16_bit() {
        assert!(controller_value_check(
            ControllerControl::LeftTrigger,
            i32::from(CONTROLLER_TRIGGER_MAX),
        )
        .is_ok());
        assert!(controller_value_check(
            ControllerControl::LeftTrigger,
            i32::from(CONTROLLER_TRIGGER_MAX) + 1,
        )
        .is_err());
        assert!(controller_value_check(ControllerControl::RightStickX, -32768).is_ok());
        assert!(controller_value_check(ControllerControl::RightStickX, 32767).is_ok());
    }

    #[test]
    fn complete_state_rejects_out_of_range_trigger() {
        let state = ControllerState {
            left_trigger: CONTROLLER_TRIGGER_MAX + 1,
            ..ControllerState::default()
        };
        assert!(controller_state_check(state).is_err());
    }
}
