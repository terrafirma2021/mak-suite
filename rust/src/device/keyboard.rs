use crate::error::Result;
use crate::protocol::api::{ApiOpcode, ApiVerb, keyboard_code};
use crate::protocol::keyboard as builder;
use crate::timed;
use crate::types::KeyboardKey;

use super::Device;

impl Device {
    pub fn keyboard_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down", {
            let command = builder::build_down(&key)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyDown,
                ApiVerb::Exec,
                &[keyboard_code(&key)?],
            )
        })
    }

    pub fn keyboard_down_dt<K: Into<KeyboardKey>>(&self, key: K, dt_uframes: u16) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down_dt", {
            let command = builder::build_down_dt(&key, dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyDown,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up", {
            let command = builder::build_up(&key)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyUp,
                ApiVerb::Exec,
                &[keyboard_code(&key)?],
            )
        })
    }

    pub fn keyboard_up_dt<K: Into<KeyboardKey>>(&self, key: K, dt_uframes: u16) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up_dt", {
            let command = builder::build_up_dt(&key, dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyUp,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_press<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        self.keyboard_press_with_timing(key, None, None)
    }

    pub fn keyboard_press_for<K: Into<KeyboardKey>>(&self, key: K, hold_ms: u32) -> Result<()> {
        self.keyboard_press_with_timing(key, Some(hold_ms), None)
    }

    pub fn keyboard_press_randomized<K: Into<KeyboardKey>>(
        &self,
        key: K,
        hold_ms: u32,
        rand_ms: u32,
    ) -> Result<()> {
        self.keyboard_press_with_timing(key, Some(hold_ms), Some(rand_ms))
    }

    fn keyboard_press_with_timing<K: Into<KeyboardKey>>(
        &self,
        key: K,
        hold_ms: Option<u32>,
        rand_ms: Option<u32>,
    ) -> Result<()> {
        let key = key.into();
        timed!("keyboard_press", {
            let command = builder::build_press(&key, hold_ms, rand_ms)?;
            let mut payload = vec![keyboard_code(&key)?];
            if let Some(hold) = hold_ms {
                payload.extend_from_slice(&hold.to_le_bytes());
            }
            if let Some(random) = rand_ms {
                payload.extend_from_slice(&random.to_le_bytes());
            }
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyPress,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_string(&self, text: &str) -> Result<()> {
        timed!("keyboard_string", {
            let command = builder::build_string(text)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyString,
                ApiVerb::Exec,
                text.as_bytes(),
            )
        })
    }

    pub fn keyboard_init(&self) -> Result<()> {
        timed!("keyboard_init", {
            self.exec_api(
                builder::build_init().as_bytes(),
                ApiOpcode::KeyInit,
                ApiVerb::Exec,
                &[],
            )
        })
    }

    pub fn keyboard_init_dt(&self, dt_uframes: u16) -> Result<()> {
        timed!("keyboard_init_dt", {
            let command = builder::build_init_dt(dt_uframes)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyInit,
                ApiVerb::Exec,
                &dt_uframes.to_le_bytes(),
            )
        })
    }

    pub fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        timed!("keyboard_is_down", {
            let command = builder::build_is_down(&key)?;
            let value = self.query_api(
                command.as_bytes(),
                ApiOpcode::KeyIsDown,
                ApiVerb::Get,
                &[keyboard_code(&key)?],
            )?;
            Ok(value == b"1" || value == b"\x01")
        })
    }

    pub fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        timed!("keyboard_mask", {
            let command = builder::build_mask(&key, enable)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMask,
                ApiVerb::Set,
                &[keyboard_code(&key)?, enable as u8],
            )
        })
    }

    pub fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        &self,
        source: S,
        target: T,
    ) -> Result<()> {
        let source = source.into();
        let target = target.into();
        timed!("keyboard_remap", {
            let command = builder::build_remap(&source, &target)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyRemap,
                ApiVerb::Set,
                &[keyboard_code(&source)?, keyboard_code(&target)?],
            )
        })
    }

    pub fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_down", {
            let command = builder::build_key_list("km.multidown", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiDown,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_up", {
            let command = builder::build_key_list("km.multiup", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiUp,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_press", {
            let command = builder::build_key_list("km.multipress", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiPress,
                ApiVerb::Exec,
                &payload,
            )
        })
    }

    pub fn keyboard_keys(&self) -> Result<String> {
        timed!("keyboard_keys", {
            let command = builder::build_keys(None);
            let value =
                self.query_api(command.as_bytes(), ApiOpcode::KeyKeys, ApiVerb::Get, &[])?;
            String::from_utf8(value).map_err(|_| {
                crate::error::MakxdError::Protocol("keyboard keys response is not ASCII".into())
            })
        })
    }

    pub fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        timed!("keyboard_keys_set", {
            self.exec_api(
                builder::build_keys(Some(enabled)).as_bytes(),
                ApiOpcode::KeyKeys,
                ApiVerb::Set,
                &[enabled as u8],
            )
        })
    }
}

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn keyboard_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down", {
            let command = builder::build_down(&key)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyDown,
                ApiVerb::Exec,
                &[keyboard_code(&key)?],
            )
            .await
        })
    }

    pub async fn keyboard_down_dt<K: Into<KeyboardKey>>(
        &self,
        key: K,
        dt_uframes: u16,
    ) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down_dt", {
            let command = builder::build_down_dt(&key, dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyDown,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up", {
            let command = builder::build_up(&key)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyUp,
                ApiVerb::Exec,
                &[keyboard_code(&key)?],
            )
            .await
        })
    }

    pub async fn keyboard_up_dt<K: Into<KeyboardKey>>(
        &self,
        key: K,
        dt_uframes: u16,
    ) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up_dt", {
            let command = builder::build_up_dt(&key, dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyUp,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_press<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        self.keyboard_press_with_timing(key, None, None).await
    }

    pub async fn keyboard_press_for<K: Into<KeyboardKey>>(
        &self,
        key: K,
        hold_ms: u32,
    ) -> Result<()> {
        self.keyboard_press_with_timing(key, Some(hold_ms), None)
            .await
    }

    pub async fn keyboard_press_randomized<K: Into<KeyboardKey>>(
        &self,
        key: K,
        hold_ms: u32,
        rand_ms: u32,
    ) -> Result<()> {
        self.keyboard_press_with_timing(key, Some(hold_ms), Some(rand_ms))
            .await
    }

    async fn keyboard_press_with_timing<K: Into<KeyboardKey>>(
        &self,
        key: K,
        hold_ms: Option<u32>,
        rand_ms: Option<u32>,
    ) -> Result<()> {
        let key = key.into();
        timed!("keyboard_press", {
            let command = builder::build_press(&key, hold_ms, rand_ms)?;
            let mut payload = vec![keyboard_code(&key)?];
            if let Some(hold) = hold_ms {
                payload.extend_from_slice(&hold.to_le_bytes());
            }
            if let Some(random) = rand_ms {
                payload.extend_from_slice(&random.to_le_bytes());
            }
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyPress,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_string(&self, text: &str) -> Result<()> {
        timed!("keyboard_string", {
            let command = builder::build_string(text)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyString,
                ApiVerb::Exec,
                text.as_bytes(),
            )
            .await
        })
    }

    pub async fn keyboard_init(&self) -> Result<()> {
        timed!("keyboard_init", {
            self.exec_api(
                builder::build_init().as_bytes(),
                ApiOpcode::KeyInit,
                ApiVerb::Exec,
                &[],
            )
            .await
        })
    }

    pub async fn keyboard_init_dt(&self, dt_uframes: u16) -> Result<()> {
        timed!("keyboard_init_dt", {
            let command = builder::build_init_dt(dt_uframes)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyInit,
                ApiVerb::Exec,
                &dt_uframes.to_le_bytes(),
            )
            .await
        })
    }

    pub async fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        timed!("keyboard_is_down", {
            let command = builder::build_is_down(&key)?;
            let value = self
                .query_api(
                    command.as_bytes(),
                    ApiOpcode::KeyIsDown,
                    ApiVerb::Get,
                    &[keyboard_code(&key)?],
                )
                .await?;
            Ok(value == b"1" || value == b"\x01")
        })
    }

    pub async fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        timed!("keyboard_mask", {
            let command = builder::build_mask(&key, enable)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMask,
                ApiVerb::Set,
                &[keyboard_code(&key)?, enable as u8],
            )
            .await
        })
    }

    pub async fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        &self,
        source: S,
        target: T,
    ) -> Result<()> {
        let source = source.into();
        let target = target.into();
        timed!("keyboard_remap", {
            let command = builder::build_remap(&source, &target)?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyRemap,
                ApiVerb::Set,
                &[keyboard_code(&source)?, keyboard_code(&target)?],
            )
            .await
        })
    }

    pub async fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_down", {
            let command = builder::build_key_list("km.multidown", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiDown,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_up", {
            let command = builder::build_key_list("km.multiup", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiUp,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_press", {
            let command = builder::build_key_list("km.multipress", keys)?;
            let payload = keys.iter().map(keyboard_code).collect::<Result<Vec<_>>>()?;
            self.exec_api(
                command.as_bytes(),
                ApiOpcode::KeyMultiPress,
                ApiVerb::Exec,
                &payload,
            )
            .await
        })
    }

    pub async fn keyboard_keys(&self) -> Result<String> {
        timed!("keyboard_keys", {
            let command = builder::build_keys(None);
            let value = self
                .query_api(command.as_bytes(), ApiOpcode::KeyKeys, ApiVerb::Get, &[])
                .await?;
            String::from_utf8(value).map_err(|_| {
                crate::error::MakxdError::Protocol("keyboard keys response is not ASCII".into())
            })
        })
    }

    pub async fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        timed!("keyboard_keys_set", {
            self.exec_api(
                builder::build_keys(Some(enabled)).as_bytes(),
                ApiOpcode::KeyKeys,
                ApiVerb::Set,
                &[enabled as u8],
            )
            .await
        })
    }
}
