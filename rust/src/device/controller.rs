use crate::error::{MakxdError, Result};
use crate::protocol::api::{
    ApiOpcode, ApiVerb, controller_button_mask_opcode, controller_button_opcode,
};
use crate::protocol::builder;
use crate::types::{ControllerButton, ControllerState};

#[cfg(feature = "async")]
use super::AsyncDevice;
use super::Device;

fn controller_state_values(state: ControllerState) -> [i64; 10] {
    [
        state.buttons as i64,
        state.hat as i64,
        state.lt as i64,
        state.rt as i64,
        state.x as i64,
        state.y as i64,
        state.rx as i64,
        state.ry as i64,
        state.z as i64,
        state.rz as i64,
    ]
}

fn controller_state_payload(state: ControllerState, dt_uframes: Option<u16>) -> Vec<u8> {
    let mut payload = Vec::with_capacity(23);
    payload.extend_from_slice(&state.buttons.to_le_bytes());
    payload.push(state.hat);
    payload.extend_from_slice(&state.lt.to_le_bytes());
    payload.extend_from_slice(&state.rt.to_le_bytes());
    payload.extend_from_slice(&state.x.to_le_bytes());
    payload.extend_from_slice(&state.y.to_le_bytes());
    payload.extend_from_slice(&state.rx.to_le_bytes());
    payload.extend_from_slice(&state.ry.to_le_bytes());
    payload.extend_from_slice(&state.z.to_le_bytes());
    payload.extend_from_slice(&state.rz.to_le_bytes());
    if let Some(dt) = dt_uframes {
        payload.extend_from_slice(&dt.to_le_bytes());
    }
    payload
}

fn controller_hat_check(hat: u8) -> Result<()> {
    if hat > 8 {
        return Err(MakxdError::OutOfRange {
            value: hat as i64,
            min: 0,
            max: 8,
        });
    }
    Ok(())
}

fn controller_boolean_response(value: &[u8]) -> Result<bool> {
    match value {
        [0] | b"0" => Ok(false),
        [1] | b"1" => Ok(true),
        _ => Err(MakxdError::Protocol(
            "controller status response must be 0 or 1".into(),
        )),
    }
}

