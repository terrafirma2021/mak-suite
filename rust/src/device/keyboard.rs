use crate::error::Result;
use crate::protocol::keyboard as builder;
use crate::timed;
use crate::types::KeyboardKey;

use super::Device;

impl Device {
    pub fn keyboard_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_down", {
            let command = builder::build_down(&key)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up", {
            let command = builder::build_up(&key)?;
            self.exec_dynamic(command.as_bytes())
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
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_string(&self, text: &str) -> Result<()> {
        timed!("keyboard_string", {
            let command = builder::build_string(text)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_init(&self) -> Result<()> {
        timed!("keyboard_init", {
            self.exec_dynamic(builder::build_init().as_bytes())
        })
    }

    pub fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        timed!("keyboard_is_down", {
            let command = builder::build_is_down(&key)?;
            let value = self.query_dynamic(command.as_bytes())?;
            Ok(value.trim() == "1")
        })
    }

    pub fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        timed!("keyboard_mask", {
            let command = builder::build_mask(&key, enable)?;
            self.exec_dynamic(command.as_bytes())
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
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_down", {
            let command = builder::build_key_list("km.multidown", keys)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_up", {
            let command = builder::build_key_list("km.multiup", keys)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_press", {
            let command = builder::build_key_list("km.multipress", keys)?;
            self.exec_dynamic(command.as_bytes())
        })
    }

    pub fn keyboard_keys(&self) -> Result<String> {
        timed!("keyboard_keys", { self.query_dynamic(builder::build_keys(None).as_bytes()) })
    }

    pub fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        timed!("keyboard_keys_set", {
            self.exec_dynamic(builder::build_keys(Some(enabled)).as_bytes())
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
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_up<K: Into<KeyboardKey>>(&self, key: K) -> Result<()> {
        let key = key.into();
        timed!("keyboard_up", {
            let command = builder::build_up(&key)?;
            self.exec_dynamic(command.as_bytes()).await
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
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_string(&self, text: &str) -> Result<()> {
        timed!("keyboard_string", {
            let command = builder::build_string(text)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_init(&self) -> Result<()> {
        timed!("keyboard_init", {
            self.exec_dynamic(builder::build_init().as_bytes()).await
        })
    }

    pub async fn keyboard_is_down<K: Into<KeyboardKey>>(&self, key: K) -> Result<bool> {
        let key = key.into();
        timed!("keyboard_is_down", {
            let command = builder::build_is_down(&key)?;
            let value = self.query_dynamic(command.as_bytes()).await?;
            Ok(value.trim() == "1")
        })
    }

    pub async fn keyboard_mask<K: Into<KeyboardKey>>(&self, key: K, enable: bool) -> Result<()> {
        let key = key.into();
        timed!("keyboard_mask", {
            let command = builder::build_mask(&key, enable)?;
            self.exec_dynamic(command.as_bytes()).await
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
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_multi_down(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_down", {
            let command = builder::build_key_list("km.multidown", keys)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_multi_up(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_up", {
            let command = builder::build_key_list("km.multiup", keys)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_multi_press(&self, keys: &[KeyboardKey]) -> Result<()> {
        timed!("keyboard_multi_press", {
            let command = builder::build_key_list("km.multipress", keys)?;
            self.exec_dynamic(command.as_bytes()).await
        })
    }

    pub async fn keyboard_keys(&self) -> Result<String> {
        timed!("keyboard_keys", {
            self.query_dynamic(builder::build_keys(None).as_bytes()).await
        })
    }

    pub async fn keyboard_keys_set(&self, enabled: bool) -> Result<()> {
        timed!("keyboard_keys_set", {
            self.exec_dynamic(builder::build_keys(Some(enabled)).as_bytes()).await
        })
    }
}
