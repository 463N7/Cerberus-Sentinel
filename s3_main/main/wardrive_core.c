#include "wardrive_core.h"

#include <stdio.h>
#include <string.h>

static bool same_identity(const cerberus_wardrive_dedupe_entry_t *entry,
    const cerberus_wardrive_observation_t *observation)
{
    return entry->occupied && entry->type == observation->type &&
        entry->address_type == (observation->type == CERBERUS_WARDRIVE_BLE ?
            observation->address_type : 0U) &&
        memcmp(entry->address, observation->address, sizeof(entry->address)) == 0;
}

void cerberus_wardrive_dedupe_reset(cerberus_wardrive_dedupe_t *state)
{
    if (state != NULL) memset(state, 0, sizeof(*state));
}

bool cerberus_wardrive_observation_valid(const cerberus_wardrive_observation_t *observation)
{
    if (observation == NULL ||
        (observation->type != CERBERUS_WARDRIVE_WIFI &&
         observation->type != CERBERUS_WARDRIVE_BLE &&
         observation->type != CERBERUS_WARDRIVE_WIFI_PROBE &&
         observation->type != CERBERUS_WARDRIVE_WIFI_RELATION) ||
        observation->radio > CERBERUS_WARDRIVE_RADIO_R6 ||
        observation->label_length > CERBERUS_WARDRIVE_LABEL_CAPACITY)
        return false;
    if (observation->type == CERBERUS_WARDRIVE_WIFI_RELATION) {
        const cerberus_wardrive_relation_evidence_t *e = &observation->relation_evidence;
        if (observation->radio != CERBERUS_WARDRIVE_RADIO_C5 || observation->address_type ||
            observation->security || observation->manufacturer_id || observation->label_length ||
            !cerberus_wifi_relation_mac_valid(observation->address) ||
            !cerberus_wifi_relation_mac_valid(e->bssid) ||
            !memcmp(observation->address, e->bssid, sizeof(e->bssid)) ||
            !cerberus_wifi_relation_channel_valid(observation->channel) ||
            (e->latest_direction != CERBERUS_WIFI_RELATION_TO_AP &&
             e->latest_direction != CERBERUS_WIFI_RELATION_FROM_AP) ||
            !(e->latest_direction == CERBERUS_WIFI_RELATION_TO_AP ? e->to_ap_frames : e->from_ap_frames) ||
            e->rssi_valid > 1U || cerberus_wardrive_relation_callback_us(e) > INT64_MAX)
            return false;
        for (size_t i = 0U; i < sizeof(observation->label); ++i)
            if (observation->label[i]) return false;
        for (size_t i = 0U; i < sizeof(e->reserved); ++i)
            if (e->reserved[i]) return false;
        return e->rssi_valid ? cerberus_wifi_relation_rssi_valid(observation->rssi_dbm) :
            observation->rssi_dbm == CERBERUS_WIFI_RELATION_RSSI_UNAVAILABLE;
    }
    if (observation->type == CERBERUS_WARDRIVE_WIFI_PROBE) {
        const cerberus_wardrive_probe_evidence_t *evidence = &observation->probe_evidence;
        if (observation->radio != CERBERUS_WARDRIVE_RADIO_C5 ||
            observation->address_type || observation->security || observation->manufacturer_id)
            return false;
        for (size_t i = 0; i < sizeof(evidence->reserved); ++i)
            if (evidence->reserved[i]) return false;
        cerberus_wifi_probe_row_t row = {
            .ssid_length = observation->label_length, .channel = observation->channel,
            .rssi = observation->rssi_dbm, .name_bits = evidence->name_bits,
            .oui_hint = evidence->oui_clue,
            .callback_ms = cerberus_wardrive_probe_observed_ms(evidence), .frames = evidence->count,
        };
        memcpy(row.transmitter, observation->address, sizeof(row.transmitter));
        memcpy(row.ssid, observation->label, sizeof(row.ssid));
        return cerberus_wifi_probe_row_valid(&row);
    }
    if (
        !cerberus_s3_ble_wardrive_evidence_valid(&observation->ble_evidence) ||
        observation->rssi_dbm > 0 || observation->rssi_dbm < -127)
        return false;
    bool any_nonzero = false;
    for (size_t i = 0; i < 6U; ++i) any_nonzero |= observation->address[i] != 0U;
    if (!any_nonzero) return false;
    if (observation->type == CERBERUS_WARDRIVE_WIFI)
        return !observation->ble_evidence.flags && observation->channel != 0U &&
            observation->radio != CERBERUS_WARDRIVE_RADIO_S3;
    return observation->radio == CERBERUS_WARDRIVE_RADIO_S3 &&
        observation->channel == 0U;
}

