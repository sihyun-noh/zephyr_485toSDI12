"""USB CDC serial worker for the Zephyr shell provisioning channel."""

from __future__ import annotations

from queue import Empty, Queue

import serial
from PySide6.QtCore import QThread, Signal

CDC_BAUDRATE = 115_200


class LineBuffer:
    """Convert arbitrary serial chunks into normalized text lines."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, chunk: bytes) -> list[str]:
        self._buffer.extend(chunk)
        lines: list[str] = []
        while b"\n" in self._buffer:
            raw, _, remainder = self._buffer.partition(b"\n")
            self._buffer = bytearray(remainder)
            lines.append(raw.rstrip(b"\r").decode("utf-8", errors="replace"))
        return lines


class UsbCdcWorker(QThread):
    line_received = Signal(str)
    command_sent = Signal(str)
    connection_changed = Signal(bool, str)
    error_occurred = Signal(str)

    def __init__(self, port: str) -> None:
        super().__init__()
        self._port = port
        self._tx: Queue[str] = Queue()

    def send_command(self, command: str) -> None:
        command = command.strip()
        if command:
            self._tx.put(command)

    def run(self) -> None:
        connection: serial.Serial | None = None
        lines = LineBuffer()
        try:
            connection = serial.Serial(
                self._port,
                CDC_BAUDRATE,
                bytesize=8,
                parity="N",
                stopbits=1,
                timeout=0.05,
                write_timeout=1.0,
            )
            self.connection_changed.emit(True, self._port)
            connection.write(b"\r\n")
            connection.flush()
            while not self.isInterruptionRequested():
                try:
                    while True:
                        command = self._tx.get_nowait()
                        connection.write(command.encode("utf-8") + b"\r\n")
                        connection.flush()
                        self.command_sent.emit(command)
                except Empty:
                    pass
                chunk = connection.read(connection.in_waiting or 1)
                for line in lines.feed(chunk):
                    self.line_received.emit(line)
        except Exception as exc:  # noqa: BLE001
            self.error_occurred.emit(str(exc))
        finally:
            if connection is not None:
                connection.close()
            self.connection_changed.emit(False, self._port)

