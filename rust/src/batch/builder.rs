#[cfg(feature = "extras")]
use std::time::Duration;

use crate::device::Device;
use crate::error::{MakxdError, Result};
use crate::protocol::{builder as proto_builder, constants, keyboard as keyboard_builder};
use crate::types::{Button, KeyboardKey, LockTarget};

// ===========================================================================
// Sync BatchBuilder
// ===========================================================================

#[cfg(feature = "extras")]
type ExtrasStepFn = Box<dyn FnOnce(&Device) -> Result<()> + Send>;

enum BatchStep {
    Native(Vec<u8>),
    #[cfg(feature = "extras")]
    Extras(ExtrasStepFn),
}

/// Fluent command sequence builder.
///
/// Collects commands and executes them in order on `.execute()`.
/// Range errors from parametric commands (move, wheel) are deferred
/// until `.execute()` is called.
pub struct BatchBuilder<'d> {
    device: &'d Device,
    steps: Vec<BatchStep>,
    error: Option<MakxdError>,
}

impl<'d> BatchBuilder<'d> {
    pub(crate) fn new(device: &'d Device) -> Self {
        Self {
            device,
            steps: Vec::new(),
            error: None,
        }
    }

    fn push_built(mut self, result: Result<proto_builder::CommandBuf>) -> Self {
        match result {
            Ok(cmd) => self.steps.push(BatchStep::Native(cmd.as_bytes().to_vec())),
            Err(e) => {
                if self.error.is_none() {
                    self.error = Some(e);
                }
            }
        }
        self
    }

    fn push_keyboard(mut self, result: Result<String>) -> Self {
        match result {
            Ok(command) => self.steps.push(BatchStep::Native(command.into_bytes())),
            Err(e) => {
                if self.error.is_none() {
                    self.error = Some(e);
                }
            }
        }
        self
    }

    pub fn move_xy(self, x: i32, y: i32) -> Self {
        self.push_built(proto_builder::build_move(x, y))
    }

    pub fn silent_move(self, x: i32, y: i32) -> Self {
        self.push_built(proto_builder::build_silent_move(x, y))
    }

    pub fn button_down(mut self, button: Button) -> Self {
        self.steps.push(BatchStep::Native(
            constants::button_down_cmd(button).to_vec(),
        ));
        self
    }

    pub fn button_up(mut self, button: Button) -> Self {
        self.steps
            .push(BatchStep::Native(constants::button_up_cmd(button).to_vec()));
        self
    }

    pub fn button_up_force(mut self, button: Button) -> Self {
        self.steps.push(BatchStep::Native(
            constants::button_force_up_cmd(button).to_vec(),
        ));
        self
    }

    pub fn wheel(self, delta: i32) -> Self {
        self.push_built(proto_builder::build_wheel(delta))
    }

    pub fn keyboard_down<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_down(&key))
    }

    pub fn keyboard_up<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_up(&key))
    }

    pub fn keyboard_press<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(&key, None, None))
    }

    pub fn keyboard_press_for<K: Into<KeyboardKey>>(self, key: K, hold_ms: u32) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(&key, Some(hold_ms), None))
    }

    pub fn keyboard_press_randomized<K: Into<KeyboardKey>>(
        self,
        key: K,
        hold_ms: u32,
        rand_ms: u32,
    ) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(
            &key,
            Some(hold_ms),
            Some(rand_ms),
        ))
    }

    pub fn keyboard_string(self, text: &str) -> Self {
        self.push_keyboard(keyboard_builder::build_string(text))
    }

    pub fn keyboard_init(mut self) -> Self {
        self.steps.push(BatchStep::Native(
            keyboard_builder::build_init().into_bytes(),
        ));
        self
    }

    pub fn keyboard_multi_down(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multidown", keys))
    }

    pub fn keyboard_multi_up(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multiup", keys))
    }

    pub fn keyboard_multi_press(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multipress", keys))
    }

    pub fn keyboard_keys(mut self, enabled: bool) -> Self {
        self.steps.push(BatchStep::Native(
            keyboard_builder::build_keys(Some(enabled)).into_bytes(),
        ));
        self
    }

    pub fn keyboard_mask<K: Into<KeyboardKey>>(self, key: K, enable: bool) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_mask(&key, enable))
    }

    pub fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        self,
        source: S,
        target: T,
    ) -> Self {
        let source = source.into();
        let target = target.into();
        self.push_keyboard(keyboard_builder::build_remap(&source, &target))
    }

    pub fn set_lock(mut self, target: LockTarget, locked: bool) -> Self {
        self.steps.push(BatchStep::Native(
            constants::lock_set_cmd(target, locked).to_vec(),
        ));
        self
    }

    pub fn enable_catch(mut self, button: Button) -> Self {
        self.steps.push(BatchStep::Native(
            constants::catch_enable_cmd(button).to_vec(),
        ));
        self
    }

    pub fn enable_button_stream(mut self) -> Self {
        self.steps
            .push(BatchStep::Native(constants::CMD_BUTTONS_ON.to_vec()));
        self
    }

    pub fn disable_button_stream(mut self) -> Self {
        self.steps
            .push(BatchStep::Native(constants::CMD_BUTTONS_OFF.to_vec()));
        self
    }

    /// Queue raw command bytes. The `\r\n` terminator must already be included.
    pub fn send_raw(mut self, cmd: &[u8]) -> Self {
        self.steps.push(BatchStep::Native(cmd.to_vec()));
        self
    }

    /// Execute all queued commands.
    /// Consecutive native commands are coalesced into single writes.
    pub fn execute(self) -> Result<()> {
        if let Some(e) = self.error {
            return Err(e);
        }
        let mut native_buf: Vec<u8> = Vec::new();

        for step in self.steps {
            match step {
                BatchStep::Native(data) => {
                    native_buf.extend_from_slice(&data);
                }
                #[cfg(feature = "extras")]
                BatchStep::Extras(f) => {
                    if !native_buf.is_empty() {
                        flush_native(self.device, &mut native_buf)?;
                    }
                    f(self.device)?;
                }
            }
        }

        if !native_buf.is_empty() {
            flush_native(self.device, &mut native_buf)?;
        }

        Ok(())
    }
}

