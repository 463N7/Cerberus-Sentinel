#!/usr/bin/env python3
"""Exercise the actual S3 password-transfer and cleanup function on the host.

The production function is extracted verbatim from c5_spi_link.c. SPI exchanges
and the mutex are simulated; no ESP-IDF installation or device access is needed.
"""
from __future__ import annotations

import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    main_dir = root / "s3_main/main"
    link = (main_dir / "c5_spi_link.c").read_text()
    start = link.index("esp_err_t c5_spi_link_connect_wifi(")
    end = link.index("\nstatic esp_err_t wifi_connection_status_command(", start)
    function = link[start:end]
    source = (root / "tools/tests/sentinel_wifi_connect_test.c").read_text().replace("/* ACTUAL_CONNECT_FUNCTION */", function)
    if os.environ.get("CC"):
        compiler = shlex.split(os.environ["CC"])
    elif importlib.util.find_spec("ziglang") is not None:
        compiler = [sys.executable, "-m", "ziglang", "cc"]
    else:
        compiler = next(([path] for name in ("cc", "clang", "gcc") if (path := shutil.which(name))), None)
        if compiler is None:
            raise SystemExit("Install a host C compiler or: python -m pip install ziglang==0.14.1")
    with tempfile.TemporaryDirectory(prefix="cerberus-wifi-connect-") as temp:
        temp_dir = Path(temp)
        source_file = temp_dir / "wifi_connect_test.c"
        source_file.write_text(source)
        (temp_dir / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_TIMEOUT 0x107
""")
        executable = temp_dir / ("test_wifi_connect.exe" if os.name == "nt" else "test_wifi_connect")
        subprocess.run(compiler + [
            "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-I", str(temp_dir), "-I", str(main_dir), "-I", str(root / "shared"),
            str(source_file), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
