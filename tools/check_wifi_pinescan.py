"""Exercise strict PineScan beacon predicates and bounded evidence retention."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-pinescan-checks"
out.mkdir(parents=True, exist_ok=True)
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
exe = out / ("wifi-pinescan-check.exe" if os.name == "nt" else "wifi-pinescan-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_pinescan_check.c"),
    str(root / "shared/wifi_pinescan.c"), str(root / "shared/wifi_beacon_analysis.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