fn flush_native(device: &Device, buf: &mut Vec<u8>) -> Result<()> {
    let data = std::mem::take(buf);
    // Always fire-and-forget for coalesced batches: the batch contains N
    // commands but we write them as one blob. Registering a single response
    // slot for N commands would cause response misalignment (N-1 extra
    // prompts consumed by unrelated pending commands).
    device.transport().send_command(
        data,
        true, // always F&F for coalesced writes
        device.timeout(),
    )?;
    Ok(())
}

#[cfg(feature = "extras")]
impl<'d> BatchBuilder<'d> {
    /// Lock the button and enable catch in one batch step.
    pub fn start_catch(mut self, button: Button) -> Self {
        self.steps.push(BatchStep::Native(
            constants::lock_set_cmd(constants::button_to_lock_target(button), true).to_vec(),
        ));
        self.steps.push(BatchStep::Native(
            constants::catch_enable_cmd(button).to_vec(),
        ));
        self
    }

    /// Unlock the button, stopping the catch stream.
    pub fn stop_catch(mut self, button: Button) -> Self {
        self.steps.push(BatchStep::Native(
            constants::lock_set_cmd(constants::button_to_lock_target(button), false).to_vec(),
        ));
        self
    }

    pub fn click(mut self, button: Button, hold: Duration) -> Self {
        self.steps.push(BatchStep::Extras(Box::new(move |dev| {
            dev.click(button, hold)
        })));
        self
    }

    pub fn click_sequence(
        mut self,
        button: Button,
        hold: Duration,
        count: u32,
        interval: Duration,
    ) -> Self {
        self.steps.push(BatchStep::Extras(Box::new(move |dev| {
            dev.click_sequence(button, hold, count, interval)
        })));
        self
    }

    pub fn move_smooth(mut self, x: i32, y: i32, steps: u32, interval: Duration) -> Self {
        self.steps.push(BatchStep::Extras(Box::new(move |dev| {
            dev.move_smooth(x, y, steps, interval)
        })));
        self
    }

    pub fn move_pattern(
        mut self,
        waypoints: Vec<(i32, i32)>,
        steps: u32,
        interval: Duration,
    ) -> Self {
        self.steps.push(BatchStep::Extras(Box::new(move |dev| {
            dev.move_pattern(&waypoints, steps, interval)
        })));
        self
    }

    pub fn drag(mut self, button: Button, x: i32, y: i32, steps: u32, interval: Duration) -> Self {
        self.steps.push(BatchStep::Extras(Box::new(move |dev| {
            dev.drag(button, x, y, steps, interval)
        })));
        self
    }
}

// ===========================================================================
// Async BatchBuilder
// ===========================================================================

#[cfg(feature = "async")]
use crate::device::AsyncDevice;

