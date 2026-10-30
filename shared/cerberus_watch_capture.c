#include "cerberus_watch_capture.h"

_Static_assert(sizeof(cerberus_watch_scan_request_t) == 2U, "Stable Watch query layout");
_Static_assert(sizeof(cerberus_watch_scan_status_payload_t) == 16U, "Stable Watch status layout");
_Static_assert(sizeof(cerberus_watch_scan_data_request_t) == 4U, "Stable Watch chunk query layout");
_Static_assert(sizeof(cerberus_watch_scan_data_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Watch chunks must fill SPI payload");
_Static_assert(sizeof(cerberus_ble_activity_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE activity must fill SPI payload");
_Static_assert(sizeof(cerberus_watch_observation_t) == 46U, "Stable observation wire layout");
_Static_assert(sizeof(sentinel_watch_motion_t) == 8U, "Bounded motion value layout");

void cerberus_watch_capture_begin(cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint32_t now_ms)
{
    cerberus_watch_capture_begin_with_epoch(capture, radio, generation, now_ms, 0U);
}

void cerberus_watch_capture_begin_with_epoch(cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint32_t now_ms, uint64_t epoch)
{
    memset(capture, 0, sizeof(*capture));
    capture->status.radio = radio;
    capture->status.generation = generation;
    capture->status.state = CERBERUS_WATCH_RUNNING;
    capture->started_ms = now_ms;
    capture->capture_epoch = epoch;
}

bool cerberus_watch_capture_protect_wifi_source(cerberus_watch_capture_t *capture,
    uint8_t transport, const uint8_t address[6])
{
    if (!capture || !address || capture->status.radio != CERBERUS_WATCH_RADIO_WIFI ||
        capture->status.state != CERBERUS_WATCH_RUNNING || capture->status.reports ||
        capture->status.stored_count || (address[0] & 1U) ||
        (transport != SENTINEL_WATCH_TRANSPORT_WIFI_BEACON &&
         transport != SENTINEL_WATCH_TRANSPORT_WIFI_NAN)) return false;
    uint8_t any = 0U;
    for (unsigned i = 0U; i < 6U; ++i) any |= address[i];
    if (!any) return false;
    memcpy(capture->protected_address, address, sizeof(capture->protected_address));
    capture->protected_transport = transport;
    return true;
}

static bool protected_source(const cerberus_watch_capture_t *capture,
    uint8_t kind, uint8_t transport, uint8_t address_type, const uint8_t address[6])
{
    return capture->status.radio == CERBERUS_WATCH_RADIO_WIFI &&
        capture->protected_transport && kind == SENTINEL_WATCH_PROTOCOL_DRONE &&
        transport == capture->protected_transport && address_type == 0U &&
        memcmp(capture->protected_address, address, 6U) == 0;
}

void cerberus_watch_capture_finish(cerberus_watch_capture_t *capture,
    uint8_t state, uint32_t now_ms)
{
    if (state != CERBERUS_WATCH_COMPLETE) {
        memset(capture->qualifying_us, 0, sizeof(capture->qualifying_us));
        capture->qualifying_valid = 0U;
        memset(capture->motion_us, 0, sizeof(capture->motion_us));
        capture->motion_valid = 0U;
    }
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    capture->status.duration_ms = now_ms - capture->started_ms;
    capture->status.state = state;
    if (state != CERBERUS_WATCH_COMPLETE) {
        capture->status.stored_count = 0U;
        memset(capture->records, 0, sizeof(capture->records));
        memset(capture->motion, 0, sizeof(capture->motion));
    }
}

static void observe(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel, bool timed, uint64_t callback_us)
{
    if (capture->status.state != CERBERUS_WATCH_RUNNING) return;
    if (capture->status.reports != UINT32_MAX) ++capture->status.reports;
    if (protocol == NULL || address == NULL || protocol->kind == SENTINEL_WATCH_PROTOCOL_NONE) return;
    if (!sentinel_watch_motion_valid(&protocol->motion) ||
        (protocol->kind != SENTINEL_WATCH_PROTOCOL_DRONE && protocol->motion.flags)) return;
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
            if (!protected_source(capture, protocol->kind, protocol->transport, address_type, address)) return;
            /* The exact source was not already found above. Prefer the last
             * non-target slot so all earlier retained row indexes stay stable. */
            for (uint8_t i = capture->status.stored_count; i > 0U; --i) {
                cerberus_watch_observation_t *candidate = &capture->records[i - 1U];
                if (!protected_source(capture, candidate->kind, candidate->transport,
                        candidate->address_type, candidate->address)) {
                    record = candidate;
                    memset(record, 0, sizeof(*record));
                    capture->qualifying_us[i - 1U] = 0U;
                    capture->qualifying_valid &= (uint16_t)~(1U << (i - 1U));
                    memset(&capture->motion[i - 1U], 0, sizeof(capture->motion[0]));
                    capture->motion_us[i - 1U] = 0U;
                    capture->motion_valid &= (uint16_t)~(1U << (i - 1U));
                    break;
                }
            }
            if (!record) return;
        } else {
            record = &capture->records[capture->status.stored_count++];
        }
        memcpy(record->address, address, 6U);
        record->address_type = address_type;
        record->kind = protocol->kind;
        record->transport = protocol->transport;
    }
    size_t index = (size_t)(record - capture->records);
    uint16_t bit = (uint16_t)(1U << index);
    bool source_backwards = (capture->qualifying_valid & bit) && callback_us < capture->qualifying_us[index];
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
        memset(&capture->motion[index], 0, sizeof(capture->motion[0]));
        capture->motion_us[index] = 0U;
        capture->motion_valid &= (uint16_t)~bit;
    }
    if (protocol->motion.flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT) {
        /* One Location is one group. New unavailable coordinates/altitude must
         * not be displayed beside motion from that newer report. */
        record->flags &= (uint8_t)~(SENTINEL_WATCH_PROTOCOL_HAS_POSITION | SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE);
        record->latitude_e7 = 0;
        record->longitude_e7 = 0;
        record->altitude_dm = 0;
        record->altitude_kind = SENTINEL_WATCH_ALTITUDE_NONE;
        bool backwards = source_backwards ||
            ((capture->motion_valid & bit) && callback_us < capture->motion_us[index]);
        capture->motion[index] = protocol->motion;
        if (timed && capture->capture_epoch && !backwards) {
            capture->motion_us[index] = callback_us;
            capture->motion_valid |= bit;
        } else {
            capture->motion_us[index] = 0U;
            capture->motion_valid &= (uint16_t)~bit;
        }
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
    if (timed && capture->capture_epoch && !source_backwards) {
        capture->qualifying_us[index] = callback_us;
        capture->qualifying_valid |= bit;
    } else {
        capture->qualifying_us[index] = 0U;
        capture->qualifying_valid &= (uint16_t)~bit;
    }
}

void cerberus_watch_capture_observe(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel)
{
    observe(capture, protocol, address, address_type, rssi, channel, false, 0U);
}

void cerberus_watch_capture_observe_timed(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel, uint64_t callback_us)
{
    observe(capture, protocol, address, address_type, rssi, channel, true, callback_us);
}

bool cerberus_watch_capture_get_timing(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint64_t now_us,
    cerberus_watch_scan_timing_payload_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!capture || (radio != CERBERUS_WATCH_RADIO_BLE && radio != CERBERUS_WATCH_RADIO_WIFI) ||
        capture->status.radio != radio || capture->status.generation != generation ||
        capture->status.state != CERBERUS_WATCH_COMPLETE || !capture->capture_epoch ||
        index >= capture->status.stored_count || index >= CERBERUS_WATCH_MAX_OBSERVATIONS) return false;
    out->radio = radio; out->generation = generation; out->index = index;
    out->capture_epoch = capture->capture_epoch;
    if ((capture->qualifying_valid & (uint16_t)(1U << index)) && now_us >= capture->qualifying_us[index]) {
        uint64_t elapsed = now_us - capture->qualifying_us[index];
        out->source_age_ms = elapsed / 1000U + (elapsed % 1000U != 0U);
        out->flags = CERBERUS_WATCH_TIMING_VALID;
    }
    return true;
}

bool cerberus_watch_capture_get_motion(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint64_t now_us,
    sentinel_watch_motion_t *motion, cerberus_watch_scan_timing_payload_t *timing)
{
    if (motion) memset(motion, 0, sizeof(*motion));
    if (timing) memset(timing, 0, sizeof(*timing));
    if (!motion || !timing || !capture ||
        (radio != CERBERUS_WATCH_RADIO_BLE && radio != CERBERUS_WATCH_RADIO_WIFI) ||
        capture->status.radio != radio || capture->status.generation != generation ||
        capture->status.state != CERBERUS_WATCH_COMPLETE || !capture->capture_epoch ||
        index >= capture->status.stored_count || index >= CERBERUS_WATCH_MAX_OBSERVATIONS ||
        !sentinel_watch_motion_valid(&capture->motion[index])) return false;
    *motion = capture->motion[index];
    timing->radio = radio; timing->generation = generation; timing->index = index;
    timing->capture_epoch = capture->capture_epoch;
    if ((motion->flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT) &&
        (capture->motion_valid & (uint16_t)(1U << index)) && now_us >= capture->motion_us[index]) {
        uint64_t elapsed = now_us - capture->motion_us[index];
        timing->source_age_ms = elapsed / 1000U + (elapsed % 1000U != 0U);
        timing->flags = CERBERUS_WATCH_TIMING_VALID;
    }
    return true;
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
