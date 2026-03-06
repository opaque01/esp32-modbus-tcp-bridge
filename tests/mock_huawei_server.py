#!/usr/bin/env python3
"""Minimaler Mock-Huawei-Modbus-TCP-Server für ESP32-Bridge-Tests."""

import argparse
import socket
import threading

RANGES = [
    (30000, 30074),
    (32000, 32116),
    (37000, 37122),
    (38210, 38233),
]


def valid_register(addr: int) -> bool:
    for start, end in RANGES:
        if start <= addr <= end:
            return True
    return False


def value_for(addr: int) -> int:
    # Deterministischer Testwert: unteres 16-Bit-Wort der Registeradresse
    return addr & 0xFFFF


def recv_all(conn: socket.socket, n: int) -> bytes | None:
    buf = bytearray()
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def send_exception(conn: socket.socket, tid: int, unit: int, fc: int, ex: int) -> None:
    resp = bytes([
        (tid >> 8) & 0xFF,
        tid & 0xFF,
        0x00,
        0x00,
        0x00,
        0x03,
        unit,
        fc | 0x80,
        ex,
    ])
    conn.sendall(resp)


def handle_client(conn: socket.socket, addr: tuple[str, int]) -> None:
    print(f"Client verbunden: {addr}")
    try:
        while True:
            mbap = recv_all(conn, 7)
            if mbap is None:
                break

            tid = (mbap[0] << 8) | mbap[1]
            length = (mbap[4] << 8) | mbap[5]
            unit = mbap[6]
            if length < 2:
                break

            pdu = recv_all(conn, length - 1)
            if pdu is None or len(pdu) < 5:
                break

            fc = pdu[0]
            if fc != 0x03:
                send_exception(conn, tid, unit, fc, 0x01)
                continue

            start = (pdu[1] << 8) | pdu[2]
            count = (pdu[3] << 8) | pdu[4]
            if count == 0 or count > 125:
                send_exception(conn, tid, unit, fc, 0x03)
                continue

            data = bytearray()
            bad = False
            for i in range(count):
                reg = start + i
                if not valid_register(reg):
                    bad = True
                    break
                val = value_for(reg)
                data.append((val >> 8) & 0xFF)
                data.append(val & 0xFF)

            if bad:
                send_exception(conn, tid, unit, fc, 0x02)
                continue

            byte_count = len(data)
            pdu_resp = bytes([0x03, byte_count]) + bytes(data)
            length_resp = 1 + len(pdu_resp)
            mbap_resp = bytes([
                (tid >> 8) & 0xFF,
                tid & 0xFF,
                0x00,
                0x00,
                (length_resp >> 8) & 0xFF,
                length_resp & 0xFF,
                unit,
            ])
            conn.sendall(mbap_resp + pdu_resp)
    finally:
        conn.close()
        print(f"Client getrennt: {addr}")


def main() -> None:
    parser = argparse.ArgumentParser(description="Mock Huawei Modbus TCP Server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=1502)
    args = parser.parse_args()

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((args.host, args.port))
        srv.listen(8)
        print(f"Mock Huawei läuft auf {args.host}:{args.port}")
        while True:
            conn, caddr = srv.accept()
            threading.Thread(target=handle_client, args=(conn, caddr), daemon=True).start()


if __name__ == "__main__":
    main()
