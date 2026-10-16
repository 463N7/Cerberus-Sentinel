#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_ieee802154_scanner_init(void);
esp_err_t cerberus_ieee802154_scanner_start(void);
bool cerberus_ieee802154_scanner_is_running(void);
void cerberus_ieee802154_scanner_get_status(
    cerberus_ieee802154_energy_scan_status_payload_t *status);
esp_err_t cerberus_ieee802154_scanner_get_result(
    uint8_t generation,
    cerberus_ieee802154_energy_scan_result_payload_t *result);
esp_err_t cerberus_ieee802154_beacon_scanner_start(void);
void cerberus_ieee802154_beacon_scanner_get_status(
    cerberus_ieee802154_beacon_scan_status_payload_t *status);
esp_err_t cerberus_ieee802154_beacon_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ieee802154_beacon_scan_result_payload_t *result);
esp_err_t cerberus_ieee802154_activity_scanner_start(void);
void cerberus_ieee802154_activity_scanner_get_status(
    cerberus_ieee802154_activity_scan_status_payload_t *status);
esp_err_t cerberus_ieee802154_activity_scanner_get_result(
    uint8_t generation,
    uint8_t result_index,
    cerberus_ieee802154_activity_scan_result_payload_t *result);

#ifdef __cplusplus
}
#endif
