"""Bounded serial probe/control with DTR/RTS disabled; no implicit commands."""
import argparse
from pathlib import Path
import time
import serial
parser = argparse.ArgumentParser()
parser.add_argument("--port", required=True)
parser.add_argument("--baud", type=int, default=115200)
parser.add_argument("--seconds", type=float, default=3)
parser.add_argument("--command", action="append", default=[])
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--expected-bridge-mac")
args = parser.parse_args()
if not 0 < args.seconds <= 30: raise ValueError("Probe duration must be 0..30 seconds")
port = serial.Serial(port=None, baudrate=args.baud, timeout=0.1, write_timeout=2)
port.dtr = False; port.rts = False; port.port = args.port
buffer = bytearray()
with port:
    for command in args.command:
        port.write((command + "\n").encode()); time.sleep(0.1)
    deadline = time.monotonic() + args.seconds
    while time.monotonic() < deadline: buffer.extend(port.read(4096))
args.output.write_bytes(buffer)
if args.expected_bridge_mac:
    expected = "CERBERUS_BRIDGE_V1 s3_mac=" + args.expected_bridge_mac.lower()
    if expected.lower().encode() not in buffer.lower():
        raise RuntimeError("Bridge identity did not match the selected S3")
    print("Bridge carrier identity matches the selected S3.")
else:
    print(f"Saved {len(buffer)} console bytes to {args.output}")
