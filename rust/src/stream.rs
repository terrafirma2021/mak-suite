//! Independent, on-change input subscriptions. All events use the MAK frame.
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum StreamKind {
    Mouse = 1,
    Keyboard = 2,
    Controller = 3,
}
pub const STREAM_COMMAND: u8 = 0x52;
pub const STREAM_EVENT: u8 = 0x53;
pub const STREAM_TRIGGER_MAX: u16 = 1023;
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct StreamFrame {
    pub command: u8,
    pub payload: Vec<u8>,
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct InputChange {
    pub kind: StreamKind,
    pub control: u8,
    pub value: u16,
    pub overflow: bool,
}
impl InputChange {
    pub fn is_trigger(self) -> bool {
        self.kind == StreamKind::Controller && matches!(self.control, 10 | 11)
    }
}
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct StreamRequest {
    pub kind: StreamKind,
    pub enabled: Option<bool>,
}
impl StreamRequest {
    pub fn new(kind: StreamKind, enabled: Option<bool>) -> Self {
        Self { kind, enabled }
    }
    pub fn mouse(enabled: bool) -> Self {
        Self::new(StreamKind::Mouse, Some(enabled))
    }
    pub fn keyboard(enabled: bool) -> Self {
        Self::new(StreamKind::Keyboard, Some(enabled))
    }
    pub fn controller(enabled: bool) -> Self {
        Self::new(StreamKind::Controller, Some(enabled))
    }
    pub fn encode(self) -> Vec<u8> {
        let mut frame = vec![
            0xde,
            0xad,
            1 + u8::from(self.enabled.is_some()),
            0,
            STREAM_COMMAND,
            self.kind as u8,
        ];
        if let Some(enabled) = self.enabled {
            frame.push(u8::from(enabled));
        }
        frame
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
}
impl Iterator for StreamFrameDecoder {
    type Item = StreamFrame;
    fn next(&mut self) -> Option<StreamFrame> {
        while self.buffer.len() >= 2 {
            if self.buffer[..2] != [0xde, 0xad] {
                self.buffer.remove(0);
                continue;
            }
            if self.buffer.len() < 5 {
                return None;
            }
            let len = u16::from_le_bytes([self.buffer[2], self.buffer[3]]) as usize;
            if len > 251 {
                self.buffer.remove(0);
                continue;
            }
            if self.buffer.len() < len + 5 {
                return None;
            }
            let frame = StreamFrame {
                command: self.buffer[4],
                payload: self.buffer[5..5 + len].to_vec(),
            };
            self.buffer.drain(..5 + len);
            return Some(frame);
        }
        None
    }
}
pub fn decode_input_change(frame: &StreamFrame) -> Option<InputChange> {
    let p = &frame.payload;
    if frame.command != STREAM_EVENT || !(3..=4).contains(&p.len()) {
        return None;
    }
    let kind = match p[0] {
        1 => StreamKind::Mouse,
        2 => StreamKind::Keyboard,
        3 => StreamKind::Controller,
        _ => return None,
    };
    let mut event = InputChange {
        kind,
        control: p[1],
        value: u16::from(p[2]),
        overflow: false,
    };
    if p.len() == 3 && p[1..] == [255, 255] {
        event.overflow = true;
        return Some(event);
    }
    if event.is_trigger() {
        if p.len() != 4 {
            return None;
        }
        event.value = u16::from_le_bytes([p[2], p[3]]);
        if event.value > STREAM_TRIGGER_MAX {
            return None;
        }
    } else if p.len() != 3
        || p[2] > 1
        || (kind == StreamKind::Mouse && p[1] > 31)
        || (kind == StreamKind::Controller && (p[1] > 54 || (12..=15).contains(&p[1])))
    {
        return None;
    }
    Some(event)
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn fragmented_mixed_frames() {
        let mut d = StreamFrameDecoder::new();
        let wire = [
            9, 0xde, 0xad, 3, 0, 0x53, 1, 31, 1, 0xde, 0xad, 4, 0, 0x53, 3, 10, 255, 3, 0xde, 0xad,
            1, 0, 0x52, 1,
        ];
        let mut frames = Vec::new();
        for b in wire {
            d.feed(&[b]);
            frames.extend(d.by_ref());
        }
        assert_eq!(frames.len(), 3);
        assert_eq!(decode_input_change(&frames[0]).unwrap().control, 31);
        assert_eq!(decode_input_change(&frames[1]).unwrap().value, 1023);
        assert!(decode_input_change(&frames[2]).is_none());
        assert_eq!(
            StreamRequest::controller(false).encode(),
            [0xde, 0xad, 2, 0, 0x52, 3, 0]
        );
    }
    #[test]
    fn invalid_events_and_overflow() {
        for payload in [
            vec![3, 12, 1],
            vec![3, 10, 1],
            vec![1, 32, 1],
            vec![2, 4, 2],
            vec![4, 1, 1],
        ] {
            assert!(
                decode_input_change(&StreamFrame {
                    command: STREAM_EVENT,
                    payload
                })
                .is_none()
            );
        }
        let event = decode_input_change(&StreamFrame {
            command: STREAM_EVENT,
            payload: vec![2, 255, 255],
        })
        .unwrap();
        assert!(event.overflow);
    }
}
