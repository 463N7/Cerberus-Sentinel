"""Exercise actual focused BLE cards, frozen selection, detail and Fox Hunt routes."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import zlib

root = Path(__file__).resolve().parents[1]
native = "--native-live" in sys.argv
subprocess.run([sys.executable, str(root / "tools/check_s3_findmy_monitor_ui.py")], check=True)
out = root / "artifacts/ble-discovery-ui-checks"
out.mkdir(parents=True, exist_ok=True)
ui = (root / "s3_main/main/ui.c").read_text()
live = (root / "s3_main/main/sentinel_live_ui.h").read_text()
discovery = (root / "s3_main/main/s3_ble_discovery_live_ui.h").read_text()
assert "!ble_discovery_defers_c5_telemetry()" in ui, "Periodic C5 telemetry must use the production discovery gate"
assert "!(wifi_relation_ui && wifi_relation_ui->reading)" in ui, "Relation transfers must defer optional C5 telemetry"
def between(source, first, after):
    begin = source.index(first)
    return source[begin:source.index(after, begin)]

# Full production view builder and live tap handler: no second index map,
# row binding, or hit-test implementation in the fixture.
parts = between(live, "typedef struct {", "static sentinel_scan_session_t live_session;")
(out / "discovery_live_types.h").write_text(parts)
(out / "discovery_native_types.h").write_text(between(discovery, "typedef struct {", "static ble_discovery_live_store_t *"))
parts = between(ui, "static const char *ble_filter_name(", "static esp_err_t load_ble_results(")
parts += between(live, "static live_scan_record_t *live_display_record(", "static bool live_has_snapshot(")
parts += between(live, "static bool live_is_view(", "static void live_status(")
parts += between(live, "static uint32_t live_ble_recent_mask(", "static void live_make_view(")
parts += between(live, "static void live_make_view(", "static void live_scan_tick(")
parts += between(live, "static void live_scan_tick(", "static void live_reset_observations(")
parts += between(live, "static void handle_live_scan_tap(", "static esp_err_t live_connect_selected_wifi(")
# Preserve all dispatcher guards before Fox Hunt, then the exact Detail
# branches through TRACK RSSI. Unrelated menu/GATT controls are outside scope.
parts += between(ui, "static void handle_ble_tap(", "    if (ble_mode == UI_BLE_MODE_MENU)")
start = ui.index("    if (ble_mode == UI_BLE_MODE_DETAIL) {", ui.index("static void handle_ble_tap("))
end = ui.index("            bool selected_connected =", start)
parts += ui[start:end] + "        }\n    }\n}\n"
parts += between(ui, "static void ble_fox_hunt_finish_exit(", "static void ble_analyzer_finish_exit(")
parts += between(ui, "static esp_err_t draw_ble_analyzer(", "static esp_err_t draw_ble_feature_menu(")
metadata_start = ui.index("    if (cerberus_ble_rssi_valid(", ui.index("    format_ble_service(&ble_selected_result, service, sizeof(service));"))
metadata_end = ui.index('\n', ui.index('TAG, "BLE detail metadata failed");', metadata_start))
parts += "static esp_err_t actual_ble_peak_metadata(void) { char line[64]; bool connectable=(ble_selected_result.flags&CERBERUS_BLE_RESULT_CONNECTABLE)!=0;\n" + ui[metadata_start:metadata_end] + "\nreturn ESP_OK; }\n"
back_start = ui.index("            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_FOX_HUNT)", ui.index("static void handle_tap("))
back_end = ui.index("            } else if", back_start + 20)
detail_start = ui.index("            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_DETAIL)", back_end)
detail_end = ui.index("            } else if", detail_start + 20)
list_start = ui.index("            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST)", detail_end)
list_end = ui.index("            } else if", list_start + 20)
parts += "static void actual_back(uint16_t y) { if(y < 420U)return;\n" + ui[back_start:back_end].replace("} else if", "if", 1) + ui[detail_start:detail_end] + ui[list_start:list_end] + "\n}\n}\n"
(out / "discovery_routes_impl.h").write_text(parts)
exe = out / ("ble-discovery-native-check.exe" if native else "ble-discovery-ui-check.exe")
env = os.environ.copy()
env["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-cache")
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "artifacts/findmy-monitor-ui-checks"),
    "-I", str(root / "tools/host_checks/stubs"), "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks" / ("s3_ble_discovery_live_check.c" if native else "s3_ble_discovery_ui_check.c")), str(root / "s3_main/main/privacy.c"),
    str(root / "s3_main/main/sentinel_scan_view.c"), str(root / "s3_main/main/sentinel_scan_session.c"),
    str(root / "s3_main/main/sentinel_watch_detect.c"),
    str(root / "s3_main/main/ble_analyzer.c"),
    str(root / "s3_main/main/s3_ble_discovery_session.c"),
    str(root / "s3_main/main/s3_ble_discovery_inventory.c"),
    str(root / "s3_main/main/s3_ble_snapshot.c"),
    str(root / "shared/cerberus_watch_capture.c"),
    str(root / "shared/sentinel_watch_protocol.c"),
    "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe), str(out / ("live" if native else "flipper"))], check=True)

def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
for path in out.glob(("live" if native else "flipper") + "-*.ppm"):
    data = path.read_bytes(); header = b"P6\n320 480\n255\n"
    assert data.startswith(header) and len(data) == len(header) + 320 * 480 * 3
    pixels = data[len(header):]
    path.with_suffix(".png").write_bytes(b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", 320, 480, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(b"".join(b"\0" + pixels[y*960:(y+1)*960] for y in range(480)))) + chunk(b"IEND", b""))
print(out)
