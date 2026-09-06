"""CAN transport abstraction and python-can implementation."""

from __future__ import annotations

from typing import Protocol

from .device_info import CanFrame


class CanTransport(Protocol):
    def send(self, frame: CanFrame) -> None: ...

    def receive(self, timeout: float) -> CanFrame | None: ...

    def close(self) -> None: ...


class PythonCanTransport:
    def __init__(self, *, interface: str, channel: str, bitrate: int) -> None:
        try:
            import can
        except ImportError as exc:
            raise RuntimeError(
                "python-can is required; install the project with 'pip install -e .'"
            ) from exc
        self._can = can
        self._bus = can.Bus(interface=interface, channel=channel, bitrate=bitrate)

    def send(self, frame: CanFrame) -> None:
        message = self._can.Message(
            arbitration_id=frame.arbitration_id,
            is_extended_id=False,
            data=frame.data,
        )
        self._bus.send(message)

    def receive(self, timeout: float) -> CanFrame | None:
        message = self._bus.recv(timeout)
        if message is None:
            return None
        if message.is_extended_id or message.is_remote_frame or message.is_error_frame:
            return CanFrame(message.arbitration_id, b"")
        return CanFrame(message.arbitration_id, bytes(message.data))

    def close(self) -> None:
        self._bus.shutdown()

    def __enter__(self) -> "PythonCanTransport":
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

