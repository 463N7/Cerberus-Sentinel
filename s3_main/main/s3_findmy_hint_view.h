#pragma once

/* Display-only Find My format hints. These descriptions never select a Sound
 * profile, establish device identity, or change the original packet time. */
#include "ble_findmy_hints.h"
#include <stdio.h>

static inline const char *cerberus_findmy_hint_summary(uint8_t bits)
{
    switch (bits) {
    case 0U: return "NONE";
    case CERBERUS_BLE_FINDMY_HINT_APPLE: return "APPLE";
    case CERBERUS_BLE_FINDMY_HINT_FMNA: return "FMNA";
    case CERBERUS_BLE_FINDMY_HINT_DULT: return "DULT";
    default: return "MIXED";
    }
}

static inline void cerberus_findmy_hint_sources_line(
    const cerberus_ble_findmy_hints_t *advertisement,
    const cerberus_ble_findmy_hints_t *response, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    uint8_t adv = advertisement ? advertisement->bits : 0U;
    uint8_t rsp = response ? response->bits : 0U;
    /* List cards expose 33 characters; full profile/UUID evidence is shown
     * in the selected detail and Monitor without losing the second source. */
    snprintf(out, capacity, "ADV %s / RSP %s / HINTS",
        cerberus_findmy_hint_summary(adv), cerberus_findmy_hint_summary(rsp));
}

static inline void cerberus_findmy_hint_line(
    const cerberus_ble_findmy_hints_t *hints, bool response,
    uint32_t observed_ms, uint32_t now_ms, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    const char *source = response ? "RSP" : "ADV";
    if (!hints || !hints->bits) {
        snprintf(out, capacity, "%s NO %sFIND MY HINT", source,
            response ? "MATCHED " : "");
        return;
    }
    char profiles[40];
    snprintf(profiles, sizeof(profiles), "%s%s%s%s%s%s",
        hints->bits & CERBERUS_BLE_FINDMY_HINT_APPLE ? " APPLE" : "",
        hints->bits & CERBERUS_BLE_FINDMY_HINT_FMNA ? " FMNA" : "",
        hints->fmna_uuid_widths & CERBERUS_BLE_FINDMY_UUID16 ? "16" : "",
        hints->fmna_uuid_widths & CERBERUS_BLE_FINDMY_UUID32 ?
            (hints->fmna_uuid_widths & CERBERUS_BLE_FINDMY_UUID16 ? "+32" : "32") : "",
        hints->fmna_uuid_widths & CERBERUS_BLE_FINDMY_UUID128 ?
            (hints->fmna_uuid_widths & (CERBERUS_BLE_FINDMY_UUID16 | CERBERUS_BLE_FINDMY_UUID32) ? "+128" : "128") : "",
        hints->bits & CERBERUS_BLE_FINDMY_HINT_DULT ? " DULT" : "");
    uint32_t age_ms = (uint32_t)(now_ms - observed_ms);
    if (age_ms > INT32_MAX)
        snprintf(out, capacity, "%s HINT%s / AGE UNKNOWN", source, profiles);
    else
        snprintf(out, capacity, "%s HINT%s / %lus", source, profiles,
            (unsigned long)(age_ms / 1000U));
}
