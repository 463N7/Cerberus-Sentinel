"""Compile and exercise the original bounded CSI model; no radio/device work."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/csi-motion-model-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "csi-motion-model-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), str(root / "tools/host_checks/csi_motion_model_check.c"),
    str(root / "shared/csi_motion_model.c"), "-lm", "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
