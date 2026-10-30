#pragma once

/* Bounded, allocation-free observation cache. The UI owns this object in
 * PSRAM and serializes access; records represent addresses, never physical
 * tracker identity. Selection copies a record before another sweep starts. */
#include "c5_spi_link.h"
#include "s3_ble_snapshot.h"
#include "s3_findmy_core.h"
#include "s3_findmy_ui_route.h"
#include "s3_findmy_hint_view.h"

#define CERBERUS_FINDMY_MONITOR_CAPACITY 32U
#define CERBERUS_FINDMY_MONITOR_EXPIRE_MS 120000U

typedef struct {
    cerberus_c5_ble_result_t peer;
    cerberus_c5_ble_advertisement_t advertisement;
    cerberus_s3_ble_focus_t focus;
    uint8_t generation;
    uint8_t index;
    uint32_t observed_ms;
} cerberus_findmy_monitor_record_t;

typedef struct {
    cerberus_findmy_monitor_record_t records[CERBERUS_FINDMY_MONITOR_CAPACITY];
    uint8_t count;
    uint32_t replacements;
} cerberus_findmy_monitor_t;

static inline void cerberus_findmy_monitor_init(cerberus_findmy_monitor_t *cache)
{
    if (cache) memset(cache, 0, sizeof(*cache));
}

static inline uint32_t cerberus_findmy_monitor_record_age_ms(
    const cerberus_findmy_monitor_record_t *record, uint32_t now_ms)
{
    return record ? (uint32_t)(now_ms - record->observed_ms) : UINT32_MAX;
}

static inline bool cerberus_findmy_monitor_record_fresh(
    const cerberus_findmy_monitor_record_t *record, uint32_t now_ms)
{
    return record && cerberus_findmy_ui_fresh(record->observed_ms, now_ms);
}

/* Passive hint ages are independent of raw ADV freshness used by Sound. */
static inline uint32_t cerberus_findmy_monitor_hint_age_ms(
    const cerberus_findmy_monitor_record_t *record, uint32_t now_ms)
{
    if (!record) return UINT32_MAX;
    const cerberus_s3_ble_discovery_evidence_t *hints = &record->focus.discovery;
    uint32_t adv = hints->findmy_advertisement.bits ?
        (uint32_t)(now_ms - hints->advertisement_observed_ms) : UINT32_MAX;
    uint32_t rsp = hints->findmy_scan_response.bits ?
        (uint32_t)(now_ms - hints->scan_response_observed_ms) : UINT32_MAX;
    return adv < rsp ? adv : rsp;
}

static inline size_t cerberus_findmy_monitor_expire(
    cerberus_findmy_monitor_t *cache, uint32_t now_ms)
{
    if (!cache) return 0U;
    size_t kept = 0U;
    size_t removed = 0U;
    for (size_t i = 0U; i < cache->count; ++i) {
        if (cerberus_findmy_monitor_hint_age_ms(&cache->records[i], now_ms) >=
                CERBERUS_FINDMY_MONITOR_EXPIRE_MS) {
            ++removed;
            continue;
        }
        if (kept != i) cache->records[kept] = cache->records[i];
        ++kept;
    }
    if (removed) memset(cache->records + kept, 0,
        removed * sizeof(cache->records[0]));
    cache->count = (uint8_t)kept;
    return removed;
}

static inline bool cerberus_findmy_monitor_hint_valid(
    const cerberus_ble_findmy_hints_t *hint)
{
    const uint8_t known = CERBERUS_BLE_FINDMY_HINT_APPLE |
        CERBERUS_BLE_FINDMY_HINT_FMNA | CERBERUS_BLE_FINDMY_HINT_DULT;
    const uint8_t widths = CERBERUS_BLE_FINDMY_UUID16 |
        CERBERUS_BLE_FINDMY_UUID32 | CERBERUS_BLE_FINDMY_UUID128;
    return !(hint->bits & (uint8_t)~known) &&
        !(hint->fmna_uuid_widths & (uint8_t)~widths) &&
        ((hint->bits & CERBERUS_BLE_FINDMY_HINT_FMNA) != 0U) ==
            (hint->fmna_uuid_widths != 0U);
}

/* Validate current report provenance, never the active Sound classifier.
 * A complete source may exceed the bounded raw copy. In that case only the
 * local snapshot's full-source validation permits observational hints. */
