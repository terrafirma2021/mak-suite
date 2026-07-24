/// Events emitted by the stream parser.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ParseEvent {
    /// A button state change event (raw mask byte from `km.` prefix).
    ButtonEvent(u8),
    /// A complete command response (everything before the `>>> ` prompt).
    Response(Vec<u8>),
}

/// State machine that parses the interleaved device stream.
///
/// Handles:
/// - `km.` prefix detection for button events (mask byte follows)
/// - `>>> ` prompt detection for command response boundaries
/// - Interleaved button events and command responses
pub struct StreamParser {
    /// How many bytes of "km." we've matched (0-3).
    km_matched: usize,
    /// Accumulated response bytes (between prompts).
    response_buf: Vec<u8>,
    /// How many bytes of ">>> " we've matched (0-4).
    prompt_matched: usize,
}

impl StreamParser {
    pub fn new() -> Self {
        Self {
            km_matched: 0,
            response_buf: Vec::with_capacity(256),
            prompt_matched: 0,
        }
    }

    /// Feed a single byte into the parser. Returns an event if one is complete.
    pub fn feed(&mut self, byte: u8) -> Option<ParseEvent> {
        const KM: &[u8] = b"km.";

        // State: we've matched the full "km." prefix, this byte is the mask.
        if self.km_matched == 3 {
            self.km_matched = 0;
            // Distinguish button mask (< 0x20) from command echo (>= 0x20).
            // Command names after "km." always start with a letter (>= 0x61),
            // so any byte < 0x20 is a button event mask.
            if byte < 0x20 {
                return Some(ParseEvent::ButtonEvent(byte));
            }
            // False positive — flush "km." + this byte to response buffer.
            self.push_response_bytes(KM);
            return self.push_response_byte(byte);
        }

        // State: partially matching "km." prefix.
        if self.km_matched > 0 {
            if byte == KM[self.km_matched] {
                self.km_matched += 1;
                return None;
            }
            // Mismatch — flush partial "km" to response buffer.
            let partial = &KM[..self.km_matched];
            self.km_matched = 0;
            self.push_response_bytes(partial);
            // Check if current byte starts a new "km." match.
            if byte == KM[0] {
                self.km_matched = 1;
                return None;
            }
            return self.push_response_byte(byte);
        }

        // State: normal — check if this byte starts "km." prefix.
        if byte == KM[0] {
            self.km_matched = 1;
            return None;
        }

        // Normal byte — add to response buffer and check for prompt.
        self.push_response_byte(byte)
    }

    /// Push a single byte to the response buffer and check for prompt completion.
    fn push_response_byte(&mut self, byte: u8) -> Option<ParseEvent> {
        const PROMPT_BYTES: &[u8] = b">>> ";

        self.response_buf.push(byte);

        if byte == PROMPT_BYTES[self.prompt_matched] {
            self.prompt_matched += 1;
            if self.prompt_matched == PROMPT_BYTES.len() {
                // Complete prompt found — emit response.
                let len = self.response_buf.len() - PROMPT_BYTES.len();
                let response = self.response_buf[..len].to_vec();
                self.response_buf.clear();
                self.prompt_matched = 0;
                return Some(ParseEvent::Response(response));
            }
        } else if byte == PROMPT_BYTES[0] {
            self.prompt_matched = 1;
        } else {
            self.prompt_matched = 0;
        }

        None
    }

    /// Push multiple bytes to response buffer, checking prompt after each.
    fn push_response_bytes(&mut self, bytes: &[u8]) {
        for &b in bytes {
            // We ignore any events from flushing partial km. matches to response buf
            // because "km." chars can't complete a ">>> " prompt.
            self.response_buf.push(b);
            // No need to check prompt — 'k', 'm', '.' never match '>', '>', '>', ' '.
        }
    }

    /// Reset parser state (e.g. on reconnection).
    #[allow(dead_code)]
    pub fn reset(&mut self) {
        self.km_matched = 0;
        self.response_buf.clear();
        self.prompt_matched = 0;
    }
}

