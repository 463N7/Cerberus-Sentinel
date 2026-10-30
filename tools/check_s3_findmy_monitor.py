"""Compile the pure Find My monitor observation cache on the host."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-findmy-monitor-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-findmy-monitor-check.exe"
environment = os.environ.copy()
environment.setdefault("ZIG_GLOBAL_CACHE_DIR", str(out / "zig-cache"))
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"), "-I", str(root / "tools/host_checks/stubs"),
    str(root / "tools/host_checks/s3_findmy_monitor_check.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
