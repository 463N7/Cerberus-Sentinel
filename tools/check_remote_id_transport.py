"""Exercise the actual S3 priority-job transport with bounded SPI/peer mocks."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/remote-id-transport-checks"
out.mkdir(parents=True, exist_ok=True)
include = out / "include"
include.mkdir(exist_ok=True)
(include / "esp_err.h").write_text(
    "#pragma once\ntypedef int esp_err_t;\n"
    "enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,"
    "ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE};\n", encoding="utf-8")

env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("remote-id-transport-check.exe" if os.name == "nt" else "remote-id-transport-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(include), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/remote_id_transport_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
