"""Command-line entry point for Dajilog Provisioner."""

from __future__ import annotations

import argparse
import json
from dataclasses import asdict
from enum import Enum

from .device_info import Command
from .service import DeviceInfoService
from .transport import PythonCanTransport


def _json_default(value: object) -> object:
    if isinstance(value, Enum):
        return value.name
    raise TypeError(f"cannot serialize {type(value).__name__}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(prog="dajilog-provision")
    subparsers = parser.add_subparsers(dest="operation", required=True)
    info = subparsers.add_parser("info", help="read Device Info v0.1")
    info.add_argument("--interface", required=True, help="python-can interface")
    info.add_argument("--channel", required=True, help="adapter channel")
    info.add_argument("--bitrate", type=int, default=500_000)
    info.add_argument("--node-id", type=int, required=True)
    info.add_argument("--timeout", type=float, default=0.5)
    return parser


def run_info(args: argparse.Namespace) -> int:
    with PythonCanTransport(
        interface=args.interface, channel=args.channel, bitrate=args.bitrate
    ) as transport:
        service = DeviceInfoService(transport, timeout=args.timeout)
        output = {}
        for command in (
            Command.GET_BASIC,
            Command.GET_FIRMWARE,
            Command.GET_SERIAL,
            Command.GET_STATUS,
        ):
            output[command.name.lower()] = asdict(service.request(args.node_id, command))
    print(json.dumps(output, indent=2, default=_json_default))
    return 0


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    if args.operation == "info":
        return run_info(args)
    raise AssertionError(f"unhandled operation: {args.operation}")


if __name__ == "__main__":
    raise SystemExit(main())

