#!/usr/bin/env python3
"""Mehrclient-Validierung gegen den ESP32-Modbus-Server."""

import argparse
import threading
import time

from pymodbus.client import ModbusTcpClient


def worker(host: str, port: int, unit: int, register: int, count: int, iterations: int,
           timeout: float, results: dict[str, int], lock: threading.Lock) -> None:
    ok = 0
    fail = 0
    client = ModbusTcpClient(host=host, port=port, timeout=timeout)
    if not client.connect():
        with lock:
            results["fail"] += iterations
        return

    try:
        for _ in range(iterations):
            resp = client.read_holding_registers(address=register, count=count, device_id=unit)
            if resp.isError():
                fail += 1
            else:
                ok += 1
            time.sleep(0.05)
    finally:
        client.close()

    with lock:
        results["ok"] += ok
        results["fail"] += fail


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate concurrent Modbus reads")
    parser.add_argument("--host", required=True, help="ESP32 IP")
    parser.add_argument("--port", type=int, default=502)
    parser.add_argument("--unit", type=int, default=1)
    parser.add_argument("--register", type=int, default=37004)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--clients", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=20)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    threads: list[threading.Thread] = []
    results = {"ok": 0, "fail": 0}
    lock = threading.Lock()

    for _ in range(args.clients):
        t = threading.Thread(
            target=worker,
            args=(args.host, args.port, args.unit, args.register,
                  args.count, args.iterations, args.timeout, results, lock),
        )
        t.start()
        threads.append(t)

    for t in threads:
        t.join()

    total = args.clients * args.iterations
    print(f"Total requests: {total}")
    print(f"OK: {results['ok']}")
    print(f"Fail: {results['fail']}")

    return 0 if results["fail"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
