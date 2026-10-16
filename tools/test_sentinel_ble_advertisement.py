#!/usr/bin/env python3
"""Exercise actual C5 AD capture/chunks and S3 raw-AD getter together on host."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def extract(source, names):
    output = []
    for name in names:
        match = re.search(r"^(?:static )?(?:void|bool|int|size_t|esp_err_t) " + name + r"\([^;]*?\)\s*\{", source, re.M)
        if not match:
            raise RuntimeError(f"Missing function {name}")
        end = source.index("{", match.start()) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        output.append(source[match.start():end])
    return "\n\n".join(output)


HEADER = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "c5_spi_link.h"
#include "cerberus_watch_capture.h"
#define pdTRUE 1
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(n) (n)
#define BLE_HCI_ADV_RPT_EVTYPE_ADV_IND 0
#define BLE_HCI_ADV_RPT_EVTYPE_DIR_IND 1
#define BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP 4
typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
struct ble_gap_disc_desc { ble_addr_t addr; int8_t rssi; uint8_t event_type, length_data; const uint8_t *data; };
struct uuid16 { uint16_t value; };
struct uuid32 { uint32_t value; };
struct uuid128 { uint8_t value[16]; };
struct ble_hs_adv_fields {
    const uint8_t *name, *mfg_data;
    uint8_t name_len, mfg_data_len, num_uuids16, num_uuids32, num_uuids128;
    const struct uuid16 *uuids16;
    const struct uuid32 *uuids32;
    const struct uuid128 *uuids128;
};
static int parse_error;
static int ble_hs_adv_parse_fields(struct ble_hs_adv_fields *fields, const uint8_t *data, uint8_t length)
{
    assert(data && length);
    static const uint8_t manufacturer[] = {0x4c, 0};
    static const struct uuid16 uuid = {0x180f};
    fields->name = (const uint8_t *)"LegacyName"; fields->name_len = 10;
    fields->mfg_data = manufacturer; fields->mfg_data_len = 2;
    fields->uuids16 = &uuid; fields->num_uuids16 = 1;
    return parse_error;
}
static void *scanner_mutex = (void *)1, *link_mutex = (void *)2;
static bool locks[3], fail_lock;
static int xSemaphoreTake(void *mutex, unsigned wait)
{ (void)wait; assert(mutex && !locks[(uintptr_t)mutex]); if (fail_lock && mutex == link_mutex) return 0;
  locks[(uintptr_t)mutex] = true; return pdTRUE; }
static void xSemaphoreGive(void *mutex)
{ assert(locks[(uintptr_t)mutex]); locks[(uintptr_t)mutex] = false; }
/* ACTUAL_RECORD_TYPE */
static cerberus_ble_record_t scan_results[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_ble_scan_status_payload_t scan_status;
static cerberus_watch_capture_t watch_capture;
static cerberus_ble_activity_capture_t activity_capture;
static bool scan_cancelled;
static unsigned scan_timeout_callout;
static void ble_npl_callout_stop(unsigned *callout) { assert(callout == &scan_timeout_callout); }
#define ESP_LOGI(...) ((void)0)
static int64_t esp_timer_get_time(void) { return 1000000; }
/* ACTUAL_C5_FUNCTIONS */
static uint8_t sequence;
static unsigned status_calls, metadata_calls, data_calls, fault;
static uint32_t remote_version = 30, remote_caps = CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT;
static esp_err_t status_error;
static esp_err_t get_status_v2_locked(uint8_t request_sequence, cerberus_c5_status_t *out)
{
    (void)request_sequence; assert(locks[2]); ++status_calls;
    out->firmware_version = remote_version; out->capabilities = remote_caps;
    return status_error;
}
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command, const void *request,
    size_t request_length, void *response, size_t response_length)
{
    assert(locks[2]);
    if (command == CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT) {
        assert(request_length == sizeof(cerberus_ble_scan_result_request_t));
        assert(response_length == sizeof(cerberus_ble_scan_result_payload_t));
        ++metadata_calls;
        const cerberus_ble_scan_result_request_t *query = request;
        cerberus_ble_scan_result_payload_t *result = response;
        esp_err_t err = cerberus_ble_scanner_get_result(query->generation, query->index, result);
        if (fault == 1) result->generation++;
        if (fault == 2) result->index++;
        if (fault == 3) result->advertisement_length = 32;
        if (fault == 4) result->advertisement_event_type = 5;
        if (fault == 5) result->advertisement_flags = 4;
        if (fault == 6) result->address_type = 4;
        if (fault == 7) return ESP_FAIL;
        return err;
    }
    assert(command == CERBERUS_SPI_COMMAND_BLE_SCAN_DATA); /* Getter cannot start RF work. */
    assert(request_length == sizeof(cerberus_ble_scan_data_request_t));
    assert(response_length == sizeof(cerberus_ble_scan_data_payload_t));
    ++data_calls;
    const cerberus_ble_scan_data_request_t *query = request;
    cerberus_ble_scan_data_payload_t *result = response;
    if (fault == 8 && query->chunk == 1) scan_status.generation = 0;
    esp_err_t err = cerberus_ble_scanner_get_data_chunk(query->generation, query->index,
        query->kind, query->chunk, result);
    if (fault == 9) result->generation++;
    if (fault == 10) result->index++;
    if (fault == 11) result->kind++;
    if (fault == 12) result->chunk++;
    if (fault == 13) result->byte_count = 18;
    if (fault == 14) result->byte_count = 0;
    if (fault == 15 && query->chunk == 0) result->byte_count = 16;
    if (fault == 16 && query->chunk == 1) return ESP_FAIL;
    return err;
}
/* ACTUAL_S3_FUNCTIONS */
static struct ble_gap_disc_desc discovery(const uint8_t *data, uint8_t length, uint8_t address)
{
    struct ble_gap_disc_desc result = { .addr = { .type = 1, .val = {address, 2, 3, 4, 5, 6} },
        .rssi = -60, .event_type = 0, .length_data = length, .data = data };
    return result;
}
static void reset(void)
{
    assert(!locks[1] && !locks[2]);
    memset(&scan_status, 0, sizeof(scan_status)); memset(scan_results, 0, sizeof(scan_results));
    scan_status.state = CERBERUS_BLE_SCAN_RUNNING; scan_status.generation = 255;
    cerberus_watch_capture_begin(&watch_capture, CERBERUS_WATCH_RADIO_BLE, 255, 0);
    cerberus_ble_activity_begin(&activity_capture, 255, 0, true);
    status_calls = metadata_calls = data_calls = fault = 0;
    remote_version = 30; remote_caps = CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT;
    status_error = ESP_OK; parse_error = 0; fail_lock = false;
}
static void empty(const cerberus_c5_ble_advertisement_t *result)
{
    cerberus_c5_ble_advertisement_t zero = {0}; assert(!memcmp(result, &zero, sizeof(zero)));
}
static void test_capture(void)
{
    reset();
    const uint8_t data[] = {2, 1, 6, 5, 0xff, 0x4c, 0, 0x12, 0x19};
    struct ble_gap_disc_desc report = discovery(data, sizeof(data), 1);
    record_advertisement(&report);
    assert(scan_status.stored_count == 1 && scan_status.total_count == 1 && !scan_status.dropped_reports);
    assert(scan_results[0].advertisement_length == sizeof(data) && scan_results[0].reports_seen == 1);
    assert(!memcmp(scan_results[0].advertisement, data, sizeof(data)) && !scan_results[0].advertisement_flags);
    assert(scan_results[0].manufacturer_id == 0x004c && scan_results[0].name_length == 10);
    assert(scan_results[0].uuid_length == 2 && scan_results[0].uuid[0] == 0x0f);
    record_advertisement(NULL);
    report.data = NULL; record_advertisement(&report); assert(scan_results[0].reports_seen == 1);
    const uint8_t changed[] = {2, 1, 4}; report.data = changed; report.length_data = sizeof(changed);
    report.event_type = BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
    record_advertisement(&report); assert(scan_results[0].reports_seen == 1);
    report.event_type = 3; report.rssi = -45; record_advertisement(&report);
    assert(scan_results[0].reports_seen == 2 && scan_results[0].rssi == -45);
    assert(scan_results[0].advertisement_length == sizeof(changed));
    assert(!memcmp(scan_results[0].advertisement, changed, sizeof(changed)));
    for (unsigned i = sizeof(changed); i < 31; ++i) assert(scan_results[0].advertisement[i] == 0);
    scan_results[0].reports_seen = UINT16_MAX; record_advertisement(&report);
    assert(scan_results[0].reports_seen == UINT16_MAX);
    const uint8_t malformed[] = {4, 0xff, 1}; report.data = malformed; report.length_data = sizeof(malformed);
    record_advertisement(&report);
    assert(scan_results[0].advertisement_flags & CERBERUS_BLE_ADVERTISEMENT_MALFORMED);
    report.data = changed; report.length_data = sizeof(changed); parse_error = 1;
    record_advertisement(&report);
    assert(scan_results[0].advertisement_flags & CERBERUS_BLE_ADVERTISEMENT_MALFORMED);
    parse_error = 0;
    uint8_t oversized[40] = {30, 0xff}; report.data = oversized; report.length_data = sizeof(oversized);
    record_advertisement(&report);
    assert(scan_results[0].advertisement_length == 31);
    assert(scan_results[0].advertisement_flags == CERBERUS_BLE_ADVERTISEMENT_TRUNCATED);
    for (unsigned i = 2; i <= 35; ++i) { report.addr.val[0] = (uint8_t)i; record_advertisement(&report); }
    assert(scan_status.stored_count == 32 && scan_status.dropped_reports == 3);
    scan_status.dropped_reports = UINT16_MAX; scan_status.total_count = UINT16_MAX;
    record_advertisement(&report);
    assert(scan_status.dropped_reports == UINT16_MAX && scan_status.total_count == UINT16_MAX);
}
static void test_chunks(void)
{
    reset(); uint8_t data[31] = {30, 0xff};
    for (unsigned i = 2; i < sizeof(data); ++i) data[i] = (uint8_t)(i * 17);
    struct ble_gap_disc_desc report = discovery(data, sizeof(data), 1); record_advertisement(&report);
    cerberus_ble_scan_data_payload_t chunk;
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 3, 0, &chunk) == ESP_ERR_INVALID_STATE);
    scan_status.state = CERBERUS_BLE_SCAN_COMPLETE;
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 3, 0, &chunk) == ESP_OK);
    assert(chunk.kind == 3 && chunk.byte_count == 17 && !memcmp(chunk.bytes, data, 17));
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 3, 1, &chunk) == ESP_OK);
    assert(chunk.byte_count == 14 && !memcmp(chunk.bytes, data + 17, 14) && !chunk.bytes[14]);
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 3, 2, &chunk) == ESP_ERR_INVALID_ARG);
    assert(cerberus_ble_scanner_get_data_chunk(0, 0, 3, 0, &chunk) == ESP_ERR_INVALID_STATE);
    assert(cerberus_ble_scanner_get_data_chunk(255, 1, 3, 0, &chunk) == ESP_ERR_INVALID_STATE);
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 99, 0, &chunk) == ESP_ERR_INVALID_ARG);
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 1, 0, &chunk) == ESP_OK);
    assert(chunk.byte_count == 10 && !memcmp(chunk.bytes, "LegacyName", 10));
    assert(cerberus_ble_scanner_get_data_chunk(255, 0, 2, 0, &chunk) == ESP_OK);
    assert(chunk.byte_count == 2 && chunk.bytes[0] == 0x0f && chunk.bytes[1] == 0x18);
}
static void test_end_to_end(void)
{
    for (unsigned length = 0; length <= 31; ++length) {
        reset(); uint8_t data[31] = {0};
        if (length > 1) { data[0] = (uint8_t)(length - 1); data[1] = 0xff; }
        struct ble_gap_disc_desc report = discovery(data, (uint8_t)length, 1); record_advertisement(&report);
        scan_status.state = CERBERUS_BLE_SCAN_COMPLETE;
        cerberus_c5_ble_advertisement_t output;
        assert(c5_spi_link_get_ble_advertisement(255, 0, &output) == ESP_OK);
        assert(output.length == length && !memcmp(output.data, data, length));
        assert(output.reports_seen == 1 && output.address_type == 1 && output.address[0] == 1);
        assert(status_calls == 1 && metadata_calls == 1 && data_calls == (length + 16) / 17);
    }
    for (unsigned injection = 1; injection <= 16; ++injection) {
        reset(); uint8_t data[31] = {30, 0xff};
        struct ble_gap_disc_desc report = discovery(data, 31, 1); record_advertisement(&report);
        scan_status.state = CERBERUS_BLE_SCAN_COMPLETE; fault = injection;
        cerberus_c5_ble_advertisement_t output; memset(&output, 0x55, sizeof(output));
        assert(c5_spi_link_get_ble_advertisement(255, 0, &output) != ESP_OK); empty(&output);
        assert(!locks[1] && !locks[2]);
    }
    for (unsigned unsupported = 0; unsupported < 2; ++unsupported) {
        reset(); if (unsupported) remote_caps = 0; else remote_version = 29;
        cerberus_c5_ble_advertisement_t output; memset(&output, 0x55, sizeof(output));
        assert(c5_spi_link_get_ble_advertisement(255, 0, &output) == ESP_ERR_NOT_SUPPORTED);
        empty(&output); assert(status_calls == 1 && !metadata_calls && !data_calls);
    }
    reset(); cerberus_c5_ble_advertisement_t output;
    status_error = ESP_FAIL; assert(c5_spi_link_get_ble_advertisement(255, 0, &output) == ESP_FAIL); empty(&output);
    assert(!metadata_calls && !data_calls);
    assert(c5_spi_link_get_ble_advertisement(255, 0, NULL) == ESP_ERR_INVALID_ARG);
    fail_lock = true; assert(c5_spi_link_get_ble_advertisement(255, 0, &output) == ESP_ERR_TIMEOUT); empty(&output);
    link_mutex = NULL; assert(c5_spi_link_get_ble_advertisement(255, 0, &output) == ESP_ERR_INVALID_STATE); empty(&output);
}
static void test_completion(void)
{
    for (unsigned outcome = 0; outcome < 3; ++outcome) {
        reset(); uint8_t data[] = {2, 1, 6};
        struct ble_gap_disc_desc report = discovery(data, 3, 1); record_advertisement(&report);
        scan_cancelled = outcome == 1;
        finish_scan(outcome == 2 ? 42 : 0);
        uint8_t expected = outcome == 0 ? CERBERUS_BLE_SCAN_COMPLETE :
            outcome == 1 ? CERBERUS_BLE_SCAN_IDLE : CERBERUS_BLE_SCAN_ERROR;
        assert(scan_status.state == expected && watch_capture.status.state == expected && activity_capture.status.state == expected);
        assert(scan_status.stored_count == (outcome == 0 ? 1 : 0));
        assert(scan_status.error_code == (outcome == 2 ? 42 : 0) && !scan_cancelled);
        /* Late duplicate completion cannot resurrect or republish the sweep. */
        finish_scan(0); assert(scan_status.state == expected);
    }
}
int main(void)
{
    assert(sizeof(cerberus_ble_scan_status_payload_t) == 12);
    assert(sizeof(cerberus_ble_scan_result_payload_t) == 22);
    test_capture(); test_chunks(); test_end_to_end(); test_completion();
    puts("PASS: actual C5 BLE capture and S3 raw-AD getter: all 0..31 lengths, legacy fields, malformed/truncated packets, no scan-response merge, inventory overflow, capability/version gate, bad chunks, generation races and atomic failure output.");
    return 0;
}
'''


def main():
    c5 = (ROOT / "c5_firmware/main/ble_scanner.c").read_text()
    s3 = (ROOT / "s3_main/main/c5_spi_link.c").read_text()
    # Keep observation passive; these checks do not exercise controller behavior.
    assert re.search(r"\.passive\s*=\s*1", c5)
    assert ".scan_type = WIFI_SCAN_TYPE_PASSIVE" in (ROOT / "c5_firmware/main/wifi_scanner.c").read_text()
    start = c5.index("typedef struct {")
    record = c5[start:c5.index("} cerberus_ble_record_t;", start) + len("} cerberus_ble_record_t;")]
    code = HEADER.replace("/* ACTUAL_RECORD_TYPE */", record)
    code = code.replace("/* ACTUAL_C5_FUNCTIONS */", extract(c5, ["bounded_size", "find_record",
        "copy_advertisement_fields", "copy_raw_advertisement", "record_advertisement", "finish_scan",
        "cerberus_ble_scanner_get_result", "cerberus_ble_scanner_get_data_chunk"]))
    code = code.replace("/* ACTUAL_S3_FUNCTIONS */", extract(s3, ["read_ble_data_locked", "c5_spi_link_get_ble_advertisement"]))
    with tempfile.TemporaryDirectory(prefix="sentinel-ble-ad-") as directory:
        path = Path(directory)
        (path / "esp_err.h").write_text("#pragma once\ntypedef int esp_err_t;\n#define ESP_OK 0\n#define ESP_FAIL -1\n"
            "#define ESP_ERR_INVALID_ARG 0x102\n#define ESP_ERR_INVALID_STATE 0x103\n"
            "#define ESP_ERR_NOT_SUPPORTED 0x106\n#define ESP_ERR_TIMEOUT 0x107\n#define ESP_ERR_INVALID_RESPONSE 0x108\n")
        source = path / "test_ble_advertisement.c"
        source.write_text(code)
        exe = path / ("ble_advertisement.exe" if os.name == "nt" else "ble_advertisement")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path), "-I", str(ROOT / "shared"),
            "-I", str(ROOT / "s3_main/main"), str(source), str(ROOT / "shared/cerberus_watch_capture.c"),
            str(ROOT / "shared/sentinel_watch_protocol.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
