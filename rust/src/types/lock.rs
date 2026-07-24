/// A mouse input axis or button that can be locked.
///
/// Locking prevents the corresponding physical input from being forwarded to
/// the host PC while still allowing software-injected input.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum LockTarget {
    X,
    Y,
    Left,
    Right,
    Middle,
    Side1,
    Side2,
}

/// Snapshot of all seven lock states, returned by `lock_states_all()`.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct LockStates {
    pub x: bool,
    pub y: bool,
    pub left: bool,
    pub right: bool,
    pub middle: bool,
    pub side1: bool,
    pub side2: bool,
}
