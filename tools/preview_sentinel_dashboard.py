#!/usr/bin/env python3
"""Compile and preview the actual firmware dashboard with a memory display.

Requires a host C compiler; defaults to `python -m ziglang cc`. The only generated
assets are previews under artifacts/. No Pillow or other rendering package is
needed. The font is extracted from display.c, so the screenshots use exactly the
same glyphs as the device. Also checks screen bounds, hit areas, propagated
display errors, and that partial animation matches a complete redraw.
"""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import zlib


ROOT = Path(__file__).resolve().parents[1]

HOST_C = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "sentinel_dashboard.h"
#include "display.h"

static uint16_t framebuffer[320 * 480];
static uint16_t expected[320 * 480];
static int fail_display;

static void bounds(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    assert(w > 0 && h > 0 && (unsigned)x + w <= 320 && (unsigned)y + h <= 480);
}

esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t c)
{
    if (fail_display) return ESP_FAIL;
    bounds(x, y, w, h);
    for (unsigned row = y; row < y + h; ++row)
        for (unsigned col = x; col < x + w; ++col) framebuffer[row * 320 + col] = c;
    return ESP_OK;
}

esp_err_t cerberus_display_draw_rgb565_bitmap(uint16_t x, uint16_t y, uint16_t w,
                                             uint16_t h, const uint16_t *pixels)
{
    if (fail_display) return ESP_FAIL;
    bounds(x, y, w, h);
    assert(pixels != NULL);
    for (unsigned row = 0; row < h; ++row)
        memcpy(&framebuffer[(y + row) * 320 + x], pixels + row * w, w * sizeof(uint16_t));
    return ESP_OK;
}

FONT_FUNCTION

esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text,
                                    uint16_t color, uint16_t background, uint8_t scale)
{
    if (fail_display) return ESP_FAIL;
    assert(text != NULL && scale != 0);
    unsigned width = (unsigned)strlen(text) * 6 * scale;
    bounds(x, y, (uint16_t)width, (uint16_t)(7 * scale));
    for (unsigned row = 0; row < 7U * scale; ++row) {
        for (unsigned col = 0; col < width; ++col) {
            unsigned glyph_col = (col % (6U * scale)) / scale;
            const uint8_t *glyph = font5x7(text[col / (6U * scale)]);
            bool on = glyph_col < 5 && (glyph[row / scale] & (1U << (4U - glyph_col)));
            framebuffer[(y + row) * 320 + x + col] = on ? color : background;
        }
    }
    return ESP_OK;
}

static void save(const char *name)
{
    FILE *file = fopen(name, "wb");
    assert(file != NULL);
    fprintf(file, "P6\n320 480\n255\n");
    for (unsigned i = 0; i < 320 * 480; ++i) {
        uint16_t c = framebuffer[i];
        unsigned char pixel[3] = {
            (unsigned char)(((c >> 11) & 31) * 255 / 31),
            (unsigned char)(((c >> 5) & 63) * 255 / 63),
            (unsigned char)((c & 31) * 255 / 31)
        };
        assert(fwrite(pixel, 1, 3, file) == 3);
    }
    assert(fclose(file) == 0);
}

