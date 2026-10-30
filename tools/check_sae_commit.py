"""Check the new bounded selected-AP SAE commit serializer without RF."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/sae-commit-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "sae-commit-checks.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/sae_commit_check.c"),
    str(root / "shared/wifi_sae_commit.c"),
    str(root / "shared/wifi_ap_job.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
