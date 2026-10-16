#!/usr/bin/env python3
"""Check and preview the actual compact battery renderer with Home and Watch.

Uses the device font and existing memory-display harness. The normal tool header
is reproduced for layout comparison; Home and Watch use their production code.
"""
from __future__ import annotations

import argparse
import os
from pathlib import Path
import shlex
import subprocess
import sys

from preview_sentinel_dashboard import HOST_C, ROOT, ppm_to_png


REVISION_C = r'''
static uint32_t top_revision;
static unsigned battery_writes;
static bool battery_only;

uint32_t cerberus_display_top_revision(void) { return top_revision; }
static bool record_draw(uint16_t x, uint16_t y, uint16_t width, uint16_t height, bool bitmap)
{
    if (y < 16U) ++top_revision;
    if (battery_only) {
        assert(bitmap && x == 244U && y == 3U && width == 68U && height == 13U);
    }
    if (bitmap && x == 244U && y == 3U && width == 68U && height == 13U) ++battery_writes;
    return fail_display != 0;
}
'''


def gallery(output: Path) -> None:
    names = ["home", "watch", "normal", "low", "critical", "unknown"]
    images = []
    for name in names:
        with (output / f"battery-{name}.ppm").open("rb") as source:
            assert source.readline() == b"P6\n"
            assert source.readline() == b"320 480\n"
            assert source.readline() == b"255\n"
            images.append(source.read())
    # Top portions at native size; all six headers stay readable together.
    width, height, crop_height = 960, 220, 110
    content = bytearray()
    for y in range(height):
        row, image_y = divmod(y, crop_height)
        for col in range(3):
            source = images[row * 3 + col]
            content += source[image_y * 320 * 3:(image_y + 1) * 320 * 3]
    path = output / "battery-gallery.ppm"
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode() + content)
    ppm_to_png(path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts/sentinel-battery")
    parser.add_argument("--cc", help="C compiler command")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "esp_err.h").write_text(
        "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
        "#define ESP_ERR_INVALID_ARG 0x102\n", encoding="utf-8")
    display = (ROOT / "s3_main/main/display.c").read_text(encoding="utf-8")
    start = display.index("static const uint8_t *font5x7")
    end = display.index("esp_err_t cerberus_display_draw_text", start)
    host = HOST_C[:HOST_C.index("int main(void)")]
    host = host.replace("static int fail_display;", "static int fail_display;\n" + REVISION_C)
    host = host.replace("    if (fail_display) return ESP_FAIL;\n", "")
    host = host.replace("    bounds(x, y, w, h);", "    bounds(x, y, w, h);\n"
        "    if (record_draw(x, y, w, h, BITMAP)) return ESP_FAIL;", 2)
    host = host.replace("BITMAP", "false", 1).replace("BITMAP", "true", 1)
    host = host.replace("    bounds(x, y, (uint16_t)width, (uint16_t)(7 * scale));",
        "    bounds(x, y, (uint16_t)width, (uint16_t)(7 * scale));\n"
        "    if (record_draw(x, y, (uint16_t)width, (uint16_t)(7 * scale), false)) return ESP_FAIL;")
    host = host.replace("FONT_FUNCTION", display[start:end])
    fixture = (ROOT / "tools/tests/sentinel_battery_view_test.c").read_text(encoding="utf-8")
    source = output / "battery_host.c"
    source.write_text(host + fixture, encoding="utf-8")
    executable = output / ("battery_host.exe" if os.name == "nt" else "battery_host")
    compiler = shlex.split(args.cc) if args.cc else [sys.executable, "-m", "ziglang", "cc"]
    command = compiler + ["-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I", str(output), "-I", str(ROOT / "s3_main/main"), "-I", str(ROOT / "shared"),
        str(source), *[str(ROOT / "s3_main/main" / filename) for filename in
            ("sentinel_battery_view.c", "sentinel_dashboard.c", "sentinel_watch_view.c")],
        "-o", str(executable)]
    subprocess.run(command, check=True)
    subprocess.run([str(executable)], cwd=output, check=True)
    for path in sorted(output.glob("battery-*.ppm")):
        ppm_to_png(path)
    gallery(output)
    print(f"Battery previews: {output}")


if __name__ == "__main__":
    main()
