#pragma once

/* Compact completed-sweep evidence. Packet facts belong to their own current
 * original/matched response; an OUI is only typed-address context. No ESP,
 * allocation, radio operation or raw advertising bytes are required here. */
#include "ble_flock_hints.h"

enum {
    CERBERUS_S3_BLE_WARDRIVE_ADV_VALID = 1U << 0,
    CERBERUS_S3_BLE_WARDRIVE_RSP_VALID = 1U << 1,
    CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE = 1U << 2,
    CERBERUS_S3_BLE_WARDRIVE_SID_VALID = 1U << 3,
};

typedef struct {
    uint32_t advertisement_sequence, advertisement_observed_ms;
    uint32_t scan_response_sequence, scan_response_observed_ms;
    cerberus_ble_flock_hints_t advertisement, scan_response;
    uint8_t flags, sid, reserved[2];
} cerberus_s3_ble_wardrive_evidence_t;

_Static_assert(sizeof(cerberus_s3_ble_wardrive_evidence_t) == 28U,
    "Wardriving Flock evidence must remain compact");

static inline bool cerberus_s3_ble_wardrive_hints_valid(const cerberus_ble_flock_hints_t *h)
{
    if (!h || (h->facts & ~63U) ||
        (h->accessory_contexts & ~(CERBERUS_BLE_FLOCK_LIST128 | CERBERUS_BLE_FLOCK_DATA128)) ||
        (h->raven_3100_contexts & ~63U) || (h->raven_3200_contexts & ~63U)) return false;
    uint8_t name = h->facts & (CERBERUS_BLE_FLOCK_FS_BATTERY |
        CERBERUS_BLE_FLOCK_PENGUIN | CERBERUS_BLE_FLOCK_SERIAL10);
    if ((name && (name & (uint8_t)(name - 1U))) ||
        (name && (h->facts & CERBERUS_BLE_FLOCK_NO_LOCAL_NAME))) return false;
    return !(h->facts & CERBERUS_BLE_FLOCK_MFG_PAYLOAD) ||
        (h->facts & CERBERUS_BLE_FLOCK_MFG_09C8);
}

static inline bool cerberus_s3_ble_wardrive_hints_empty(const cerberus_ble_flock_hints_t *h)
{
    return !(h->facts | h->accessory_contexts | h->raven_3100_contexts | h->raven_3200_contexts);
}

/* Sequence, not a nonzero timestamp, establishes presence: time zero is valid.
 * The response window is the existing <=2-second same-original admission
 * window, including one uint32 timestamp rollover. These are observation times,
 * not report receipt or sweep completion times. */
static inline bool cerberus_s3_ble_wardrive_evidence_valid(
    const cerberus_s3_ble_wardrive_evidence_t *e)
{
    if (!e || (e->flags & ~15U) || e->reserved[0] || e->reserved[1] ||
        !cerberus_s3_ble_wardrive_hints_valid(&e->advertisement) ||
        !cerberus_s3_ble_wardrive_hints_valid(&e->scan_response)) return false;
    if (e->flags & CERBERUS_S3_BLE_WARDRIVE_ADV_VALID) {
        if (!e->advertisement_sequence) return false;
    } else if (e->advertisement_sequence || e->advertisement_observed_ms ||
        !cerberus_s3_ble_wardrive_hints_empty(&e->advertisement)) return false;
    if (e->flags & CERBERUS_S3_BLE_WARDRIVE_RSP_VALID) {
        if (!(e->flags & CERBERUS_S3_BLE_WARDRIVE_ADV_VALID) ||
            e->scan_response_sequence <= e->advertisement_sequence ||
            (uint32_t)(e->scan_response_observed_ms - e->advertisement_observed_ms) > 2000U)
            return false;
    } else if (e->scan_response_sequence || e->scan_response_observed_ms ||
        !cerberus_s3_ble_wardrive_hints_empty(&e->scan_response)) return false;
    if (e->flags & CERBERUS_S3_BLE_WARDRIVE_SID_VALID) {
        if (!(e->flags & CERBERUS_S3_BLE_WARDRIVE_ADV_VALID) || e->sid > 15U) return false;
    } else if (e->sid) return false;
    return true;
}

static inline cerberus_ble_flock_level_t cerberus_s3_ble_wardrive_evidence_level(
    const cerberus_s3_ble_wardrive_evidence_t *e)
{
    if (!cerberus_s3_ble_wardrive_evidence_valid(e)) return CERBERUS_BLE_FLOCK_LEVEL_NONE;
    cerberus_ble_flock_level_t level = (e->flags & CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE) ?
        CERBERUS_BLE_FLOCK_LEVEL_CLUE : CERBERUS_BLE_FLOCK_LEVEL_NONE;
    cerberus_ble_flock_level_t adv = cerberus_ble_flock_hints_level(&e->advertisement);
    cerberus_ble_flock_level_t rsp = cerberus_ble_flock_hints_level(&e->scan_response);
    if (adv > level) level = adv;
    if (rsp > level) level = rsp;
    return level; /* Never join facts from different reports. */
}

static inline uint32_t cerberus_s3_ble_wardrive_packet_key(const cerberus_ble_flock_hints_t *h)
{
    uint32_t accessory = ((h->accessory_contexts & CERBERUS_BLE_FLOCK_LIST128) ? 1U : 0U) |
        ((h->accessory_contexts & CERBERUS_BLE_FLOCK_DATA128) ? 2U : 0U);
    return h->facts | (accessory << 6U) | ((uint32_t)h->raven_3100_contexts << 8U) |
        ((uint32_t)h->raven_3200_contexts << 14U);
}

/* Exact canonical 41-bit key: ADV20 + RSP20 + OUI1. Source times, sequences
 * and SID do not turn identical clues into a new observation. Zero denotes
 * no qualifying clue. This is a dedupe key, never a device identity. */
static inline uint64_t cerberus_s3_ble_wardrive_evidence_key(
    const cerberus_s3_ble_wardrive_evidence_t *e)
{
    if (cerberus_s3_ble_wardrive_evidence_level(e) == CERBERUS_BLE_FLOCK_LEVEL_NONE) return 0U;
    return (uint64_t)cerberus_s3_ble_wardrive_packet_key(&e->advertisement) |
        ((uint64_t)cerberus_s3_ble_wardrive_packet_key(&e->scan_response) << 20U) |
        ((e->flags & CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE) ? (UINT64_C(1) << 40U) : 0U);
}
