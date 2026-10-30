"""Host-check the selected-AP Clone Spam beacon and S3 receiver contract."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ap-clone-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "ap-clone.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/ap_clone_check.c"),
    str(root / "shared/wifi_ap_clone.c"),
    str(root / "shared/wifi_ap_job.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