static inline bool cerberus_findmy_monitor_evidence_valid(
    const cerberus_c5_ble_result_t *peer,
    const cerberus_c5_ble_advertisement_t *raw,
    const cerberus_s3_ble_focus_t *focus, uint8_t generation,
    uint32_t observed_ms, uint32_t now_ms)
{
    if (!peer || !raw || !focus || peer->address_type > 3U ||
        raw->address_type != peer->address_type ||
        memcmp(raw->address, peer->address, sizeof(raw->address)) != 0 ||
        raw->event_type > 3U ||
        raw->length > sizeof(raw->data) || peer->uuid_length > sizeof(peer->uuid) ||
        !memchr(peer->name, '\0', sizeof(peer->name))) return false;
    const cerberus_s3_ble_discovery_evidence_t *hints = &focus->discovery;
    const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
    const uint8_t allowed = required | CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID | CERBERUS_S3_BLE_EVIDENCE_SID_VALID;
    if ((hints->flags & required) != required || (hints->flags & (uint8_t)~allowed) ||
        hints->generation != generation || hints->address_type != peer->address_type ||
        memcmp(hints->address, peer->address, sizeof(hints->address)) != 0 ||
        !hints->advertisement_sequence || hints->advertisement_observed_ms != observed_ms ||
        (uint32_t)(now_ms - observed_ms) > INT32_MAX ||
        ((hints->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) && hints->sid > 15U) ||
        !cerberus_findmy_monitor_hint_valid(&hints->findmy_advertisement) ||
        !cerberus_findmy_monitor_hint_valid(&hints->findmy_scan_response) ||
        !(hints->findmy_advertisement.bits | hints->findmy_scan_response.bits)) return false;
    if (hints->flags & CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE) {
        cerberus_ble_findmy_hints_t parsed;
        if (raw->flags || !cerberus_ble_findmy_hints_parse(raw->data, raw->length, &parsed) ||
            memcmp(&parsed, &hints->findmy_advertisement, sizeof(parsed)) != 0) return false;
    } else if (raw->flags != CERBERUS_BLE_ADVERTISEMENT_TRUNCATED ||
               raw->length != sizeof(raw->data)) return false;
    if (hints->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) {
        if (hints->scan_response_sequence <= hints->advertisement_sequence ||
            (uint32_t)(hints->scan_response_observed_ms - observed_ms) > 2000U ||
            (uint32_t)(now_ms - hints->scan_response_observed_ms) > INT32_MAX)
            return false;
    } else if (hints->findmy_scan_response.bits || hints->scan_response_sequence ||
               hints->scan_response_observed_ms) {
        return false;
    }
    return true;
}

static inline bool cerberus_findmy_monitor_ingest(
    cerberus_findmy_monitor_t *cache, const cerberus_c5_ble_result_t *peer,
    const cerberus_c5_ble_advertisement_t *raw,
    const cerberus_s3_ble_focus_t *focus, uint8_t generation, uint8_t index,
    uint32_t observed_ms, uint32_t now_ms)
{
    if (!cache || index >= CERBERUS_BLE_SCAN_MAX_RESULTS ||
        !cerberus_findmy_monitor_evidence_valid(peer, raw, focus, generation,
            observed_ms, now_ms))
        return false;
    /* Freeze inputs before expiry compacts storage, even if the caller passes
     * a cache record back through this API. */
    cerberus_findmy_monitor_record_t incoming = {
        .peer = *peer, .advertisement = *raw, .focus = *focus,
        .generation = generation, .index = index, .observed_ms = observed_ms,
    };
    if (cerberus_findmy_monitor_hint_age_ms(&incoming, now_ms) >=
        CERBERUS_FINDMY_MONITOR_EXPIRE_MS) return false;
    incoming.focus.category_bits |= CERBERUS_S3_BLE_FOCUS_FIND_MY;
    (void)cerberus_findmy_monitor_expire(cache, now_ms);
    size_t slot = cache->count;
    for (size_t i = 0U; i < cache->count; ++i) {
        const cerberus_findmy_monitor_record_t *record = &cache->records[i];
        if (record->peer.address_type == incoming.peer.address_type &&
            memcmp(record->peer.address, incoming.peer.address,
                sizeof(incoming.peer.address)) == 0) {
            /* Late delivery cannot replace a newer rotating advertisement. */
            if ((int32_t)(observed_ms - record->observed_ms) < 0) return false;
            if (generation == record->generation && observed_ms == record->observed_ms) {
                const cerberus_s3_ble_discovery_evidence_t *old = &record->focus.discovery;
                const cerberus_s3_ble_discovery_evidence_t *next = &incoming.focus.discovery;
                if (next->advertisement_sequence < old->advertisement_sequence)
                    return false;
                if (next->advertisement_sequence == old->advertisement_sequence &&
                    (next->scan_response_sequence < old->scan_response_sequence ||
                     (old->scan_response_sequence &&
                      (int32_t)(next->scan_response_observed_ms - old->scan_response_observed_ms) < 0)))
                    return false;
            }
            slot = i;
            break;
        }
    }
    if (slot == CERBERUS_FINDMY_MONITOR_CAPACITY) {
        slot = 0U;
        for (size_t i = 1U; i < cache->count; ++i)
            if (cerberus_findmy_monitor_hint_age_ms(&cache->records[i], now_ms) >
                cerberus_findmy_monitor_hint_age_ms(&cache->records[slot], now_ms))
                slot = i;
        if (cache->replacements != UINT32_MAX) ++cache->replacements;
    } else if (slot == cache->count) {
        ++cache->count;
    }
    cache->records[slot] = incoming;
    return true;
}

/* The caller owns this value after return; later sweeps/expiry cannot mutate
 * the selected raw address, generation, timestamp, or advertisement. */
static inline bool cerberus_findmy_monitor_copy(
    const cerberus_findmy_monitor_t *cache, size_t slot,
    cerberus_findmy_monitor_record_t *out)
{
    if (!cache || !out || slot >= cache->count) return false;
    *out = cache->records[slot];
    return true;
}
