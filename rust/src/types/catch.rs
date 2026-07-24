use super::Button;

/// A catch event reported by the `km.catch_m*()` firmware stream.
///
/// Each event corresponds to a single physical press or release of the
/// button that catch was enabled on.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CatchEvent {
    /// Which button produced this event.
    pub button: Button,
    /// `true` for press, `false` for release.
    pub pressed: bool,
}
