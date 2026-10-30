#pragma once

/* Presentation-only source ages for an immutable admitted BLE snapshot.
 * Caller must reject snapshots older than this window using a 64-bit monotonic
 * publication time. That is an expiry ceiling, never a replacement source time:
 * uint32_t source times alone cannot distinguish a complete timer revolution. */
#include "s3_ble_snapshot.h"

#include <stdbool.h>
#include <stdint.h>

#define CERBERUS_S3_BLE_RECENT_WINDOW_MS 30000U

static inline bool cerberus_s3_ble_recent_supported(cerberus_s3_ble_filter_t filter)
{
    return filter == CERBERUS_S3_BLE_FILTER_FIND_MY ||
        filter == CERBERUS_S3_BLE_FILTER_IBEACON;
}

/* Time zero is a real source time when validity and sequence establish that
 * it exists. The bounded unsigned difference handles a nearby timer wrap and
 * rejects future/rewound times, subject to the full-revolution ceiling above.
 * This does not reparse, merge reports, refresh evidence, or authorize actions. */
static inline bool cerberus_s3_ble_focus_recent(const cerberus_s3_ble_focus_t *focus,
    cerberus_s3_ble_filter_t filter, uint32_t now_ms)
{
    if (!focus) return false;
    if (filter == CERBERUS_S3_BLE_FILTER_IBEACON)
        return focus->ibeacon_valid && focus->ibeacon_sequence &&
            (uint32_t)(now_ms - focus->ibeacon_observed_ms) <= CERBERUS_S3_BLE_RECENT_WINDOW_MS;
    if (filter != CERBERUS_S3_BLE_FILTER_FIND_MY) return false;
    const cerberus_s3_ble_discovery_evidence_t *source = &focus->discovery;
    const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
    const uint8_t hints = CERBERUS_BLE_FINDMY_HINT_APPLE |
        CERBERUS_BLE_FINDMY_HINT_FMNA | CERBERUS_BLE_FINDMY_HINT_DULT;
    if ((source->flags & required) != required || !source->advertisement_sequence ||
        (source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID) ||
        ((source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) && source->sid > 15U)) return false;
    if ((source->findmy_advertisement.bits & hints) &&
        (uint32_t)(now_ms - source->advertisement_observed_ms) <= CERBERUS_S3_BLE_RECENT_WINDOW_MS)
        return true;
    return (source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) &&
        source->scan_response_sequence > source->advertisement_sequence &&
        (source->findmy_scan_response.bits & hints) &&
        (uint32_t)(now_ms - source->scan_response_observed_ms) <= CERBERUS_S3_BLE_RECENT_WINDOW_MS;
}
