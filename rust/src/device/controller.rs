use crate::error::{MakxdError, Result};
use crate::protocol::api::{ApiOpcode, ApiVerb};
use crate::types::{ControllerControl, ControllerMaskMode, ControllerState};

use super::Device;

fn controller_value_check(control: ControllerControl, value: i32) -> Result<()> {
    let valid = match control {
        ControllerControl::LeftTrigger | ControllerControl::RightTrigger => {
            (0..=65535).contains(&value)
        }
        ControllerControl::LeftStickX
        | ControllerControl::LeftStickY
        | ControllerControl::RightStickX
        | ControllerControl::RightStickY => (-32768..=32767).contains(&value),
        _ => (0..=1).contains(&value),
    };
    if valid {
        Ok(())
    } else {
        Err(MakxdError::OutOfRange {
            value: value as i64,
            min: -32768,
            max: 65535,
        })
    }
}

fn controller_state_payload(state: ControllerState, dt_uframes: u16, generation: u32) -> Vec<u8> {
    let mut payload = Vec::with_capacity(26);
    payload.extend_from_slice(&state.digital_low.to_le_bytes());
    payload.extend_from_slice(&state.digital_high.to_le_bytes());
    payload.extend_from_slice(&state.left_trigger.to_le_bytes());
    payload.extend_from_slice(&state.right_trigger.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_y.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_y.to_le_bytes());
    payload.extend_from_slice(&dt_uframes.to_le_bytes());
    payload.extend_from_slice(&generation.to_le_bytes());
    payload
}

fn controller_state_parse(value: &[u8]) -> Result<ControllerState> {
    if value.len() != 24 {
        return Err(MakxdError::Protocol(
            "controller state response length is invalid".into(),
        ));
    }
    Ok(ControllerState {
        digital_low: u32::from_le_bytes(value[0..4].try_into().unwrap()),
        digital_high: u32::from_le_bytes(value[4..8].try_into().unwrap()),
        left_trigger: u16::from_le_bytes(value[8..10].try_into().unwrap()),
        right_trigger: u16::from_le_bytes(value[10..12].try_into().unwrap()),
        left_stick_x: i16::from_le_bytes(value[12..14].try_into().unwrap()),
        left_stick_y: i16::from_le_bytes(value[14..16].try_into().unwrap()),
        right_stick_x: i16::from_le_bytes(value[16..18].try_into().unwrap()),
        right_stick_y: i16::from_le_bytes(value[18..20].try_into().unwrap()),
    })
}

impl Device {
    fn controller_generation(&self) -> Result<u32> {
        Ok(self.device()?.generation)
    }

    pub fn controller_control_state(&self, control: ControllerControl) -> Result<i32> {
        let command = format!("km.controller({})", control.name());
        let value = self.query_api(
            command.as_bytes(),
            ApiOpcode::ControllerControl,
            ApiVerb::Get,
            &[control as u8],
        )?;
        if value.len() != 9 || value[0] != control as u8 {
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
        let generation = self.controller_generation()?;
        let command = if dt_uframes == 0 {
            format!("km.controller({},{value})", control.name())
        } else {
            format!("km.controller({},{value},{dt_uframes})", control.name())
        };
        let mut payload = vec![control as u8];
        payload.extend_from_slice(&value.to_le_bytes());
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        payload.extend_from_slice(&generation.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerControl,
            ApiVerb::Set,
            &payload,
        )
    }

    pub fn controller_mask(
        &self,
        control: ControllerControl,
        mode: ControllerMaskMode,
    ) -> Result<()> {
        let generation = self.controller_generation()?;
        let command = format!("km.controller_mask({},{})", control.name(), mode as u8);
        let mut payload = vec![control as u8, mode as u8];
        payload.extend_from_slice(&generation.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerMask,
            ApiVerb::Set,
            &payload,
        )
    }

    pub fn controller_state(&self) -> Result<ControllerState> {
        let value = self.query_api(
            b"km.controller_state()",
            ApiOpcode::ControllerState,
            ApiVerb::Get,
            &[],
        )?;
        controller_state_parse(&value)
    }

    pub fn set_controller_state(&self, state: ControllerState) -> Result<()> {
        self.set_controller_state_dt(state, 0)
    }

    pub fn set_controller_state_dt(&self, state: ControllerState, dt_uframes: u16) -> Result<()> {
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        let generation = self.controller_generation()?;
        let command = format!(
            "km.controller_state({},{},{},{},{},{},{},{},{})",
            state.digital_low,
            state.digital_high,
            state.left_trigger,
            state.right_trigger,
            state.left_stick_x,
            state.left_stick_y,
            state.right_stick_x,
            state.right_stick_y,
            dt_uframes,
        );
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, dt_uframes, generation),
        )
    }
}

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    async fn controller_generation(&self) -> Result<u32> {
        Ok(self.device().await?.generation)
    }

    pub async fn controller_control_state(&self, control: ControllerControl) -> Result<i32> {
        let command = format!("km.controller({})", control.name());
        let value = self
            .query_api(
                command.as_bytes(),
                ApiOpcode::ControllerControl,
                ApiVerb::Get,
                &[control as u8],
            )
            .await?;
        if value.len() != 9 || value[0] != control as u8 {
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
        let generation = self.controller_generation().await?;
        let command = if dt_uframes == 0 {
            format!("km.controller({},{value})", control.name())
        } else {
            format!("km.controller({},{value},{dt_uframes})", control.name())
        };
        let mut payload = vec![control as u8];
        payload.extend_from_slice(&value.to_le_bytes());
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        payload.extend_from_slice(&generation.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerControl,
            ApiVerb::Set,
            &payload,
        )
        .await
    }

    pub async fn controller_mask(
        &self,
        control: ControllerControl,
        mode: ControllerMaskMode,
    ) -> Result<()> {
        let generation = self.controller_generation().await?;
        let command = format!("km.controller_mask({},{})", control.name(), mode as u8);
        let mut payload = vec![control as u8, mode as u8];
        payload.extend_from_slice(&generation.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerMask,
            ApiVerb::Set,
            &payload,
        )
        .await
    }

    pub async fn controller_state(&self) -> Result<ControllerState> {
        let value = self
            .query_api(
                b"km.controller_state()",
                ApiOpcode::ControllerState,
                ApiVerb::Get,
                &[],
            )
            .await?;
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
        if dt_uframes > 0x3fff {
            return Err(MakxdError::OutOfRange {
                value: dt_uframes as i64,
                min: 0,
                max: 0x3fff,
            });
        }
        let generation = self.controller_generation().await?;
        let command = format!(
            "km.controller_state({},{},{},{},{},{},{},{},{})",
            state.digital_low,
            state.digital_high,
            state.left_trigger,
            state.right_trigger,
            state.left_stick_x,
            state.left_stick_y,
            state.right_stick_x,
            state.right_stick_y,
            dt_uframes,
        );
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, dt_uframes, generation),
        )
        .await
    }
}