int main(void)
{
    sentinel_dashboard_status_t status = {
        .c5_online = true, .touch_online = true, .privacy_masked = true,
        .wifi_count = 12, .ble_count = 8, .lessons_complete = 2,
        .wifi_observed = true, .ble_observed = true,
    };
    assert(sentinel_dashboard_draw(NULL, 0) == ESP_ERR_INVALID_ARG);
    assert(sentinel_dashboard_animate(NULL, 0) == ESP_ERR_INVALID_ARG);
    assert(sentinel_dashboard_draw(&status, 0) == ESP_OK);
    save("dashboard-observed.ppm");
    for (uint32_t frame = 1; frame <= 26; ++frame) {
        assert(sentinel_dashboard_animate(&status, frame) == ESP_OK);
        memcpy(expected, framebuffer, sizeof(expected));
        assert(sentinel_dashboard_draw(&status, frame) == ESP_OK);
        assert(memcmp(expected, framebuffer, sizeof(expected)) == 0);
    }
    status.wifi_observed = false;
    status.ble_observed = false;
    status.lessons_complete = 0;
    assert(sentinel_dashboard_draw(&status, 1) == ESP_OK);
    save("dashboard-ready.ppm");
    status.c5_online = false;
    assert(sentinel_dashboard_draw(&status, 1) == ESP_OK);
    save("dashboard-offline.ppm");
    status.c5_online = true;
    status.touch_online = false;
    status.privacy_masked = false;
    status.wifi_count = 255;
    status.ble_count = 255;
    status.lessons_complete = 255;
    status.wifi_observed = true;
    status.ble_observed = true;
    assert(sentinel_dashboard_draw(&status, 1) == ESP_OK);
    status.touch_online = true;
    for (unsigned state = 0; state <= 255; ++state) {
        status.radio_state = (uint8_t)state;
        assert(sentinel_dashboard_animate(&status, state) == ESP_OK);
    }
    /* Every pixel in every launch target maps to its visible card, including edges. */
    const unsigned target_bounds[][5] = {
        {12, 246, 94, 98, SENTINEL_DASHBOARD_WIFI},
        {113, 246, 94, 98, SENTINEL_DASHBOARD_BLE},
        {214, 246, 94, 98, SENTINEL_DASHBOARD_RADIO},
        {12, 356, 296, 52, SENTINEL_DASHBOARD_GUIDE},
        {12, 418, 144, 46, SENTINEL_DASHBOARD_WATCH},
        {164, 418, 144, 46, SENTINEL_DASHBOARD_TOOLS},
    };
    for (unsigned y = 0; y < 480; ++y) {
        for (unsigned x = 0; x < 320; ++x) {
            unsigned action = SENTINEL_DASHBOARD_NONE;
            for (unsigned i = 0; i < 6; ++i) {
                const unsigned *b = target_bounds[i];
                if (x >= b[0] && x < b[0] + b[2] && y >= b[1] && y < b[1] + b[3]) action = b[4];
            }
            assert((unsigned)sentinel_dashboard_hit_test((uint16_t)x, (uint16_t)y) == action);
        }
    }
    assert(sentinel_dashboard_hit_test(65535, 65535) == SENTINEL_DASHBOARD_NONE);
    fail_display = 1;
    assert(sentinel_dashboard_draw(&status, 1) == ESP_FAIL);
    assert(sentinel_dashboard_animate(&status, 1) == ESP_FAIL);
    puts("Dashboard: bounds, animation parity, state variants, all hit pixels and display errors passed.");
    return 0;
}
'''


def chunk(kind: bytes, data: bytes) -> bytes:
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


def ppm_to_png(path: Path) -> Path:
    with path.open("rb") as file:
        assert file.readline() == b"P6\n"
        width, height = map(int, file.readline().split())
        assert file.readline() == b"255\n"
        rgb = file.read()
    rows = b"".join(b"\x00" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b"")
    result = path.with_suffix(".png")
    result.write_bytes(png)
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cc", help="C compiler command, e.g. clang or 'python -m ziglang cc'")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "sentinel-dashboard")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n", encoding="utf-8")
    display = (ROOT / "s3_main/main/display.c").read_text(encoding="utf-8")
    font_start = display.index("static const uint8_t *font5x7")
    font_end = display.index("esp_err_t cerberus_display_draw_text", font_start)
    source = output / "dashboard_host.c"
    source.write_text(HOST_C.replace("FONT_FUNCTION", display[font_start:font_end]), encoding="utf-8")
    executable = output / ("dashboard_host.exe" if os.name == "nt" else "dashboard_host")
    compiler = shlex.split(args.cc) if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(output), "-I", str(ROOT / "s3_main/main"), "-I", str(ROOT / "shared"),
        str(source), str(ROOT / "s3_main/main/sentinel_dashboard.c"), "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], cwd=output, check=True)
    for path in sorted(output.glob("dashboard-*.ppm")):
        print(ppm_to_png(path))


if __name__ == "__main__":
    main()
