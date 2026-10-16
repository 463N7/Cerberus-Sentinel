#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_wifi_scanner_init(void);
esp_err_t cerberus_wifi_scanner_start(void);
esp_err_t cerberus_wifi_scanner_stop(void);
esp_err_t cerberus_wifi_watch_get_status(uint8_t generation, cerberus_watch_scan_status_payload_t *out);
esp_err_t cerberus_wifi_watch_get_data(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_data_payload_t *out);
void cerberus_wifi_scanner_get_status(cerberus_wifi_scan_status_payload_t *status);
void cerberus_wifi_scanner_get_radio_stats(cerberus_wifi_radio_stats_payload_t *stats);
esp_err_t cerberus_wifi_defense_survey_start(void);
esp_err_t cerberus_wifi_defense_survey_stop(void);
bool cerberus_wifi_defense_survey_is_running(void);
void cerberus_wifi_defense_survey_get_status(cerberus_wifi_defense_status_payload_t *status);
esp_err_t cerberus_wifi_defense_survey_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_defense_result_payload_t *result);
esp_err_t cerberus_wifi_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_scan_result_payload_t *result);
esp_err_t cerberus_wifi_scanner_get_ssid_chunk(
    uint8_t generation,
    uint8_t index,
    uint8_t chunk,
    cerberus_wifi_scan_ssid_payload_t *result);
esp_err_t cerberus_wifi_connection_begin(const cerberus_wifi_connect_begin_request_t *request);
esp_err_t cerberus_wifi_connection_password(const cerberus_wifi_connect_password_request_t *request);
esp_err_t cerberus_wifi_connection_commit(void);
esp_err_t cerberus_wifi_connection_disconnect(void);
void cerberus_wifi_connection_get_status(cerberus_wifi_connection_status_payload_t *status);
void cerberus_wifi_connection_get_dns_status(cerberus_wifi_dns_status_payload_t *status);

#ifdef __cplusplus
}
#endif
