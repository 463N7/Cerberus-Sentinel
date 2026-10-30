"""Validate the shared qualifying-callback sidecar and actual C5 producers."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/watch-capture-timing-checks"
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
source = (root / "c5_firmware/main/ble_scanner.c").read_text()
record_start = source.index("typedef struct {")
(out / "c5_ble_watch_record_type.h").write_text(source[record_start:source.index("} cerberus_ble_record_t;", record_start) + len("} cerberus_ble_record_t;")])
pieces = []
for first, after in (
    ("static void record_advertisement(", "static int gap_event("),
    ("static void start_scan_event(", "static void stop_scan_event("),
    ("esp_err_t cerberus_ble_scanner_start(", "esp_err_t cerberus_ble_watch_get_status("),
    ("esp_err_t cerberus_ble_watch_get_timing(", "esp_err_t cerberus_ble_watch_get_activity("),
):
    start = source.index(first)
    pieces.append(source[start:source.index(after, start)])
# Actual receive switch cases; unrelated central/GATT event cases are omitted.
start = source.index("static int gap_event(")
gap = source[start:source.index("    case BLE_GAP_EVENT_CONNECT:", start)] + "    default: break;\n    }\n    return 0;\n}\n"
pieces.insert(1, gap)
(out / "c5_ble_watch_timing_impl.h").write_text("\n".join(pieces))
source = (root / "c5_firmware/main/wifi_scanner.c").read_text()
pieces = []
for first, after in (
    ("static void wifi_promiscuous_callback(", "static void reset_defense_capture("),
    ("esp_err_t cerberus_wifi_watch_get_timing(", "esp_err_t cerberus_wifi_scanner_init("),
):
    start = source.index(first)
    pieces.append(source[start:source.index(after, start)])
(out / "c5_wifi_watch_timing_impl.h").write_text("\n".join(pieces))
source = (root / "c5_firmware/main/main.c").read_text()
start = source.index("    case CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION:")
(out / "c5_watch_motion_dispatch.h").write_text(
    source[start:source.index("    case CERBERUS_SPI_COMMAND_REMOTE_ID_START:", start)])
exe = out / ("watch-capture-timing-check.exe" if os.name == "nt" else "watch-capture-timing-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "shared"), str(root / "tools/host_checks/watch_capture_timing_check.c"),
    str(root / "shared/cerberus_watch_capture.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
exe = out / "c5-watch-motion-dispatch.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"),
    str(root / "tools/host_checks/c5_watch_motion_dispatch_check.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
for extended in (0, 1):
    exe = out / f"c5-ble-watch-timing-{extended}.exe"
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
        f"-DTEST_EXTENDED={extended}", "-I", str(out), "-I", str(root / "shared"),
        str(root / "tools/host_checks/c5_ble_watch_timing_check.c"),
        str(root / "shared/cerberus_watch_capture.c"), str(root / "shared/sentinel_watch_protocol.c"),
        "-o", str(exe),
    ], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
exe = out / "c5-wifi-watch-timing.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"),
    str(root / "tools/host_checks/c5_wifi_watch_timing_check.c"),
    str(root / "shared/cerberus_watch_capture.c"), str(root / "shared/sentinel_watch_protocol.c"),
    "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
