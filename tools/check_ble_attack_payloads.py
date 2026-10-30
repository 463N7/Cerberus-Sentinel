"""Compile and exercise pure S3 Bluetooth attack payload plans without RF."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
output = root / "artifacts/ble-attack-payload-checks"
output.mkdir(parents=True, exist_ok=True)
binary = output / "ble-attack-payload-checks.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "shared"),
    str(root / "tools/host_checks/ble_attack_payload_checks.c"),
    str(root / "shared/ble_attack_payload.c"), "-o", str(binary),
], check=True)
subprocess.run([str(binary)], check=True)
