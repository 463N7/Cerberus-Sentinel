#include "cerberus_watch_capture.h"

_Static_assert(sizeof(cerberus_watch_scan_request_t) == 2U, "Stable Watch query layout");
_Static_assert(sizeof(cerberus_watch_scan_status_payload_t) == 16U, "Stable Watch status layout");
_Static_assert(sizeof(cerberus_watch_scan_data_request_t) == 4U, "Stable Watch chunk query layout");
_Static_assert(sizeof(cerberus_watch_scan_data_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Watch chunks must fill SPI payload");
_Static_assert(sizeof(cerberus_ble_activity_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE activity must fill SPI payload");
_Static_assert(sizeof(cerberus_watch_observation_t) == 46U, "Stable observation wire layout");

void cerberus_watch_capture_begin(cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint32_t now_ms)
{
    memset(capture, 0, sizeof(*capture));
    capture->status.radio = radio;
    capture->status.generation = generation;
    capture->status.state = CERBERUS_WATCH_RUNNING;
    capture->started_ms = now_ms;
}

void cerberus_watch_capture_finish(cerberus_watch_capture_t *capture,
    uint8_t state, uint32_t now_ms)
{
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    capture->status.duration_ms = now_ms - capture->started_ms;
    capture->status.state = state;
    if (state != CERBERUS_WATCH_COMPLETE) {
        capture->status.stored_count = 0U;
        memset(capture->records, 0, sizeof(capture->records));
    }
}

void cerberus_watch_capture_observe(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel)
{
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    if (capture->status.reports != UINT32_MAX) ++capture->status.reports;
    if (protocol == NULL || address == NULL || protocol->kind == SENTINEL_WATCH_PROTOCOL_NONE) return;
    cerberus_watch_observation_t *record = NULL;
    for (uint8_t i = 0U; i < capture->status.stored_count; ++i) {
        cerberus_watch_observation_t *candidate = &capture->records[i];
        if (candidate->address_type == address_type && candidate->kind == protocol->kind &&
            candidate->transport == protocol->transport && memcmp(candidate->address, address, 6U) == 0) {
            record = candidate;
            break;
        }
    }
    if (record == NULL) {
        if (capture->status.stored_count == CERBERUS_WATCH_MAX_OBSERVATIONS) {
            capture->status.flags |= CERBERUS_WATCH_CAPTURE_OVERFLOW;
            if (capture->status.dropped_records != UINT16_MAX) ++capture->status.dropped_records;
            return;
        }
        record = &capture->records[capture->status.stored_count++];
        memcpy(record->address, address, 6U);
        record->address_type = address_type;
        record->kind = protocol->kind;
        record->transport = protocol->transport;
    }
    /* An advertiser can change or spoof its ID. Never carry a prior object's
       location into a newly claimed identity sharing that address. */
    if ((protocol->flags & SENTINEL_WATCH_PROTOCOL_HAS_ID) != 0U &&
        (record->flags & SENTINEL_WATCH_PROTOCOL_HAS_ID) != 0U &&
        memcmp(record->id, protocol->id, sizeof(record->id)) != 0) {
        record->flags = 0U;
        record->latitude_e7 = 0;
        record->longitude_e7 = 0;
        record->altitude_dm = 0;
        record->altitude_kind = SENTINEL_WATCH_ALTITUDE_NONE;
    }
    record->rssi = rssi;
    record->channel = channel;
    if ((protocol->flags & SENTINEL_WATCH_PROTOCOL_HAS_ID) != 0U) {
        memcpy(record->id, protocol->id, sizeof(record->id));
        record->id[sizeof(record->id) - 1U] = '\0';
    }
    if ((protocol->flags & SENTINEL_WATCH_PROTOCOL_HAS_POSITION) != 0U) {
        record->latitude_e7 = protocol->latitude_e7;
        record->longitude_e7 = protocol->longitude_e7;
    }
    if ((protocol->flags & SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE) != 0U) {
        record->altitude_dm = protocol->altitude_dm;
        record->altitude_kind = protocol->altitude_kind;
    }
    record->flags |= protocol->flags;
}

bool cerberus_watch_capture_get_chunk(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint8_t chunk,
    cerberus_watch_scan_data_payload_t *out)
{
    if (out == NULL) return false;
    memset(out, 0, sizeof(*out));
    const size_t offset = (size_t)chunk * CERBERUS_WATCH_DATA_CHUNK_SIZE;
    if (capture == NULL || capture->status.radio != radio || capture->status.generation != generation ||
        capture->status.state != CERBERUS_WATCH_COMPLETE || index >= capture->status.stored_count ||
        offset >= sizeof(cerberus_watch_observation_t)) return false;
    size_t count = sizeof(cerberus_watch_observation_t) - offset;
    if (count > CERBERUS_WATCH_DATA_CHUNK_SIZE) count = CERBERUS_WATCH_DATA_CHUNK_SIZE;
    out->radio = radio;
    out->generation = generation;
    out->index = index;
    out->chunk = chunk;
    out->byte_count = (uint8_t)count;
    memcpy(out->bytes, (const uint8_t *)&capture->records[index] + offset, count);
    return true;
}

static void complete_activity_bucket(cerberus_ble_activity_capture_t *capture, uint32_t now_ms)
{
    uint32_t bucket = (now_ms - capture->started_ms) / 1000U;
    if (bucket == capture->bucket_index) return;
    uint16_t count = capture->bucket_reports > UINT16_MAX ? UINT16_MAX : (uint16_t)capture->bucket_reports;
    if (count > capture->status.peak_reports_per_second) capture->status.peak_reports_per_second = count;
    if (capture->bucket_reports > UINT16_MAX) capture->status.flags |= CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED;
    capture->bucket_index = bucket;
    capture->bucket_reports = 0U;
}

static bool complete_ad_valid(const uint8_t *data, size_t length)
{
    if ((data == NULL && length != 0U) || length > SENTINEL_WATCH_PROTOCOL_BLE_MAX) return false;
    for (size_t offset = 0U; offset < length;) {
        uint8_t field = data[offset++];
        if (field == 0U) {
            while (offset < length) if (data[offset++] != 0U) return false;
            return true;
        }
        if (field > length - offset) return false;
        offset += field;
    }
    return true;
}

void cerberus_ble_activity_begin(cerberus_ble_activity_capture_t *capture,
    uint8_t generation, uint32_t now_ms, bool extended)
{
    memset(capture, 0, sizeof(*capture));
    capture->status.generation = generation;
    capture->status.state = CERBERUS_WATCH_RUNNING;
    capture->status.flags = extended ? CERBERUS_BLE_ACTIVITY_EXTENDED_ENABLED : 0U;
    capture->started_ms = now_ms;
}

void cerberus_ble_activity_observe(cerberus_ble_activity_capture_t *capture,
    const uint8_t address[6], uint8_t address_type, const uint8_t *data,
    size_t length, bool complete, uint32_t now_ms)
{
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    complete_activity_bucket(capture, now_ms);
    if (capture->bucket_reports != UINT32_MAX) ++capture->bucket_reports;
    if (capture->status.reports != UINT32_MAX) ++capture->status.reports;
    else capture->status.flags |= CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED;
    if (complete && complete_ad_valid(data, length)) {
        if (capture->status.valid_reports != UINT32_MAX) ++capture->status.valid_reports;
        else capture->status.flags |= CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED;
    }
    if (address == NULL || address_type > 3U) return; /* Anonymous extended reports count only as reports. */
    for (uint16_t i = 0U; i < capture->status.identities; ++i) {
        if (capture->addresses[i][6] == address_type && memcmp(capture->addresses[i], address, 6U) == 0) return;
    }
    if (capture->status.identities == CERBERUS_BLE_ACTIVITY_MAX_IDENTITIES) {
        capture->status.flags |= CERBERUS_BLE_ACTIVITY_IDENTITIES_SATURATED;
        return;
    }
    uint8_t *identity = capture->addresses[capture->status.identities++];
    memcpy(identity, address, 6U);
    identity[6] = address_type;
}

void cerberus_ble_activity_finish(cerberus_ble_activity_capture_t *capture,
    uint8_t state, uint32_t now_ms)
{
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    complete_activity_bucket(capture, now_ms);
    capture->status.duration_ms = now_ms - capture->started_ms;
    capture->status.state = state;
}
