"""Exercise the standalone, allocation-free 802.11 PCAPNG codec on the host."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-pcap-checks"
out.mkdir(parents=True, exist_ok=True)
exe = out / "wifi-pcap-checks.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_pcap_check.c"),
    str(root / "shared/wifi_pcap.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
