"""Exercise the actual incremental S3 relation reader and SPI frame validation."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-relation-transport-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/c5_spi_link.c").read_text()
start = source.index("static esp_err_t get_status_v2_locked(")
end = source.index("static esp_err_t exchange_v2_locked(", start)
(out / "relation_peer_impl.h").write_text(source[start:end])
console = (root / "s3_main/main/usb_console.c").read_text()
start = console.index("/* BEGIN WIFI RELATION DIAGNOSTICS:")
end = console.index("/* END WIFI RELATION DIAGNOSTICS. */", start)
(out / "relation_console_impl.h").write_text(console[start:end])
radio = console[console.index("static void print_radio(void)"):console.index("static void print_pinescan(void)")]
assert "print_wifi_relations();" in radio and "print_wardrive_relations(&wardrive);" in radio
include = out / "include"
include.mkdir(exist_ok=True)
(include / "esp_err.h").write_text(
    "#pragma once\ntypedef int esp_err_t;\n"
    "enum {ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,"
    "ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE,ESP_ERR_INVALID_SIZE,ESP_ERR_NOT_FINISHED};\n")
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("wifi-relation-transport-check.exe" if os.name == "nt" else "wifi-relation-transport-check")
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(include), "-I", str(out), "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/wifi_relation_transport_check.c"),
    str(root / "s3_main/main/s3_wifi_relation.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
