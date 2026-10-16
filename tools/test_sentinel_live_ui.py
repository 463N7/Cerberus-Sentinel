#!/usr/bin/env python3
"""Run focused radio-adapter regressions against extracted production C functions.

Compiles live status/capture/observe, connection guards, and the live scan tick
directly from sentinel_live_ui.h, plus the defense loader from ui.c. Radio I/O,
page selection, and rendering are mocked at their boundaries; scheduling is real.
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


def extract_function(source: str, signature: str) -> str:
    start = source.index(signature)
    end = source.find("\nstatic ", start + len(signature))
    if end == -1:
        end = len(source)
    return source[start:end]


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    main_dir = root / "s3_main/main"
    adapter = (main_dir / "sentinel_live_ui.h").read_text()
    ui = (main_dir / "ui.c").read_text()
    fixture = (root / "tools/tests/sentinel_live_ui_test.c").read_text()
    declarations = adapter[adapter.index("typedef struct {"):adapter.index("static uint32_t live_last_poll")]
    implementation = extract_function(adapter, "static bool live_supported(") + "\n"
    implementation += adapter[adapter.index("static void live_status("):adapter.index("static esp_err_t live_execute(")]
    if "static bool live_connection_ready(" in adapter:
        implementation += extract_function(adapter, "static bool live_connection_ready(")
    elif "static esp_err_t live_connection_ready(" in adapter:
        implementation += extract_function(adapter, "static esp_err_t live_connection_ready(")
    implementation += extract_function(adapter, "static esp_err_t live_connect_selected_wifi(")
    implementation += extract_function(adapter, "static void live_scan_tick(")
    source = fixture.replace("/* ACTUAL_DECLARATIONS */", declarations)
    source = source.replace("/* ACTUAL_DEFENSE_REFRESH */", extract_function(ui, "static esp_err_t refresh_wifi_defense_survey("))
    source = source.replace("/* ACTUAL_IMPLEMENTATION */", implementation)
    if os.environ.get("CC"):
        compiler = shlex.split(os.environ["CC"])
    elif importlib.util.find_spec("ziglang") is not None:
        compiler = [sys.executable, "-m", "ziglang", "cc"]
    else:
        compiler = next(([path] for name in ("cc", "clang", "gcc") if (path := shutil.which(name))), None)
        if compiler is None:
            raise SystemExit("Install a host C compiler or: python -m pip install ziglang==0.14.1")
    with tempfile.TemporaryDirectory(prefix="cerberus-live-adapter-") as temp:
        temp_dir = Path(temp)
        source_file = temp_dir / "live_test.c"
        source_file.write_text(source)
        (temp_dir / "esp_err.h").write_text("""#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_INVALID_RESPONSE 0x108
""")
        executable = temp_dir / ("test_live.exe" if os.name == "nt" else "test_live")
        subprocess.run(compiler + [
            "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-g",
            "-I", str(temp_dir), "-I", str(main_dir), "-I", str(root / "shared"),
            str(source_file), str(main_dir / "sentinel_scan_session.c"), "-o", str(executable),
        ], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