#[cfg(feature = "async")]
enum AsyncBatchStep {
    Native(Vec<u8>),
    #[cfg(feature = "extras")]
    Click {
        button: Button,
        hold: Duration,
    },
    #[cfg(feature = "extras")]
    ClickSequence {
        button: Button,
        hold: Duration,
        count: u32,
        interval: Duration,
    },
    #[cfg(feature = "extras")]
    MoveSmooth {
        x: i32,
        y: i32,
        steps: u32,
        interval: Duration,
    },
    #[cfg(feature = "extras")]
    MovePattern {
        waypoints: Vec<(i32, i32)>,
        steps: u32,
        interval: Duration,
    },
    #[cfg(feature = "extras")]
    Drag {
        button: Button,
        x: i32,
        y: i32,
        steps: u32,
        interval: Duration,
    },
}

#[cfg(feature = "async")]
pub struct AsyncBatchBuilder<'d> {
    device: &'d AsyncDevice,
    steps: Vec<AsyncBatchStep>,
    error: Option<MakxdError>,
}

#[cfg(feature = "async")]
impl<'d> AsyncBatchBuilder<'d> {
    pub(crate) fn new(device: &'d AsyncDevice) -> Self {
        Self {
            device,
            steps: Vec::new(),
            error: None,
        }
    }

    fn push_built(mut self, result: Result<proto_builder::CommandBuf>) -> Self {
        match result {
            Ok(cmd) => self
                .steps
                .push(AsyncBatchStep::Native(cmd.as_bytes().to_vec())),
            Err(e) => {
                if self.error.is_none() {
                    self.error = Some(e);
                }
            }
        }
        self
    }

    fn push_keyboard(mut self, result: Result<String>) -> Self {
        match result {
            Ok(command) => self
                .steps
                .push(AsyncBatchStep::Native(command.into_bytes())),
            Err(e) => {
                if self.error.is_none() {
                    self.error = Some(e);
                }
            }
        }
        self
    }

    pub fn move_xy(self, x: i32, y: i32) -> Self {
        self.push_built(proto_builder::build_move(x, y))
    }

    pub fn silent_move(self, x: i32, y: i32) -> Self {
        self.push_built(proto_builder::build_silent_move(x, y))
    }

    pub fn button_down(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::button_down_cmd(button).to_vec(),
        ));
        self
    }

    pub fn button_up(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::button_up_cmd(button).to_vec(),
        ));
        self
    }

    pub fn button_up_force(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::button_force_up_cmd(button).to_vec(),
        ));
        self
    }

    pub fn wheel(self, delta: i32) -> Self {
        self.push_built(proto_builder::build_wheel(delta))
    }

    pub fn keyboard_down<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_down(&key))
    }

    pub fn keyboard_up<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_up(&key))
    }

    pub fn keyboard_press<K: Into<KeyboardKey>>(self, key: K) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(&key, None, None))
    }

    pub fn keyboard_press_for<K: Into<KeyboardKey>>(self, key: K, hold_ms: u32) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(&key, Some(hold_ms), None))
    }

    pub fn keyboard_press_randomized<K: Into<KeyboardKey>>(
        self,
        key: K,
        hold_ms: u32,
        rand_ms: u32,
    ) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_press(
            &key,
            Some(hold_ms),
            Some(rand_ms),
        ))
    }

    pub fn keyboard_string(self, text: &str) -> Self {
        self.push_keyboard(keyboard_builder::build_string(text))
    }

    pub fn keyboard_init(mut self) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            keyboard_builder::build_init().into_bytes(),
        ));
        self
    }

    pub fn keyboard_multi_down(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multidown", keys))
    }

    pub fn keyboard_multi_up(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multiup", keys))
    }

    pub fn keyboard_multi_press(self, keys: &[KeyboardKey]) -> Self {
        self.push_keyboard(keyboard_builder::build_key_list("km.multipress", keys))
    }

    pub fn keyboard_keys(mut self, enabled: bool) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            keyboard_builder::build_keys(Some(enabled)).into_bytes(),
        ));
        self
    }

    pub fn keyboard_mask<K: Into<KeyboardKey>>(self, key: K, enable: bool) -> Self {
        let key = key.into();
        self.push_keyboard(keyboard_builder::build_mask(&key, enable))
    }

    pub fn keyboard_remap<S: Into<KeyboardKey>, T: Into<KeyboardKey>>(
        self,
        source: S,
        target: T,
    ) -> Self {
        let source = source.into();
        let target = target.into();
        self.push_keyboard(keyboard_builder::build_remap(&source, &target))
    }

    pub fn set_lock(mut self, target: LockTarget, locked: bool) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::lock_set_cmd(target, locked).to_vec(),
        ));
        self
    }

    pub fn enable_catch(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::catch_enable_cmd(button).to_vec(),
        ));
        self
    }

    pub fn enable_button_stream(mut self) -> Self {
        self.steps
            .push(AsyncBatchStep::Native(constants::CMD_BUTTONS_ON.to_vec()));
        self
    }

    pub fn disable_button_stream(mut self) -> Self {
        self.steps
            .push(AsyncBatchStep::Native(constants::CMD_BUTTONS_OFF.to_vec()));
        self
    }

    /// Queue raw command bytes. The `\r\n` terminator must already be included.
    pub fn send_raw(mut self, cmd: &[u8]) -> Self {
        self.steps.push(AsyncBatchStep::Native(cmd.to_vec()));
        self
    }

    /// Execute all queued commands (async).
    pub async fn execute(self) -> Result<()> {
        if let Some(e) = self.error {
            return Err(e);
        }
        let mut native_buf: Vec<u8> = Vec::new();

        for step in self.steps {
            match step {
                AsyncBatchStep::Native(data) => {
                    native_buf.extend_from_slice(&data);
                }
                #[cfg(feature = "extras")]
                extras_step => {
                    if !native_buf.is_empty() {
                        async_flush_native(self.device, &mut native_buf).await?;
                    }
                    execute_extras_step(self.device, extras_step).await?;
                }
            }
        }

        if !native_buf.is_empty() {
            async_flush_native(self.device, &mut native_buf).await?;
        }

        Ok(())
    }
}

