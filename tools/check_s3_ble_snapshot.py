"""Compile the S3 BLE passive-scan collector and Watch parsers on the host."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-ble-snapshot-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-ble-snapshot-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_snapshot_check.c"),
    str(root / "s3_main/main/s3_ble_snapshot.c"),
    str(root / "shared/cerberus_watch_capture.c"),
    str(root / "shared/sentinel_watch_protocol.c"),
    str(root / "s3_main/main/sentinel_watch_detect.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
