#pragma once

/* Advertised service codes and same-report name corroboration are format
 * hints. They do not confirm a physical Flipper, its color, or its identity. */
#include "ble_flipper_hints.h"
#include <stdio.h>

static inline const char *cerberus_flipper_hint_summary(uint8_t bits)
{
    switch (bits) {
    case 0U: return "NONE";
    case CERBERUS_BLE_FLIPPER_HINT_BASE: return "3080";
    case CERBERUS_BLE_FLIPPER_HINT_BLACK: return "3081";
    case CERBERUS_BLE_FLIPPER_HINT_WHITE: return "3082";
    case CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT: return "3083";
    default: return "MIXED";
    }
}

static inline void cerberus_flipper_hint_sources_line(
    const cerberus_ble_flipper_hints_t *advertisement,
    const cerberus_ble_flipper_hints_t *response, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    snprintf(out, capacity, "ADV %s / RSP %s / HINTS",
        cerberus_flipper_hint_summary(advertisement ? advertisement->bits : 0U),
        cerberus_flipper_hint_summary(response ? response->bits : 0U));
}

static inline void cerberus_flipper_hint_line(
    const cerberus_ble_flipper_hints_t *hints, bool response,
    uint32_t observed_ms, uint32_t now_ms, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    const char *source = response ? "RSP" : "ADV";
    uint8_t bits = hints ? hints->bits : 0U;
    if (!bits) {
        snprintf(out, capacity, "%s %s", source,
            hints && (hints->flags & CERBERUS_BLE_FLIPPER_NAME) ?
                "NAME ONLY / NO UUID HINT" : response ?
                "NO MATCHED FLIPPER UUID HINT" : "NO FLIPPER UUID HINT");
        return;
    }
    char codes[32];
    const char *description;
    switch (bits) {
    case CERBERUS_BLE_FLIPPER_HINT_BASE: description = "3080 BASE CODE"; break;
    case CERBERUS_BLE_FLIPPER_HINT_BLACK: description = "3081 BLACK CODE"; break;
    case CERBERUS_BLE_FLIPPER_HINT_WHITE: description = "3082 WHITE CODE"; break;
    case CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT: description = "3083 TRANSPARENT CODE"; break;
    default:
        snprintf(codes, sizeof(codes), "%s%s%s%s CODES",
            bits & CERBERUS_BLE_FLIPPER_HINT_BASE ? "3080" : "",
            bits & CERBERUS_BLE_FLIPPER_HINT_BLACK ?
                (bits & CERBERUS_BLE_FLIPPER_HINT_BASE ? "/81" : "3081") : "",
            bits & CERBERUS_BLE_FLIPPER_HINT_WHITE ?
                (bits & (CERBERUS_BLE_FLIPPER_HINT_BASE | CERBERUS_BLE_FLIPPER_HINT_BLACK) ? "/82" : "3082") : "",
            bits & CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT ?
                (bits & (CERBERUS_BLE_FLIPPER_HINT_BASE | CERBERUS_BLE_FLIPPER_HINT_BLACK | CERBERUS_BLE_FLIPPER_HINT_WHITE) ? "/83" : "3083") : "");
        description = codes;
        break;
    }
    uint32_t age_ms = (uint32_t)(now_ms - observed_ms);
    const char *name = hints->flags & CERBERUS_BLE_FLIPPER_NAME ? "+NAME" : "";
    const char *data = hints->flags & CERBERUS_BLE_FLIPPER_SERVICE_DATA ? "+DATA" : "";
    if (age_ms > INT32_MAX)
        snprintf(out, capacity, "%s HINT %s%s%s / AGE ?", source, description, name, data);
    else
        snprintf(out, capacity, "%s HINT %s%s%s / %lus", source, description, name, data,
            (unsigned long)(age_ms / 1000U));
}