/// Parse a raw response buffer into a classified result.
///
/// Returns `(echo_stripped_value, is_query)` where `is_query` indicates
/// whether the response contained a return value.
pub fn classify_response(raw: &[u8]) -> ResponseKind {
    let body = trim_bytes(raw);
    if body.is_empty() {
        return ResponseKind::Executed;
    }
    let text = String::from_utf8_lossy(body);
    // If there's a newline: first line is the echo, rest is the return value.
    if let Some(nl) = body.iter().position(|&b| b == b'\n') {
        let value = String::from_utf8_lossy(&body[nl + 1..]).trim().to_string();
        return ResponseKind::Value(value);
    }
    // Single line — could be echo-only or a value without echo (km.version special case).
    // We return it as a value and let the caller decide.
    ResponseKind::ValueOrEcho(text.trim().to_string())
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ResponseKind {
    /// No body — command executed, prompt returned immediately.
    Executed,
    /// Multi-line: echo + value. The value string is extracted.
    Value(String),
    /// Single line — might be just an echo or a value without echo.
    /// Caller must compare with sent command to disambiguate.
    ValueOrEcho(String),
}

/// Try to parse a response buffer as a catch event.
///
/// Catch events arrive as unsolicited responses like `km.catch_ml(1)\r\n`
/// where the value 1 = press and 2 = release. Each event is terminated by
/// its own `>>> ` prompt, so the response buffer always contains a single
/// catch line.
pub fn parse_catch_event(raw: &[u8]) -> Option<crate::types::CatchEvent> {
    use crate::types::Button;

    let text = std::str::from_utf8(trim_bytes(raw)).ok()?;
    let rest = text.strip_prefix("km.catch_m")?;

    // Parse button suffix and value.
    let (button, rest) = if let Some(r) = rest.strip_prefix("s1") {
        (Button::Side1, r)
    } else if let Some(r) = rest.strip_prefix("s2") {
        (Button::Side2, r)
    } else if let Some(r) = rest.strip_prefix('l') {
        (Button::Left, r)
    } else if let Some(r) = rest.strip_prefix('r') {
        (Button::Right, r)
    } else if let Some(r) = rest.strip_prefix('m') {
        (Button::Middle, r)
    } else {
        return None;
    };

    match rest {
        "(1)" => Some(crate::types::CatchEvent {
            button,
            pressed: true,
        }),
        "(2)" => Some(crate::types::CatchEvent {
            button,
            pressed: false,
        }),
        _ => None,
    }
}

fn trim_bytes(b: &[u8]) -> &[u8] {
    let is_ws = |&x: &u8| x == b'\r' || x == b'\n' || x == b' ';
    let start = b.iter().position(|x| !is_ws(x)).unwrap_or(b.len());
    let end = b
        .iter()
        .rposition(|x| !is_ws(x))
        .map(|i| i + 1)
        .unwrap_or(0);
    if start >= end { &[] } else { &b[start..end] }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn button_event_basic() {
        let mut parser = StreamParser::new();
        // Feed "km." + mask byte 0x05 (left + middle)
        assert_eq!(parser.feed(b'k'), None);
        assert_eq!(parser.feed(b'm'), None);
        assert_eq!(parser.feed(b'.'), None);
        assert_eq!(parser.feed(0x05), Some(ParseEvent::ButtonEvent(0x05)));
    }

    #[test]
    fn button_event_mask_0x0a() {
        let mut parser = StreamParser::new();
        assert_eq!(parser.feed(b'k'), None);
        assert_eq!(parser.feed(b'm'), None);
        assert_eq!(parser.feed(b'.'), None);
        // 0x0A = right + side1
        assert_eq!(parser.feed(0x0A), Some(ParseEvent::ButtonEvent(0x0A)));
    }

    #[test]
    fn button_event_mask_0x0d() {
        let mut parser = StreamParser::new();
        assert_eq!(parser.feed(b'k'), None);
        assert_eq!(parser.feed(b'm'), None);
        assert_eq!(parser.feed(b'.'), None);
        // 0x0D = left + middle + side1
        assert_eq!(parser.feed(0x0D), Some(ParseEvent::ButtonEvent(0x0D)));
    }

    #[test]
    fn button_event_mask_zero() {
        let mut parser = StreamParser::new();
        for &b in b"km." {
            assert_eq!(parser.feed(b), None);
        }
        assert_eq!(parser.feed(0x00), Some(ParseEvent::ButtonEvent(0x00)));
    }

    #[test]
    fn command_echo_not_confused_with_button() {
        let mut parser = StreamParser::new();
        // Feed "km.left(1)\r\n>>> "
        let input = b"km.left(1)\r\n>>> ";
        let mut events = Vec::new();
        for &b in input.iter() {
            if let Some(ev) = parser.feed(b) {
                events.push(ev);
            }
        }
        // Should get one Response, not a button event
        assert_eq!(events.len(), 1);
        match &events[0] {
            ParseEvent::Response(data) => {
                let text = String::from_utf8_lossy(data);
                assert!(text.contains("km.left(1)"), "got: {}", text);
            }
            other => panic!("expected Response, got {:?}", other),
        }
    }

    #[test]
    fn interleaved_button_and_response() {
        let mut parser = StreamParser::new();
        // Command echo, then button event, then prompt
        // "km.left(1)\r\n" + "km." + 0x05 + ">>> "
        let mut input: Vec<u8> = b"km.left(1)\r\n".to_vec();
        input.extend_from_slice(b"km.");
        input.push(0x05);
        input.extend_from_slice(b">>> ");

        let mut events = Vec::new();
        for &b in &input {
            if let Some(ev) = parser.feed(b) {
                events.push(ev);
            }
        }
        assert_eq!(events.len(), 2);
        assert_eq!(events[0], ParseEvent::ButtonEvent(0x05));
        assert!(matches!(&events[1], ParseEvent::Response(_)));
    }

    #[test]
    fn version_response_no_echo() {
        let mut parser = StreamParser::new();
        // km.version() is special: response is just "km.MAKCU\r\n>>> "
        // but "km." starts km prefix matching. 'M' >= 0x20 so it's flushed.
        let input = b"km.MAKCU\r\n>>> ";
        let mut events = Vec::new();
        for &b in input.iter() {
            if let Some(ev) = parser.feed(b) {
                events.push(ev);
            }
        }
        assert_eq!(events.len(), 1);
        match &events[0] {
            ParseEvent::Response(data) => {
                let text = String::from_utf8_lossy(data);
                assert!(text.contains("km.MAKCU"), "got: {}", text);
            }
            other => panic!("expected Response, got {:?}", other),
        }
    }

    #[test]
    fn classify_executed() {
        assert_eq!(classify_response(b""), ResponseKind::Executed);
        assert_eq!(classify_response(b"\r\n"), ResponseKind::Executed);
    }

    #[test]
    fn classify_value_multiline() {
        let resp = b"km.left()\r\n1";
        assert_eq!(
            classify_response(resp),
            ResponseKind::Value("1".to_string())
        );
    }

    #[test]
    fn catch_event_press() {
        let event = parse_catch_event(b"km.catch_ml(1)\r\n").unwrap();
        assert_eq!(event.button, crate::types::Button::Left);
        assert!(event.pressed);
    }

    #[test]
    fn catch_event_release() {
        let event = parse_catch_event(b"km.catch_ml(2)\r\n").unwrap();
        assert_eq!(event.button, crate::types::Button::Left);
        assert!(!event.pressed);
    }

    #[test]
    fn catch_event_right() {
        let event = parse_catch_event(b"km.catch_mr(1)").unwrap();
        assert_eq!(event.button, crate::types::Button::Right);
        assert!(event.pressed);
    }

    #[test]
    fn catch_event_middle() {
        let event = parse_catch_event(b"km.catch_mm(2)").unwrap();
        assert_eq!(event.button, crate::types::Button::Middle);
        assert!(!event.pressed);
    }

    #[test]
    fn catch_event_side1() {
        let event = parse_catch_event(b"km.catch_ms1(1)").unwrap();
        assert_eq!(event.button, crate::types::Button::Side1);
    }

    #[test]
    fn catch_event_side2() {
        let event = parse_catch_event(b"km.catch_ms2(2)").unwrap();
        assert_eq!(event.button, crate::types::Button::Side2);
    }

    #[test]
    fn catch_event_not_catch() {
        // Normal command response should not parse as catch
        assert!(parse_catch_event(b"km.left(1)\r\n").is_none());
        assert!(parse_catch_event(b"km.lock_ml(1)\r\n").is_none());
        assert!(parse_catch_event(b"").is_none());
    }

    #[test]
    fn catch_event_enable_response_not_catch() {
        // The enable command echo "km.catch_ml(0)" should NOT be a catch event
        assert!(parse_catch_event(b"km.catch_ml(0)\r\n").is_none());
    }

    #[test]
    fn catch_event_through_parser() {
        // Full catch event as it arrives on the wire: "km.catch_ml(1)\r\n>>> "
        let mut parser = StreamParser::new();
        let input = b"km.catch_ml(1)\r\n>>> ";
        let mut events = Vec::new();
        for &b in input.iter() {
            if let Some(ev) = parser.feed(b) {
                events.push(ev);
            }
        }
        // Parser sees "km." then 'c' >= 0x20, flushes to response buffer
        assert_eq!(events.len(), 1);
        match &events[0] {
            ParseEvent::Response(data) => {
                let catch = parse_catch_event(data).unwrap();
                assert_eq!(catch.button, crate::types::Button::Left);
                assert!(catch.pressed);
            }
            other => panic!("expected Response, got {:?}", other),
        }
    }

    #[test]
    fn classify_single_line() {
        let resp = b"km.MAKCU";
        assert_eq!(
            classify_response(resp),
            ResponseKind::ValueOrEcho("km.MAKCU".to_string())
        );
    }
}
