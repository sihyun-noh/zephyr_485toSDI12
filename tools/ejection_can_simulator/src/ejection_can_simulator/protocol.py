from dataclasses import dataclass

from ejection_can_simulator.can_adapter import CanFrame

OUTPUT_ID = 0x101
HEARTBEAT_ID = 0x102
STATUS_ID = 0x201
PROTOCOL_VERSION = 0x01

STATE_NAMES = {0: "BOOT", 1: "SELF_TEST", 2: "IDLE", 3: "READY", 4: "RUNNING", 5: "FAULT"}
FAULT_NAMES = {
    0: "NONE", 1: "IO_INIT", 2: "CAN_INIT", 3: "OLED_INIT", 4: "EEPROM_INIT",
    5: "INVALID_CONFIG", 6: "CAN_TIMEOUT", 7: "CAN_BUS_OFF",
    8: "EMERGENCY_STOP",
}


def output_frame(mask: int) -> CanFrame:
    if not 0 <= mask <= 0x0FFF:
        raise ValueError("출력 mask는 CH1~CH12 범위여야 합니다.")
    data = bytes((mask & 0xFF, (mask >> 8) & 0x0F, 0, 0, 0, 0, 0, 0))
    return CanFrame(OUTPUT_ID, data)


def heartbeat_frame(sequence: int) -> CanFrame:
    return CanFrame(HEARTBEAT_ID, bytes((PROTOCOL_VERSION, sequence & 0xFF)))


@dataclass(frozen=True)
class EjectionStatus:
    sequence: int
    state_code: int
    fault_code: int
    inputs: int
    outputs: int
    settings_ready: bool
    can_ready: bool
    encoder_ready: bool
    oled_ready: bool

    @property
    def state_name(self) -> str:
        return STATE_NAMES.get(self.state_code, f"UNKNOWN({self.state_code})")

    @property
    def fault_name(self) -> str:
        return FAULT_NAMES.get(self.fault_code, f"UNKNOWN({self.fault_code})")


def parse_status(frame: CanFrame) -> EjectionStatus:
    if frame.arbitration_id != STATUS_ID or frame.is_extended:
        raise ValueError("12CH 상태 프레임이 아닙니다.")
    if len(frame.data) != 8:
        raise ValueError("상태 프레임 DLC는 8이어야 합니다.")
    data = frame.data
    if data[0] != PROTOCOL_VERSION:
        raise ValueError(f"지원하지 않는 protocol version: {data[0]}")
    return EjectionStatus(
        sequence=data[1],
        state_code=data[2],
        fault_code=data[3],
        inputs=data[4] | ((data[5] & 0x3F) << 8),
        outputs=data[6] | ((data[7] & 0x0F) << 8),
        settings_ready=bool(data[7] & 0x10),
        can_ready=bool(data[7] & 0x20),
        encoder_ready=bool(data[7] & 0x40),
        oled_ready=bool(data[7] & 0x80),
    )
