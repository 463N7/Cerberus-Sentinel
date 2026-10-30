"""Check bounded AP evidence and the actual C5 scan/callback lifecycle."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-scan-observation-checks"
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
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
for stem in ("wifi_scan_observation_check", "wifi_scan_observation_lifecycle_check"):
    exe = out / (stem + (".exe" if os.name == "nt" else ""))
    subprocess.run([
        sys.executable, "-m", "ziglang", "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1",
        "-Wall", "-Wextra", "-Werror", "-I", str(out), "-I", str(root / "shared"),
        str(root / "tools/host_checks" / (stem + ".c")), "-o", str(exe),
    ], check=True, env=env)
    subprocess.run([str(exe)], check=True, env=env)
