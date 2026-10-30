#pragma once

/* Exact advertised local names remain unauthenticated source-local hints. */
#include "ble_hc_hints.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

static inline void cerberus_hc_hint_line(uint8_t code, bool response,
    uint32_t age_ms, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    const char *source = response ? "RSP" : "ADV";
    if (code != CERBERUS_BLE_HC_03 && code != CERBERUS_BLE_HC_05 && code != CERBERUS_BLE_HC_06) {
        snprintf(out, capacity, "%s NO EXACT HC NAME", source);
        return;
    }
    snprintf(out, capacity, "%s NAME %s / %lus", source,
        cerberus_ble_hc_name_label(code), (unsigned long)(age_ms / 1000U));
}

static inline void cerberus_hc_sources_line(uint8_t advertisement, uint8_t response,
    char *out, size_t capacity)
{
    if (!out || !capacity) return;
    snprintf(out, capacity, "ADV %s / RSP %s / HINTS",
        cerberus_ble_hc_name_label(advertisement), cerberus_ble_hc_name_label(response));
}
