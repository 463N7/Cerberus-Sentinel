"""Exercise production S3 Wi-Fi observation decoding and SPI result publication."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-wifi-observation-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/c5_spi_link.c").read_text(encoding="utf-8")
start = source.index("esp_err_t c5_spi_link_get_wifi_scan_observed_result(")
end = source.index("esp_err_t c5_spi_link_get_wifi_scan_metadata(", start)
(out / "s3_wifi_observation_link_impl.h").write_text(source[start:end], encoding="utf-8")
exe = out / "s3-wifi-observation-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(out),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/s3_wifi_observation_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
