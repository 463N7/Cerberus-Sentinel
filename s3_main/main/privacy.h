#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void cerberus_privacy_format_wifi_identifier(
    const uint8_t address[6],
    bool masked,
    char *output,
    size_t output_size);

void cerberus_privacy_format_ble_identifier(
    const uint8_t address[6],
    bool masked,
    char *output,
    size_t output_size);

#ifdef __cplusplus
}
#endif
