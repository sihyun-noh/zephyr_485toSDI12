from collections import deque
from dataclasses import dataclass
from queue import Empty, Queue
from threading import Timer

import serial
from PySide6.QtCore import QThread, Signal

SERIAL_BAUDRATE = 2_000_000
CAN_BITRATE_CODES = {
    125_000: 0x07,
    250_000: 0x05,
    500_000: 0x03,
    1_000_000: 0x01,
}


@dataclass(frozen=True)
class CanFrame:
    arbitration_id: int
    data: bytes = b""
    is_extended: bool = False

    def __post_init__(self) -> None:
        max_id = 0x1FFFFFFF if self.is_extended else 0x7FF
        if not 0 <= self.arbitration_id <= max_id:
            raise ValueError("CAN ID가 허용 범위를 벗어났습니다.")
        if len(self.data) > 8:
            raise ValueError("Classical CAN 데이터는 최대 8바이트입니다.")


def encode_config(bitrate: int = 250_000) -> bytes:
    """Waveshare USB-CAN-A: selected bitrate, standard frame, normal mode."""
    try:
        bitrate_code = CAN_BITRATE_CODES[bitrate]
    except KeyError as exc:
        raise ValueError(f"지원하지 않는 CAN bitrate: {bitrate}") from exc
    packet = bytearray(20)
    packet[0:5] = bytes((0xAA, 0x55, 0x12, bitrate_code, 0x01))
    packet[13] = 0x00
    packet[14] = 0x00
    packet[19] = sum(packet[2:19]) & 0xFF
    return bytes(packet)


def encode_frame(frame: CanFrame) -> bytes:
    type_byte = 0xC0 | len(frame.data) | (0x20 if frame.is_extended else 0)
    id_size = 4 if frame.is_extended else 2
    identifier = frame.arbitration_id.to_bytes(id_size, "little")
    return bytes((0xAA, type_byte)) + identifier + frame.data + b"\x55"


class FrameParser:
    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> list[CanFrame]:
        self._buffer.extend(chunk)
        frames: list[CanFrame] = []
        while True:
            start = self._buffer.find(0xAA)
            if start < 0:
                self._buffer.clear()
                break
            del self._buffer[:start]
            if len(self._buffer) < 2:
                break
            frame_type = self._buffer[1]
            dlc = frame_type & 0x0F
            if frame_type & 0xC0 != 0xC0 or dlc > 8:
                del self._buffer[0]
                continue
            extended = bool(frame_type & 0x20)
            remote = bool(frame_type & 0x10)
            id_size = 4 if extended else 2
            payload_size = 0 if remote else dlc
            packet_size = 2 + id_size + payload_size + 1
            if len(self._buffer) < packet_size:
                break
            if self._buffer[packet_size - 1] != 0x55:
                del self._buffer[0]
                continue
            can_id = int.from_bytes(self._buffer[2 : 2 + id_size], "little")
            data_start = 2 + id_size
            try:
                frames.append(
                    CanFrame(
                        can_id,
                        bytes(self._buffer[data_start : data_start + payload_size]),
                        extended,
                    )
                )
            except ValueError:
                pass
            del self._buffer[:packet_size]
        return frames


class CanWorker(QThread):
    frame_received = Signal(object)
    frame_sent = Signal(object)
    connection_changed = Signal(bool, str)
    error_occurred = Signal(str)

    def __init__(self, port: str, bitrate: int = 250_000) -> None:
        super().__init__()
        self._port = port
        self._bitrate = bitrate
        self._tx: Queue[CanFrame] = Queue()

    def enqueue(self, frame: CanFrame) -> None:
        self._tx.put(frame)

    def enqueue_after(self, frame: CanFrame, delay_ms: int) -> None:
        """Queue a frame independently of the potentially busy GUI event loop."""
        timer = Timer(delay_ms / 1000.0, self.enqueue, args=(frame,))
        timer.daemon = True
        timer.start()

    def run(self) -> None:
        connection: serial.Serial | None = None
        pending: deque[CanFrame] = deque()
        parser = FrameParser()
        try:
            connection = serial.Serial(
                self._port,
                SERIAL_BAUDRATE,
                bytesize=8,
                parity="N",
                stopbits=1,
                timeout=0.005,
                write_timeout=1.0,
            )
            connection.reset_input_buffer()
            connection.write(encode_config(self._bitrate))
            connection.flush()
            self.connection_changed.emit(True, self._port)
            while not self.isInterruptionRequested():
                try:
                    while True:
                        frame = self._tx.get_nowait()
                        connection.write(encode_frame(frame))
                        self.frame_sent.emit(frame)
                except Empty:
                    pass
                chunk = connection.read(connection.in_waiting or 1)
                pending.extend(parser.feed(chunk))
                while pending:
                    self.frame_received.emit(pending.popleft())
        except Exception as exc:  # noqa: BLE001
            self.error_occurred.emit(str(exc))
        finally:
            if connection is not None:
                connection.close()
            self.connection_changed.emit(False, self._port)
