"""Exercise existing passive Remote ID parsers, collectors and Watch getters."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-receive-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-receive-check.exe" if os.name == "nt" else "remote-id-receive-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/remote_id_receive_check.c"),
    str(root / "shared/sentinel_watch_protocol.c"),
    str(root / "shared/cerberus_watch_capture.c"),
    str(root / "s3_main/main/s3_ble_snapshot.c"),
    str(root / "s3_main/main/sentinel_watch_detect.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
