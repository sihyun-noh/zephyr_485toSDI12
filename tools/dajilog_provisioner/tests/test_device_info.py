import unittest

from dajilog_provisioner.device_info import (
    BasicInfo,
    CanFrame,
    Command,
    FirmwareInfo,
    Health,
    ProtocolError,
    SerialInfo,
    StatusInfo,
    decode_response,
    encode_request,
)


class DeviceInfoCodecTest(unittest.TestCase):
    def test_encodes_get_basic_request_for_node_12(self) -> None:
        frame = encode_request(12, Command.GET_BASIC, 1)
        self.assertEqual(frame.arbitration_id, 0x60C)
        self.assertEqual(frame.data, bytes.fromhex("01 01 01 00 00 00 00 00"))

    def test_decodes_basic_response(self) -> None:
        frame = CanFrame(0x58C, bytes.fromhex("01 81 01 0C 01 01 07 02"))
        self.assertEqual(
            decode_response(frame, 12, Command.GET_BASIC, 1),
            BasicInfo(12, 1, 1, 0x0207),
        )

    def test_decodes_firmware_response(self) -> None:
        frame = CanFrame(0x58C, bytes.fromhex("01 82 02 00 01 00 01 01"))
        self.assertEqual(
            decode_response(frame, 12, Command.GET_FIRMWARE, 2),
            FirmwareInfo(0, 1, 0, 1, 1),
        )

    def test_decodes_serial_response_little_endian(self) -> None:
        frame = CanFrame(0x58C, bytes.fromhex("01 83 03 A1 86 01 00 00"))
        self.assertEqual(
            decode_response(frame, 12, Command.GET_SERIAL, 3),
            SerialInfo(100001),
        )

    def test_decodes_signed_status_error(self) -> None:
        frame = CanFrame(0x58C, bytes.fromhex("01 84 04 02 FF FF FF FF"))
        self.assertEqual(
            decode_response(frame, 12, Command.GET_STATUS, 4),
            StatusInfo(Health.DEGRADED, -1),
        )

    def test_rejects_sequence_mismatch(self) -> None:
        frame = CanFrame(0x58C, bytes.fromhex("01 81 09 0C 01 01 07 02"))
        with self.assertRaisesRegex(ProtocolError, "sequence mismatch"):
            decode_response(frame, 12, Command.GET_BASIC, 1)

    def test_rejects_invalid_node_id(self) -> None:
        with self.assertRaises(ValueError):
            encode_request(0, Command.GET_BASIC, 1)


if __name__ == "__main__":
    unittest.main()