bool cerberus_wardrive_observation_from_probe(const cerberus_wifi_probe_row_t *probe,
    cerberus_wardrive_observation_t *observation)
{
    if (!observation || !cerberus_wifi_probe_row_valid(probe)) return false;
    cerberus_wardrive_observation_t next = {
        .type = CERBERUS_WARDRIVE_WIFI_PROBE, .radio = CERBERUS_WARDRIVE_RADIO_C5,
        .channel = probe->channel, .rssi_dbm = probe->rssi, .label_length = probe->ssid_length,
        .probe_evidence = {
            .observed_ms_low = (uint32_t)probe->callback_ms,
            .observed_ms_high = (uint32_t)(probe->callback_ms >> 32U),
            .count = probe->frames, .name_bits = probe->name_bits, .oui_clue = probe->oui_hint,
        },
    };
    memcpy(next.address, probe->transmitter, sizeof(next.address));
    memcpy(next.label, probe->ssid, sizeof(next.label));
    *observation = next;
    return true;
}

bool cerberus_wardrive_observation_from_relation(const cerberus_wifi_relation_row_t *relation,
    cerberus_wardrive_observation_t *observation)
{
    if (!observation || !cerberus_wifi_relation_row_valid(relation)) return false;
    const uint64_t callback_us = cerberus_wifi_relation_latest_us(relation);
    cerberus_wardrive_observation_t next = {
        .type = CERBERUS_WARDRIVE_WIFI_RELATION, .radio = CERBERUS_WARDRIVE_RADIO_C5,
        .channel = relation->channel,
        .rssi_dbm = relation->latest_direction == CERBERUS_WIFI_RELATION_TO_AP ?
            relation->to_ap_rssi : relation->from_ap_rssi,
        .relation_evidence = {
            .callback_us_low = (uint32_t)callback_us,
            .callback_us_high = (uint32_t)(callback_us >> 32U),
            .to_ap_frames = relation->to_ap_frames, .from_ap_frames = relation->from_ap_frames,
            .latest_direction = relation->latest_direction,
            .rssi_valid = !!(relation->rssi_valid & relation->latest_direction),
        },
    };
    memcpy(next.address, relation->station, sizeof(next.address));
    memcpy(next.relation_evidence.bssid, relation->bssid, sizeof(next.relation_evidence.bssid));
    *observation = next;
    return true;
}

static bool same_relation(const cerberus_wardrive_relation_dedupe_entry_t *entry,
    const cerberus_wardrive_observation_t *observation)
{
    return entry->occupied && entry->channel == observation->channel &&
        !memcmp(entry->station, observation->address, sizeof(entry->station)) &&
        !memcmp(entry->bssid, observation->relation_evidence.bssid, sizeof(entry->bssid));
}

static uint8_t relation_directions(const cerberus_wardrive_relation_evidence_t *e)
{
    return (e->to_ap_frames ? CERBERUS_WIFI_RELATION_TO_AP : 0U) |
        (e->from_ap_frames ? CERBERUS_WIFI_RELATION_FROM_AP : 0U);
}

