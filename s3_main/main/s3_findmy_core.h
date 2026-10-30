#pragma once

/* Pure, bounded Find My advertisement recognition and Beep All scheduling.
 * One entry represents one observed address and address type, never a
 * persistent physical tag identity. No BLE host or platform calls occur here. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_FINDMY_MAX_CANDIDATES 32U
#define CERBERUS_FINDMY_MAX_AD_LENGTH 255U
#define CERBERUS_FINDMY_STALE_MS 120000U
#define CERBERUS_FINDMY_ATTEMPT_TIMEOUT_MS 65000U
#define CERBERUS_FINDMY_SUCCESS_COOLDOWN_MS 30000U
#define CERBERUS_FINDMY_FAILURE_COOLDOWN_MS 5000U
#define CERBERUS_FINDMY_MAX_COOLDOWN_MS 60000U

typedef enum {
    CERBERUS_FINDMY_PROFILE_NONE = 0,
    CERBERUS_FINDMY_PROFILE_AIRTAG,
    CERBERUS_FINDMY_PROFILE_FMNA,
    CERBERUS_FINDMY_PROFILE_DULT,
} cerberus_findmy_profile_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    cerberus_findmy_profile_t profile;
    int8_t rssi;
    uint32_t last_seen_ms;
    uint32_t last_seen_sweep;
    uint32_t last_attempt_ms;
    uint32_t cooldown_ms;
    uint8_t failures;
    bool used;
    bool attempted;
} cerberus_findmy_candidate_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    cerberus_findmy_profile_t profile;
    int8_t rssi;
    uint32_t last_seen_ms;
    uint32_t ticket;
} cerberus_findmy_selection_t;

typedef struct {
    cerberus_findmy_candidate_t candidates[CERBERUS_FINDMY_MAX_CANDIDATES];
    uint32_t ticket;
    uint32_t sweep_id;
    uint32_t in_flight_since_ms;
    uint8_t count;
    uint8_t cursor;
    uint8_t in_flight_index;
    bool active;
    bool sweep_open;
    bool issued_this_sweep;
    bool in_flight;
} cerberus_findmy_queue_t;

/* Only a complete advertisement may establish a sound profile. Incomplete
 * extended reports and scan responses cannot be strengthened by cached data. */
static inline cerberus_findmy_profile_t cerberus_findmy_classify_ad(
    const uint8_t *data, size_t length, bool complete, bool scan_response)
{
    static const uint8_t dult_uuid_le[16] = {
        0x85U, 0x2AU, 0x9FU, 0x57U, 0xC5U, 0x2AU, 0xEDU, 0x88U,
        0x26U, 0xC2U, 0xF4U, 0x12U, 0x01U, 0x00U, 0x19U, 0x15U,
    };
    if (!complete || scan_response || !data || !length ||
        length > CERBERUS_FINDMY_MAX_AD_LENGTH) return CERBERUS_FINDMY_PROFILE_NONE;
    bool airtag = false, fmna = false, dult = false;
    for (size_t offset = 0U; offset < length;) {
        const uint8_t field_length = data[offset++];
        if (!field_length) {
            while (offset < length) if (data[offset++] != 0U)
                return CERBERUS_FINDMY_PROFILE_NONE;
            break;
        }
        if (field_length > length - offset)
            return CERBERUS_FINDMY_PROFILE_NONE;
        const uint8_t type = data[offset];
        const uint8_t *value = data + offset + 1U;
        const size_t value_length = (size_t)field_length - 1U;
        if (type == 0xFFU) {
            if (value_length < 2U) return CERBERUS_FINDMY_PROFILE_NONE;
            /* Apple 004C, type 12, length 19, with the full 25-byte frame.
             * This is a broadcast format hint, not proof of an AirTag. */
            if (value_length == 29U && value[0] == 0x4CU &&
                value[1] == 0x00U && value[2] == 0x12U && value[3] == 0x19U)
                airtag = true;
        } else if (type == 0x02U || type == 0x03U) {
            if (value_length % 2U) return CERBERUS_FINDMY_PROFILE_NONE;
            for (size_t i = 0U; i < value_length; i += 2U)
                if (value[i] == 0x44U && value[i + 1U] == 0xFDU) fmna = true;
        } else if (type == 0x06U || type == 0x07U) {
            if (value_length % 16U) return CERBERUS_FINDMY_PROFILE_NONE;
            for (size_t i = 0U; i < value_length; i += 16U)
                if (memcmp(value + i, dult_uuid_le, 16U) == 0) dult = true;
        } else if (type == 0x16U && value_length < 2U) {
            return CERBERUS_FINDMY_PROFILE_NONE;
        } else if (type == 0x21U && value_length < 16U) {
            return CERBERUS_FINDMY_PROFILE_NONE;
        }
        offset += field_length;
    }
    /* A mixed-profile packet is unsuitable for unattended GATT writes. */
    if ((unsigned)airtag + (unsigned)fmna + (unsigned)dult != 1U)
        return CERBERUS_FINDMY_PROFILE_NONE;
    return airtag ? CERBERUS_FINDMY_PROFILE_AIRTAG :
        fmna ? CERBERUS_FINDMY_PROFILE_FMNA : CERBERUS_FINDMY_PROFILE_DULT;
}

