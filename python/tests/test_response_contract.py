from makxd.connection import parse_ascii_response_body


def test_ascii_set_response_returns_echo_only():
    assert parse_ascii_response_body(b"km.left(1)\r\n") == "km.left(1)"


def test_ascii_get_response_discards_echo_and_returns_value():
    assert parse_ascii_response_body(b"km.left()\r\n1\r\n") == "1"


def test_ascii_multiline_query_discards_only_echo_line():
    assert parse_ascii_response_body(b"km.version()\r\nMAKXD\r\n") == "MAKXD"
