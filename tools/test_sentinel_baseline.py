#!/usr/bin/env python3
"""Exercise the real baseline comparison against changing/broken radio snapshots."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = r'''
#include "sentinel_baseline.h"
#include "c5_spi_link.h"
#include "settings.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned status_reads, record_reads, change, fail_index = 255;
static unsigned count = 3;
static uint8_t state = CERBERUS_WIFI_SCAN_COMPLETE;
static bool status_fail;
size_t cerberus_settings_wifi_trusted_count(void) { return 4; }
cerberus_wifi_trust_state_t cerberus_settings_wifi_trust_state(
    const char *ssid, const uint8_t bssid[6], uint8_t security)
{
    assert(ssid[0] == 'T' && security == 3);
    return bssid[0] % 3 == 0 ? CERBERUS_WIFI_TRUST_EXACT :
        bssid[0] % 3 == 1 ? CERBERUS_WIFI_TRUST_MISMATCH : CERBERUS_WIFI_TRUST_NONE;
}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *out)
{
    memset(out, 0, sizeof(*out));
    ++status_reads;
    if (status_fail) return ESP_FAIL;
    out->state = state;
    out->stored_count = count;
    out->generation = 255;
    if (status_reads == 2) {
        if (change == 1) out->generation = 0;
        if (change == 2) out->state = CERBERUS_WIFI_SCAN_RUNNING;
        if (change == 3) out->stored_count++;
    }
    return ESP_OK;
}
esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation, uint8_t index,
    cerberus_c5_wifi_result_t *out)
{
    assert(generation == 255 && index < count);
    ++record_reads;
    if (index == fail_index) return ESP_FAIL;
    memset(out, 0, sizeof(*out));
    strcpy(out->ssid, "Test AP"); out->security = 3; out->bssid[0] = index;
    return ESP_OK;
}
static esp_err_t compare(sentinel_baseline_t *out)
{ status_reads = record_reads = 0; return sentinel_baseline_compare(out); }
static void empty(const sentinel_baseline_t *out)
{ assert(out->checked && out->exact == 0 && out->review == 0 && out->other == 0 && out->compared == 0); }
int main(void)
{
    sentinel_baseline_t out;
    assert(compare(NULL) == ESP_ERR_INVALID_ARG);
    assert(compare(&out) == ESP_OK);
    assert(out.exact == 1 && out.review == 1 && out.other == 1 && out.compared == 3 && out.saved == 4);
    assert(status_reads == 2 && record_reads == 3 && out.generation == 255);
    for (change = 1; change <= 3; ++change) {
        assert(compare(&out) == ESP_ERR_INVALID_RESPONSE); empty(&out);
    }
    change = 0;
    for (fail_index = 0; fail_index < 3; ++fail_index) {
        assert(compare(&out) == ESP_FAIL); empty(&out);
    }
    fail_index = 255;
    state = CERBERUS_WIFI_SCAN_RUNNING;
    assert(compare(&out) == ESP_ERR_INVALID_STATE); empty(&out); assert(record_reads == 0);
    state = CERBERUS_WIFI_SCAN_COMPLETE;
    count = 33;
    assert(compare(&out) == ESP_ERR_INVALID_RESPONSE); empty(&out); assert(record_reads == 0);
    count = 0;
    assert(compare(&out) == ESP_OK); empty(&out); assert(status_reads == 2);
    count = 32;
    assert(compare(&out) == ESP_OK); assert(out.compared == 32 && record_reads == 32);
    status_fail = true;
    assert(compare(&out) == ESP_FAIL); empty(&out); assert(record_reads == 0);
    puts("PASS: baseline matches, differences, empty/max scans, transport faults, generation rollover and atomic results.");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="sentinel-baseline-") as temp:
        path = Path(temp)
        (path / "esp_err.h").write_text(
            "#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_INVALID_RESPONSE 0x108\n")
        source = path / "test_baseline.c"
        source.write_text(HARNESS)
        exe = path / ("baseline.exe" if os.name == "nt" else "baseline")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path),
            "-I", str(ROOT / "s3_main/main"), "-I", str(ROOT / "shared"),
            str(source), str(ROOT / "s3_main/main/sentinel_baseline.c"),
            "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
