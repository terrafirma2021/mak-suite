use crate::error::{MakxdError, Result};
use crate::protocol::api::{ApiOpcode, keyboard_code};
use crate::timed;
use crate::types::KeyboardKey;

use super::Device;

const KEYBOARD_DT_MAX: u16 = 0x3fff;
const KEYBOARD_STRING_MAX: usize = 248;
const KEYBOARD_MULTI_MAX: usize = 14;

fn keyboard_dt_check(dt_uframes: u16) -> Result<()> {
    if dt_uframes > KEYBOARD_DT_MAX {
        return Err(MakxdError::OutOfRange {
            value: dt_uframes as i64,
            min: 0,
            max: KEYBOARD_DT_MAX as i64,
        });
    }
    Ok(())
}

fn keyboard_string_check(text: &str) -> Result<()> {
    if !text.is_ascii() {
        return Err(MakxdError::Protocol(
            "keyboard string must contain ASCII bytes".into(),
        ));
    }
    if text.len() > KEYBOARD_STRING_MAX {
        return Err(MakxdError::OutOfRange {
            value: text.len() as i64,
            min: 0,
            max: KEYBOARD_STRING_MAX as i64,
        });
    }
    Ok(())
}

fn keyboard_multi_payload(keys: &[KeyboardKey]) -> Result<Vec<u8>> {
    if keys.is_empty() || keys.len() > KEYBOARD_MULTI_MAX {
        return Err(MakxdError::OutOfRange {
            value: keys.len() as i64,
            min: 1,
            max: KEYBOARD_MULTI_MAX as i64,
        });
    }
    keys.iter().map(keyboard_code).collect()
}

fn keyboard_press_payload(
    key: &KeyboardKey,
    hold_ms: Option<u32>,
    rand_ms: Option<u32>,
) -> Result<Vec<u8>> {
    if rand_ms.is_some() && hold_ms.is_none() {
        return Err(MakxdError::Protocol(
            "keyboard random time requires a hold time".into(),
        ));
    }
    let mut payload = vec![keyboard_code(key)?];
    if let Some(hold) = hold_ms {
        payload.extend_from_slice(&hold.to_le_bytes());
    }
    if let Some(random) = rand_ms {
        payload.extend_from_slice(&random.to_le_bytes());
    }
    Ok(payload)
}

impl Device {
    pub fn keyboard_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!(
            "keyboard_down",
            self.write_api(ApiOpcode::KeyDown, &[keyboard_code(&key)?])
        )
    }

    pub fn keyboard_down_dt<K: Into<KeyboardKey>>(&self, key: K, dt_uframes: u16) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down_dt", {
            keyboard_dt_check(dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::KeyDown, &payload)
        })
    }

    pub fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!(
            "keyboard_up",
            self.write_api(ApiOpcode::KeyUp, &[keyboard_code(&key)?])
        )
    }

    pub fn keyboard_up_dt<K: Into<KeyboardKey>>(&self, key: K, dt_uframes: u16) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up_dt", {
            keyboard_dt_check(dt_uframes)?;
            let mut payload = vec![keyboard_code(&key)?];
            payload.extend_from_slice(&dt_uframes.to_le_bytes());
            self.write_api(ApiOpcode::KeyUp, &payload)
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
            let payload = keyboard_press_payload(&key, hold_ms, rand_ms)?;
            self.write_api(ApiOpcode::KeyPress, &payload)
        })
    }

    pub fn keyboard_string(&self, text: &str) -> Result<()> {
        timed!("keyboard_string", {
            keyboard_string_check(text)?;
            self.write_api(ApiOpcode::KeyString, text.as_bytes())
        })
    }

    pub fn keyboard_init(&self) -> Result<()> {
        timed!("keyboard_init", self.write_api(ApiOpcode::KeyInit, &[]))
    }

    pub fn keyboard_init_dt(&self, dt_uframes: u16) -> Result<()> {
        timed!("keyboard_init_dt", {
            keyboard_dt_check(dt_uframes)?;
            self.write_api(ApiOpcode::KeyInit, &dt_uframes.to_le_bytes())
        })
    }

    pub fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        timed!("keyboard_is_down", {
            let value = self.query_api(ApiOpcode::KeyIsDown, &[keyboard_code(&key)?])?;
            Ok(value == b"\x01")
        })
    }

    pub fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        timed!(
            "keyboard_mask",
            self.write_api(ApiOpcode::KeyMask, &[keyboard_code(&key)?, enable as u8],)
        )
    }

    pub fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        &self,
        source: S,
        target: T,
    ) -> Result<()> {
        let source = source.into();
        let target = target.into();
        timed!(
            "keyboard_remap",
            self.write_api(
                ApiOpcode::KeyRemap,
                &[keyboard_code(&source)?, keyboard_code(&target)?],
            )
        )
    }

    fn keyboard_multi(&self, opcode: ApiOpcode, keys: &[KeyboardKey]) -> Result<()> {
        let payload = keyboard_multi_payload(keys)?;
        self.write_api(opcode, &payload)
    }

    pub fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!(
            "keyboard_multi_down",
            self.keyboard_multi(ApiOpcode::KeyMultiDown, keys)
        )
    }

    pub fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!(
            "keyboard_multi_up",
            self.keyboard_multi(ApiOpcode::KeyMultiUp, keys)
        )
    }

    pub fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!(
            "keyboard_multi_press",
            self.keyboard_multi(ApiOpcode::KeyMultiPress, keys)
        )
    }

    pub fn keyboard_keys(&self) -> Result<String> {
        timed!("keyboard_keys", {
            let value = self.query_api(ApiOpcode::KeyKeys, &[])?;
            match value.as_slice() {
                [enabled] => Ok(enabled.to_string()),
                _ => Err(MakxdError::Protocol(
                    "keyboard keys response is invalid".into(),
                )),
            }
        })
    }

    pub fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        timed!(
            "keyboard_keys_set",
            self.write_api(ApiOpcode::KeyKeys, &[enabled as u8])
        )
    }
}

