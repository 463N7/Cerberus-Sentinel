"""Exercise the real monitor service and shared scanner dispatch on the host."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/packet-monitor-worker-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "c5_firmware/main/wifi_scanner.c").read_text()
assert "packet_monitor_task_handle" not in source
assert '"wifi_packets"' not in source
start = source.index("static void wifi_scan_task(void *arg)")
end = source.index("esp_err_t cerberus_wifi_watch_get_status(", start)
(out / "wifi_scan_worker_impl.h").write_text(source[start:end])
exe = out / "wifi-packet-monitor-worker-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "c5_firmware/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_packet_monitor_worker_check.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
