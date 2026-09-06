"""Host-side Device Info service."""

from __future__ import annotations

import time

from .device_info import Command, ProtocolError, decode_response, encode_request
from .transport import CanTransport


class ResponseTimeout(TimeoutError):
    pass


class DeviceInfoService:
    def __init__(self, transport: CanTransport, *, timeout: float = 0.5) -> None:
        self._transport = transport
        self._timeout = timeout
        self._sequence = 0

    def request(self, node_id: int, command: Command):
        self._sequence = (self._sequence + 1) & 0xFF
        request = encode_request(node_id, command, self._sequence)
        self._transport.send(request)

        deadline = time.monotonic() + self._timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ResponseTimeout(
                    f"node {node_id} did not answer {command.name} within {self._timeout:.3f}s"
                )
            response = self._transport.receive(remaining)
            if response is None:
                raise ResponseTimeout(
                    f"node {node_id} did not answer {command.name} within {self._timeout:.3f}s"
                )
            try:
                return decode_response(response, node_id, command, self._sequence)
            except ProtocolError:
                # Other traffic can coexist on the shared CAN bus. Ignore it and
                # continue until a matching response arrives or receive times out.
                continue
