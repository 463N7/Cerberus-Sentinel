#!/usr/bin/env python3
"""Check actual Tools routes, hardware-action boundaries and QR navigation.

The production launcher/hit testing and selected ui.c functions are compiled
unchanged. Child-page rendering, keyboard events, QR encoding and storage I/O
are mocked at their boundaries; no board, radio or filesystem access occurs.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

from test_sentinel_live_ui import extract_function

ROOT = Path(__file__).resolve().parents[1]


def main():
    main_dir = ROOT / "s3_main/main"
    ui = (main_dir / "ui.c").read_text()
    enums = "\n".join(
        re.search(r"typedef enum \{[^}]*\} " + name + r";", ui, re.S).group()
        for name in ("ui_page_t", "ui_qr_mode_t", "ui_qr_wifi_security_t", "ui_self_test_mode_t")
    )
    signatures = (
        "static const char *tools_group_title(",
        "static esp_err_t draw_tools_menu(",
        "static void open_main_menu(",
        "static void return_to_launcher(",
        "static void open_tools_menu(",
        "static void handle_tools_tap(",
        "static bool qr_mode_has_code(",
        "static void reset_qr_save_status(",
        "static void secure_clear_buffer(",
        "static void clear_qr_wifi_credentials(",
        "static esp_err_t escape_wifi_qr_field(",
        "static esp_err_t build_wifi_qr_payload(",
        "static esp_err_t validate_http_url(",
        "static esp_err_t prepare_qr_payload(",
        "static void begin_custom_qr_input(",
        "static void begin_wifi_qr_ssid_input(",
        "static void begin_wifi_qr_password_input(",
        "static void finish_wifi_qr_input(",
        "static void handle_qr_tap(",
        "static void handle_self_test_tap(",
    )
    implementation = "\n".join(extract_function(ui, signature) for signature in signatures)
    fixture = (ROOT / "tools/tests/sentinel_tools_ui_test.c").read_text()
    source = fixture.replace("/* ACTUAL_ENUMS */", enums).replace("/* ACTUAL_FUNCTIONS */", implementation)
    with tempfile.TemporaryDirectory(prefix="sentinel-tools-ui-") as directory:
        path = Path(directory)
        (path / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_INVALID_SIZE 0x104\n#define ESP_ERR_NOT_FOUND 0x105\n"
        )
        (path / "sdmmc_cmd.h").write_text("#pragma once\ntypedef struct sdmmc_card sdmmc_card_t;\n")
        test_source = path / "tools_ui_test.c"
        test_source.write_text(source)
        executable = path / ("tools_ui.exe" if os.name == "nt" else "tools_ui")
        subprocess.run([
            sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
            "-I", str(path), "-I", str(main_dir), str(test_source),
            str(main_dir / "sentinel_tools.c"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
