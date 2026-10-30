"""Compile the actual S3 passive probe bank reader with deterministic SPI peers."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-probe-transport-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/c5_spi_link.c").read_text(encoding="utf-8")
start = source.index("/* BEGIN PASSIVE WIFI PROBE READER:")
end = source.index("/* END PASSIVE WIFI PROBE READER. */", start)
(out / "wifi_probe_transport_impl.h").write_text(source[start:end], encoding="utf-8")
include = out / "include"
include.mkdir(exist_ok=True)
(include / "esp_err.h").write_text(
    "#pragma once\ntypedef int esp_err_t;\n"
    "enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,"
    "ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE,ESP_ERR_INVALID_SIZE,ESP_ERR_NOT_FINISHED};\n",
    encoding="utf-8")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("wifi-probe-transport-check.exe" if os.name == "nt" else "wifi-probe-transport-check")
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(include), "-I", str(out), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/wifi_probe_transport_check.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
