#pragma once

/* Exact unauthenticated local-name codes, not hardware or skimmer identity.
 * Parsing and complete-report validation remain in ble_ad_hints.h. */
#include <stdint.h>

enum {
    CERBERUS_BLE_HC_NONE = 0U,
    CERBERUS_BLE_HC_03 = 3U,
    CERBERUS_BLE_HC_05 = 5U,
    CERBERUS_BLE_HC_06 = 6U,
};

static inline const char *cerberus_ble_hc_name_label(uint8_t code)
{
    switch (code) {
    case CERBERUS_BLE_HC_NONE: return "NONE";
    case CERBERUS_BLE_HC_03: return "HC-03";
    case CERBERUS_BLE_HC_05: return "HC-05";
    case CERBERUS_BLE_HC_06: return "HC-06";
    default: return "UNKNOWN";
    }
}