static inline void cerberus_findmy_queue_init(cerberus_findmy_queue_t *queue)
{
    if (queue) memset(queue, 0, sizeof(*queue));
}

static inline void cerberus_findmy_queue_start(cerberus_findmy_queue_t *queue)
{
    if (!queue) return;
    uint32_t ticket = queue->ticket + 1U;
    memset(queue, 0, sizeof(*queue));
    queue->ticket = ticket;
    queue->cursor = CERBERUS_FINDMY_MAX_CANDIDATES - 1U;
    queue->active = true;
}

static inline void cerberus_findmy_queue_stop(cerberus_findmy_queue_t *queue)
{
    if (!queue) return;
    uint32_t ticket = queue->ticket + 1U;
    memset(queue, 0, sizeof(*queue));
    queue->ticket = ticket;
}

static inline bool cerberus_findmy_queue_finish_attempt(
    cerberus_findmy_queue_t *queue, uint32_t ticket, bool success,
    uint32_t now_ms)
{
    if (!queue || !queue->active || !queue->in_flight ||
        queue->ticket != ticket) return false;
    cerberus_findmy_candidate_t *candidate =
        &queue->candidates[queue->in_flight_index];
    if (candidate->used) {
        candidate->attempted = true;
        candidate->last_attempt_ms = now_ms;
        if (success) {
            candidate->failures = 0U;
            candidate->cooldown_ms = CERBERUS_FINDMY_SUCCESS_COOLDOWN_MS;
        } else {
            if (candidate->failures < 5U) ++candidate->failures;
            uint32_t cooldown = CERBERUS_FINDMY_FAILURE_COOLDOWN_MS <<
                (candidate->failures - 1U);
            candidate->cooldown_ms = cooldown > CERBERUS_FINDMY_MAX_COOLDOWN_MS ?
                CERBERUS_FINDMY_MAX_COOLDOWN_MS : cooldown;
        }
    }
    queue->in_flight = false;
    return true;
}

/* Call exactly once when a new bounded BLE sweep starts. A lost asynchronous
 * result times out, so no candidate can hold the queue indefinitely. */
static inline void cerberus_findmy_queue_begin_sweep(
    cerberus_findmy_queue_t *queue, uint32_t now_ms)
{
    if (!queue || !queue->active) return;
    if (queue->in_flight && (uint32_t)(now_ms - queue->in_flight_since_ms) >=
        CERBERUS_FINDMY_ATTEMPT_TIMEOUT_MS)
        (void)cerberus_findmy_queue_finish_attempt(queue, queue->ticket,
            false, now_ms);
    for (size_t i = 0U; i < CERBERUS_FINDMY_MAX_CANDIDATES; ++i) {
        cerberus_findmy_candidate_t *candidate = &queue->candidates[i];
        if (candidate->used && (!queue->in_flight || i != queue->in_flight_index) &&
            (uint32_t)(now_ms - candidate->last_seen_ms) >= CERBERUS_FINDMY_STALE_MS) {
            memset(candidate, 0, sizeof(*candidate));
            --queue->count;
        }
    }
    queue->sweep_open = true;
    ++queue->sweep_id;
    if (!queue->sweep_id) ++queue->sweep_id;
    queue->issued_this_sweep = false;
}

