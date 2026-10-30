"""Exercise production C5 SPI framing and latched READY waits without hardware."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/c5-ready-handshake-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/c5_spi_link.c").read_text()
start = source.index("#define C5_SPI_HOST")
end = source.index("esp_err_t c5_spi_link_get_event_queue_status(")
legacy = source.index("static esp_err_t get_status_v1_locked(")
scan_end = source.index("esp_err_t c5_spi_link_get_wifi_radio_stats(", legacy)
(out / "c5_spi_transport_impl.h").write_text(source[start:end] + source[legacy:scan_end])
assert source.count("xSemaphoreTake(link_mutex,") == 1
assert source.count("xSemaphoreGive(link_mutex)") == 1
exe = out / "c5-ready-handshake-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/c5_ready_handshake_check.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
