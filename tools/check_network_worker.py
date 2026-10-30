"""Exercise the actual C5 TCP/ICMP/ARP worker using deterministic platform fakes."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/network-worker-checks"
out.mkdir(parents=True, exist_ok=True)
for name in ["esp_timer.h", "esp_netif.h", "esp_random.h", "freertos/FreeRTOS.h",
             "freertos/semphr.h", "freertos/task.h", "lwip/sockets.h", "lwip/etharp.h",
             "lwip/tcpip.h", "fcntl.h"]:
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "network_worker_platform.h"\n')
exe = out / "network-worker-check.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks"), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "c5_firmware/main"),
    str(root / "tools/host_checks/network_worker_check.c"), str(root / "shared/network_scan.c"),
    "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
