"""Compile production S3 capture SPI helpers against deterministic responses."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-capture-link-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/c5_spi_link.c").read_text()
start = source.index("static bool wifi_capture_status_channel_valid(")
end = source.index("static esp_err_t wifi_defense_status_command(", start)
(out / "wifi_capture_link_impl.h").write_text(source[start:end])
exe = out / "wifi-capture-link-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_capture_link_check.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