#[cfg(feature = "async")]
use super::AsyncDevice;

#[cfg(feature = "async")]
impl AsyncDevice {
    pub async fn keyboard_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        self.write_api(ApiOpcode::KeyDown, &[keyboard_code(&key)?])
            .await
    }

    pub async fn keyboard_down_dt<K: Into<KeyboardKey>>(
        &self,
        key: K,
        dt_uframes: u16,
    ) -> Result<()> {
        let key = key.into();
        keyboard_dt_check(dt_uframes)?;
        let mut payload = vec![keyboard_code(&key)?];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.write_api(ApiOpcode::KeyDown, &payload).await
    }

    pub async fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        self.write_api(ApiOpcode::KeyUp, &[keyboard_code(&key)?])
            .await
    }

    pub async fn keyboard_up_dt<K: Into<KeyboardKey>>(
        &self,
        key: K,
        dt_uframes: u16,
    ) -> Result<()> {
        let key = key.into();
        keyboard_dt_check(dt_uframes)?;
        let mut payload = vec![keyboard_code(&key)?];
        payload.extend_from_slice(&dt_uframes.to_le_bytes());
        self.write_api(ApiOpcode::KeyUp, &payload).await
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
        let payload = keyboard_press_payload(&key, hold_ms, rand_ms)?;
        self.write_api(ApiOpcode::KeyPress, &payload).await
    }

    pub async fn keyboard_string(&self, text: &str) -> Result<()> {
        keyboard_string_check(text)?;
        self.write_api(ApiOpcode::KeyString, text.as_bytes()).await
    }

    pub async fn keyboard_init(&self) -> Result<()> {
        self.write_api(ApiOpcode::KeyInit, &[]).await
    }

    pub async fn keyboard_init_dt(&self, dt_uframes: u16) -> Result<()> {
        keyboard_dt_check(dt_uframes)?;
        self.write_api(ApiOpcode::KeyInit, &dt_uframes.to_le_bytes())
            .await
    }

    pub async fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        let value = self
            .query_api(ApiOpcode::KeyIsDown, &[keyboard_code(&key)?])
            .await?;
        Ok(value == b"\x01")
    }

    pub async fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        self.write_api(ApiOpcode::KeyMask, &[keyboard_code(&key)?, enable as u8])
            .await
    }

    pub async fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        &self,
        source: S,
        target: T,
    ) -> Result<()> {
        let source = source.into();
        let target = target.into();
        self.write_api(
            ApiOpcode::KeyRemap,
            &[keyboard_code(&source)?, keyboard_code(&target)?],
        )
        .await
    }

    async fn keyboard_multi(&self, opcode: ApiOpcode, keys: &[KeyboardKey]) -> Result<()> {
        let payload = keyboard_multi_payload(keys)?;
        self.write_api(opcode, &payload).await
    }

    pub async fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        self.keyboard_multi(ApiOpcode::KeyMultiDown, keys).await
    }

    pub async fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        self.keyboard_multi(ApiOpcode::KeyMultiUp, keys).await
    }

    pub async fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        self.keyboard_multi(ApiOpcode::KeyMultiPress, keys).await
    }

    pub async fn keyboard_keys(&self) -> Result<String> {
        let value = self.query_api(ApiOpcode::KeyKeys, &[]).await?;
        match value.as_slice() {
            [enabled] => Ok(enabled.to_string()),
            _ => Err(MakxdError::Protocol(
                "keyboard keys response is invalid".into(),
            )),
        }
    }

    pub async fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        self.write_api(ApiOpcode::KeyKeys, &[enabled as u8]).await
    }
}
