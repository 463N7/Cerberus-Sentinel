#!/usr/bin/env python3
"""Compile the real scan renderer and save labeled example-data previews.

Reuses the exact RGB565 and display.c font harness from the dashboard preview.
Verifies header/footer ownership, all states, long names, invalid input, control
and row hit areas, and display error propagation. Requires `ziglang` or --cc.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

from preview_sentinel_dashboard import HOST_C, ROOT, ppm_to_png


TRACE_C = r'''
typedef struct { unsigned x, y, width, height; } draw_operation_t;
static draw_operation_t draw_operations[4096];
static unsigned draw_count;
static bool trace_enabled;
static int fail_after = -1;
static bool display_failure(void)
{
    if (fail_display || fail_after == 0) return true;
    if (fail_after > 0) --fail_after;
    return false;
}
static void trace_reset(void) { draw_count = 0; trace_enabled = true; }
static void trace_region(unsigned x, unsigned y, unsigned width, unsigned height)
{
    assert(draw_count > 0);
    for (unsigned i = 0; i < draw_count; ++i) {
        const draw_operation_t *op = &draw_operations[i];
        assert(op->x >= x && op->y >= y && op->x + op->width <= x + width && op->y + op->height <= y + height);
    }
}
'''


SCAN_C = r'''
#include "sentinel_scan_view.h"

esp_err_t cerberus_display_draw_mono_bitmap(uint16_t x, uint16_t y, uint16_t w,
    uint16_t h, const uint8_t *bitmap, uint16_t color, uint16_t background)
{
    if (fail_display) return ESP_FAIL;
    bounds(x, y, w, h);
    unsigned stride = (w + 7U) / 8U;
    for (unsigned row = 0; row < h; ++row) {
        for (unsigned col = 0; col < w; ++col) {
            bool on = bitmap[row * stride + col / 8U] & (0x80U >> (col % 8U));
            framebuffer[(y + row) * 320 + x + col] = on ? color : background;
        }
    }
    return ESP_OK;
}

static void chrome(const char *title)
{
    assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x0062) == ESP_OK);
    assert(cerberus_display_draw_text(12, 14, title, 0xE77E, 0x0062, 2) == ESP_OK);
    assert(cerberus_display_draw_text(12, 34, "SENTINEL / EXAMPLE DATA PREVIEW", 0x8495, 0x0062, 1) == ESP_OK);
    assert(cerberus_display_fill_rect(12, 420, 296, 44, 0x08A4) == ESP_OK);
    assert(cerberus_display_draw_text(118, 434, "< HOME", 0xE77E, 0x08A4, 2) == ESP_OK);
}

static void check_pixels(const sentinel_scan_view_t *view, uint32_t frame)
{
    memcpy(expected, framebuffer, sizeof(expected));
    trace_enabled = false;
    assert(sentinel_scan_view_draw(view, frame) == ESP_OK);
    for (unsigned i = 0; i < 320U * 480U; ++i) {
        if (expected[i] != framebuffer[i]) {
            fprintf(stderr, "Incremental mismatch at %u,%u: %04x != %04x\n", i % 320, i / 320, expected[i], framebuffer[i]);
            abort();
        }
    }
}

static void check_transition(const sentinel_scan_view_t *view, uint32_t frame)
{
    trace_reset();
    assert(sentinel_scan_view_update(view, frame) == ESP_OK);
    if (draw_count != 0) trace_region(0, 48, 320, 363);
    check_pixels(view, frame);
}

static void partial_update_tests(sentinel_scan_view_t view)
{
    view.state = SENTINEL_VIEW_COMPLETE;
    view.progress_known = false;
    assert(sentinel_scan_view_draw(&view, 0) == ESP_OK);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 1) == ESP_OK);
    assert(draw_count == 0);

    /* Mutate the SAME pointer the UI passes each tick: an address-only cache
     * would miss this change. An age update must be one small text transfer. */
    char age[80] = "LATEST SWEEP 5S AGO";
    view.hint = age;
    check_transition(&view, 1);
    strcpy(age, "LATEST SWEEP 6S AGO");
    trace_reset();
    assert(sentinel_scan_view_update(&view, 1) == ESP_OK);
    assert(draw_count == 1);
    trace_region(9, 352, 300, 7);
    check_pixels(&view, 1);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 14) == ESP_OK);
    assert(draw_count == 0);
    view.hint = NULL;
    check_transition(&view, 14);

    view.state = SENTINEL_VIEW_LIVE;
    check_transition(&view, 0);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 1) == ESP_OK);
    trace_region(8, 103, 304, 3);
    check_pixels(&view, 1);
    view.progress_known = true;
    view.progress_percent = 37;
    trace_reset();
    assert(sentinel_scan_view_update(&view, 2) == ESP_OK);
    trace_region(8, 103, 304, 3);
    check_pixels(&view, 2);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 12) == ESP_OK);
    assert(draw_count == 0);

    char name[80] = "A NETWORK NAME THAT IS MUCH LONGER THAN ITS FIELD";
    char detail[80] = "DETAILS DETAILS DETAILS DETAILS DETAILS DETAILS DETAILS";
    char badge[80] = "LONG BADGE LONG BADGE LONG BADGE LONG BADGE";
    view.rows[0].name = name; view.rows[0].detail = detail; view.rows[0].badge = badge;
    check_transition(&view, 12);
    strcpy(name, "SHORT"); strcpy(detail, "CH 1"); strcpy(badge, "SAVED");
    trace_reset();
    assert(sentinel_scan_view_update(&view, 12) == ESP_OK);
    trace_region(8, 126, 304, 52);
    check_pixels(&view, 12);
    view.rows[0].signal_valid = false;
    view.rows[0].marked = false;
    view.rows[0].review = true;
    check_transition(&view, 12);

    /* State changes must retain the previous records without clearing them. */
    for (unsigned state = 0; state <= SENTINEL_VIEW_OFFLINE; ++state) {
        view.state = (sentinel_view_state_t)state;
        trace_reset();
        assert(sentinel_scan_view_update(&view, 12) == ESP_OK);
        for (unsigned i = 0; i < draw_count; ++i) {
            const draw_operation_t *op = &draw_operations[i];
            assert(op->y + op->height <= 126 || op->y >= 346);
        }
        check_pixels(&view, 12);
    }
    view.state = SENTINEL_VIEW_COMPLETE;
    view.row_count = 1;
    check_transition(&view, 12);
    view.row_count = 0;
    check_transition(&view, 12);
    strcpy(age, "LATEST SWEEP 7S AGO"); view.hint = age;
    trace_reset();
    assert(sentinel_scan_view_update(&view, 12) == ESP_OK);
    assert(draw_count == 1);
    trace_region(9, 352, 300, 7); /* No empty panel repaint from age changes. */
    check_pixels(&view, 12);
    for (unsigned state = 0; state <= SENTINEL_VIEW_OFFLINE; ++state) {
        view.state = (sentinel_view_state_t)state;
        view.message = state == SENTINEL_VIEW_ERROR ? "A LONG ERROR MESSAGE TO CHECK CLIPPING" : NULL;
        check_transition(&view, 12);
    }
    view.message = NULL;
    view.state = SENTINEL_VIEW_IDLE;
    check_transition(&view, 12);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 13) == ESP_OK);
    assert(draw_count == 0);
    view.row_count = 4;
    check_transition(&view, 13);
    view.kind = SENTINEL_VIEW_ENERGY;
    view.energy_valid_mask = 0xA55A;
    for (unsigned i = 0; i < 16; ++i) view.energy_dbm[i] = (int8_t)(-97 + 3 * i);
    check_transition(&view, 13);
    view.energy_dbm[0] = -25; /* Unsampled channel: no visible change. */
    trace_reset();
    assert(sentinel_scan_view_update(&view, 13) == ESP_OK);
    assert(draw_count == 0);
    view.energy_dbm[1] = -25;
    check_transition(&view, 13);
    view.energy_valid_mask = 0;
    check_transition(&view, 13);
    view.energy_valid_mask = 0xFFFF;
    check_transition(&view, 13);
    view.kind = SENTINEL_VIEW_RADIO_LIST;
    check_transition(&view, 13);
    view.page = 1; view.page_count = 2; view.has_previous = true; view.has_next = false;
    check_transition(&view, 13);
    view.page = 0; view.page_count = 1;
    check_transition(&view, 13);

    /* Fail after several status writes, then require full repair on retry. */
    view.state = SENTINEL_VIEW_ERROR;
    fail_after = 3;
    assert(sentinel_scan_view_update(&view, 13) == ESP_FAIL);
    fail_after = -1;
    trace_reset();
    assert(sentinel_scan_view_update(&view, 13) == ESP_OK);
    assert(draw_count > 0 && draw_operations[0].x == 0 && draw_operations[0].y == 48 &&
           draw_operations[0].width == 320 && draw_operations[0].height == 363);
    check_pixels(&view, 13);
    assert(sentinel_scan_view_update(NULL, 13) == ESP_ERR_INVALID_ARG);
    trace_reset();
    assert(sentinel_scan_view_update(&view, 13) == ESP_OK);
    assert(draw_operations[0].width == 320 && draw_operations[0].height == 363);
    check_pixels(&view, 13);
    /* Page re-entry must restore content even when the logical view is equal. */
    assert(cerberus_display_fill_rect(0, 48, 320, 363, 0xF800) == ESP_OK);
    assert(sentinel_scan_view_draw(&view, 13) == ESP_OK);
    check_pixels(&view, 13);
    puts("Partial renderer: zero-write idle, progress-only writes, age-only writes, owned mutable strings, transitions and error repair passed.");
}

int main(void)
{
    sentinel_scan_view_t view = {
        .kind = SENTINEL_VIEW_WIFI, .state = SENTINEL_VIEW_LIVE,
        .total_count = 18, .page = 0, .page_count = 5, .has_next = true,
        .hint = "LAST COMPLETED SWEEP 5S AGO",
        .row_count = 4,
        .rows = {
            { .name = "WORKSHOP", .detail = "CH 6 / 2.4 GHz / WPA2", .badge = "SAVED NETWORK", .rssi = -43, .signal_valid = true, .marked = true },
            { .name = "CERBERUS-LAB", .detail = "CH 36 / 5 GHz / WPA3", .badge = "AP 3A:91:..:..:B2", .rssi = -61, .signal_valid = true },
            { .name = "GUEST", .detail = "CH 11 / 2.4 GHz / WPA2", .badge = "AP 80:42:..:..:17", .rssi = -77, .signal_valid = true },
            { .name = "HIDDEN NETWORK", .detail = "CH 149 / 5 GHz / WPA2", .badge = "NAME NOT ADVERTISED", .rssi = -89, .signal_valid = true },
        },
    };
    assert(sentinel_scan_view_draw(NULL, 0) == ESP_ERR_INVALID_ARG);
    assert(sentinel_scan_view_animate(NULL, 0) == ESP_ERR_INVALID_ARG);
    assert(sentinel_scan_view_hit_test(NULL, 0, 0) == SENTINEL_VIEW_NONE);
    chrome("WI-FI WATCH");
    memcpy(expected, framebuffer, sizeof(expected));
    assert(sentinel_scan_view_draw(&view, 6) == ESP_OK);
    assert(memcmp(expected, framebuffer, 48 * 320 * sizeof(uint16_t)) == 0);
    assert(memcmp(expected + 411 * 320, framebuffer + 411 * 320, 69 * 320 * sizeof(uint16_t)) == 0);
    save("scan-wifi.ppm");
    partial_update_tests(view);
    assert(sentinel_scan_view_draw(&view, 6) == ESP_OK);
    assert(sentinel_scan_view_hit_test(&view, 84, 364) == SENTINEL_VIEW_PAUSE);
    assert(sentinel_scan_view_hit_test(&view, 151, 405) == SENTINEL_VIEW_PAUSE);
    assert(sentinel_scan_view_hit_test(&view, 152, 405) == SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view, 8, 364) == SENTINEL_VIEW_NONE);
    assert(sentinel_scan_view_hit_test(&view, 160, 364) == SENTINEL_VIEW_NEXT);
    assert(sentinel_scan_view_hit_test(&view, 236, 364) == SENTINEL_VIEW_TOOLS);
    for (unsigned row = 0; row < 4; ++row) {
        assert(sentinel_scan_view_hit_test(&view, 8, 126 + row * 56) == SENTINEL_VIEW_ROW0 + row);
        assert(sentinel_scan_view_hit_test(&view, 311, 177 + row * 56) == SENTINEL_VIEW_ROW0 + row);
        assert(sentinel_scan_view_hit_test(&view, 8, 178 + row * 56) == SENTINEL_VIEW_NONE);
    }
    char age[64];
    for (unsigned frame = 0; frame < 16; ++frame) {
        snprintf(age, sizeof(age), "LAST COMPLETED SWEEP %uS AGO", frame + 5U);
        view.hint = frame == 15 ? NULL : age;
        assert(sentinel_scan_view_animate(&view, frame) == ESP_OK);
        memcpy(expected, framebuffer, sizeof(expected));
        assert(sentinel_scan_view_draw(&view, frame) == ESP_OK);
        assert(memcmp(expected, framebuffer, sizeof(expected)) == 0);
    }
    view.hint = "LAST COMPLETED SWEEP 12S AGO";
    view.kind = SENTINEL_VIEW_BLE;
    view.state = SENTINEL_VIEW_PAUSED;
    view.total_count = 9;
    view.rows[0].name = "LAB THERMOMETER";
    view.rows[0].detail = "ENVIRONMENTAL / CONNECTABLE";
    view.rows[0].badge = "FAVORITE DEVICE";
    view.rows[1].name = "KEYBOARD";
    view.rows[1].detail = "HUMAN INTERFACE / CONNECTABLE";
    view.rows[2].name = "SENSOR-17";
    view.rows[2].detail = "BATTERY SERVICE / ADVERTISEMENT";
    view.rows[3].name = "UNNAMED";
    view.rows[3].detail = "RANDOM ADDRESS / ADVERTISEMENT";
    chrome("BLUETOOTH SCOUT");
    assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
    save("scan-ble.ppm");
    view.kind = SENTINEL_VIEW_RADIO_LIST;
    view.state = SENTINEL_VIEW_COMPLETE;
    view.total_count = 4;
    view.page_count = 1;
    view.has_next = false;
    view.count_label = "BEACONS OBSERVED";
    view.subtitle = "LATEST SWEEP 5S AGO / PASSIVE 802.15.4";
    view.rows[0] = (sentinel_scan_row_t){ .name = "PAN 7A21", .detail = "CHANNEL 15 / COORDINATOR 0000", .badge = "ZIGBEE BEACON OBSERVED", .rssi = -46, .signal_valid = true };
    view.rows[1] = (sentinel_scan_row_t){ .name = "PAN 09B4", .detail = "CHANNEL 20 / COORDINATOR 0000", .badge = "ZIGBEE BEACON OBSERVED", .rssi = -68, .signal_valid = true };
    view.rows[2] = (sentinel_scan_row_t){ .name = "PAN 20F0", .detail = "CHANNEL 25 / COORDINATOR 0A10", .badge = "802.15.4 BEACON OBSERVED", .rssi = -78, .signal_valid = true };
    view.rows[3] = (sentinel_scan_row_t){ .name = "PAN 31C2", .detail = "CHANNEL 11 / COORDINATOR 0000", .badge = "802.15.4 BEACON OBSERVED", .rssi = -91, .signal_valid = true };
    chrome("RADIO SENSE");
    assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
    save("scan-radio-list.ppm");
    assert(sentinel_scan_view_hit_test(&view, 8, 126) == SENTINEL_VIEW_ROW0);
    assert(sentinel_scan_view_hit_test(&view, 311, 345) == SENTINEL_VIEW_ROW3);
    view.kind = SENTINEL_VIEW_ENERGY;
    view.state = SENTINEL_VIEW_LIVE;
    view.count_label = NULL;
    view.subtitle = NULL;
    view.row_count = 0;
    view.progress_known = true;
    view.progress_percent = 75;
    view.energy_valid_mask = 0xFFFF;
    const int8_t energy[] = {-94,-90,-82,-66,-48,-37,-49,-70,-88,-91,-85,-79,-61,-53,-75,-93};
    memcpy(view.energy_dbm, energy, sizeof(energy));
    view.has_next = false;
    chrome("RADIO SENSE");
    assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
    save("scan-energy.ppm");
    assert(sentinel_scan_view_hit_test(&view, 8, 126) == SENTINEL_VIEW_NONE);
    for (unsigned kind = 0; kind <= SENTINEL_VIEW_RADIO_LIST; ++kind) {
        view.kind = (sentinel_view_kind_t)kind;
        view.energy_valid_mask = 0;
        view.row_count = 0;
        for (unsigned state = 0; state <= SENTINEL_VIEW_OFFLINE; ++state) {
            view.state = (sentinel_view_state_t)state;
            assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
        }
    }
    view.kind = SENTINEL_VIEW_WIFI;
    view.state = SENTINEL_VIEW_LIVE;
    view.total_count = 0;
    view.page_count = 1;
    chrome("WI-FI WATCH");
    assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
    save("scan-empty.ppm");
    view.row_count = 1;
    view.rows[0].name = "A VERY LONG NETWORK NAME THAT WILL NEED TO BE TRUNCATED";
    view.rows[0].detail = "LONG DETAILS LONG DETAILS LONG DETAILS LONG DETAILS LONG DETAILS";
    view.rows[0].badge = "LONG BADGE LONG BADGE LONG BADGE LONG BADGE";
    view.total_count = 65535;
    view.page = 65535;
    view.page_count = 65535;
    assert(sentinel_scan_view_draw(&view, 4) == ESP_OK);
    view.row_count = 5;
    assert(sentinel_scan_view_draw(&view, 4) == ESP_ERR_INVALID_ARG);
    view.row_count = 0;
    view.state = SENTINEL_VIEW_OFFLINE;
    assert(sentinel_scan_view_hit_test(&view, 84, 364) == SENTINEL_VIEW_NONE);
    fail_display = 1;
    assert(sentinel_scan_view_draw(&view, 0) == ESP_FAIL);
    assert(sentinel_scan_view_animate(&view, 0) == ESP_FAIL);
    puts("Scan renderer: ownership, all states, animation parity, text bounds, touch controls and errors passed.");
    return 0;
}
'''


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "sentinel-scan")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n", encoding="utf-8")
    display = (ROOT / "s3_main/main/display.c").read_text(encoding="utf-8")
    start = display.index("static const uint8_t *font5x7")
    end = display.index("esp_err_t cerberus_display_draw_text", start)
    source = output / "scan_host.c"
    host = HOST_C[:HOST_C.index("int main(void)")].replace("FONT_FUNCTION", display[start:end])
    host = host.replace("static int fail_display;", "static int fail_display;\n" + TRACE_C)
    host = host.replace("    assert(w > 0 && h > 0", """    if (trace_enabled) {
        assert(draw_count < sizeof(draw_operations) / sizeof(draw_operations[0]));
        draw_operations[draw_count++] = (draw_operation_t){x, y, w, h};
    }
    assert(w > 0 && h > 0""")
    code = (host + SCAN_C).replace("if (fail_display) return ESP_FAIL;", "if (display_failure()) return ESP_FAIL;")
    source.write_text(code, encoding="utf-8")
    executable = output / ("scan_host.exe" if os.name == "nt" else "scan_host")
    compiler = shlex.split(args.cc) if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    subprocess.run(compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(output), "-I", str(ROOT / "s3_main/main"), str(source),
        str(ROOT / "s3_main/main/sentinel_scan_view.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=output, check=True)
    for path in sorted(output.glob("scan-*.ppm")):
        print(ppm_to_png(path))


if __name__ == "__main__":
    main()
