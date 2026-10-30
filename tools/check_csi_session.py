"""Exercise the actual passive CSI session and model with deterministic transport."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/csi-session-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("csi-session-check.exe" if os.name == "nt" else "csi-session-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/csi_session_check.c"),
    str(root / "s3_main/main/csi_session.c"), str(root / "shared/csi_motion_model.c"),
    "-lm", "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
