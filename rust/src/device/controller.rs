use crate::error::{MakxdError, Result};
use crate::protocol::api::ApiOpcode;
use crate::types::{
    CONTROLLER_TRIGGER_MAX, ControllerControl, ControllerMaskMode, ControllerState, ControllerSnapshot,
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

fn controller_state_payload(state: ControllerState) -> Vec<u8> {
    let mut payload = Vec::with_capacity(20);
    payload.extend_from_slice(&state.digital_low.to_le_bytes());
    payload.extend_from_slice(&state.digital_high.to_le_bytes());
    payload.extend_from_slice(&state.left_trigger.to_le_bytes());
    payload.extend_from_slice(&state.right_trigger.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.left_stick_y.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_x.to_le_bytes());
    payload.extend_from_slice(&state.right_stick_y.to_le_bytes());

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

fn controller_snapshot_parse(value: &[u8]) -> Result<ControllerSnapshot> {
    if value.len() != 32 {
        return Err(MakxdError::Protocol("physical controller snapshot unavailable or invalid".into()));
    }
    Ok(ControllerSnapshot {
        state: controller_state_parse(&value[..20])?,
        sequence: u32::from_le_bytes(value[20..24].try_into().unwrap()),
        usb_timestamp: u32::from_le_bytes(value[24..28].try_into().unwrap()),
        timing: u16::from_le_bytes(value[28..30].try_into().unwrap()),
        report_uframes: u16::from_le_bytes(value[30..32].try_into().unwrap()),
    })
}

impl Device {
    pub fn controller_physical(&self) -> Result<ControllerSnapshot> {
        controller_snapshot_parse(&self.query_api(ApiOpcode::ControllerPhysical, &[])?)
    }
    pub fn controller_stream(&self, enabled: bool) -> Result<()> {
        self.write_api(ApiOpcode::ControllerStream, &[u8::from(enabled)])
    }
    pub fn controller_stream_state(&self) -> Result<bool> {
        let value = self.query_api(ApiOpcode::ControllerStream, &[])?;
        super::stream::stream_state_parse(&value)
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

    /// Send a complete target state, not a timed move. On MAKCU handoff firmware,
    /// finish with a zero stick pair / trigger, retaining other active controls.
    /// Silence is not release. See protocol/MAK_API.md for firmware requirements.
    pub fn set_controller_state(&self, state: ControllerState) -> Result<()> {
        controller_state_check(state)?;
        self.write_api(ApiOpcode::ControllerState, &controller_state_payload(state))
    }
}

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn controller_physical(&self) -> Result<ControllerSnapshot> {
        controller_snapshot_parse(&self.query_api(ApiOpcode::ControllerPhysical, &[]).await?)
    }
    pub async fn controller_stream(&self, enabled: bool) -> Result<()> {
        self.write_api(ApiOpcode::ControllerStream, &[u8::from(enabled)])
            .await
    }
    pub async fn controller_stream_state(&self) -> Result<bool> {
        let value = self.query_api(ApiOpcode::ControllerStream, &[]).await?;
        super::stream::stream_state_parse(&value)
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

    /// Send a complete target state, not a timed move. On MAKCU handoff firmware,
    /// finish with a zero stick pair / trigger, retaining other active controls.
    /// Silence is not release. See protocol/MAK_API.md for firmware requirements.
    pub async fn set_controller_state(&self, state: ControllerState) -> Result<()> {
        controller_state_check(state)?;
        self.write_api(ApiOpcode::ControllerState, &controller_state_payload(state))
            .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn physical_snapshot_layout_and_errors() {
        let state = ControllerState { left_trigger: 1023, left_stick_x: -32768,
            right_stick_y: 32767, ..Default::default() };
        let mut raw = controller_state_payload(state);
        raw.extend_from_slice(&u32::MAX.to_le_bytes());
        raw.extend_from_slice(&1234u32.to_le_bytes());
        raw.extend_from_slice(&8u16.to_le_bytes());
        raw.extend_from_slice(&8u16.to_le_bytes());
        let snapshot = controller_snapshot_parse(&raw).unwrap();
        assert_eq!(snapshot.state, state);
        assert_eq!(snapshot.sequence, u32::MAX);
        assert_eq!(snapshot.usb_timestamp, 1234);
        assert_eq!(snapshot.dt_uframes(), 8);
        assert!(controller_snapshot_parse(&[255]).is_err());
        assert!(controller_snapshot_parse(&raw[..27]).is_err());
        raw[8] = 0; raw[9] = 4;
        assert!(controller_snapshot_parse(&raw).is_err());
    }

    #[cfg(feature = "mock")]
    #[test]
    fn input_commands_send_exact_payloads() {
        use crate::types::Button;
        let (device, mock) = Device::mock();
        device.button_down(Button::Left).unwrap();
        device.button_up(Button::Left).unwrap();
        device.move_xy(12, -7).unwrap();
        device.wheel(-2).unwrap();
        device.keyboard_down(4u8).unwrap();
        device.keyboard_up(4u8).unwrap();
        device.keyboard_init().unwrap();
        device.controller_stream(true).unwrap();
        device
            .set_controller_state(ControllerState::default())
            .unwrap();
        device.keyboard_press_randomized(4u8, 10, 5).unwrap();
        // A query waits for the worker to drain preceding SETs.
        let _ = device.controller_stream_state();
        let commands = mock.sent_commands();
        let expected: &[(u8, &[u8])] = &[
            (0x11, &[1]),
            (0x11, &[0]),
            (0x18, &[12, 0, 249, 255]),
            (0x19, &[254, 255]),
            (0x20, &[4]),
            (0x21, &[4]),
            (0x22, &[]),
            (0x41, &[1]),
            (0x40, &[0; 20]),
            (0x23, &[4, 10, 0, 0, 0, 5, 0, 0, 0]),
            (0x41, &[]),
        ];
        assert_eq!(commands.len(), expected.len());
        for (actual, &(opcode, payload)) in commands.iter().zip(expected) {
            assert_eq!(&actual[..5], &[0xde, 0xad, payload.len() as u8, 0, opcode]);
            assert_eq!(&actual[5..], payload);
        }
    }

    #[test]
    fn trigger_is_10_bit_and_stick_is_signed_16_bit() {
        assert!(
            controller_value_check(
                ControllerControl::LeftTrigger,
                i32::from(CONTROLLER_TRIGGER_MAX),
            )
            .is_ok()
        );
        assert!(
            controller_value_check(
                ControllerControl::LeftTrigger,
                i32::from(CONTROLLER_TRIGGER_MAX) + 1,
            )
            .is_err()
        );
        assert!(controller_value_check(ControllerControl::RightStickX, -32768).is_ok());
        assert!(controller_value_check(ControllerControl::RightStickX, 32767).is_ok());
    }

    #[test]
    fn complete_state_accepts_full_trigger_range() {
        let state = ControllerState {
            left_trigger: 1023,
            right_trigger: 1023,
            ..ControllerState::default()
        };
        assert_eq!(
            controller_state_parse(&controller_state_payload(state))
                .unwrap()
                .left_trigger,
            1023
        );
    }
}
