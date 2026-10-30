"""Exercise production C5 capture radio lifecycle with deterministic ESP edges."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-capture-radio-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "wifi-capture-radio-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "c5_firmware/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_capture_radio_check.c"),
    str(root / "shared/wifi_capture_ring.c"),
    str(root / "shared/wifi_capture_filter.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
