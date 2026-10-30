"""Compile and exercise the actual bounded receive-only channel planner."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-channel-plan-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-channel-plan-check.exe" if os.name == "nt" else "remote-id-channel-plan-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), str(root / "tools/host_checks/remote_id_channel_plan_check.c"),
    str(root / "shared/remote_id_channel_plan.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