static inline bool cerberus_findmy_queue_observe(
    cerberus_findmy_queue_t *queue, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, const uint8_t *data, size_t length,
    bool complete, bool scan_response, uint32_t now_ms)
{
    if (!queue || !queue->active || !queue->sweep_open || !address ||
        address_type > 3U) return false;
    cerberus_findmy_profile_t profile = cerberus_findmy_classify_ad(
        data, length, complete, scan_response);
    if (profile == CERBERUS_FINDMY_PROFILE_NONE) return false;
    size_t empty = CERBERUS_FINDMY_MAX_CANDIDATES;
    size_t oldest = CERBERUS_FINDMY_MAX_CANDIDATES;
    uint32_t oldest_age = 0U;
    for (size_t i = 0U; i < CERBERUS_FINDMY_MAX_CANDIDATES; ++i) {
        cerberus_findmy_candidate_t *candidate = &queue->candidates[i];
        if (candidate->used && candidate->address_type == address_type &&
            memcmp(candidate->address, address, 6U) == 0) {
            /* Keep the in-flight profile stable until its result arrives. */
            if (!queue->in_flight || i != queue->in_flight_index)
                candidate->profile = profile;
            candidate->rssi = rssi;
            candidate->last_seen_ms = now_ms;
            candidate->last_seen_sweep = queue->sweep_id;
            return true;
        }
        if (!candidate->used) {
            if (empty == CERBERUS_FINDMY_MAX_CANDIDATES) empty = i;
        } else if ((!queue->in_flight || i != queue->in_flight_index) &&
            (oldest == CERBERUS_FINDMY_MAX_CANDIDATES ||
             (uint32_t)(now_ms - candidate->last_seen_ms) > oldest_age)) {
            oldest_age = (uint32_t)(now_ms - candidate->last_seen_ms);
            oldest = i;
        }
    }
    size_t slot = empty != CERBERUS_FINDMY_MAX_CANDIDATES ? empty : oldest;
    if (slot == CERBERUS_FINDMY_MAX_CANDIDATES) return false;
    cerberus_findmy_candidate_t *candidate = &queue->candidates[slot];
    if (!candidate->used) ++queue->count;
    memset(candidate, 0, sizeof(*candidate));
    candidate->used = true;
    memcpy(candidate->address, address, 6U);
    candidate->address_type = address_type;
    candidate->profile = profile;
    candidate->rssi = rssi;
    candidate->last_seen_ms = now_ms;
    candidate->last_seen_sweep = queue->sweep_id;
    return true;
}

/* Claim consumes this sweep's single opportunity, even if the GATT action
 * later fails. The caller must finish the returned ticket or Stop the queue. */
static inline bool cerberus_findmy_queue_claim_next(
    cerberus_findmy_queue_t *queue, uint32_t now_ms,
    cerberus_findmy_selection_t *selection)
{
    if (!queue || !selection || !queue->active || !queue->sweep_open ||
        queue->issued_this_sweep || queue->in_flight) return false;
    for (size_t step = 1U; step <= CERBERUS_FINDMY_MAX_CANDIDATES; ++step) {
        size_t index = ((size_t)queue->cursor + step) % CERBERUS_FINDMY_MAX_CANDIDATES;
        cerberus_findmy_candidate_t *candidate = &queue->candidates[index];
        if (!candidate->used || candidate->last_seen_sweep != queue->sweep_id ||
            (uint32_t)(now_ms - candidate->last_seen_ms) >= CERBERUS_FINDMY_STALE_MS ||
            (candidate->attempted &&
             (uint32_t)(now_ms - candidate->last_attempt_ms) < candidate->cooldown_ms))
            continue;
        queue->cursor = (uint8_t)index;
        queue->issued_this_sweep = true;
        queue->in_flight = true;
        queue->in_flight_index = (uint8_t)index;
        queue->in_flight_since_ms = now_ms;
        ++queue->ticket;
        if (!queue->ticket) ++queue->ticket;
        memcpy(selection->address, candidate->address, 6U);
        selection->address_type = candidate->address_type;
        selection->profile = candidate->profile;
        selection->rssi = candidate->rssi;
        selection->last_seen_ms = candidate->last_seen_ms;
        selection->ticket = queue->ticket;
        return true;
    }
    return false;
}
