#!/usr/bin/env python3
"""Kleines Probe-Tool fuer Huawei-Modbus-TCP-Tests.

Unterstuetzt:
- FC03 / FC04 Lesen
- FC06 / FC10 Schreiben
- 0x2B / 0x0E Device-ID und Device-List

Das Tool ist absichtlich minimal gehalten und dient zur schnellen Feldanalyse
gegen Inverter, sDongle oder Wallbox.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys
from typing import Iterable


def recv_all(sock: socket.socket, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = sock.recv(size - len(buf))
        if not chunk:
            raise ConnectionError("socket closed while receiving response")
        buf.extend(chunk)
    return bytes(buf)


def transact(host: str, port: int, unit: int, pdu: bytes, timeout: float) -> tuple[bytes, bytes]:
    req = struct.pack(">HHHB", 1, 0, len(pdu) + 1, unit) + pdu
    with socket.create_connection((host, port), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(req)
        mbap = recv_all(sock, 7)
        length = struct.unpack(">H", mbap[4:6])[0]
        body = recv_all(sock, length - 1)
    return mbap, body


def parse_words(values: Iterable[int]) -> list[int]:
    parsed: list[int] = []
    for item in values:
        if item < 0 or item > 0xFFFF:
            raise ValueError(f"value out of range: {item}")
        parsed.append(item)
    return parsed


def format_register_words(body: bytes) -> str:
    if len(body) < 2:
        return body.hex()
    if body[0] & 0x80:
        return f"exception fc=0x{body[0]:02x} ex=0x{body[1]:02x}"
    if body[0] not in (0x03, 0x04) or len(body) < 2:
        return body.hex()
    byte_count = body[1]
    data = body[2 : 2 + byte_count]
    words = [f"0x{struct.unpack('>H', data[i:i+2])[0]:04x}" for i in range(0, len(data), 2)]
    return f"fc=0x{body[0]:02x} words=[{', '.join(words)}]"


def cmd_read(args: argparse.Namespace) -> int:
    fc = 0x04 if args.input else 0x03
    pdu = struct.pack(">BHH", fc, args.register, args.count)
    mbap, body = transact(args.host, args.port, args.unit, pdu, args.timeout)
    print(f"mbap={mbap.hex()}")
    print(f"body={body.hex()}")
    print(format_register_words(body))
    return 0


def cmd_write_single(args: argparse.Namespace) -> int:
    pdu = struct.pack(">BHH", 0x06, args.register, args.value)
    mbap, body = transact(args.host, args.port, args.unit, pdu, args.timeout)
    print(f"mbap={mbap.hex()}")
    print(f"body={body.hex()}")
    return 0


def cmd_write_multi(args: argparse.Namespace) -> int:
    values = parse_words(args.values)
    payload = b"".join(struct.pack(">H", value) for value in values)
    pdu = struct.pack(">BHHB", 0x10, args.register, len(values), len(payload)) + payload
    mbap, body = transact(args.host, args.port, args.unit, pdu, args.timeout)
    print(f"mbap={mbap.hex()}")
    print(f"body={body.hex()}")
    return 0


def cmd_device_id(args: argparse.Namespace) -> int:
    pdu = bytes((0x2B, 0x0E, 0x01, 0x00))
    mbap, body = transact(args.host, args.port, args.unit, pdu, args.timeout)
    print(f"mbap={mbap.hex()}")
    print(f"body={body.hex()}")
    return 0


def cmd_device_list(args: argparse.Namespace) -> int:
    pdu = bytes((0x2B, 0x0E, 0x03, 0x87))
    mbap, body = transact(args.host, args.port, args.unit, pdu, args.timeout)
    print(f"mbap={mbap.hex()}")
    print(f"body={body.hex()}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Huawei Modbus TCP probe tool")
    parser.add_argument("--host", required=True, help="Target host, for example 192.168.1.101")
    parser.add_argument("--port", type=int, default=502, help="Target TCP port")
    parser.add_argument("--unit", type=int, default=3, help="Modbus logical device id")
    parser.add_argument("--timeout", type=float, default=1.0, help="Socket timeout in seconds")

    sub = parser.add_subparsers(dest="command", required=True)

    read_parser = sub.add_parser("read", help="Read holding or input registers")
    read_parser.add_argument("--register", type=int, required=True)
    read_parser.add_argument("--count", type=int, default=1)
    read_parser.add_argument("--input", action="store_true", help="Use FC04 instead of FC03")
    read_parser.set_defaults(func=cmd_read)

    write_single_parser = sub.add_parser("write-single", help="Write one holding register using FC06")
    write_single_parser.add_argument("--register", type=int, required=True)
    write_single_parser.add_argument("--value", type=int, required=True)
    write_single_parser.set_defaults(func=cmd_write_single)

    write_multi_parser = sub.add_parser("write-multi", help="Write multiple holding registers using FC10")
    write_multi_parser.add_argument("--register", type=int, required=True)
    write_multi_parser.add_argument("values", type=int, nargs="+")
    write_multi_parser.set_defaults(func=cmd_write_multi)

    device_id_parser = sub.add_parser("device-id", help="Send Huawei 0x2B/0x0E device-id request")
    device_id_parser.set_defaults(func=cmd_device_id)

    device_list_parser = sub.add_parser("device-list", help="Send Huawei 0x2B/0x0E device-list request")
    device_list_parser.set_defaults(func=cmd_device_list)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return int(args.func(args))
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
