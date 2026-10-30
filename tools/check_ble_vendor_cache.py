"""Exercise the shared selected-vendor cache and both production detail callers."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/ble-vendor-cache-checks"
out.mkdir(parents=True, exist_ok=True)
source = (root / "s3_main/main/ui.c").read_text()
cache_type = source.index("} ui_selected_vendor_cache_t;")
cache_start = source.rindex("typedef struct {", 0, cache_type)
cache_end = source.index("static esp_err_t draw_wifi_detail(void)", cache_type)
callers = []
for radio, width, first, last_label in (
    ("wifi", 72, "    refresh_selected_vendor(false,", '"Wi-Fi vendor"'),
    ("ble", 64, "    int64_t now_ms = esp_timer_get_time() / 1000LL;", '"BLE detail vendor failed"'),
):
    detail = source.index(f"static esp_err_t draw_{radio}_detail(void)\n{{")
    begin = source.index(first, detail)
    last = source.index(last_label, begin)
    end = source.index(";", last) + 1
    callers.append(f"static esp_err_t render_{radio}_vendor(void)\n{{\n    char line[{width}];\n" +
                   source[begin:end] + "\n    return ESP_OK;\n}\n")
(out / "ble_vendor_cache_impl.h").write_text(
    source[cache_start:cache_end] + "\n" + "\n".join(callers))

# Exercise the exact zero-wait storage guard as well as the UI. The normal
# blocking getter and recon lookup contract are intentionally unchanged.
storage = (root / "s3_main/main/storage.c").read_text()
def function(text, signature):
    begin = text.index(signature)
    brace = text.index("{", begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[begin:end]
(out / "storage_try_epoch_impl.h").write_text("\n\n".join(function(storage, signature) for signature in (
    "static esp_err_t require_firmware_owner_locked(void)",
    "esp_err_t cerberus_storage_try_get_read_epoch(uint32_t *epoch)",
)))

env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
exe = out / ("ble-vendor-cache-check.exe" if os.name == "nt" else "ble-vendor-cache-check")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "shared"),
    "-I", str(root / "tools/host_checks/stubs"),
    str(root / "tools/host_checks/ble_vendor_cache_check.c"),
    str(root / "shared/recon_db.c"), "-o", str(exe),
], check=True, env=env)
subprocess.run([str(exe)], check=True, env=env)
