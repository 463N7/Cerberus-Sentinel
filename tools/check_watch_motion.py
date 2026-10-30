"""Exercise actual bounded Remote ID motion decoder/collector and canonical codec."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/watch-motion-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("watch-motion-check.exe" if os.name == "nt" else "watch-motion-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), str(root / "tools/host_checks/watch_motion_check.c"),
    str(root / "shared/sentinel_watch_protocol.c"), str(root / "shared/cerberus_watch_capture.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
