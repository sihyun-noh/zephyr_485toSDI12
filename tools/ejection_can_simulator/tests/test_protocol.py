import pytest

from ejection_can_simulator.can_adapter import CanFrame, FrameParser, encode_config, encode_frame
from ejection_can_simulator.protocol import heartbeat_frame, output_frame, parse_status


def test_waveshare_config_is_250k_standard() -> None:
    packet = encode_config()
    assert len(packet) == 20
    assert packet[:5] == bytes.fromhex("AA 55 12 05 01")
    assert packet[-1] == sum(packet[2:19]) & 0xFF


@pytest.mark.parametrize(
    ("bitrate", "code"),
    ((125_000, 0x07), (250_000, 0x05), (500_000, 0x03), (1_000_000, 0x01)),
)
def test_waveshare_config_bitrates(bitrate: int, code: int) -> None:
    packet = encode_config(bitrate)
    assert packet[3] == code
    assert packet[4] == 0x01


def test_waveshare_config_rejects_unknown_bitrate() -> None:
    with pytest.raises(ValueError):
        encode_config(333_000)


def test_standard_output_frame_encoding() -> None:
    frame = output_frame(0x0801)
    assert encode_frame(frame) == bytes.fromhex("AA C8 01 01 01 08 00 00 00 00 00 00 55")
    assert FrameParser().feed(encode_frame(frame)) == [frame]


def test_output_range_and_heartbeat() -> None:
    with pytest.raises(ValueError):
        output_frame(0x1000)
    assert heartbeat_frame(0x102).data == bytes.fromhex("01 02")


def test_status_decode() -> None:
    status = parse_status(CanFrame(0x201, bytes.fromhex("01 2A 04 00 55 15 AA F5")))
    assert status.sequence == 0x2A
    assert status.state_name == "RUNNING"
    assert status.inputs == 0x1555
    assert status.outputs == 0x05AA
    assert status.settings_ready and status.can_ready
    assert status.encoder_ready and status.oled_ready
