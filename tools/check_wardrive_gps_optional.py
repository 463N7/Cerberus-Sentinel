"""Exercise the Wardriving GPS opt-in helpers with an unwired receiver fixture."""
from pathlib import Path
import importlib.util
import os
import subprocess
import sys


root = Path(__file__).resolve().parents[1]
service = (root / "s3_main/main/wardrive_service.c").read_text(encoding="utf-8")
ui = (root / "s3_main/main/ui.c").read_text(encoding="utf-8")
helpers = service[service.index("static esp_err_t start_optional_gps(bool enabled)"):
    service.index("static uint32_t plus_one(uint32_t value)")]
assert "bool gps_enabled = wardrive_status.gps_requested;" in service
assert "gps_error = start_optional_gps(gps_enabled);" in service
assert "gps_error = stop_optional_gps(gps_enabled);" in service
assert "snapshot_optional_gps(wardrive_status.gps_requested, &gps, NULL);" in service
assert "wardrive_status.gps_requested = enable_gps;" in service
assert "wardrive_gps_enabled = false;" in ui
assert "cerberus_wardrive_start(wardrive_include_expansions," in ui

fixture = r'''
#undef NDEBUG
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "wardrive_gps_core.h"
typedef int esp_err_t;
#define ESP_OK 0
static unsigned starts, stops, snapshots;
static esp_err_t start_result, stop_result, snapshot_result;
static esp_err_t cerberus_wardrive_gps_uart_start(void)
{ ++starts; return start_result; }
static esp_err_t cerberus_wardrive_gps_uart_stop(void)
{ ++stops; return stop_result; }
static void cerberus_wardrive_gps_uart_snapshot(
    cerberus_wardrive_gps_snapshot_t *out, esp_err_t *error)
{
    ++snapshots;
    out->state = CERBERUS_WARDRIVE_GPS_FRESH;
    out->coordinates_valid = true;
    out->fix_age_ms = 123U;
    if (error) *error = snapshot_result;
}
''' + helpers + r'''
int main(void)
{
    cerberus_wardrive_gps_snapshot_t gps = {
        .state = CERBERUS_WARDRIVE_GPS_FRESH,
        .coordinates_valid = true,
    };
    esp_err_t error = 99;
    start_result = 77;
    stop_result = 78;
    snapshot_result = 79;
    assert(start_optional_gps(false) == ESP_OK);
    snapshot_optional_gps(false, &gps, &error);
    assert(gps.state == CERBERUS_WARDRIVE_GPS_ABSENT);
    assert(!gps.coordinates_valid && gps.sentence_age_ms == UINT32_MAX);
    assert(gps.fix_age_ms == UINT32_MAX && error == ESP_OK);
    assert(stop_optional_gps(false) == ESP_OK);
    assert(starts == 0U && stops == 0U && snapshots == 0U);

    assert(start_optional_gps(true) == 77);
    snapshot_optional_gps(true, &gps, &error);
    assert(gps.state == CERBERUS_WARDRIVE_GPS_FRESH);
    assert(gps.coordinates_valid && gps.fix_age_ms == 123U && error == 79);
    assert(stop_optional_gps(true) == 78);
    assert(starts == 1U && stops == 1U && snapshots == 1U);
    return 0;
}
'''
out = root / "artifacts/wardrive-checks"
out.mkdir(parents=True, exist_ok=True)
source_path = out / "wardrive-gps-optional-check.c"
source_path.write_text(fixture, encoding="utf-8")
exe = out / "wardrive-gps-optional-check.exe"
mingw_bin = Path.home() / ".platformio/packages/toolchain-gccmingw32/bin"
use_zig = importlib.util.find_spec("ziglang") is not None
compiler = ([sys.executable, "-m", "ziglang", "cc"] if use_zig
    else [str(mingw_bin / "gcc.exe")])
environment = os.environ.copy()
if not use_zig:
    environment["PATH"] = str(mingw_bin) + os.pathsep + environment.get("PATH", "")
subprocess.run(compiler + ["-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(root / "s3_main/main"), str(source_path), "-o", str(exe)],
    check=True, env=environment)
subprocess.run([str(exe)], check=True, env=environment)
print("wardrive GPS OFF avoids UART; ON preserves reader lifecycle: pass")
