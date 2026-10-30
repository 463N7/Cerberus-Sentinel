"""Exercise the actual S3 Watch getter with controlled SPI/clock dependencies."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/watch-receive-transport-checks"
out.mkdir(parents=True, exist_ok=True)
include = out / "include"
include.mkdir(exist_ok=True)
(include / "esp_err.h").write_text(
    "#pragma once\ntypedef int esp_err_t;\n"
    "enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,"
    "ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE};\n", encoding="utf-8")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("watch-receive-transport-check.exe" if os.name == "nt" else "watch-receive-transport-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(include), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/watch_receive_transport_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