impl Device {
    pub fn controller_state(&self, state: ControllerState) -> Result<()> {
        controller_hat_check(state.hat)?;
        let values = controller_state_values(state);
        let command = builder::build_controller_command("controller", &values, None)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, None),
        )
    }

    pub fn controller_state_dt(&self, state: ControllerState, dt_uframes: u16) -> Result<()> {
        controller_hat_check(state.hat)?;
        let values = controller_state_values(state);
        let command = builder::build_controller_command("controller", &values, Some(dt_uframes))?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, Some(dt_uframes)),
        )
    }

    pub fn controller_button_state(&self, button: ControllerButton) -> Result<bool> {
        let number = button.number();
        let command = format!("km.controller_button{number}()");
        controller_boolean_response(&self.query_api(
            command.as_bytes(),
            controller_button_opcode(button),
            ApiVerb::Get,
            &[],
        )?)
    }

    pub fn controller_button(&self, button: ControllerButton, pressed: bool) -> Result<()> {
        let number = button.number();
        let command = format!("km.controller_button{number}({})", pressed as u8,);
        self.exec_api(
            command.as_bytes(),
            controller_button_opcode(button),
            ApiVerb::Set,
            &[pressed as u8],
        )
    }

    pub fn controller_button_dt(
        &self,
        button: ControllerButton,
        pressed: bool,
        dt_uframes: u16,
    ) -> Result<()> {
        let number = button.number();
        let command = builder::build_controller_command(
            &format!("controller_button{number}"),
            &[pressed as i64],
            Some(dt_uframes),
        )?;
        let mut payload = vec![pressed as u8];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            controller_button_opcode(button),
            ApiVerb::Set,
            &payload,
        )
    }

    pub fn controller_button_mask(&self, button: ControllerButton, enabled: bool) -> Result<()> {
        let number = button.number();
        let command = format!("km.controller_button{number}_mask({})", enabled as u8,);
        self.exec_api(
            command.as_bytes(),
            controller_button_mask_opcode(button),
            ApiVerb::Set,
            &[enabled as u8],
        )
    }

    pub fn controller_button_mask_dt(
        &self,
        button: ControllerButton,
        enabled: bool,
        dt_uframes: u16,
    ) -> Result<()> {
        let number = button.number();
        let command = builder::build_controller_command(
            &format!("controller_button{number}_mask"),
            &[enabled as i64],
            Some(dt_uframes),
        )?;
        let mut payload = vec![enabled as u8];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            controller_button_mask_opcode(button),
            ApiVerb::Set,
            &payload,
        )
    }
}

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn controller_state(&self, state: ControllerState) -> Result<()> {
        controller_hat_check(state.hat)?;
        let values = controller_state_values(state);
        let command = builder::build_controller_command("controller", &values, None)?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, None),
        )
        .await
    }

    pub async fn controller_state_dt(&self, state: ControllerState, dt_uframes: u16) -> Result<()> {
        controller_hat_check(state.hat)?;
        let values = controller_state_values(state);
        let command = builder::build_controller_command("controller", &values, Some(dt_uframes))?;
        self.exec_api(
            command.as_bytes(),
            ApiOpcode::ControllerState,
            ApiVerb::Set,
            &controller_state_payload(state, Some(dt_uframes)),
        )
        .await
    }

    pub async fn controller_button_state(&self, button: ControllerButton) -> Result<bool> {
        let number = button.number();
        let command = format!("km.controller_button{number}()");
        controller_boolean_response(
            &self
                .query_api(
                    command.as_bytes(),
                    controller_button_opcode(button),
                    ApiVerb::Get,
                    &[],
                )
                .await?,
        )
    }

    pub async fn controller_button(&self, button: ControllerButton, pressed: bool) -> Result<()> {
        let number = button.number();
        let command = format!("km.controller_button{number}({})", pressed as u8,);
        self.exec_api(
            command.as_bytes(),
            controller_button_opcode(button),
            ApiVerb::Set,
            &[pressed as u8],
        )
        .await
    }

    pub async fn controller_button_dt(
        &self,
        button: ControllerButton,
        pressed: bool,
        dt_uframes: u16,
    ) -> Result<()> {
        let number = button.number();
        let command = builder::build_controller_command(
            &format!("controller_button{number}"),
            &[pressed as i64],
            Some(dt_uframes),
        )?;
        let mut payload = vec![pressed as u8];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            controller_button_opcode(button),
            ApiVerb::Set,
            &payload,
        )
        .await
    }

    pub async fn controller_button_mask(
        &self,
        button: ControllerButton,
        enabled: bool,
    ) -> Result<()> {
        let number = button.number();
        let command = format!("km.controller_button{number}_mask({})", enabled as u8,);
        self.exec_api(
            command.as_bytes(),
            controller_button_mask_opcode(button),
            ApiVerb::Set,
            &[enabled as u8],
        )
        .await
    }

    pub async fn controller_button_mask_dt(
        &self,
        button: ControllerButton,
        enabled: bool,
        dt_uframes: u16,
    ) -> Result<()> {
        let number = button.number();
        let command = builder::build_controller_command(
            &format!("controller_button{number}_mask"),
            &[enabled as i64],
            Some(dt_uframes),
        )?;
        let mut payload = vec![enabled as u8];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.exec_api(
            command.as_bytes(),
            controller_button_mask_opcode(button),
            ApiVerb::Set,
            &payload,
        )
        .await
    }
}

macro_rules! controller_value_methods {
    ($plain:ident, $timed:ident, $command:literal, $opcode:expr) => {
        impl Device {
            pub fn $plain(&self, value: u16) -> Result<()> {
                let command = builder::build_controller_command($command, &[value as i64], None)?;
                self.exec_api(
                    command.as_bytes(),
                    $opcode,
                    ApiVerb::Set,
                    &value.to_le_bytes(),
                )
            }

            pub fn $timed(&self, value: u16, dt_uframes: u16) -> Result<()> {
                let command =
                    builder::build_controller_command($command, &[value as i64], Some(dt_uframes))?;
                let mut payload = value.to_le_bytes().to_vec();
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }
        }
        #[cfg(feature = "async")]
        impl AsyncDevice {
            pub async fn $plain(&self, value: u16) -> Result<()> {
                let command = builder::build_controller_command($command, &[value as i64], None)?;
                self.exec_api(
                    command.as_bytes(),
                    $opcode,
                    ApiVerb::Set,
                    &value.to_le_bytes(),
                )
                .await
            }

            pub async fn $timed(&self, value: u16, dt_uframes: u16) -> Result<()> {
                let command =
                    builder::build_controller_command($command, &[value as i64], Some(dt_uframes))?;
                let mut payload = value.to_le_bytes().to_vec();
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }
        }
    };
}

controller_value_methods!(
    controller_left_trigger,
    controller_left_trigger_dt,
    "controller_lt",
    ApiOpcode::ControllerLt
);
controller_value_methods!(
    controller_right_trigger,
    controller_right_trigger_dt,
    "controller_rt",
    ApiOpcode::ControllerRt
);

