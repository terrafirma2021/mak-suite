#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ParseEvent {
    ButtonEvent(u8),
}

pub struct StreamParser {
    button_event_prefix_matched: usize,
}

impl StreamParser {
    pub fn new() -> Self {
        Self {
            button_event_prefix_matched: 0,
        }
    }

    pub fn feed(&mut self, byte: u8) -> Option<ParseEvent> {
        const BUTTON_EVENT_PREFIX: [u8; 3] = [0x6b, 0x6d, 0x2e];
        if self.button_event_prefix_matched == BUTTON_EVENT_PREFIX.len() {
            self.button_event_prefix_matched = usize::from(byte == BUTTON_EVENT_PREFIX[0]);
            return (byte < 0x20).then_some(ParseEvent::ButtonEvent(byte));
        }
        if byte == BUTTON_EVENT_PREFIX[self.button_event_prefix_matched] {
            self.button_event_prefix_matched += 1;
        } else {
            self.button_event_prefix_matched = usize::from(byte == BUTTON_EVENT_PREFIX[0]);
        }
        None
    }
}

impl Default for StreamParser {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn button_event_is_detected_across_fragments() {
        let mut parser = StreamParser::new();
        assert_eq!(parser.feed(0x6b), None);
        assert_eq!(parser.feed(0x6d), None);
        assert_eq!(parser.feed(0x2e), None);
        assert_eq!(parser.feed(0x05), Some(ParseEvent::ButtonEvent(0x05)));
    }

    #[test]
    fn invalid_prefix_restarts_at_first_byte() {
        let mut parser = StreamParser::new();
        for byte in [0x6b, 0x6b, 0x6d, 0x2e] {
            assert_eq!(parser.feed(byte), None);
        }
        assert_eq!(parser.feed(0x01), Some(ParseEvent::ButtonEvent(0x01)));
    }

    #[test]
    fn printable_suffix_is_not_an_event() {
        let mut parser = StreamParser::new();
        for byte in [0x6b, 0x6d, 0x2e] {
            assert_eq!(parser.feed(byte), None);
        }
        assert_eq!(parser.feed(0x20), None);
    }
}
