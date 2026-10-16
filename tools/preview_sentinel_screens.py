#!/usr/bin/env python3
"""Render the actual hub/lesson code with the device font and check touch bounds."""
from pathlib import Path
import os
import subprocess
import sys

from preview_sentinel_dashboard import HOST_C, ppm_to_png

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "artifacts/sentinel-screens"

EXTRA_C = r'''
#include "sentinel_hub.h"
#include "sentinel_guide.h"
#include "nvs.h"
static bool saved_record;
static uint32_t record;
esp_err_t nvs_open(const char *name, nvs_open_mode_t mode, nvs_handle_t *handle)
{ (void)name; (void)mode; *handle = 1; return ESP_OK; }
esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *value)
{ (void)h; (void)key; if (!saved_record) return ESP_ERR_NVS_NOT_FOUND; *value = record; return ESP_OK; }
esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value)
{ (void)h; (void)key; record = value; saved_record = true; return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h; return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }

int main(void)
{
    const cerberus_ui_widget_theme_t theme = {0x0842, 0x10C5, 0x2EFD, 0xEF9F, 0x9D16};
    const char *titles[] = {"Wi-Fi Watch", "Bluetooth Scout", "Radio Sense"};
    char path[64];
    for (unsigned hub = 0; hub < 3; ++hub) {
        assert(cerberus_display_fill_rect(0, 0, 320, 480, theme.background) == ESP_OK);
        unsigned scale = strlen(titles[hub]) * 18 <= 296 ? 3 : 2;
        assert(cerberus_display_draw_text((320 - strlen(titles[hub]) * 6 * scale) / 2,
            12, titles[hub], theme.text, theme.background, scale) == ESP_OK);
        assert(sentinel_hub_draw(hub, &theme) == ESP_OK);
        assert(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){64, 420, 192, 44},
            "HOME", 2, true, &theme) == ESP_OK);
        snprintf(path, sizeof(path), "hub-%u.ppm", hub + 1);
        save(path);
        for (unsigned y = 0; y < 480; ++y) {
            for (unsigned x = 0; x < 320; ++x) {
                int action = -1;
                if (x >= 12 && x < 308) {
                    unsigned count = hub == 0 ? 6 : 4;
                    unsigned step = hub == 0 ? 48 : 66;
                    unsigned height = hub == 0 ? 42 : 54;
                    for (unsigned row = 0; row < count; ++row) {
                        unsigned top = 70 + row * step;
                        if (y >= top && y < top + height) action = row;
                    }
                    if (y >= 364 && y < 406) action = 6;
                }
                assert(sentinel_hub_hit_test(hub, x, y) == action);
            }
        }
        assert(sentinel_hub_hit_test(hub, 65535, 65535) == -1);
    }
    assert(sentinel_guide_init() == ESP_OK);
    sentinel_guide_open();
    assert(sentinel_guide_draw() == ESP_OK); save("field-guide.ppm");
    for (unsigned lesson = 0; lesson < 6; ++lesson) {
        sentinel_guide_open_lesson(lesson);
        assert(sentinel_guide_draw() == ESP_OK);
        snprintf(path, sizeof(path), "lesson-%u.ppm", lesson + 1); save(path);
        assert(sentinel_guide_handle_touch(240, 440) == SENTINEL_GUIDE_REDRAW);
        assert(sentinel_guide_draw() == ESP_OK);
        snprintf(path, sizeof(path), "experiment-%u.ppm", lesson + 1); save(path);
        assert(sentinel_guide_handle_touch(240, 440) == SENTINEL_GUIDE_REDRAW);
        assert(sentinel_guide_handle_touch(240, 440) == SENTINEL_GUIDE_REDRAW);
        assert(sentinel_guide_draw() == ESP_OK);
        snprintf(path, sizeof(path), "quiz-%u.ppm", lesson + 1); save(path);
    }
    sentinel_guide_open_demo();
    for (unsigned panel = 0; panel < 3; ++panel) {
        assert(sentinel_guide_draw() == ESP_OK);
        snprintf(path, sizeof(path), "demo-%u.ppm", panel + 1); save(path);
        assert(sentinel_guide_handle_touch(240, 440) == SENTINEL_GUIDE_REDRAW);
    }
    puts("PASS: all hub hit pixels, device-font screen bounds and lesson/demo rendering.");
    return 0;
}
'''


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    display = (ROOT / "s3_main/main/display.c").read_text()
    a = display.index("static const uint8_t *font5x7")
    b = display.index("esp_err_t cerberus_display_draw_text", a)
    renderer = HOST_C[:HOST_C.index("int main(void)")]
    renderer = renderer.replace("static uint16_t expected[320 * 480];", "")
    source = OUT / "screens_host.c"
    source.write_text(renderer.replace("FONT_FUNCTION", display[a:b]) + EXTRA_C)
    executable = OUT / ("screens_host.exe" if os.name == "nt" else "screens_host")
    subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O2",
        "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "tools/tests/sentinel_guide"),
        "-I", str(ROOT / "s3_main/main"), str(source),
        *[str(ROOT / "s3_main/main" / name) for name in
          ("sentinel_hub.c", "sentinel_guide.c", "ui_widgets.c")],
        "-o", str(executable)], check=True)
    subprocess.run([str(executable)], cwd=OUT, check=True)
    for path in OUT.glob("*.ppm"):
        ppm_to_png(path)
    print(f"Device-font previews: {OUT}")


if __name__ == "__main__":
    main()
