#include "privacy.h"

#include <stdio.h>

static uint32_t identifier_hash(const uint8_t address[6], uint8_t domain)
{
    uint32_t hash = 2166136261UL;
    hash ^= domain;
    hash *= 16777619UL;
    for (size_t index = 0U; index < 6U; ++index) {
        hash ^= address[index];
        hash *= 16777619UL;
    }
    return hash;
}

void cerberus_privacy_format_wifi_identifier(
    const uint8_t address[6],
    bool masked,
    char *output,
    size_t output_size)
{
    if (output == NULL || output_size == 0U) {
        return;
    }
    if (address == NULL) {
        snprintf(output, output_size, "WIFI-UNKNOWN");
    } else if (masked) {
        snprintf(output, output_size, "WIFI-%08lX",
            (unsigned long)identifier_hash(address, 0x57U));
    } else {
        snprintf(output, output_size, "%02X:%02X:%02X:%02X:%02X:%02X",
            address[0], address[1], address[2], address[3], address[4], address[5]);
    }
}

void cerberus_privacy_format_ble_identifier(
    const uint8_t address[6],
    bool masked,
    char *output,
    size_t output_size)
{
    if (output == NULL || output_size == 0U) {
        return;
    }
    if (address == NULL) {
        snprintf(output, output_size, "BLE-UNKNOWN");
    } else if (masked) {
        snprintf(output, output_size, "BLE-%08lX",
            (unsigned long)identifier_hash(address, 0x42U));
    } else {
        snprintf(output, output_size, "%02X:%02X:%02X:%02X:%02X:%02X",
            address[5], address[4], address[3], address[2], address[1], address[0]);
    }
}
