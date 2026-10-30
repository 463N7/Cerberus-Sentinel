"""Compile and run the bounded, header-only iBeacon AD decoder checks."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ibeacon-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("ibeacon-check.exe" if os.name == "nt" else "ibeacon-check")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "shared"),
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/ibeacon_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
