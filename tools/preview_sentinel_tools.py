#!/usr/bin/env python3
"""Preview and check the actual Device Tools renderer with the device font.

Checks every touch pixel on all three menus, reserved header ownership, draw
bounds, invalid inputs and propagated display errors. Also extracts the actual
header, QR menu and hardware-check summary from ui.c for layout previews.
The battery and buttons use their production renderers; hardware data is fake.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

from preview_sentinel_dashboard import HOST_C, ROOT, ppm_to_png
from preview_sentinel_scan import TRACE_C


TEST_C = r'''
#include "sentinel_tools.h"
#include "sentinel_battery_view.h"
#include "storage.h"
#include "esp_check.h"

static const cerberus_ui_widget_theme_t theme = {0x0062, 0x08A4, 0x3F1B, 0xE77E, 0x84B5};
static uint32_t top_revision;
uint32_t cerberus_display_top_revision(void) { return top_revision; }

/* Only hardware state is stubbed; UI functions below come directly from ui.c. */
#define UI_BACKGROUND (theme.background)
#define UI_PANEL (theme.panel)
#define UI_TEXT (theme.text)
#define UI_MUTED (theme.muted)
#define UI_GOOD 0x3F1BU
#define UI_BAD 0xF471U
#define UI_WIDGET_THEME (&theme)
#define UI_HEADER_HEIGHT 48U
static const char *TAG = "tools_preview";
enum { UI_PAGE_TOOLS, UI_PAGE_BLE, UI_PAGE_IEEE802154 };
static unsigned current_page = UI_PAGE_TOOLS;
enum { UI_SELF_TEST_MODE_SUMMARY, UI_SELF_TEST_MODE_TOUCH };
static unsigned self_test_mode = UI_SELF_TEST_MODE_SUMMARY;
static struct { bool touch_available; } ui_config = {true};
static bool storage_benchmark_running;
static bool storage_benchmark_checked;
static bool storage_safely_removed;
static esp_err_t storage_benchmark_error = ESP_OK;
static cerberus_storage_benchmark_result_t storage_benchmark_result;
static bool storage_mounted = true;
static cerberus_storage_owner_t storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
bool cerberus_storage_is_mounted(void) { return storage_mounted; }
cerberus_storage_owner_t cerberus_storage_get_owner(void) { return storage_owner; }
static const char *esp_err_to_name(esp_err_t error) { return error == ESP_OK ? "ESP_OK" : "ESP_ERR_INVALID_STATE"; }
static esp_err_t draw_touch_test(void) { assert(!"touch test is outside this summary preview"); return ESP_FAIL; }

UI_FUNCTIONS

static void chrome(const char *title)
{
    trace_enabled = false;
    assert(cerberus_display_fill_rect(0, 0, 320, 480, theme.background) == ESP_OK);
    assert(draw_header(title) == ESP_OK);
    ++top_revision;
    assert(sentinel_battery_view_update(true, 93, theme.background) == ESP_OK);
}

static void preview_detail(const char *title, const char *back,
    esp_err_t (*draw)(void), const char *file)
{
    chrome(title);
    memcpy(expected, framebuffer, sizeof(expected));
    trace_reset();
    assert(draw() == ESP_OK);
    trace_region(0, 48, 320, 372);
    assert(memcmp(expected, framebuffer, 48 * 320 * sizeof(uint16_t)) == 0);
    assert(memcmp(expected + 420 * 320, framebuffer + 420 * 320, 60 * 320 * sizeof(uint16_t)) == 0);
    assert(draw_button(64, 420, 192, 44, back) == ESP_OK);
    save(file);
    fail_display = 1;
    assert(draw() == ESP_FAIL);
    fail_display = 0;
}

static void preview_detail_pages(void)
{
    preview_detail("QR MAKER", "EXTRAS", draw_qr_menu, "tools-qr.ppm");
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-unrun.ppm");
    storage_benchmark_checked = true;
    storage_benchmark_result = (cerberus_storage_benchmark_result_t){
        .bytes_written = CERBERUS_STORAGE_BENCHMARK_BYTES,
        .bytes_read = CERBERUS_STORAGE_BENCHMARK_BYTES,
        .write_time_us = 100000, .read_time_us = 80000, .data_verified = true,
    };
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-passed.ppm");
    storage_benchmark_checked = false;
    storage_owner = CERBERUS_STORAGE_OWNER_USB;
    storage_mounted = false;
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-usb.ppm");
    storage_owner = CERBERUS_STORAGE_OWNER_NONE;
    storage_safely_removed = true;
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-removed.ppm");
    storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
    storage_mounted = true;
    storage_safely_removed = false;
    storage_benchmark_running = true;
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-running.ppm");
    storage_benchmark_running = false;
    storage_benchmark_checked = true;
    storage_benchmark_error = ESP_FAIL;
    ui_config.touch_available = false;
    preview_detail("HARDWARE CHECKS", "DEVICE CARE", draw_self_test, "tools-checks-failed.ppm");
    puts("QR and hardware checks: actual ui.c rendering, six storage states, header/footer ownership and display bounds passed.");
}

static void test_hits(sentinel_tools_group_t group)
{
    static const sentinel_tools_action_t actions[][6] = {
        {SENTINEL_TOOLS_SETTINGS, SENTINEL_TOOLS_FILES, SENTINEL_TOOLS_LOGS,
         SENTINEL_TOOLS_WEB, SENTINEL_TOOLS_OPEN_CARE, SENTINEL_TOOLS_OPEN_EXTRAS},
        {SENTINEL_TOOLS_STATUS, SENTINEL_TOOLS_CHECKS, SENTINEL_TOOLS_USB},
        {SENTINEL_TOOLS_QR, SENTINEL_TOOLS_SCRIPTS},
    };
    static const unsigned counts[] = {6, 3, 2};
    for (unsigned y = 0; y < 480; ++y) {
        for (unsigned x = 0; x < 320; ++x) {
            sentinel_tools_action_t action = SENTINEL_TOOLS_NONE;
            for (unsigned i = 0; i < counts[group]; ++i) {
                unsigned top = 70 + i * 54;
                if (x >= 12 && x < 308 && y >= top && y < top + 48) action = actions[group][i];
            }
            if (x >= 12 && x < 308 && y >= 420 && y < 464) action = SENTINEL_TOOLS_BACK;
            assert(sentinel_tools_hit_test(group, (uint16_t)x, (uint16_t)y) == action);
        }
    }
    assert(sentinel_tools_hit_test(group, 320, 420) == SENTINEL_TOOLS_NONE);
    assert(sentinel_tools_hit_test(group, 12, 480) == SENTINEL_TOOLS_NONE);
    assert(sentinel_tools_hit_test(group, UINT16_MAX, UINT16_MAX) == SENTINEL_TOOLS_NONE);
}

int main(void)
{
    const char *titles[] = {"DEVICE TOOLS", "DEVICE CARE", "EXTRAS"};
    const char *files[] = {"tools-main.ppm", "tools-care.ppm", "tools-extras.ppm"};
    for (unsigned group = 0; group < 3; ++group) {
        chrome(titles[group]);
        memcpy(expected, framebuffer, sizeof(expected));
        trace_reset();
        assert(sentinel_tools_draw((sentinel_tools_group_t)group, &theme) == ESP_OK);
        trace_region(12, 48, 296, 416);
        assert(memcmp(expected, framebuffer, 48 * 320 * sizeof(uint16_t)) == 0);
        assert(memcmp(expected + 464 * 320, framebuffer + 464 * 320, 16 * 320 * sizeof(uint16_t)) == 0);
        save(files[group]);
        test_hits((sentinel_tools_group_t)group);
        memcpy(expected, framebuffer, sizeof(expected));
        assert(sentinel_tools_draw((sentinel_tools_group_t)group, &theme) == ESP_OK);
        assert(memcmp(expected, framebuffer, sizeof(expected)) == 0);
        fail_display = 1;
        assert(sentinel_tools_draw((sentinel_tools_group_t)group, &theme) == ESP_FAIL);
        fail_display = 0;
        fail_after = 4;
        assert(sentinel_tools_draw((sentinel_tools_group_t)group, &theme) == ESP_FAIL);
        fail_after = -1;
        assert(sentinel_tools_draw((sentinel_tools_group_t)group, &theme) == ESP_OK);
        assert(memcmp(expected, framebuffer, sizeof(expected)) == 0);
    }
    trace_reset();
    assert(sentinel_tools_draw(SENTINEL_TOOLS_MAIN, NULL) == ESP_ERR_INVALID_ARG);
    assert(sentinel_tools_draw((sentinel_tools_group_t)-1, &theme) == ESP_ERR_INVALID_ARG);
    assert(sentinel_tools_draw((sentinel_tools_group_t)3, &theme) == ESP_ERR_INVALID_ARG);
    assert(sentinel_tools_hit_test((sentinel_tools_group_t)-1, 12, 70) == SENTINEL_TOOLS_NONE);
    assert(sentinel_tools_hit_test((sentinel_tools_group_t)3, 12, 420) == SENTINEL_TOOLS_NONE);
    assert(draw_count == 0);
    puts("Device Tools: 3 menus, all 460,800 touch pixels, header ownership, bounds, invalid inputs and display failures passed.");
    preview_detail_pages();
    return 0;
}
'''


def gallery(output: Path, names: tuple[str, ...], filename: str, columns: int = 3) -> None:
    images = []
    for name in names:
        with (output / f"tools-{name}.ppm").open("rb") as source:
            assert source.readline() == b"P6\n"
            assert source.readline() == b"320 480\n"
            assert source.readline() == b"255\n"
            images.append(source.read())
    pixels = bytearray()
    rows = (len(images) + columns - 1) // columns
    for row in range(rows):
        for y in range(480):
            for column in range(columns):
                index = row * columns + column
                pixels += images[index][y * 960:(y + 1) * 960] if index < len(images) else bytes(960)
    path = output / filename
    path.write_bytes(f"P6\n{columns * 320} {rows * 480}\n255\n".encode() + pixels)
    ppm_to_png(path)


def extract_function(source: str, signature: str) -> str:
    """These file-scope C functions end at an unindented closing brace."""
    start = source.index(signature + "\n{")
    end = source.index("\n}\n", start) + 3
    return source[start:end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/sentinel-tools")
    parser.add_argument("--cc", help="C compiler command")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_SIZE 0x104\n", encoding="utf-8")
    (output / "esp_check.h").write_text(
        '#pragma once\n#include "esp_err.h"\n'
        "#define ESP_RETURN_ON_ERROR(call, tag, ...) do { (void)(tag); "
        "esp_err_t result_ = (call); if (result_ != ESP_OK) return result_; } while (0)\n",
        encoding="utf-8")
    (output / "sdmmc_cmd.h").write_text(
        "#pragma once\ntypedef struct sdmmc_card sdmmc_card_t;\n", encoding="utf-8")
    display = (ROOT / "s3_main/main/display.c").read_text(encoding="utf-8")
    start = display.index("static const uint8_t *font5x7")
    end = display.index("esp_err_t cerberus_display_draw_text", start)
    host = HOST_C[:HOST_C.index("int main(void)")].replace("FONT_FUNCTION", display[start:end])
    host = host.replace("static int fail_display;", "static int fail_display;\n" + TRACE_C)
    host = host.replace("    assert(w > 0 && h > 0", """    if (trace_enabled) {
        assert(draw_count < sizeof(draw_operations) / sizeof(draw_operations[0]));
        draw_operations[draw_count++] = (draw_operation_t){x,y,w,h};
    }
    assert(w > 0 && h > 0""")
    host = host.replace("if (fail_display) return ESP_FAIL;", "if (display_failure()) return ESP_FAIL;")
    ui_source = (ROOT / "s3_main/main/ui.c").read_text(encoding="utf-8")
    functions = "\n".join(extract_function(ui_source, signature) for signature in (
        "static uint16_t text_width(const char *text, uint8_t scale)",
        "static esp_err_t draw_centered(uint16_t y, const char *text, uint16_t color, uint8_t scale)",
        "static esp_err_t draw_header(const char *title)",
        "static esp_err_t draw_button(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *label)",
        "static esp_err_t draw_qr_menu(void)",
        "static esp_err_t draw_self_test(void)",
    ))
    source = output / "tools_host.c"
    source.write_text(host + TEST_C.replace("UI_FUNCTIONS", functions), encoding="utf-8")
    executable = output / ("tools_host.exe" if os.name == "nt" else "tools_host")
    compiler = shlex.split(args.cc) if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    subprocess.run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(output), "-I", str(ROOT / "s3_main/main"), str(source),
        *[str(ROOT / "s3_main/main" / name) for name in
            ("sentinel_tools.c", "ui_widgets.c", "sentinel_battery_view.c")],
        "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=output, check=True)
    names = ("main", "care", "extras", "qr", "checks-unrun", "checks-passed",
             "checks-usb", "checks-removed", "checks-running", "checks-failed")
    for name in names:
        ppm_to_png(output / f"tools-{name}.ppm")
    gallery(output, ("main", "care", "extras"), "tools-gallery.ppm")
    gallery(output, ("qr", "checks-unrun", "checks-passed", "checks-usb"),
            "tools-details-gallery.ppm", columns=4)
    gallery(output, ("checks-unrun", "checks-passed", "checks-usb", "checks-removed",
            "checks-running", "checks-failed"), "tools-checks-gallery.ppm")
    print(f"Device Tools previews: {output}")


if __name__ == "__main__":
    main()
