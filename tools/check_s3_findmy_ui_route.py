"""Check the selected/Beep All raw-advertisement action gate."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-findmy-ui-route-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-findmy-ui-route-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "s3_main/main"),
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/s3_findmy_ui_route_check.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
