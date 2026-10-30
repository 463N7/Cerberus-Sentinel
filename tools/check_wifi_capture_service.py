"""Host exercise for the C5 capture ring service with minimal ESP stubs."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-capture-service-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "wifi-capture-service-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "c5_firmware/main"),
    "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_capture_service_check.c"),
    str(root / "c5_firmware/main/wifi_capture_service.c"),
    str(root / "shared/wifi_capture_ring.c"),
    str(root / "shared/wifi_capture_filter.c"),
    "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