#[cfg(feature = "async")]
async fn async_flush_native(device: &AsyncDevice, buf: &mut Vec<u8>) -> Result<()> {
    let data = std::mem::take(buf);
    // Always fire-and-forget: see flush_native comment above.
    device
        .transport()
        .send_command_async(data, true, device.timeout())
        .await?;
    Ok(())
}

#[cfg(all(feature = "async", feature = "extras"))]
async fn execute_extras_step(device: &AsyncDevice, step: AsyncBatchStep) -> Result<()> {
    match step {
        AsyncBatchStep::Native(_) => unreachable!(),
        AsyncBatchStep::Click { button, hold } => device.click(button, hold).await,
        AsyncBatchStep::ClickSequence {
            button,
            hold,
            count,
            interval,
        } => device.click_sequence(button, hold, count, interval).await,
        AsyncBatchStep::MoveSmooth {
            x,
            y,
            steps,
            interval,
        } => device.move_smooth(x, y, steps, interval).await,
        AsyncBatchStep::MovePattern {
            waypoints,
            steps,
            interval,
        } => device.move_pattern(&waypoints, steps, interval).await,
        AsyncBatchStep::Drag {
            button,
            x,
            y,
            steps,
            interval,
        } => device.drag(button, x, y, steps, interval).await,
    }
}

// Extras methods on async builder
#[cfg(all(feature = "async", feature = "extras"))]
impl<'d> AsyncBatchBuilder<'d> {
    /// Lock the button and enable catch in one batch step.
    pub fn start_catch(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::lock_set_cmd(constants::button_to_lock_target(button), true).to_vec(),
        ));
        self.steps.push(AsyncBatchStep::Native(
            constants::catch_enable_cmd(button).to_vec(),
        ));
        self
    }

    /// Unlock the button, stopping the catch stream.
    pub fn stop_catch(mut self, button: Button) -> Self {
        self.steps.push(AsyncBatchStep::Native(
            constants::lock_set_cmd(constants::button_to_lock_target(button), false).to_vec(),
        ));
        self
    }

    pub fn click(mut self, button: Button, hold: Duration) -> Self {
        self.steps.push(AsyncBatchStep::Click { button, hold });
        self
    }

    pub fn click_sequence(
        mut self,
        button: Button,
        hold: Duration,
        count: u32,
        interval: Duration,
    ) -> Self {
        self.steps.push(AsyncBatchStep::ClickSequence {
            button,
            hold,
            count,
            interval,
        });
        self
    }

    pub fn move_smooth(mut self, x: i32, y: i32, steps: u32, interval: Duration) -> Self {
        self.steps.push(AsyncBatchStep::MoveSmooth {
            x,
            y,
            steps,
            interval,
        });
        self
    }

    pub fn move_pattern(
        mut self,
        waypoints: Vec<(i32, i32)>,
        steps: u32,
        interval: Duration,
    ) -> Self {
        self.steps.push(AsyncBatchStep::MovePattern {
            waypoints,
            steps,
            interval,
        });
        self
    }

    pub fn drag(mut self, button: Button, x: i32, y: i32, steps: u32, interval: Duration) -> Self {
        self.steps.push(AsyncBatchStep::Drag {
            button,
            x,
            y,
            steps,
            interval,
        });
        self
    }
}

// Stub for when extras is not enabled — needed so execute() compiles
#[cfg(all(feature = "async", not(feature = "extras")))]
async fn execute_extras_step(_device: &AsyncDevice, _step: AsyncBatchStep) -> Result<()> {
    unreachable!()
}
