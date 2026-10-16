#!/usr/bin/env python3
"""Exercise actual Watch UI, shared live adapter, and scheduler with transport stubs.

Reuses the legacy adapter regression fixture. Watch core is linked unchanged;
classification/rendering are mocked independently from their own test suites.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

from test_sentinel_live_ui import extract_function

ROOT = Path(__file__).resolve().parents[1]


def main():
    main_dir = ROOT / "s3_main/main"
    adapter = (main_dir / "sentinel_live_ui.h").read_text()
    ui = (main_dir / "ui.c").read_text()
    fixture = (ROOT / "tools/tests/sentinel_live_ui_test.c").read_text()
    declarations = adapter[adapter.index("typedef struct {"):adapter.index("static uint32_t live_last_poll")]
    implementation = extract_function(adapter, "static bool live_supported(") + "\n"
    implementation += adapter[adapter.index("static void live_status("):adapter.index("static esp_err_t live_execute(")]
    implementation += extract_function(adapter, "static bool live_connection_ready(")
    implementation += extract_function(adapter, "static esp_err_t live_connect_selected_wifi(")
    implementation += extract_function(adapter, "static void live_scan_tick(")
    code = fixture.replace("/* ACTUAL_DECLARATIONS */", declarations)
    code = code.replace("/* ACTUAL_DEFENSE_REFRESH */", extract_function(ui, "static esp_err_t refresh_wifi_defense_survey("))
    code = code.replace("/* ACTUAL_IMPLEMENTATION */", implementation)
    code = code.replace('#include "sentinel_scan_view.h"', '#include "sentinel_scan_view.h"\n'
        '#include "sentinel_watch.h"\n#include "sentinel_watch_view.h"\n#include "display.h"\n'
        'static sentinel_scan_tool_t watch_requested_tool(void);\nstatic uint32_t fixture_now;\n'
        'static bool defer_stop;\nstatic sentinel_scan_tool_t starts[128];\nstatic unsigned starts_count;\n')
    code = code.replace("UI_PAGE_WIFI = 1, UI_PAGE_IEEE802154 = 2,", "UI_PAGE_WIFI = 1, UI_PAGE_IEEE802154 = 2, UI_PAGE_WATCH = 3, UI_PAGE_HOME = 4,")
    code = code.replace("int64_t esp_timer_get_time(void) { return 123456000LL; }", "int64_t esp_timer_get_time(void) { return (int64_t)fixture_now * 1000; }")
    code = code.replace("static sentinel_scan_tool_t live_page_tool(void) { return fixture_selected_tool; }",
        "static sentinel_scan_tool_t live_page_tool(void) { return current_page == UI_PAGE_WATCH ? watch_requested_tool() : fixture_selected_tool; }")
    code = code.replace('snprintf(result->name, sizeof(result->name), "Sensor-%u", index);',
        'snprintf(result->name, sizeof(result->name), "Sensor-%u", index);\n'
        '    result->address[5] = index; result->address_type = 1; result->rssi = -40 - index;')
    code = code.replace("    if (command.kind == SENTINEL_SCAN_COMMAND_START) {\n        ++*generation;",
        "    if (command.kind == SENTINEL_SCAN_COMMAND_START) {\n"
        "        assert(hardware_wifi.state != 1 && hardware_ble.state != 1 && hardware_energy.state != 1 &&\n"
        "            hardware_beacon.state != 1 && hardware_activity.state != 1 && hardware_defense.state != 1);\n"
        "        assert(starts_count < 128); starts[starts_count++] = command.tool;\n        ++*generation;")
    code = code.replace("    } else {\n        *state = SENTINEL_SCAN_IDLE;", "    } else if (!defer_stop) {\n        *state = SENTINEL_SCAN_IDLE;")
    code = code.replace("static void reset(void)\n{", "static void reset(void)\n{\n    defer_stop = false; starts_count = 0; fixture_now = 123456;")
    code = code.replace("int main(void)", "static int legacy_adapter_tests(void)")
    code += (ROOT / "tools/tests/sentinel_watch_ui_test.c").read_text()
    with tempfile.TemporaryDirectory(prefix="sentinel-watch-ui-") as directory:
        path = Path(directory)
        (path / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_INVALID_SIZE 0x104\n#define ESP_ERR_NOT_FOUND 0x105\n"
            "#define ESP_ERR_NOT_SUPPORTED 0x106\n#define ESP_ERR_INVALID_RESPONSE 0x108\n")
        source = path / "watch_ui_test.c"
        source.write_text(code)
        executable = path / ("watch_ui.exe" if os.name == "nt" else "watch_ui")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path), "-I", str(main_dir),
            "-I", str(ROOT / "shared"), str(source), str(main_dir / "sentinel_scan_session.c"),
            str(main_dir / "sentinel_watch.c"), str(main_dir / "sentinel_watch_activity.c"),
            "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
