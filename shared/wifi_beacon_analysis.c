#include "wifi_beacon_analysis.h"

#include <string.h>

static bool unicast_address(const uint8_t address[6])
{
    uint8_t any = 0U;
    for (size_t i = 0U; i < 6U; ++i) any |= address[i];
    return any != 0U && (address[0] & 1U) == 0U;
}

static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

bool cerberus_wifi_beacon_decode(const uint8_t *frame, size_t length,
    uint8_t rx_channel, int8_t rssi, uint64_t observed_us,
    cerberus_wifi_beacon_observation_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!frame || length < 38U || length > CERBERUS_WIFI_BEACON_MAX_BYTES ||
        rx_channel == 0U || frame[0] != 0x80U ||
        (frame[1] & 0xc7U) != 0U || (frame[22] & 0x0fU) != 0U ||
        !unicast_address(frame + 10U) || !unicast_address(frame + 16U))
        return false;
    static const uint8_t broadcast[6] = {255U, 255U, 255U, 255U, 255U, 255U};
    if (memcmp(frame + 4U, broadcast, sizeof(broadcast)) != 0) return false;

    cerberus_wifi_beacon_observation_t pending = {0};
    bool have_ssid = false, have_channel = false;
    for (size_t offset = 36U; offset < length;) {
        if (length - offset < 2U) return false;
        uint8_t id = frame[offset], size = frame[offset + 1U];
        offset += 2U;
        if ((size_t)size > length - offset) return false;
        if (id == 0U) {
            if (have_ssid || size > sizeof(pending.ssid)) return false;
            have_ssid = true;
            pending.ssid_length = size;
            memcpy(pending.ssid, frame + offset, size);
            pending.hidden_like = true;
            for (size_t i = 0U; i < size; ++i)
                if (pending.ssid[i] != 0U) pending.hidden_like = false;
        } else if (id == 3U) {
            if (have_channel || size != 1U || frame[offset] == 0U) return false;
            have_channel = true;
            pending.advertised_channel = frame[offset];
        }
        offset += size;
    }
    if (!have_ssid) return false;
    memcpy(pending.transmitter, frame + 10U, sizeof(pending.transmitter));
    memcpy(pending.bssid, frame + 16U, sizeof(pending.bssid));
    pending.rx_channel = rx_channel;
    pending.rssi = rssi;
    pending.sequence = (uint16_t)(((uint16_t)frame[22] |
        ((uint16_t)frame[23] << 8U)) >> 4U);
    pending.capabilities = (uint16_t)frame[34] | ((uint16_t)frame[35] << 8U);
    pending.observed_us = observed_us;
    *out = pending;
    return true;
}

bool cerberus_wifi_multissid_init(cerberus_wifi_multissid_t *table,
    cerberus_wifi_multissid_record_t *storage, uint16_t capacity)
{
    if (!table) return false;
    memset(table, 0, sizeof(*table));
    if (!storage || capacity == 0U || capacity > CERBERUS_WIFI_MULTISSID_MAX_ROWS)
        return false;
    memset(storage, 0, sizeof(*storage) * capacity);
    table->records = storage;
    table->capacity = capacity;
    return true;
}

static bool table_valid(const cerberus_wifi_multissid_t *table)
{
    return table && table->records && table->capacity > 0U &&
        table->capacity <= CERBERUS_WIFI_MULTISSID_MAX_ROWS &&
        table->count <= table->capacity && table->confirmed_count <= table->count;
}

cerberus_wifi_multissid_event_t cerberus_wifi_multissid_observe(
    cerberus_wifi_multissid_t *table, const uint8_t *frame, size_t length,
    uint8_t rx_channel, int8_t rssi, uint64_t observed_us, uint16_t *row_index)
{
    if (row_index) *row_index = UINT16_MAX;
    if (!table_valid(table)) return CERBERUS_WIFI_MULTISSID_REJECTED;
    cerberus_wifi_beacon_observation_t observation;
    if (!cerberus_wifi_beacon_decode(frame, length, rx_channel, rssi,
            observed_us, &observation)) {
        increment(&table->rejected_frames);
        return CERBERUS_WIFI_MULTISSID_REJECTED;
    }
    increment(&table->valid_beacons);
    uint16_t index = 0U;
    while (index < table->count && memcmp(table->records[index].latest.transmitter,
            observation.transmitter, sizeof(observation.transmitter)) != 0)
        ++index;
    bool added = index == table->count;
    if (added && index == table->capacity) {
        increment(&table->capacity_drops);
        return CERBERUS_WIFI_MULTISSID_FULL;
    }
    cerberus_wifi_multissid_record_t *record = &table->records[index];
    if (!added && observation.observed_us < record->latest.observed_us) {
        increment(&table->stale_frames);
        return CERBERUS_WIFI_MULTISSID_STALE;
    }
    if (added) {
        memset(record, 0, sizeof(*record));
        record->first_observed_us = observation.observed_us;
        record->peak_rssi = observation.rssi;
        ++table->count;
    } else if (memcmp(record->latest.bssid, observation.bssid,
            sizeof(observation.bssid)) != 0) {
        record->multiple_bssids = true;
    }
    record->latest = observation;
    if (observation.rssi > record->peak_rssi) record->peak_rssi = observation.rssi;
    increment(&record->frames);

    uint8_t variant_length = observation.hidden_like ? 0U : observation.ssid_length;
    bool same = false;
    for (uint8_t i = 0U; i < record->variant_count; ++i) {
        if (record->variants[i].length == variant_length &&
            memcmp(record->variants[i].bytes, observation.ssid, variant_length) == 0) {
            same = true;
            break;
        }
    }
    bool newly_confirmed = false;
    if (!same && record->variant_count < CERBERUS_WIFI_MULTISSID_THRESHOLD) {
        cerberus_wifi_ssid_variant_t *variant = &record->variants[record->variant_count++];
        variant->length = variant_length;
        memcpy(variant->bytes, observation.ssid, variant_length);
        newly_confirmed = record->variant_count == CERBERUS_WIFI_MULTISSID_THRESHOLD;
        if (newly_confirmed) ++table->confirmed_count;
    }
    if (row_index) *row_index = index;
    return newly_confirmed ? CERBERUS_WIFI_MULTISSID_CONFIRMED :
        added ? CERBERUS_WIFI_MULTISSID_ADDED : CERBERUS_WIFI_MULTISSID_UPDATED;
}

bool cerberus_wifi_multissid_copy(const cerberus_wifi_multissid_t *table,
    uint16_t index, cerberus_wifi_multissid_record_t *out)
{
    if (!out) return false;
    if (table_valid(table) && index < table->count) {
        *out = table->records[index];
        return true;
    }
    memset(out, 0, sizeof(*out));
    return false;
}
