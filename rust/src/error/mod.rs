#[derive(Debug, thiserror::Error)]
pub enum MakxdError {
    #[error("port error: {0}")]
    Port(#[from] serialport::Error),
    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("command timed out")]
    Timeout,
    #[error("device not found")]
    NotFound,
    #[error("disconnected")]
    Disconnected,
    #[error("protocol error: {0}")]
    Protocol(String),
    #[error("value {value} out of range ({min}..={max})")]
    OutOfRange { value: i64, min: i64, max: i64 },
}

pub type Result<T> = std::result::Result<T, MakxdError>;
