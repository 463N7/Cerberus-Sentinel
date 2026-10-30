"""Compile and run the pure Find My classifier and Beep All scheduler checks."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-findmy-core-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "s3-findmy-core-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_findmy_core_check.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