macro_rules! controller_pair_methods {
    ($plain:ident, $timed:ident, $command:literal, $opcode:expr) => {
        impl Device {
            pub fn $plain(&self, first: i16, second: i16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[first as i64, second as i64],
                    None,
                )?;
                let mut payload = first.to_le_bytes().to_vec();
                payload.extend_from_slice(&second.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }

            pub fn $timed(&self, first: i16, second: i16, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[first as i64, second as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = first.to_le_bytes().to_vec();
                payload.extend_from_slice(&second.to_le_bytes());
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }
        }
        #[cfg(feature = "async")]
        impl AsyncDevice {
            pub async fn $plain(&self, first: i16, second: i16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[first as i64, second as i64],
                    None,
                )?;
                let mut payload = first.to_le_bytes().to_vec();
                payload.extend_from_slice(&second.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }

            pub async fn $timed(&self, first: i16, second: i16, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[first as i64, second as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = first.to_le_bytes().to_vec();
                payload.extend_from_slice(&second.to_le_bytes());
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }
        }
    };
}

controller_pair_methods!(
    controller_left_stick,
    controller_left_stick_dt,
    "controller_left_stick",
    ApiOpcode::ControllerLeftStick
);
controller_pair_methods!(
    controller_right_stick,
    controller_right_stick_dt,
    "controller_right_stick",
    ApiOpcode::ControllerRightStick
);
controller_pair_methods!(
    controller_aux,
    controller_aux_dt,
    "controller_aux",
    ApiOpcode::ControllerAux
);

macro_rules! controller_hat_methods {
    ($state:ident, $plain:ident, $timed:ident, $command:literal, $opcode:expr) => {
        impl Device {
            pub fn $state(&self) -> Result<bool> {
                controller_boolean_response(&self.query_api(
                    concat!("km.", $command, "()").as_bytes(),
                    $opcode,
                    ApiVerb::Get,
                    &[],
                )?)
            }

            pub fn $plain(&self, pressed: bool) -> Result<()> {
                let command = builder::build_controller_command($command, &[pressed as i64], None)?;
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &[pressed as u8])
            }

            pub fn $timed(&self, pressed: bool, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[pressed as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = vec![pressed as u8];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }
        }
        #[cfg(feature = "async")]
        impl AsyncDevice {
            pub async fn $state(&self) -> Result<bool> {
                controller_boolean_response(
                    &self
                        .query_api(
                            concat!("km.", $command, "()").as_bytes(),
                            $opcode,
                            ApiVerb::Get,
                            &[],
                        )
                        .await?,
                )
            }

            pub async fn $plain(&self, pressed: bool) -> Result<()> {
                let command = builder::build_controller_command($command, &[pressed as i64], None)?;
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &[pressed as u8])
                    .await
            }

            pub async fn $timed(&self, pressed: bool, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[pressed as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = vec![pressed as u8];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }
        }
    };
}

controller_hat_methods!(
    controller_hat_left_state,
    controller_hat_left,
    controller_hat_left_dt,
    "controller_hat_left",
    ApiOpcode::ControllerHatLeft
);
controller_hat_methods!(
    controller_hat_right_state,
    controller_hat_right,
    controller_hat_right_dt,
    "controller_hat_right",
    ApiOpcode::ControllerHatRight
);
controller_hat_methods!(
    controller_hat_down_state,
    controller_hat_down,
    controller_hat_down_dt,
    "controller_hat_down",
    ApiOpcode::ControllerHatDown
);
controller_hat_methods!(
    controller_hat_up_state,
    controller_hat_up,
    controller_hat_up_dt,
    "controller_hat_up",
    ApiOpcode::ControllerHatUp
);

macro_rules! controller_mask_methods {
    ($plain:ident, $timed:ident, $command:literal, $opcode:expr) => {
        impl Device {
            pub fn $plain(&self, enabled: bool) -> Result<()> {
                let command = builder::build_controller_command($command, &[enabled as i64], None)?;
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &[enabled as u8])
            }

            pub fn $timed(&self, enabled: bool, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[enabled as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = vec![enabled as u8];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }
        }
        #[cfg(feature = "async")]
        impl AsyncDevice {
            pub async fn $plain(&self, enabled: bool) -> Result<()> {
                let command = builder::build_controller_command($command, &[enabled as i64], None)?;
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &[enabled as u8])
                    .await
            }

            pub async fn $timed(&self, enabled: bool, dt_uframes: u16) -> Result<()> {
                let command = builder::build_controller_command(
                    $command,
                    &[enabled as i64],
                    Some(dt_uframes),
                )?;
                let mut payload = vec![enabled as u8];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }
        }
    };
}

controller_mask_methods!(
    controller_left_trigger_mask,
    controller_left_trigger_mask_dt,
    "controller_lt_mask",
    ApiOpcode::ControllerLtMask
);
controller_mask_methods!(
    controller_right_trigger_mask,
    controller_right_trigger_mask_dt,
    "controller_rt_mask",
    ApiOpcode::ControllerRtMask
);
controller_mask_methods!(
    controller_hat_left_mask,
    controller_hat_left_mask_dt,
    "controller_hat_left_mask",
    ApiOpcode::ControllerHatLeftMask
);
controller_mask_methods!(
    controller_hat_right_mask,
    controller_hat_right_mask_dt,
    "controller_hat_right_mask",
    ApiOpcode::ControllerHatRightMask
);
controller_mask_methods!(
    controller_hat_down_mask,
    controller_hat_down_mask_dt,
    "controller_hat_down_mask",
    ApiOpcode::ControllerHatDownMask
);
controller_mask_methods!(
    controller_hat_up_mask,
    controller_hat_up_mask_dt,
    "controller_hat_up_mask",
    ApiOpcode::ControllerHatUpMask
);

macro_rules! controller_direction_mask_methods {
    ($plain:ident, $timed:ident, $command:literal, $opcode:expr) => {
        impl Device {
            pub fn $plain(
                &self,
                first_negative: bool,
                first_positive: bool,
                second_negative: bool,
                second_positive: bool,
            ) -> Result<()> {
                let values = [
                    first_negative as i64,
                    first_positive as i64,
                    second_negative as i64,
                    second_positive as i64,
                ];
                let command = builder::build_controller_command($command, &values, None)?;
                let payload = [
                    first_negative as u8,
                    first_positive as u8,
                    second_negative as u8,
                    second_positive as u8,
                ];
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }

            pub fn $timed(
                &self,
                first_negative: bool,
                first_positive: bool,
                second_negative: bool,
                second_positive: bool,
                dt_uframes: u16,
            ) -> Result<()> {
                let values = [
                    first_negative as i64,
                    first_positive as i64,
                    second_negative as i64,
                    second_positive as i64,
                ];
                let command =
                    builder::build_controller_command($command, &values, Some(dt_uframes))?;
                let mut payload = vec![
                    first_negative as u8,
                    first_positive as u8,
                    second_negative as u8,
                    second_positive as u8,
                ];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
            }
        }
        #[cfg(feature = "async")]
        impl AsyncDevice {
            pub async fn $plain(
                &self,
                first_negative: bool,
                first_positive: bool,
                second_negative: bool,
                second_positive: bool,
            ) -> Result<()> {
                let values = [
                    first_negative as i64,
                    first_positive as i64,
                    second_negative as i64,
                    second_positive as i64,
                ];
                let command = builder::build_controller_command($command, &values, None)?;
                let payload = [
                    first_negative as u8,
                    first_positive as u8,
                    second_negative as u8,
                    second_positive as u8,
                ];
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }

            pub async fn $timed(
                &self,
                first_negative: bool,
                first_positive: bool,
                second_negative: bool,
                second_positive: bool,
                dt_uframes: u16,
            ) -> Result<()> {
                let values = [
                    first_negative as i64,
                    first_positive as i64,
                    second_negative as i64,
                    second_positive as i64,
                ];
                let command =
                    builder::build_controller_command($command, &values, Some(dt_uframes))?;
                let mut payload = vec![
                    first_negative as u8,
                    first_positive as u8,
                    second_negative as u8,
                    second_positive as u8,
                ];
                payload.extend_from_slice(&dt_uframes.to_le_bytes());
                self.exec_api(command.as_bytes(), $opcode, ApiVerb::Set, &payload)
                    .await
            }
        }
    };
}

controller_direction_mask_methods!(
    controller_left_stick_mask,
    controller_left_stick_mask_dt,
    "controller_left_stick_mask",
    ApiOpcode::ControllerLeftStickMask
);
controller_direction_mask_methods!(
    controller_right_stick_mask,
    controller_right_stick_mask_dt,
    "controller_right_stick_mask",
    ApiOpcode::ControllerRightStickMask
);
controller_direction_mask_methods!(
    controller_aux_mask,
    controller_aux_mask_dt,
    "controller_aux_mask",
    ApiOpcode::ControllerAuxMask
);
