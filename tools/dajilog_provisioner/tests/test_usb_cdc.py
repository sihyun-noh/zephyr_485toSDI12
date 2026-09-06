from dajilog_provisioner.usb_cdc import LineBuffer


def test_line_buffer_accepts_fragmented_crlf_lines() -> None:
    buffer = LineBuffer()
    assert buffer.feed(b"device profile") == []
    assert buffer.feed(b" get\r\nnode_id=12\r\n") == ["device profile get", "node_id=12"]


def test_line_buffer_replaces_invalid_utf8() -> None:
    assert LineBuffer().feed(b"error: \xff\n") == ["error: �"]

