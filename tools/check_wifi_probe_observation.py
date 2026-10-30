"""Check strict passive probe clues and the actual normal C5 scan/transport path."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-probe-observation-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "c5_firmware/main/wifi_scanner.c").read_text()
sections = [
    ("static void scan_observation_begin(void)", "static void reset_defense_capture(void)"),
    ("static void finish_scan(esp_err_t scan_error)", "esp_err_t cerberus_wifi_watch_get_status("),
    ("esp_err_t cerberus_wifi_scanner_start(void)", "void cerberus_wifi_scanner_get_status("),
    ("esp_err_t cerberus_wifi_scanner_get_result(", "esp_err_t cerberus_wifi_scanner_get_ssid_chunk("),
]
extracted = []
for first, after in sections:
    start = source.index(first)
    extracted.append(source[start:source.index(after, start)])
(out / "wifi_scan_observation_impl.h").write_text("\n".join(extracted))
source = (root / "c5_firmware/main/main.c").read_text()
start = source.index("    case CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS:")
(out / "wifi_probe_dispatch.h").write_text(source[start:source.index("    case CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT:", start)])
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
for stem in ("wifi_probe_observation_check", "wifi_probe_c5_check"):
    exe = out / (stem + (".exe" if os.name == "nt" else ""))
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
        "-Wall", "-Wextra", "-Werror", "-I", str(out), "-I", str(root / "shared"),
        str(root / "tools/host_checks" / (stem + ".c")), "-o", str(exe),
    ], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
