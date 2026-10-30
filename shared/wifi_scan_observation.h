#pragma once

/* Receive-only evidence beside the SDK's AP inventory. The key identifies an
 * advertised tuple, not authenticated hardware. Caller serializes the bounded
 * table and carries the begin token across parsing outside its critical section.
 * No allocator, ESP API, radio changes or raw packet retention belongs here. */
#include "cerberus_protocol.h"

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_WIFI_OBSERVATION_CAPACITY 64U
#define CERBERUS_WIFI_OBSERVATION_MAX_FRAME 2304U

typedef struct {
    uint8_t bssid[6];
    uint8_t ssid[32];
    uint8_t ssid_length;
    uint8_t channel;
} cerberus_wifi_observation_key_t;

typedef struct {
    cerberus_wifi_observation_key_t key;
    uint8_t flags;
} cerberus_wifi_observation_frame_t;

typedef struct {
    cerberus_wifi_observation_key_t key;
    uint64_t observed_us;
    uint8_t flags;
    int8_t strongest_rssi; /* retention rank only, never the dated SDK value */
} cerberus_wifi_observation_entry_t;

enum {
    CERBERUS_WIFI_OBSERVATION_WAITING = 0,
    CERBERUS_WIFI_OBSERVATION_RECEIVING,
    CERBERUS_WIFI_OBSERVATION_FINISHED,
};

typedef struct {
    cerberus_wifi_observation_entry_t entries[CERBERUS_WIFI_OBSERVATION_CAPACITY];
    uint64_t epoch;
    uint32_t capacity_drops;
    uint32_t stale_updates;
    uint32_t capacity_replacements;
    uint8_t count;
    uint8_t phase;
} cerberus_wifi_observation_table_t;

static inline bool cerberus_wifi_observation_unicast(const uint8_t address[6])
{
    uint8_t any = 0U;
    for (unsigned i=0U; i<6U; ++i) any |= address[i];
    return any != 0U && (address[0] & 1U) == 0U;
}

static inline bool cerberus_wifi_observation_channel_valid(uint8_t channel)
{
    return (channel >= 1U && channel <= 14U) ||
        (channel >= 36U && channel <= 64U && channel % 4U == 0U) ||
        (channel >= 100U && channel <= 144U && channel % 4U == 0U) ||
        (channel >= 149U && channel <= 177U && (channel - 149U) % 4U == 0U);
}

/* Complete successful-RX frame excluding FCS. These are the ordinary fixed
 * management header formats; fragments/protected/DS frames are not proof.
 * The actual receiver channel is authoritative, never the advertised DS IE.
 * Validate the entire IE stream even after finding a matching SSID. */
static inline bool cerberus_wifi_observation_decode(const uint8_t *frame,
    size_t length, uint8_t channel, cerberus_wifi_observation_frame_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!frame || length < 38U || length > CERBERUS_WIFI_OBSERVATION_MAX_FRAME ||
        !cerberus_wifi_observation_channel_valid(channel) ||
        (frame[0] != 0x80U && frame[0] != 0x50U) ||
        (frame[1] & 0xc7U) != 0U || (frame[22] & 0x0fU) != 0U ||
        !cerberus_wifi_observation_unicast(frame+10U) ||
        !cerberus_wifi_observation_unicast(frame+16U)) return false;
    static const uint8_t broadcast[6] = {255U,255U,255U,255U,255U,255U};
    bool broadcast_destination = memcmp(frame+4U,broadcast,6U) == 0;
    if (frame[0] == 0x80U ? !broadcast_destination :
        (!broadcast_destination && !cerberus_wifi_observation_unicast(frame+4U))) return false;
    cerberus_wifi_observation_frame_t pending = {0};
    bool have_ssid=false, have_channel=false;
    for (size_t offset=36U; offset<length;) {
        if (length-offset < 2U) return false;
        uint8_t id=frame[offset], bytes=frame[offset+1U];
        offset += 2U;
        if ((size_t)bytes > length-offset) return false;
        if (id == 0U) {
            if (have_ssid || bytes > sizeof(pending.key.ssid)) return false;
            have_ssid=true;
            pending.key.ssid_length=bytes;
            memcpy(pending.key.ssid,frame+offset,bytes);
        } else if (id == 3U) {
            if (have_channel || bytes != 1U || frame[offset] == 0U) return false;
            have_channel=true;
        }
        offset += bytes;
    }
    if (!have_ssid) return false;
    memcpy(pending.key.bssid,frame+16U,6U);
    pending.key.channel=channel;
    pending.flags=CERBERUS_WIFI_OBSERVATION_VALID |
        (frame[0] == 0x80U ? CERBERUS_WIFI_OBSERVATION_BEACON : CERBERUS_WIFI_OBSERVATION_PROBE_RESPONSE);
    *out=pending;
    return true;
}

static inline bool cerberus_wifi_observation_key_equal(
    const cerberus_wifi_observation_key_t *left, const cerberus_wifi_observation_key_t *right)
{
    return left->channel == right->channel && left->ssid_length == right->ssid_length &&
        memcmp(left->bssid,right->bssid,6U) == 0 &&
        memcmp(left->ssid,right->ssid,left->ssid_length) == 0;
}

/* Advance even when the public eight-bit scan generation wraps. An exhausted
 * token cannot be reused; inventory may still operate with unavailable timing. */
