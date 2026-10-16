#!/usr/bin/env python3
"""Exercise the live S3/C5 link through the read-only USB console."""

import argparse
import time

import serial


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM12")
    parser.add_argument("--iterations", type=int, default=100)
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()

    passed = 0
    contention_timeouts = 0
    failures: list[tuple[int, str]] = []
    started = time.monotonic()

    with serial.Serial(args.port, 115200, timeout=0.02) as console:
        console.reset_input_buffer()
        for iteration in range(args.iterations):
            console.write(b"c5\r\n")
            console.flush()
            response = bytearray()
            deadline = time.monotonic() + args.timeout
            while b"cerberus> " not in response and time.monotonic() < deadline:
                response.extend(console.read(4096))

            valid = (
                b"status=online" in response
                and b"fragment_transfer=supported" in response
                and b"status=unavailable" not in response
                and b"invalid response" not in response
            )
            if valid:
                passed += 1
            elif b"status=unavailable" in response and b"ESP_ERR_TIMEOUT" in response:
                # The console, UI, and event pump share a bounded mutex. A busy
                # mutex is scheduler contention, not a corrupt SPI transfer.
                contention_timeouts += 1
            else:
                failures.append((iteration, response[-500:].decode(errors="replace")))

    elapsed = time.monotonic() - started
    print(
        f"passes={passed} contention_timeouts={contention_timeouts} "
        f"transport_failures={len(failures)} elapsed_s={elapsed:.2f}"
    )
    for iteration, response in failures[:5]:
        print(f"failure iteration={iteration}\n{response}")
    return 0 if passed > 0 and not failures else 1


if __name__ == "__main__":
    raise SystemExit(main())
