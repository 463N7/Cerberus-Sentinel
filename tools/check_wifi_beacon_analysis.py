"""Exercise passive beacon parsing and exact-byte MultiSSID evidence."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-beacon-analysis-checks"
out.mkdir(parents=True, exist_ok=True)
environment = os.environ.copy()
environment["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
exe = out / ("wifi-beacon-analysis-check.exe" if os.name == "nt" else "wifi-beacon-analysis-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_beacon_analysis_check.c"),
    str(root / "shared/wifi_beacon_analysis.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
