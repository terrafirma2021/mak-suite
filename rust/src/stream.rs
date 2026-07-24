//! MAKXD lightweight multi-source input streaming protocol.

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum StreamKind {
    Mouse = 1,
    Keyboard = 2,
    Controller = 3,
}

pub const STREAM_MASK_MOUSE: u8 = 1 << 0;
pub const STREAM_MASK_KEYBOARD: u8 = 1 << 1;
pub const STREAM_MASK_CONTROLLER: u8 = 1 << 2;
pub const STREAM_MASK_ALL: u8 = STREAM_MASK_MOUSE | STREAM_MASK_KEYBOARD | STREAM_MASK_CONTROLLER;
pub const STREAM_COMMAND_INPUT: u8 = 0x01;
pub const STREAM_MAX_BODY_BYTES: usize = 252;
pub const STREAM_MAX_PAYLOAD_BYTES: usize = STREAM_MAX_BODY_BYTES - 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum StreamOperation {
    Start = 1,
    Stop = 2,
    Status = 3,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StreamTiming {
    pub raw: u16,
    pub dt_uframes: u16,
    pub baseline: bool,
    pub invalid: bool,
}

impl StreamTiming {
    pub fn from_raw(raw: u16) -> Self {
        Self {
            raw,
            dt_uframes: raw & 0x3fff,
            baseline: raw & 0x4000 != 0,
            invalid: raw & 0x8000 != 0,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamFrame {
    pub command: u8,
    pub payload: Vec<u8>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StreamControl {
    pub operation: u8,
    pub status: u8,
    pub active_mask: u8,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamInputRecord {
    pub kind: StreamKind,
    pub sequence: u32,
    pub timing: StreamTiming,
    pub values: Vec<u8>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StreamRequest {
    pub operation: StreamOperation,
    pub source_mask: u8,
}

impl StreamRequest {
    pub fn start(source_mask: u8) -> Self {
        Self {
            operation: StreamOperation::Start,
            source_mask: source_mask & STREAM_MASK_ALL,
        }
    }

    pub fn mouse() -> Self {
        Self::start(STREAM_MASK_MOUSE)
    }
    pub fn keyboard() -> Self {
        Self::start(STREAM_MASK_KEYBOARD)
    }
    pub fn controller() -> Self {
        Self::start(STREAM_MASK_CONTROLLER)
    }
    pub fn all() -> Self {
        Self::start(STREAM_MASK_ALL)
    }

    pub fn stop() -> Self {
        Self {
            operation: StreamOperation::Stop,
            source_mask: 0,
        }
    }

    pub fn status() -> Self {
        Self {
            operation: StreamOperation::Status,
            source_mask: 0,
        }
    }

    pub fn encode(&self) -> Vec<u8> {
        encode_frame(
            STREAM_COMMAND_INPUT,
            &[self.operation as u8, self.source_mask & STREAM_MASK_ALL],
        )
    }
}

#[derive(Default)]
pub struct StreamFrameDecoder {
    buffer: Vec<u8>,
}

impl StreamFrameDecoder {
    pub fn new() -> Self {
        Self::default()
    }

    pub fn feed(&mut self, bytes: &[u8]) {
        self.buffer.extend_from_slice(bytes);
    }

    pub fn next(&mut self) -> Option<StreamFrame> {
        loop {
            if self.buffer.len() < 2 {
                return None;
            }
            if self.buffer[0] != 0xDE || self.buffer[1] != 0xAD {
                self.buffer.remove(0);
                continue;
            }
            if self.buffer.len() < 4 {
                return None;
            }
            let payload_len = u16::from_le_bytes([self.buffer[2], self.buffer[3]]) as usize;
            if payload_len == 0 || payload_len > STREAM_MAX_PAYLOAD_BYTES {
                self.buffer.remove(0);
                continue;
            }
            let frame_len = 5 + payload_len;
            if self.buffer.len() < frame_len {
                return None;
            }
            let frame = StreamFrame {
                command: self.buffer[4],
                payload: self.buffer[5..frame_len].to_vec(),
            };
            self.buffer.drain(..frame_len);
            return Some(frame);
        }
    }
}

pub fn decode_stream_control(frame: &StreamFrame) -> Option<StreamControl> {
    if frame.command != STREAM_COMMAND_INPUT || frame.payload.len() != 3 {
        return None;
    }
    Some(StreamControl {
        operation: frame.payload[0],
        status: frame.payload[1],
        active_mask: frame.payload[2],
    })
}

pub fn decode_stream_input_record(frame: &StreamFrame) -> Option<StreamInputRecord> {
    if frame.command != STREAM_COMMAND_INPUT || frame.payload.len() < 8 {
        return None;
    }
    let kind = match frame.payload[0] {
        1 => StreamKind::Mouse,
        2 => StreamKind::Keyboard,
        3 => StreamKind::Controller,
        _ => return None,
    };
    Some(StreamInputRecord {
        kind,
        sequence: u32::from_le_bytes(frame.payload[3..7].try_into().ok()?),
        timing: StreamTiming::from_raw(u16::from_le_bytes(frame.payload[1..3].try_into().ok()?)),
        values: frame.payload[7..].to_vec(),
    })
}

fn encode_frame(command: u8, payload: &[u8]) -> Vec<u8> {
    assert!(!payload.is_empty() && payload.len() <= STREAM_MAX_PAYLOAD_BYTES);
    let mut frame = Vec::with_capacity(5 + payload.len());
    frame.extend_from_slice(&[0xDE, 0xAD]);
    frame.extend_from_slice(&(payload.len() as u16).to_le_bytes());
    frame.push(command);
    frame.extend_from_slice(payload);
    frame
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn encodes_all_source_request() {
        let bytes = StreamRequest::all().encode();
        assert_eq!(&bytes[..5], &[0xDE, 0xAD, 2, 0, STREAM_COMMAND_INPUT]);
        assert_eq!(&bytes[5..], &[1, STREAM_MASK_ALL]);
    }

    #[test]
    fn decodes_simultaneous_source_report() {
        let payload = [2, 0, 0x40, 7, 0, 0, 0, 0x01, 0x02];
        let mut bytes = vec![0xDE, 0xAD, payload.len() as u8, 0, STREAM_COMMAND_INPUT];
        bytes.extend_from_slice(&payload);
        let mut decoder = StreamFrameDecoder::new();
        decoder.feed(&bytes);
        let frame = decoder.next().expect("frame");
        let record = decode_stream_input_record(&frame).expect("report");
        assert_eq!(record.kind, StreamKind::Keyboard);
        assert_eq!(record.sequence, 7);
        assert_eq!(record.timing.dt_uframes, 0);
        assert!(record.timing.baseline);
        assert_eq!(record.values, vec![1, 2]);
    }
}
