#pragma once

/* Exact identifier/context pairs, never a vendor or device identity. The
 * complete bounded matrices remain in the frozen selection; this view shows
 * the first two pairs per source and explicitly counts the remainder. */
#include "ble_meta_hints.h"
#include <stdio.h>

static inline const char *cerberus_meta_context_label(cerberus_ble_meta_context_t context)
{
    switch (context) {
    case CERBERUS_BLE_META_MFG16: return "MFG16";
    case CERBERUS_BLE_META_LIST16: return "LIST16";
    case CERBERUS_BLE_META_LIST32: return "LIST32";
    case CERBERUS_BLE_META_LIST128: return "LIST128";
    case CERBERUS_BLE_META_DATA16: return "DATA16";
    case CERBERUS_BLE_META_DATA32: return "DATA32";
    case CERBERUS_BLE_META_DATA128: return "DATA128";
    default: return "?";
    }
}

static inline void cerberus_meta_hint_line(const cerberus_ble_meta_evidence_t *evidence,
    bool response, uint32_t age_ms, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    unsigned count = cerberus_ble_meta_evidence_count(evidence);
    const char *source = response ? "RSP" : "ADV";
    if (!count) {
        snprintf(out, capacity, "%s NO META IDENTIFIER PAIRS", source);
        return;
    }
    uint16_t first_id,second_id;
    cerberus_ble_meta_context_t first_context,second_context;
    if (!cerberus_ble_meta_evidence_at(evidence,0,&first_id,&first_context)) {
        snprintf(out, capacity, "%s IDENTIFIER EVIDENCE UNAVAILABLE", source);
        return;
    }
    char second[16] = "", remainder[16] = "";
    if (count > 1U && cerberus_ble_meta_evidence_at(evidence,1,&second_id,&second_context))
        snprintf(second,sizeof(second),", %04X %s",second_id,cerberus_meta_context_label(second_context));
    if (count > 2U) snprintf(remainder,sizeof(remainder)," +%u MORE",count-2U);
    snprintf(out,capacity,"%s %04X %s%s%s / %lus",source,first_id,
        cerberus_meta_context_label(first_context),second,remainder,
        (unsigned long)(age_ms/1000U));
}

static inline void cerberus_meta_hint_sources_line(
    const cerberus_ble_meta_evidence_t *advertisement,
    const cerberus_ble_meta_evidence_t *response, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    snprintf(out,capacity,"ID PAIRS / ADV %u / RSP %u",
        (unsigned)cerberus_ble_meta_evidence_count(advertisement),
        (unsigned)cerberus_ble_meta_evidence_count(response));
}
