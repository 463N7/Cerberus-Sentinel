"""Exercise the production bounded MAC report collector without a radio."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/mac-observation-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "mac-observation-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), str(root / "tools/host_checks/mac_observation_check.c"),
    str(root / "shared/mac_observation.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
