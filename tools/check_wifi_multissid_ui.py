"""Exercise the production passive MultiSSID UI and its real menu integration."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-multissid-ui-checks"
out.mkdir(parents=True, exist_ok=True)
display = (root / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[start:display.index("\nesp_err_t ", start)])
framebuffer = (root / "tools/host_checks/recon_render.c").read_text().split("/* Exercise the real private controls", 1)[0]
(out / "multissid_framebuffer.h").write_text(framebuffer)

# Compile the actual caller branches, not a second copy of their routing logic.
ui = (root / "s3_main/main/ui.c").read_text()
def between(text, start, end):
    pos = text.index(start)
    return text[pos:text.index(end, pos)]
draw = between(ui, "    if (wifi_mode == UI_WIFI_MODE_MULTISSID) return", "\n")
tap = between(ui, "    if (wifi_mode == UI_WIFI_MODE_MULTISSID) {", "\n")
entry = between(ui, "        case 8: multissid_enter();", "\n")
back_region = ui[ui.index("if (capture_back())"):]
back_start = back_region.index("            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MULTISSID)")
back = back_region[back_start:back_region.index("            } else if", back_start + 20)]
back = back.replace("            } else if", "    if", 1) + "    }\n"
tick = between(ui, "        bool multissid_visible =", "        bool packet_monitor_visible =")
(out / "multissid_routes.h").write_text(
    "static esp_err_t actual_draw(void) {\n" + draw + "\nreturn ESP_OK; }\n"
    "static void actual_tap(uint16_t x,uint16_t y) {\n" + tap + "\n}\n"
    "static void actual_menu(uint16_t x,uint16_t y) { int item=sentinel_hub_hit_test(SENTINEL_HUB_WIFI,x,y); switch(item) {\n" + entry + "\ndefault: break; }}\n"
    "static void actual_back(void) {\n" + back + "}\n"
    "static void actual_tick(uint32_t now_ms, bool screen_asleep) {\n" + tick + "}\n")
assert "!capture_busy() && !multissid_busy()" in ui
exe = out / "wifi-multissid-ui-check.exe"
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_multissid_ui_check.c"),
    str(root / "s3_main/main/ui_widgets.c"), str(root / "s3_main/main/sentinel_hub.c"),
    str(root / "s3_main/main/privacy.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe), str(out / "multissid")], check=True)

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

for path in sorted(out.glob("multissid-*.ppm")):
    data = path.read_bytes()
    header = b"P6\n320 480\n255\n"
    assert data.startswith(header) and len(data) == len(header) + 320 * 480 * 3
    pixels = data[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(b"\0" + pixels[y*960:(y+1)*960] for y in range(480)))) +
        chunk(b"IEND", b""))
print(out)
