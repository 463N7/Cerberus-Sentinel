"""Exercise actual USB SD lifecycle C code with injected peripheral failures."""
from pathlib import Path
import subprocess, sys
root = Path(__file__).resolve().parents[1]
out = root / "artifacts/usb-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "usb-checks.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root/"tools/host_checks/usb_stubs"), "-I", str(root/"s3_main/main"),
    str(root/"tools/host_checks/usb_checks.c"), str(root/"s3_main/main/usb_mass_storage.c"),
    "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
