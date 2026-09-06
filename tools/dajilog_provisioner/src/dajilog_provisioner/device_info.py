"""Codec for Dajilog Device Info protocol v0.1."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum

PROTOCOL_VERSION = 0x01
REQUEST_BASE_ID = 0x600
RESPONSE_BASE_ID = 0x580
MIN_NODE_ID = 1
MAX_NODE_ID = 127
FRAME_LENGTH = 8


class ProtocolError(ValueError):
    """A CAN frame violates the Device Info v0.1 contract."""


class Command(IntEnum):
    GET_BASIC = 0x01
    GET_FIRMWARE = 0x02
    GET_SERIAL = 0x03
    GET_UID_PART = 0x04
    GET_STATUS = 0x05


class Health(IntEnum):
    UNASSIGNED = 0
    READY = 1
    DEGRADED = 2
    FAULT = 3


RESPONSE_COMMAND = {
    Command.GET_BASIC: 0x81,
    Command.GET_FIRMWARE: 0x82,
    Command.GET_SERIAL: 0x83,
    Command.GET_STATUS: 0x84,
}


@dataclass(frozen=True)
class CanFrame:
    arbitration_id: int
    data: bytes


@dataclass(frozen=True)
class BasicInfo:
    node_id: int
    device_type: int
    hw_revision: int
    capability: int


@dataclass(frozen=True)
class FirmwareInfo:
    major: int
    minor: int
    patch: int
    app_protocol_version: int
    build_revision: int


@dataclass(frozen=True)
class SerialInfo:
    serial_number: int


@dataclass(frozen=True)
class StatusInfo:
    health: Health
    last_error: int


def validate_node_id(node_id: int) -> None:
    if not MIN_NODE_ID <= node_id <= MAX_NODE_ID:
        raise ValueError(f"node_id must be in {MIN_NODE_ID}..{MAX_NODE_ID}")


def encode_request(node_id: int, command: Command, sequence: int) -> CanFrame:
    validate_node_id(node_id)
    if not 0 <= sequence <= 0xFF:
        raise ValueError("sequence must be in 0..255")
    data = bytes((PROTOCOL_VERSION, int(command), sequence, 0, 0, 0, 0, 0))
    return CanFrame(REQUEST_BASE_ID + node_id, data)


def decode_response(
    frame: CanFrame, node_id: int, command: Command, sequence: int
) -> BasicInfo | FirmwareInfo | SerialInfo | StatusInfo:
    validate_node_id(node_id)
    expected_id = RESPONSE_BASE_ID + node_id
    if frame.arbitration_id != expected_id:
        raise ProtocolError(
            f"unexpected CAN ID 0x{frame.arbitration_id:03X}; expected 0x{expected_id:03X}"
        )
    if len(frame.data) != FRAME_LENGTH:
        raise ProtocolError(f"unexpected DLC {len(frame.data)}; expected {FRAME_LENGTH}")
    if frame.data[0] != PROTOCOL_VERSION:
        raise ProtocolError(f"unsupported protocol version {frame.data[0]}")
    try:
        expected_response = RESPONSE_COMMAND[command]
    except KeyError as exc:
        raise ProtocolError(
            f"response decoder is not implemented for {command.name}"
        ) from exc
    if frame.data[1] != expected_response:
        raise ProtocolError(
            f"unexpected response command 0x{frame.data[1]:02X}; "
            f"expected 0x{expected_response:02X}"
        )
    if frame.data[2] != sequence:
        raise ProtocolError(
            f"sequence mismatch {frame.data[2]}; expected {sequence}"
        )

    data = frame.data
    if command is Command.GET_BASIC:
        if data[3] != node_id:
            raise ProtocolError(
                f"payload node_id {data[3]} does not match requested node_id {node_id}"
            )
        return BasicInfo(data[3], data[4], data[5], int.from_bytes(data[6:8], "little"))
    if command is Command.GET_FIRMWARE:
        return FirmwareInfo(data[3], data[4], data[5], data[6], data[7])
    if command is Command.GET_SERIAL:
        return SerialInfo(int.from_bytes(data[3:7], "little"))
    if command is Command.GET_STATUS:
        try:
            health = Health(data[3])
        except ValueError as exc:
            raise ProtocolError(f"unknown health value {data[3]}") from exc
        return StatusInfo(health, int.from_bytes(data[4:8], "little", signed=True))
    raise ProtocolError(f"response decoder is not implemented for {command.name}")
