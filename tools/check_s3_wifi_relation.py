"""Compile the pure selected-AP passive relationship evidence model."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-wifi-relation-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("s3-wifi-relation-check.exe" if os.name == "nt" else "s3-wifi-relation-check")
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_wifi_relation_check.c"), str(root / "s3_main/main/s3_wifi_relation.c"),
    "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
