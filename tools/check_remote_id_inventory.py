"""Compile and exercise the pure production Remote ID inventory backend."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-inventory-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "remote-id-inventory-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"), str(root / "tools/host_checks/remote_id_inventory_check.c"),
    str(root / "s3_main/main/remote_id_inventory.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
