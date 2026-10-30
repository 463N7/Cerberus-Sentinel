"""Exercise the pure dual-session schedule used by concurrent Recon."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/recon-parallel-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "recon-parallel-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/recon_parallel_check.c"),
    str(root / "s3_main/main/sentinel_scan_session.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
