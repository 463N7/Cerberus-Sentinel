"""Host-check the shared strict parser used by BLE scans and Analyzer."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ble-ad-hints-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / ("ble-ad-hints-check.exe" if os.name == "nt" else "ble-ad-hints-check")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/ble_ad_hints_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
