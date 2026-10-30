"""Render the actual Wardriving page and check read-only counts/control routes."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wardrive-ui-checks"
out.mkdir(parents=True, exist_ok=True)
ui = (root / "s3_main/main/ui.c").read_text()

def between(source, first, after):
    begin = source.index(first)
    return source[begin:source.index(after, begin)]

parts = between(ui, "static const cerberus_ui_widget_theme_t ui_colors =", "static esp_err_t draw_detail_page(void);")
parts += between(ui, "static uint16_t text_width(", "static void refresh_capability_map(")
parts += between(ui, "static const char *wardrive_state_label(", "static const char *ble_attack_mode_label(")
parts += between(ui, "static esp_err_t draw_wardrive(", "static esp_err_t draw_detail_page(void)\n{")
parts += between(ui, "static void wardrive_handle_tap(", "static void wardrive_tick(")
(out / "wardrive_ui_impl.h").write_text(parts)
display = (root / "s3_main/main/display.c").read_text()
begin = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[begin:display.index("\nesp_err_t ", begin)])

env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / "wardrive-ui-check.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/wardrive_ui_check.c"),
    str(root / "s3_main/main/ui_widgets.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe), str(out / "wardrive")], check=True, env=env)

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

for path in out.glob("wardrive-*.ppm"):
    header = b"P6\n320 480\n255\n"
    raw = path.read_bytes()
    assert raw.startswith(header) and len(raw) == len(header) + 320 * 480 * 3
    pixels = raw[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(b"\0" + pixels[y*960:(y+1)*960] for y in range(480)))) +
        chunk(b"IEND", b""))
print(out)
