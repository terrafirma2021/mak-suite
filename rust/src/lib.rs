//! makxd — Rust library for controlling MAKXD devices.
//!
//! # Quick start
//!
//! ```no_run
//! use makxd::{Device, Button};
//!
//! let dev = Device::connect().expect("MAKXD not found");
//!
//! dev.button_down(Button::Left).unwrap();
//! dev.move_xy(100, 50).unwrap();
//! dev.button_up(Button::Left).unwrap();
//! ```

mod device;
pub mod error;
pub(crate) mod protocol;
pub mod stream;
pub(crate) mod transport;
pub mod types;

/// Per-command timing profiler. Zero-cost when the `profile` feature is disabled.
pub mod profiler;

// -- Public re-exports --

/// Re-export of `crossbeam_channel` so users can type the receivers
/// returned by `button_events()` and `connection_events()`
/// without adding `crossbeam-channel` as a direct dependency.
pub use crossbeam_channel;

pub use device::{Device, DeviceConfig};
pub use error::{MakxdError, Result};
pub use protocol::api::ApiOpcode;
pub use types::{
    BleConnectionIo, Button, ButtonMask, ConnectionConfig, ConnectionState, ControllerControl,
    ControllerMaskMode, ControllerState, DeviceInfo, DeviceKind, DeviceKinds, KeyboardKey,
    UdpWireMode,
};

#[cfg(feature = "async")]
pub use device::AsyncDevice;

#[cfg(feature = "mock")]
pub use transport::mock::MockTransport;

/// Library version.
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
