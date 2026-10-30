"""Exercise actual dual-radio Remote ID orchestration and publication on host."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-session-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-session-check.exe" if os.name == "nt" else "remote-id-session-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/remote_id_session_check.c"),
    str(root / "s3_main/main/remote_id_session.c"),
    str(root / "s3_main/main/remote_id_inventory.c"),
    str(root / "s3_main/main/sentinel_scan_session.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
