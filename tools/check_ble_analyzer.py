"""Host-check bounded S3 BLE Analyzer rate, evidence and freshness."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ble-analyzer-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "ble-analyzer-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/ble_analyzer_check.c"),
    str(root / "s3_main/main/ble_analyzer.c"),
    str(root / "s3_main/main/sentinel_watch_detect.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
print("BLE Analyzer event-rate, evidence, malformed AD and freshness: pass")
