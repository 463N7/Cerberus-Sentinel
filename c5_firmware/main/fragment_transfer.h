#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_c5_fragment_open(
    uint8_t object_type,
    const void *data,
    uint16_t length,
    cerberus_fragment_info_payload_t *info);
esp_err_t cerberus_c5_fragment_read(
    const cerberus_fragment_read_request_t *request,
    cerberus_fragment_read_payload_t *payload);

#ifdef __cplusplus
}
#endif
