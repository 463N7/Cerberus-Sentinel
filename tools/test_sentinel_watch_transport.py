#!/usr/bin/env python3
"""Exercise bounded C5 collectors, actual Wi-Fi callback and S3 Watch getters."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
from test_sentinel_ble_advertisement import extract

ROOT = Path(__file__).resolve().parents[1]


def main():
    source = (ROOT / "tools/fixtures/sentinel_watch_transport.c").read_text()
    wifi = (ROOT / "c5_firmware/main/wifi_scanner.c").read_text()
    source = source.replace("/* ACTUAL_WIFI_CALLBACK */", extract(wifi, ["wifi_promiscuous_callback"]))
    with tempfile.TemporaryDirectory(prefix="sentinel-watch-transport-") as directory:
        path = Path(directory)
        (path / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_NOT_SUPPORTED 0x106\n#define ESP_ERR_TIMEOUT 0x107\n#define ESP_ERR_INVALID_RESPONSE 0x108\n")
        test = path / "test_watch_transport.c"
        test.write_text(source)
        exe = path / ("watch_transport.exe" if os.name == "nt" else "watch_transport")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path), "-I", str(ROOT / "shared"),
            "-I", str(ROOT / "s3_main/main"), str(test), str(ROOT / "shared/cerberus_watch_capture.c"),
            str(ROOT / "shared/sentinel_watch_protocol.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