void cerberus_wardrive_relation_dedupe_reset(cerberus_wardrive_relation_dedupe_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

cerberus_wardrive_decision_t cerberus_wardrive_relation_decide(
    const cerberus_wardrive_relation_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms)
{
    cerberus_wardrive_decision_t decision = {0};
    if (!state || !cerberus_wardrive_observation_valid(observation) ||
        observation->type != CERBERUS_WARDRIVE_WIFI_RELATION) return decision;
    const cerberus_wardrive_relation_evidence_t *e = &observation->relation_evidence;
    const unsigned direction_index = e->latest_direction == CERBERUS_WIFI_RELATION_TO_AP ? 0U : 1U;
    uint16_t empty = CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY, oldest = 0U;
    uint32_t oldest_age = 0U;
    for (uint16_t i = 0U; i < CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY; ++i) {
        const cerberus_wardrive_relation_dedupe_entry_t *entry = &state->entries[i];
        if (!entry->occupied) {
            if (empty == CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY) empty = i;
            continue;
        }
        if (same_relation(entry, observation)) {
            decision.slot = i;
            decision.should_log = (relation_directions(e) & ~entry->directions) != 0U ||
                (uint32_t)(uptime_ms - entry->last_logged_ms) >= CERBERUS_WARDRIVE_RELOG_MS ||
                (e->rssi_valid && (!(entry->valid_rssi & e->latest_direction) ||
                    (int)observation->rssi_dbm >= (int)entry->best_logged_rssi[direction_index] +
                        CERBERUS_WARDRIVE_RSSI_STEP_DB));
            return decision;
        }
        const uint32_t age = (uint32_t)(uptime_ms - entry->last_logged_ms);
        if (age >= oldest_age) { oldest_age = age; oldest = i; }
    }
    decision.should_log = true;
    decision.slot = empty != CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY ? empty : oldest;
    return decision;
}

void cerberus_wardrive_relation_commit(cerberus_wardrive_relation_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms)
{
    if (!state || !decision.should_log || decision.slot >= CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY ||
        !cerberus_wardrive_observation_valid(observation) ||
        observation->type != CERBERUS_WARDRIVE_WIFI_RELATION) return;
    cerberus_wardrive_relation_dedupe_entry_t *entry = &state->entries[decision.slot];
    const cerberus_wardrive_relation_evidence_t *e = &observation->relation_evidence;
    if (!entry->occupied) ++state->used;
    else if (!same_relation(entry, observation) && state->evicted != UINT32_MAX) ++state->evicted;
    if (!same_relation(entry, observation)) memset(entry, 0, sizeof(*entry));
    memcpy(entry->bssid, e->bssid, sizeof(entry->bssid));
    memcpy(entry->station, observation->address, sizeof(entry->station));
    entry->channel = observation->channel;
    entry->last_logged_ms = uptime_ms;
    entry->directions |= relation_directions(e);
    if (e->rssi_valid) {
        entry->valid_rssi |= e->latest_direction;
        entry->best_logged_rssi[e->latest_direction == CERBERUS_WIFI_RELATION_TO_AP ? 0U : 1U] =
            observation->rssi_dbm;
    }
    entry->occupied = true;
}

static bool same_probe(const cerberus_wardrive_probe_dedupe_entry_t *entry,
    const cerberus_wardrive_observation_t *observation)
{
    return entry->occupied && entry->ssid_length == observation->label_length &&
        !memcmp(entry->address, observation->address, sizeof(entry->address)) &&
        !memcmp(entry->ssid, observation->label, observation->label_length);
}

void cerberus_wardrive_probe_dedupe_reset(cerberus_wardrive_probe_dedupe_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

cerberus_wardrive_decision_t cerberus_wardrive_probe_decide(
    const cerberus_wardrive_probe_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms)
{
    cerberus_wardrive_decision_t decision = {0};
    if (!state || !cerberus_wardrive_observation_valid(observation) ||
        observation->type != CERBERUS_WARDRIVE_WIFI_PROBE) return decision;
    uint16_t empty = CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY, oldest = 0U;
    uint32_t oldest_age = 0U;
    for (uint16_t i = 0U; i < CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY; ++i) {
        const cerberus_wardrive_probe_dedupe_entry_t *entry = &state->entries[i];
        if (!entry->occupied) {
            if (empty == CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY) empty = i;
            continue;
        }
        if (same_probe(entry, observation)) {
            decision.slot = i;
            decision.should_log = entry->channel != observation->channel ||
                (int)observation->rssi_dbm >= (int)entry->best_logged_rssi +
                    CERBERUS_WARDRIVE_RSSI_STEP_DB ||
                (uint32_t)(uptime_ms - entry->last_logged_ms) >= CERBERUS_WARDRIVE_RELOG_MS ||
                entry->name_bits != observation->probe_evidence.name_bits ||
                entry->oui_clue != observation->probe_evidence.oui_clue;
            return decision;
        }
        uint32_t age = (uint32_t)(uptime_ms - entry->last_logged_ms);
        if (age >= oldest_age) { oldest_age = age; oldest = i; }
    }
    decision.should_log = true;
    decision.slot = empty != CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY ? empty : oldest;
    return decision;
}

void cerberus_wardrive_probe_commit(cerberus_wardrive_probe_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms)
{
    if (!state || !decision.should_log || decision.slot >= CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY ||
        !cerberus_wardrive_observation_valid(observation) ||
        observation->type != CERBERUS_WARDRIVE_WIFI_PROBE) return;
    cerberus_wardrive_probe_dedupe_entry_t *entry = &state->entries[decision.slot];
    if (!entry->occupied) ++state->used;
    else if (!same_probe(entry, observation) && state->evicted != UINT32_MAX) ++state->evicted;
    memcpy(entry->address, observation->address, sizeof(entry->address));
    memcpy(entry->ssid, observation->label, sizeof(entry->ssid));
    entry->ssid_length = observation->label_length;
    entry->channel = observation->channel;
    entry->best_logged_rssi = observation->rssi_dbm;
    entry->last_logged_ms = uptime_ms;
    entry->name_bits = observation->probe_evidence.name_bits;
    entry->oui_clue = observation->probe_evidence.oui_clue;
    entry->occupied = true;
}

cerberus_wardrive_decision_t cerberus_wardrive_decide(
    const cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms)
{
    cerberus_wardrive_decision_t decision = {0};
    if (state == NULL || !cerberus_wardrive_observation_valid(observation) ||
        (observation->type != CERBERUS_WARDRIVE_WIFI &&
         observation->type != CERBERUS_WARDRIVE_BLE)) return decision;
    uint16_t empty = CERBERUS_WARDRIVE_DEDUPE_CAPACITY;
    uint16_t oldest = 0U;
    uint32_t oldest_age = 0U;
    for (uint16_t i = 0U; i < CERBERUS_WARDRIVE_DEDUPE_CAPACITY; ++i) {
        const cerberus_wardrive_dedupe_entry_t *entry = &state->entries[i];
        if (!entry->occupied) {
            if (empty == CERBERUS_WARDRIVE_DEDUPE_CAPACITY) empty = i;
            continue;
        }
        if (same_identity(entry, observation)) {
            decision.slot = i;
            decision.should_log = entry->channel != observation->channel ||
                (int)observation->rssi_dbm >= (int)entry->best_logged_rssi +
                    CERBERUS_WARDRIVE_RSSI_STEP_DB ||
                (uint32_t)(uptime_ms - entry->last_logged_ms) >=
                    CERBERUS_WARDRIVE_RELOG_MS;
            return decision;
        }
        uint32_t age = (uint32_t)(uptime_ms - entry->last_logged_ms);
        if (age >= oldest_age) { oldest_age = age; oldest = i; }
    }
    decision.should_log = true;
    decision.slot = empty != CERBERUS_WARDRIVE_DEDUPE_CAPACITY ? empty : oldest;
    return decision;
}

cerberus_wardrive_decision_t cerberus_wardrive_decide_evidence(
    const cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms)
{
    cerberus_wardrive_decision_t decision = cerberus_wardrive_decide(state, observation, uptime_ms);
    if (!decision.should_log && state && cerberus_wardrive_observation_valid(observation) &&
        observation->type == CERBERUS_WARDRIVE_BLE && decision.slot < CERBERUS_WARDRIVE_DEDUPE_CAPACITY) {
        const cerberus_wardrive_dedupe_entry_t *entry = &state->entries[decision.slot];
        uint64_t key = cerberus_s3_ble_wardrive_evidence_key(&observation->ble_evidence);
        if (same_identity(entry, observation))
            decision.should_log = entry->ble_evidence_key_low != (uint32_t)key ||
                entry->ble_evidence_key_high != (uint32_t)(key >> 32U);
    }
    return decision;
}

void cerberus_wardrive_commit(cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms)
{
    if (state == NULL || !decision.should_log ||
        decision.slot >= CERBERUS_WARDRIVE_DEDUPE_CAPACITY ||
        !cerberus_wardrive_observation_valid(observation) ||
        (observation->type != CERBERUS_WARDRIVE_WIFI &&
         observation->type != CERBERUS_WARDRIVE_BLE)) return;
    cerberus_wardrive_dedupe_entry_t *entry = &state->entries[decision.slot];
    if (!entry->occupied) ++state->used;
    else if (!same_identity(entry, observation)) ++state->evicted;
    memcpy(entry->address, observation->address, sizeof(entry->address));
    entry->address_type = observation->type == CERBERUS_WARDRIVE_BLE ?
        observation->address_type : 0U;
    entry->type = observation->type;
    entry->channel = observation->channel;
    entry->best_logged_rssi = observation->rssi_dbm;
    entry->last_logged_ms = uptime_ms;
    uint64_t key = cerberus_s3_ble_wardrive_evidence_key(&observation->ble_evidence);
    entry->ble_evidence_key_low = (uint32_t)key;
    entry->ble_evidence_key_high = (uint32_t)(key >> 32U);
    entry->occupied = true;
}

static const char *gps_state_at_log(const cerberus_wardrive_gps_snapshot_t *gps)
{
    if (gps == NULL) return "unknown";
    switch (gps->state) {
    case CERBERUS_WARDRIVE_GPS_ABSENT: return "absent";
    case CERBERUS_WARDRIVE_GPS_NO_FIX: return "no_fix";
    case CERBERUS_WARDRIVE_GPS_STALE: return "stale";
    case CERBERUS_WARDRIVE_GPS_FRESH: return "fresh";
    default: return "unknown";
    }
}

static bool validated_utc_token(const char utc[25])
{
    if (utc == NULL || utc[24] != '\0' || utc[4] != '-' || utc[7] != '-' ||
        utc[10] != 'T' || utc[13] != ':' || utc[16] != ':' ||
        utc[19] != '.' || utc[23] != 'Z') return false;
    for (size_t i = 0U; i < 24U; ++i) {
        if (i == 4U || i == 7U || i == 10U || i == 13U || i == 16U ||
            i == 19U || i == 23U) continue;
        if (utc[i] < '0' || utc[i] > '9') return false;
    }
    return true;
}

static size_t format_relation_columns(const cerberus_wardrive_observation_t *observation,
    char *out, size_t capacity, size_t used)
{
    int tail;
    if (observation->type == CERBERUS_WARDRIVE_WIFI_RELATION) {
        const cerberus_wardrive_relation_evidence_t *e = &observation->relation_evidence;
        tail = snprintf(out + used, capacity - used,
            ",%02X:%02X:%02X:%02X:%02X:%02X,%s,%llu,%lu,%lu\n",
            e->bssid[0], e->bssid[1], e->bssid[2], e->bssid[3], e->bssid[4], e->bssid[5],
            e->latest_direction == CERBERUS_WIFI_RELATION_TO_AP ? "TO_AP" : "FROM_AP",
            (unsigned long long)cerberus_wardrive_relation_callback_us(e),
            (unsigned long)e->to_ap_frames, (unsigned long)e->from_ap_frames);
    } else {
        tail = snprintf(out + used, capacity - used, ",,,,,\n");
    }
    return tail < 0 || (size_t)tail >= capacity - used ? 0U : used + (size_t)tail;
}

size_t cerberus_wardrive_format_unlocated_row(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    uint64_t uptime_ms, uint64_t session_ms, char *out, size_t capacity)
{
    if (!cerberus_wardrive_observation_valid(observation) || out == NULL ||
        capacity < 160U) return 0U;
    char label[CERBERUS_WARDRIVE_LABEL_CAPACITY * 2U + 1U];
    static const char hex[] = "0123456789ABCDEF";
    for (uint8_t i = 0U; i < observation->label_length; ++i) {
        label[2U * i] = hex[observation->label[i] >> 4U];
        label[2U * i + 1U] = hex[observation->label[i] & 0x0FU];
    }
    label[2U * observation->label_length] = '\0';
    char radio[4] = "C5";
    if (observation->radio == CERBERUS_WARDRIVE_RADIO_S3) memcpy(radio, "S3", 3U);
    else if (observation->radio >= CERBERUS_WARDRIVE_RADIO_R1) {
        radio[0] = 'R';
        radio[1] = (char)('1' + observation->radio - CERBERUS_WARDRIVE_RADIO_R1);
        radio[2] = '\0';
    }
    bool wifi = observation->type == CERBERUS_WARDRIVE_WIFI;
    bool probe = observation->type == CERBERUS_WARDRIVE_WIFI_PROBE;
    bool relation = observation->type == CERBERUS_WARDRIVE_WIFI_RELATION;
    const char *gps_state = gps_state_at_log(gps);
    const char *latest_utc = gps != NULL &&
        gps->state == CERBERUS_WARDRIVE_GPS_FRESH &&
        gps->coordinates_valid && gps->utc_valid && gps->utc_from_rmc &&
        validated_utc_token(gps->utc) ? gps->utc : "";
    int length = snprintf(out, capacity,
        "%llu,%llu,%s,%s,%02X:%02X:%02X:%02X:%02X:%02X,",
        (unsigned long long)uptime_ms, (unsigned long long)session_ms,
        wifi ? "wifi" : probe ? "wifi_probe" : relation ? "wifi_relation" : "ble", radio,
        observation->address[0], observation->address[1], observation->address[2],
        observation->address[3], observation->address[4], observation->address[5]);
    if (length < 0 || (size_t)length >= capacity) return 0U;
    char signal[5] = "";
    if (relation && observation->relation_evidence.rssi_valid)
        (void)snprintf(signal, sizeof(signal), "%d", observation->rssi_dbm);
    int tail = relation ? snprintf(out + length, capacity - (size_t)length,
        ",%u,%s,,,,,%s,%s", observation->channel, signal, gps_state, latest_utc) :
        wifi ? snprintf(out + length, capacity - (size_t)length,
        ",%u,%d,%u,%s,,,%s,%s",
        observation->channel, observation->rssi_dbm, observation->security,
        label, gps_state, latest_utc) : probe ?
        snprintf(out + length, capacity - (size_t)length,
            ",%u,%d,,,,,%s,%s", observation->channel, observation->rssi_dbm,
            gps_state, latest_utc) :
        snprintf(out + length, capacity - (size_t)length,
            "%u,,%d,,,%s,%u,%s,%s",
            observation->address_type, observation->rssi_dbm, label,
            observation->manufacturer_id, gps_state, latest_utc);
    if (tail < 0 || (size_t)tail >= capacity - (size_t)length) return 0U;
    size_t used = (size_t)length + (size_t)tail;
    if (wifi || probe || relation) {
        tail = snprintf(out + used, capacity - used, ",,,,,,,,,,");
        if (tail < 0 || (size_t)tail >= capacity - used) return 0U;
        used += (size_t)tail;
        tail = probe ? snprintf(out + used, capacity - used, ",%s,%u,%u,%llu,%lu",
            label, observation->probe_evidence.name_bits, observation->probe_evidence.oui_clue,
            (unsigned long long)cerberus_wardrive_probe_observed_ms(&observation->probe_evidence),
            (unsigned long)observation->probe_evidence.count) :
            snprintf(out + used, capacity - used, ",,,,,");
        if (tail < 0 || (size_t)tail >= capacity - used) return 0U;
        return format_relation_columns(observation, out, capacity, used + (size_t)tail);
    }
    const cerberus_s3_ble_wardrive_evidence_t *evidence = &observation->ble_evidence;
    tail = snprintf(out + used, capacity - used, ",%u",
        !!(evidence->flags & CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE));
    if (tail < 0 || (size_t)tail >= capacity - used) return 0U;
    used += (size_t)tail;
    for (unsigned response = 0; response < 2U; ++response) {
        const cerberus_ble_flock_hints_t *facts = response ?
            &evidence->scan_response : &evidence->advertisement;
        bool valid = evidence->flags & (response ?
            CERBERUS_S3_BLE_WARDRIVE_RSP_VALID : CERBERUS_S3_BLE_WARDRIVE_ADV_VALID);
        tail = valid ? snprintf(out + used, capacity - used, ",%u,%02X%02X%02X%02X,%lu,%lu",
            (unsigned)cerberus_ble_flock_hints_level(facts),
            facts->facts, facts->accessory_contexts, facts->raven_3100_contexts, facts->raven_3200_contexts,
            (unsigned long)(response ? evidence->scan_response_sequence : evidence->advertisement_sequence),
            (unsigned long)(response ? evidence->scan_response_observed_ms : evidence->advertisement_observed_ms)) :
            snprintf(out + used, capacity - used, ",,,,");
        if (tail < 0 || (size_t)tail >= capacity - used) return 0U;
        used += (size_t)tail;
    }
    tail = evidence->flags & CERBERUS_S3_BLE_WARDRIVE_SID_VALID ?
        snprintf(out + used, capacity - used, ",%u,,,,,", evidence->sid) :
        snprintf(out + used, capacity - used, ",,,,,,");
    if (tail < 0 || (size_t)tail >= capacity - used) return 0U;
    return format_relation_columns(observation, out, capacity, used + (size_t)tail);
}
