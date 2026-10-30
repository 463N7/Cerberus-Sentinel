"""Check production Flock AD evidence and typed-address compatibility clues."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ble-flock-hints-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("ble-flock-hints-check.exe" if os.name == "nt" else "ble-flock-hints-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"), str(root / "tools/host_checks/ble_flock_hints_check.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
