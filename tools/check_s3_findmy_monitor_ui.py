"""Exercise production Find My Monitor UI lifecycle with scanner/host fakes."""
from pathlib import Path
import subprocess
import sys
import struct
import zlib
import re
root = Path(__file__).resolve().parents[1]
out = root / "artifacts/findmy-monitor-ui-checks"
out.mkdir(parents=True, exist_ok=True)
ui = (root / "s3_main/main/ui.c").read_text()
start = ui.index("static cerberus_findmy_profile_t selected_findmy_profile(")
end = ui.index("static void ble_sound_open(", start)
(out / "findmy_selected_impl.h").write_text(ui[start:end])
start = ui.index("static void ble_hint_age64_line(")
end = ui.index("static esp_err_t draw_ble_detail(", start)
(out / "ble_display_age_impl.h").write_text(ui[start:end])
monitor = (root / "s3_main/main/s3_findmy_monitor_ui.h").read_text()
(out / "findmy_monitor_ui_impl.h").write_text(monitor)
display = (root / "s3_main/main/display.c").read_text()
start = display.index("static const uint8_t *font5x7(")
(out / "display_font.h").write_text(display[start:display.index("\nesp_err_t ", start)])
(out / "findmy_framebuffer.h").write_text((root / "tools/host_checks/recon_render.c").read_text().split("/* Exercise the real private controls", 1)[0])
start = ui.index("    const cerberus_ble_flock_level_t flock_adv_level =")
end = ui.index("    } else if (ble_selected_focus.category_bits != 0U)", start)
(out / "findmy_detail_draw_impl.h").write_text("static esp_err_t actual_hint_detail(void) {\nchar line[64]; int64_t now_ms=clock_ms;\n" + ui[start:end] + "\n}\nreturn ESP_OK; }\n")
live = (root / "s3_main/main/sentinel_live_ui.h").read_text()
badge_capacity = re.search(r"live_badges\[4\]\[(\d+)\]", live).group(1)
(out / "findmy_badge_capacity.h").write_text("#define FINDMY_BADGE_CAPACITY " + badge_capacity + "\n")
start = live.index("            if (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY && row_focus)")
end = live.index("            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_FLIPPER", start)
(out / "findmy_list_badge_impl.h").write_text("static void actual_hint_badge(void) { unsigned row=0;\n"
    "const cerberus_s3_ble_focus_t *row_focus=live_ble_evidence ? &live_ble_evidence[0].focus : NULL;\n"
    + live[start:end] + "}\n}\n")
exe = out / "findmy-monitor-ui-check.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/s3_findmy_monitor_ui_check.c"),
    str(root / "s3_main/main/privacy.c"), "-o", str(exe)], check=True)
subprocess.run([str(exe), str(out / "findmy")], check=True)
def chunk(kind, data):
    return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data))
for path in sorted(out.glob("findmy-*.ppm")):
    data=path.read_bytes();header=b"P6\n320 480\n255\n"
    assert data.startswith(header) and len(data)==len(header)+320*480*3
    pixels=data[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n"+
        chunk(b"IHDR",struct.pack(">IIBBBBB",320,480,8,2,0,0,0))+
        chunk(b"IDAT",zlib.compress(b"".join(b"\0"+pixels[y*960:(y+1)*960] for y in range(480))))+chunk(b"IEND",b""))
print(out)
