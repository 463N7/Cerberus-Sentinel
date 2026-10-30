"""Compile and exercise the shared network scan protocol/planner without RF I/O."""
from pathlib import Path
import subprocess
import sys
root = Path(__file__).resolve().parents[1]
out = root / "artifacts/network-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "network-checks.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root/"shared"), str(root/"tools/host_checks/network_checks.c"),
    str(root/"shared/network_scan.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
