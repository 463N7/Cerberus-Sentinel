#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_ble_scanner_init(void);
uint8_t cerberus_ble_scanner_get_boot_count(void);
uint8_t cerberus_ble_scanner_get_phase(void);
esp_err_t cerberus_ble_scanner_start(void);
esp_err_t cerberus_ble_scanner_stop(void);
esp_err_t cerberus_ble_watch_get_status(uint8_t generation, cerberus_watch_scan_status_payload_t *out);
esp_err_t cerberus_ble_watch_get_timing(const cerberus_watch_scan_timing_request_t *request,
    cerberus_watch_scan_timing_payload_t *out);
esp_err_t cerberus_ble_watch_get_motion(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_motion_payload_t *out);
esp_err_t cerberus_ble_watch_get_motion_timing(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_timing_payload_t *out);
esp_err_t cerberus_ble_watch_get_data(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_data_payload_t *out);
esp_err_t cerberus_ble_watch_get_activity(uint8_t generation, cerberus_ble_activity_payload_t *out);
void cerberus_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status);
esp_err_t cerberus_ble_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ble_scan_result_payload_t *result);
/* RAW_ADVERTISEMENT returns one bounded packet. Scan responses are not
   solicited, captured, or merged with advertisements. */
esp_err_t cerberus_ble_scanner_get_data_chunk(
    uint8_t generation,
    uint8_t index,
    uint8_t kind,
    uint8_t chunk,
    cerberus_ble_scan_data_payload_t *result);
esp_err_t cerberus_ble_connection_start(const cerberus_ble_connect_request_t *request);
esp_err_t cerberus_ble_connection_disconnect(void);
void cerberus_ble_connection_get_status(cerberus_ble_connection_status_payload_t *status);
esp_err_t cerberus_ble_gatt_service_discovery_start(void);
void cerberus_ble_gatt_service_get_status(cerberus_ble_gatt_service_status_payload_t *status);
esp_err_t cerberus_ble_gatt_service_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ble_gatt_service_result_payload_t *result);
esp_err_t cerberus_ble_gatt_characteristic_discovery_start(
    const cerberus_ble_gatt_characteristic_discover_request_t *request);
void cerberus_ble_gatt_characteristic_get_status(
    cerberus_ble_gatt_characteristic_status_payload_t *status);
esp_err_t cerberus_ble_gatt_characteristic_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ble_gatt_characteristic_result_payload_t *result);
esp_err_t cerberus_ble_gatt_read_start(const cerberus_ble_gatt_read_request_t *request);
void cerberus_ble_gatt_read_get_status(cerberus_ble_gatt_read_status_payload_t *status);
esp_err_t cerberus_ble_gatt_read_get_data(
    uint8_t generation,
    uint8_t chunk,
    cerberus_ble_gatt_read_data_payload_t *result);

#ifdef __cplusplus
}
#endif
