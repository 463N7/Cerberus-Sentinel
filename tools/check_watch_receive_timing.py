"""Check cross-clock elapsed-age bounds using the production pure helper."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/watch-receive-timing-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("watch-receive-timing-check.exe" if os.name == "nt" else "watch-receive-timing-check")
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/watch_receive_timing_check.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
