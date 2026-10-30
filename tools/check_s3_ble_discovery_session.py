"""Compile and exercise the production continuous BLE discovery controller."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-ble-discovery-session-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "check.exe"
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-UNDEBUG", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_discovery_session_check.c"),
    str(root / "s3_main/main/s3_ble_discovery_session.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True)
