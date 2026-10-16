#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_network_diagnostics_init(void);
esp_err_t cerberus_network_diagnostics_start(void);
void cerberus_network_diagnostics_get_status(
    cerberus_network_diagnostics_status_payload_t *status);

#ifdef __cplusplus
}
#endif