static inline uint64_t cerberus_wifi_observation_begin(cerberus_wifi_observation_table_t *table)
{
    if (!table) return 0U;
    uint64_t epoch=table->epoch;
    memset(table,0,sizeof(*table));
    table->epoch=epoch == UINT64_MAX ? epoch : epoch+1U;
    if (epoch == UINT64_MAX) {
        table->phase=CERBERUS_WIFI_OBSERVATION_FINISHED;
        return 0U;
    }
    return table->epoch;
}

static inline bool cerberus_wifi_observation_arm(cerberus_wifi_observation_table_t *table, uint64_t epoch)
{
    if (!table || !epoch || epoch != table->epoch || table->phase != CERBERUS_WIFI_OBSERVATION_WAITING)
        return false;
    table->phase=CERBERUS_WIFI_OBSERVATION_RECEIVING;
    return true;
}

static inline void cerberus_wifi_observation_finish(cerberus_wifi_observation_table_t *table, bool keep)
{
    if (!table) return;
    table->phase=CERBERUS_WIFI_OBSERVATION_FINISHED;
    if (!keep) table->count=0U;
}

static inline bool cerberus_wifi_observation_commit(cerberus_wifi_observation_table_t *table,
    uint64_t epoch, const cerberus_wifi_observation_frame_t *frame, uint64_t observed_us, int rssi)
{
    if (!table || !frame || !epoch || epoch != table->epoch ||
        table->phase != CERBERUS_WIFI_OBSERVATION_RECEIVING ||
        table->count > CERBERUS_WIFI_OBSERVATION_CAPACITY ||
        frame->key.ssid_length > sizeof(frame->key.ssid) ||
        !cerberus_wifi_observation_unicast(frame->key.bssid) ||
        !cerberus_wifi_observation_channel_valid(frame->key.channel) ||
        (frame->flags != (CERBERUS_WIFI_OBSERVATION_VALID | CERBERUS_WIFI_OBSERVATION_BEACON) &&
         frame->flags != (CERBERUS_WIFI_OBSERVATION_VALID | CERBERUS_WIFI_OBSERVATION_PROBE_RESPONSE))) return false;
    uint8_t index=0U;
    while (index<table->count && !cerberus_wifi_observation_key_equal(&table->entries[index].key,&frame->key)) ++index;
    int8_t rank=rssi >= -127 && rssi <= 20 ? (int8_t)rssi : INT8_MIN;
    if (index == table->count) {
        if (index == CERBERUS_WIFI_OBSERVATION_CAPACITY) {
            /* SDK results are signal-ranked. Do not let a dense first band
             * permanently occupy all evidence slots before the next band.
             * Stable ties retain the older slot; missing evidence stays unknown. */
            uint8_t weakest=0U;
            for (uint8_t i=1U; i<table->count; ++i)
                if (table->entries[i].strongest_rssi < table->entries[weakest].strongest_rssi) weakest=i;
            if (rank <= table->entries[weakest].strongest_rssi) {
                if (table->capacity_drops != UINT32_MAX) ++table->capacity_drops;
                return false;
            }
            index=weakest;
            if (table->capacity_replacements != UINT32_MAX) ++table->capacity_replacements;
        } else {
            ++table->count;
        }
        table->entries[index].strongest_rssi=rank;
    } else if (observed_us < table->entries[index].observed_us) {
        if (table->stale_updates != UINT32_MAX) ++table->stale_updates;
        return false;
    } else if (rank > table->entries[index].strongest_rssi) {
        table->entries[index].strongest_rssi=rank;
    }
    table->entries[index].key=frame->key;
    table->entries[index].observed_us=observed_us;
    table->entries[index].flags=frame->flags;
    return true;
}

/* Fill only the new timing fields; normal SDK inventory fields stay untouched.
 * A missing tuple is unknown, including inferred/nontransmitted BSSIDs or SSIDs
 * whose exact byte length was lost by the SDK/string inventory representation.
 * Full64 subtraction precedes saturation; diagnostic low32 source_ms is never
 * used for age. A backward source clock invalidates evidence rather than wraps. */
static inline bool cerberus_wifi_observation_result(const cerberus_wifi_observation_table_t *table,
    const uint8_t bssid[6], const uint8_t *ssid, uint8_t ssid_length,
    uint8_t channel, uint64_t now_us, cerberus_wifi_scan_result_payload_t *out)
{
    if (!out) return false;
    out->observation_age_ms=out->observation_source_ms=0U;
    out->observation_flags=out->observation_channel=0U;
    if (!table || !bssid || (ssid_length && !ssid) || ssid_length > 32U ||
        table->phase != CERBERUS_WIFI_OBSERVATION_FINISHED ||
        table->count > CERBERUS_WIFI_OBSERVATION_CAPACITY) return false;
    cerberus_wifi_observation_key_t key={.ssid_length=ssid_length,.channel=channel};
    memcpy(key.bssid,bssid,6U);
    if (ssid_length) memcpy(key.ssid,ssid,ssid_length);
    for (uint8_t i=0U; i<table->count; ++i) {
        const cerberus_wifi_observation_entry_t *entry=&table->entries[i];
        if (!cerberus_wifi_observation_key_equal(&entry->key,&key)) continue;
        if (now_us < entry->observed_us) return false;
        uint64_t elapsed=now_us-entry->observed_us;
        uint64_t age=elapsed/1000U+(elapsed%1000U != 0U);
        out->observation_age_ms=age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
        out->observation_source_ms=(uint32_t)(entry->observed_us/1000U);
        out->observation_flags=entry->flags;
        out->observation_channel=entry->key.channel;
        return true;
    }
    return false;
}
