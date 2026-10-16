#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_mdns_scanner_init(void);
esp_err_t cerberus_mdns_scanner_start(void);
void cerberus_mdns_scanner_get_status(cerberus_mdns_scan_status_payload_t *status);
esp_err_t cerberus_mdns_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_mdns_result_payload_t *result);

#ifdef __cplusplus
}
#endif
