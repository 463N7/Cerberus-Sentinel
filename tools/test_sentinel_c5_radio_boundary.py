#!/usr/bin/env python3
"""Run actual C5 admission/connection functions with host radio-driver stubs.

Extracts the functions from the current firmware source (not a copied model).
Exercises the serialized C5 command boundary; no hardware or firmware build.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def functions(filename, names, renames):
    source = (ROOT / "c5_firmware/main" / filename).read_text()
    extracted = []
    for name in names:
        match = re.search(r"^(?:static )?(?:esp_err_t|bool) " + name + r"\([^;]*?\)\s*\{", source, re.M)
        if not match:
            raise RuntimeError(f"Missing firmware function: {name}")
        start = match.start()
        end = source.index("{", start) + 1
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        body = source[start:end]
        for old, new in renames.items():
            body = re.sub(r"\b" + old + r"\b", new, body)
        extracted.append(body)
    return "\n\n".join(extracted)


PREFIX = r'''
#include "cerberus_protocol.h"
#include "cerberus_watch_capture.h"
#define MYNEWT_VAL(x) 1
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_FINISHED 0x10b
#define ESP_ERR_WIFI_NOT_CONNECT 0x300f
#define portMAX_DELAY 0
#define BLE_ADDR_RANDOM_ID 3
#define BLE_CONNECTION_HANDLE_NONE 0xffff
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WEP 1
#define WIFI_AUTH_WPA_PSK 2
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_AUTH_OWE 9
#define WPA3_SAE_PWE_BOTH 3
#define WIFI_IF_STA 0
typedef struct {
    uint8_t ssid[33], bssid[6], primary;
    int8_t rssi;
    unsigned authmode;
} wifi_ap_record_t;
typedef struct {
    struct {
        uint8_t ssid[32], password[64], bssid[6], channel;
        bool bssid_set, owe_enabled;
        int scan_method, sort_method, sae_pwe_h2e, failure_retry_cnt;
        struct { int rssi, authmode; } threshold;
        struct { bool capable, required; } pmf_cfg;
    } sta;
} wifi_config_t;
static void *wifi_mutex = (void *)1, *ble_mutex = (void *)2, *ieee_mutex = (void *)3;
static bool held[4];
static void xSemaphoreTake(void *mutex, int timeout)
{ (void)timeout; assert(mutex && !held[(uintptr_t)mutex]); held[(uintptr_t)mutex] = true; }
static void xSemaphoreGive(void *mutex)
{ assert(mutex && held[(uintptr_t)mutex]); held[(uintptr_t)mutex] = false; }
static void *scanner_task_handle = (void *)1, *defense_task_handle = (void *)1;
static void *beacon_task_handle = (void *)1, *activity_task_handle = (void *)1, *beacon_queue = (void *)1;
static unsigned notified, enqueued, configured, connected, disconnected;
static void xTaskNotifyGive(void *task) { assert(task); ++notified; }
static void xQueueReset(void *queue) { assert(queue); }
static int64_t esp_timer_get_time(void) { return 1234567; }
static void *nimble_port_get_dflt_eventq(void) { return (void *)1; }
static int start_event, connect_event;
static void ble_npl_eventq_put(void *queue, void *event)
{ assert(queue && event); ++enqueued; }
static uint8_t map_security(unsigned auth) { return (uint8_t)auth; }
static bool scanner_ready, host_synced, wifi_cancelled, ble_cancelled, defense_cancelled;
static bool disconnect_requested;
static int64_t scan_started_us;
static uint8_t diagnostic_phase;
static cerberus_wifi_scan_status_payload_t wifi_scan;
static cerberus_wifi_connection_status_payload_t wifi_connection;
static cerberus_wifi_radio_stats_payload_t radio_stats;
static wifi_ap_record_t wifi_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static uint8_t staged_password[CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 1];
static bool staged_password_received[CERBERUS_WIFI_PASSWORD_MAX_LENGTH];
static uint8_t staged_password_length;
static cerberus_wifi_defense_status_payload_t defense_status;
static cerberus_wifi_defense_result_payload_t defense_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static uint8_t defense_channels[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_ble_scan_status_payload_t ble_scan;
static cerberus_watch_capture_t watch_capture;
static cerberus_ble_activity_capture_t activity_capture;
static cerberus_ble_connection_status_payload_t ble_connection;
static uint8_t ble_results[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_ble_gatt_service_status_payload_t gatt_service_status;
static cerberus_ble_gatt_characteristic_status_payload_t gatt_characteristic_status;
static uint8_t gatt_services[CERBERUS_BLE_GATT_SERVICE_MAX_RESULTS];
static uint8_t gatt_characteristics[CERBERUS_BLE_GATT_CHARACTERISTIC_MAX_RESULTS];
static void reset_gatt_read_locked(uint8_t state, int error)
{ assert(state == CERBERUS_BLE_GATT_READ_IDLE && error == 0 && held[2]); }
static cerberus_ieee802154_energy_scan_status_payload_t energy_scan;
static cerberus_ieee802154_beacon_scan_status_payload_t beacon_status;
static cerberus_ieee802154_activity_scan_status_payload_t activity_status;
static int8_t scan_power[CERBERUS_IEEE802154_CHANNEL_COUNT];
static uint8_t beacon_results[32], activity_counts[16][5];
static uint32_t activity_total_frames;
static void cerberus_wifi_scanner_get_status(cerberus_wifi_scan_status_payload_t *out)
{ xSemaphoreTake(wifi_mutex, 0); *out = wifi_scan; xSemaphoreGive(wifi_mutex); }
static void cerberus_wifi_connection_get_status(cerberus_wifi_connection_status_payload_t *out)
{ xSemaphoreTake(wifi_mutex, 0); *out = wifi_connection; xSemaphoreGive(wifi_mutex); }
static void cerberus_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *out)
{ xSemaphoreTake(ble_mutex, 0); *out = ble_scan; xSemaphoreGive(ble_mutex); }
static void cerberus_ble_connection_get_status(cerberus_ble_connection_status_payload_t *out)
{ xSemaphoreTake(ble_mutex, 0); *out = ble_connection; xSemaphoreGive(ble_mutex); }
bool cerberus_wifi_defense_survey_is_running(void);
bool cerberus_ieee802154_scanner_is_running(void);
static wifi_config_t captured_config;
static esp_err_t config_error, connect_error, disconnect_error;
static esp_err_t esp_wifi_set_config(int iface, const wifi_config_t *config)
{
    assert(iface == WIFI_IF_STA && !held[1]);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_CONNECTING);
    ++configured; captured_config = *config; return config_error;
}
static esp_err_t esp_wifi_connect(void) { ++connected; return connect_error; }
static esp_err_t esp_wifi_disconnect(void) { ++disconnected; return disconnect_error; }
'''

TESTS = r'''
static const cerberus_wifi_connect_begin_request_t request = { .generation = 255, .index = 1 };
static const cerberus_ble_connect_request_t ble_request = { .address_type = 1, .address = {2,3,4,5,6,7} };
static void reset(void)
{
    assert(!held[1] && !held[2] && !held[3]);
    scanner_ready = host_synced = true;
    memset(&wifi_scan, 0, sizeof(wifi_scan));
    wifi_scan.state = CERBERUS_WIFI_SCAN_COMPLETE; wifi_scan.generation = 255; wifi_scan.stored_count = 2;
    memset(&wifi_connection, 0, sizeof(wifi_connection));
    memset(&ble_scan, 0, sizeof(ble_scan)); memset(&ble_connection, 0, sizeof(ble_connection));
    memset(&energy_scan, 0, sizeof(energy_scan)); memset(&beacon_status, 0, sizeof(beacon_status));
    memset(&activity_status, 0, sizeof(activity_status)); memset(&defense_status, 0, sizeof(defense_status));
    memset(wifi_results, 0, sizeof(wifi_results));
    for (unsigned i = 0; i < 2; ++i) {
        memcpy(wifi_results[i].ssid, "Same network", 13);
        for (unsigned j = 0; j < 6; ++j) wifi_results[i].bssid[j] = (uint8_t)(i * 16 + j);
        wifi_results[i].primary = i ? 36 : 1;
        wifi_results[i].authmode = WIFI_AUTH_WPA2_PSK;
        wifi_results[i].rssi = -55;
    }
    notified = enqueued = configured = connected = disconnected = 0;
    config_error = connect_error = ESP_OK; disconnect_error = ESP_ERR_WIFI_NOT_CONNECT;
    memset(staged_password, 0, sizeof(staged_password));
    memset(staged_password_received, 0, sizeof(staged_password_received)); staged_password_length = 0;
}
static esp_err_t start(unsigned op)
{
    switch (op) {
    case 0: return cerberus_wifi_scanner_start();
    case 1: return cerberus_ble_scanner_start();
    case 2: return cerberus_ieee802154_scanner_start();
    case 3: return cerberus_ieee802154_beacon_scanner_start();
    case 4: return cerberus_ieee802154_activity_scanner_start();
    case 5: return cerberus_wifi_defense_survey_start();
    case 6: return cerberus_wifi_connection_begin(&request);
    default: return cerberus_ble_connection_start(&ble_request);
    }
}
static uint8_t primary_state(unsigned op)
{
    switch (op) {
    case 0: return wifi_scan.state;
    case 1: return ble_scan.state;
    case 2: return energy_scan.state;
    case 3: return beacon_status.state;
    case 4: return activity_status.state;
    case 5: return defense_status.state;
    case 6: return wifi_connection.state;
    default: return ble_connection.state;
    }
}
static void test_admission_matrix(void)
{
    for (unsigned first = 0; first < 8; ++first) {
        for (unsigned second = 0; second < 8; ++second) {
            reset(); assert(start(first) == ESP_OK);
            unsigned notifications = notified + enqueued;
            uint8_t state = primary_state(first);
            assert(start(second) == ESP_ERR_INVALID_STATE);
            assert(primary_state(first) == state && notified + enqueued == notifications);
        }
    }
    const uint8_t wifi_states[] = { CERBERUS_WIFI_CONNECTION_STAGING, CERBERUS_WIFI_CONNECTION_CONNECTING,
        CERBERUS_WIFI_CONNECTION_CONNECTED, CERBERUS_WIFI_CONNECTION_DISCONNECTING };
    const uint8_t ble_states[] = { CERBERUS_BLE_CONNECTION_CONNECTING,
        CERBERUS_BLE_CONNECTION_CONNECTED, CERBERUS_BLE_CONNECTION_DISCONNECTING };
    for (unsigned op = 0; op < 8; ++op) {
        for (unsigned i = 0; i < sizeof(wifi_states); ++i) {
            reset(); wifi_connection.state = wifi_states[i]; assert(start(op) == ESP_ERR_INVALID_STATE);
            assert(wifi_connection.state == wifi_states[i] && !notified && !enqueued);
        }
        for (unsigned i = 0; i < sizeof(ble_states); ++i) {
            reset(); ble_connection.state = ble_states[i]; assert(start(op) == ESP_ERR_INVALID_STATE);
            assert(ble_connection.state == ble_states[i] && !notified && !enqueued);
        }
    }
}
static void test_commit(void)
{
    reset(); cerberus_wifi_connect_begin_request_t password_request = request;
    password_request.password_length = 8;
    assert(cerberus_wifi_connection_begin(&password_request) == ESP_OK);
    assert(cerberus_wifi_connection_commit() == ESP_ERR_INVALID_SIZE && !configured);
    cerberus_wifi_connect_password_request_t chunk = { .offset = 0, .byte_count = 8 };
    memcpy(chunk.bytes, "testpass", 8);
    assert(cerberus_wifi_connection_password(&chunk) == ESP_OK);
    assert(cerberus_wifi_connection_commit() == ESP_OK);
    assert(configured == 1 && connected == 1 && disconnected == 0);
    assert(captured_config.sta.bssid_set && captured_config.sta.channel == 36);
    assert(!memcmp(captured_config.sta.bssid, wifi_results[1].bssid, 6));
    assert(memcmp(captured_config.sta.bssid, wifi_results[0].bssid, 6));
    assert(!memcmp(captured_config.sta.password, "testpass", 8));
    assert(staged_password_length == 0);
    for (unsigned i = 0; i < sizeof(staged_password); ++i) assert(!staged_password[i]);
    for (unsigned i = 0; i < sizeof(staged_password_received); ++i) assert(!staged_password_received[i]);
    for (unsigned mutation = 0; mutation < 11; ++mutation) {
        reset(); assert(cerberus_wifi_connection_begin(&request) == ESP_OK);
        switch (mutation) {
        case 0: wifi_scan.generation = 0; break;
        case 1: wifi_scan.stored_count = 1; break;
        case 2: wifi_scan.state = CERBERUS_WIFI_SCAN_RUNNING; break;
        case 3: wifi_scan.state = CERBERUS_WIFI_SCAN_IDLE; break;
        case 4: ble_scan.state = CERBERUS_BLE_SCAN_RUNNING; break;
        case 5: ble_connection.state = CERBERUS_BLE_CONNECTION_CONNECTED; break;
        case 6: energy_scan.state = CERBERUS_IEEE802154_ENERGY_SCAN_RUNNING; break;
        case 7: beacon_status.state = CERBERUS_IEEE802154_BEACON_SCAN_RUNNING; break;
        case 8: activity_status.state = CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING; break;
        case 9: defense_status.state = CERBERUS_WIFI_DEFENSE_RUNNING; break;
        default: wifi_results[1].ssid[0] = 0; break;
        }
        assert(cerberus_wifi_connection_commit() == ESP_ERR_INVALID_STATE);
        assert(!configured && !connected && !disconnected);
        assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_STAGING);
    }
    reset(); assert(cerberus_wifi_connection_begin(&request) == ESP_OK);
    config_error = ESP_FAIL; assert(cerberus_wifi_connection_commit() == ESP_FAIL);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_ERROR && configured == 1 && !connected);
    reset(); assert(cerberus_wifi_connection_begin(&request) == ESP_OK);
    connect_error = ESP_FAIL; assert(cerberus_wifi_connection_commit() == ESP_FAIL);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_ERROR && connected == 1);
}
static void test_cancel(void)
{
    reset(); assert(cerberus_wifi_connection_begin(&request) == ESP_OK);
    memset(staged_password, 42, sizeof(staged_password));
    memset(staged_password_received, 1, sizeof(staged_password_received)); staged_password_length = 8;
    assert(cerberus_wifi_connection_disconnect() == ESP_OK);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_IDLE && !disconnected && !staged_password_length);
    for (unsigned i = 0; i < sizeof(staged_password); ++i) assert(!staged_password[i]);
    for (unsigned i = 0; i < sizeof(staged_password_received); ++i) assert(!staged_password_received[i]);
    assert(cerberus_wifi_scanner_start() == ESP_OK);
    reset(); assert(cerberus_wifi_connection_disconnect() == ESP_OK);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_IDLE && disconnected == 1);
    assert(cerberus_ble_scanner_start() == ESP_OK);
    reset(); wifi_connection.state = CERBERUS_WIFI_CONNECTION_CONNECTED;
    disconnect_error = ESP_OK; assert(cerberus_wifi_connection_disconnect() == ESP_OK);
    assert(wifi_connection.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING);
    assert(cerberus_ble_scanner_start() == ESP_ERR_INVALID_STATE);
}
int main(void)
{
    test_admission_matrix(); test_commit(); test_cancel();
    puts("PASS: C5 8x8 admission matrix, active connection states, selected BSSID/channel, stale commit rejection, credential cleanup and cancellation.");
    return 0;
}
'''


def main():
    wifi = functions("wifi_scanner.c", [
        "another_radio_operation_is_active", "wifi_connection_is_active_locked",
        "cerberus_wifi_connection_begin", "cerberus_wifi_connection_password",
        "cerberus_wifi_connection_commit", "cerberus_wifi_connection_disconnect",
        "cerberus_wifi_scanner_start", "cerberus_wifi_defense_survey_start",
        "cerberus_wifi_defense_survey_is_running",
    ], {"another_radio_operation_is_active": "wifi_peer_active", "scanner_mutex": "wifi_mutex",
        "scan_status": "wifi_scan", "connection_status": "wifi_connection",
        "scan_results": "wifi_results", "scan_cancelled": "wifi_cancelled"})
    ble = functions("ble_scanner.c", ["another_radio_operation_is_active",
        "cerberus_ble_scanner_start", "cerberus_ble_connection_start"], {
        "another_radio_operation_is_active": "ble_peer_active", "scanner_mutex": "ble_mutex",
        "scan_status": "ble_scan", "connection_status": "ble_connection",
        "scan_results": "ble_results", "scan_cancelled": "ble_cancelled"})
    ieee = functions("ieee802154_scanner.c", ["another_radio_operation_is_active",
        "cerberus_ieee802154_scanner_start", "cerberus_ieee802154_beacon_scanner_start",
        "cerberus_ieee802154_activity_scanner_start", "cerberus_ieee802154_scanner_is_running"], {
        "another_radio_operation_is_active": "ieee_peer_active", "scanner_mutex": "ieee_mutex",
        "scan_status": "energy_scan"})
    with tempfile.TemporaryDirectory(prefix="sentinel-c5-boundary-") as temp:
        path = Path(temp)
        source = path / "test_radio_boundary.c"
        source.write_text(PREFIX + wifi + ble + ieee + TESTS)
        exe = path / ("radio_boundary.exe" if os.name == "nt" else "radio_boundary")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(ROOT / "shared"),
            str(source), str(ROOT / "shared/cerberus_watch_capture.c"), "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
