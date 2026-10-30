"""Exercise the portable 802.11 capture filter on the host."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-capture-filter-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "wifi-capture-filter-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_capture_filter_check.c"),
    str(root / "shared/wifi_capture_filter.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
