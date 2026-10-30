"""Compile and run hardware-independent field kit tests with documented Zig."""
import subprocess
import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
out = ROOT / "artifacts/field-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "field-checks.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(ROOT / "s3_main/main"), "-I", str(ROOT / "shared"),
    str(ROOT / "tools/host_checks/field_checks.c"), str(ROOT / "s3_main/main/expansion_protocol.c"),
    str(ROOT / "s3_main/main/sentinel_watch.c"),
    str(ROOT / "shared/recon_db.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe), str(ROOT / "sdcard/CERBERUS/RECON.DB")], check=True)
