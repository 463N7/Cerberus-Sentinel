/* Private implementation included by c5_spi_link.c after exchange helpers. */
#include "sentinel_watch_protocol.h"
#include "watch_receive_timing.h"

static esp_err_t watch_capability_locked(uint32_t capability)
{
    cerberus_c5_status_t status = {0};
    esp_err_t err = get_status_v2_locked(++sequence, &status);
    if (err == ESP_OK && (status.firmware_version < CERBERUS_WATCH_MIN_FIRMWARE ||
        !(status.capabilities & capability))) return ESP_ERR_NOT_SUPPORTED;
    return err;
}

static esp_err_t watch_status_locked(uint8_t radio, uint8_t generation,
    cerberus_watch_scan_status_payload_t *status)
{
    cerberus_watch_scan_request_t query = {radio, generation};
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS,
        &query, sizeof(query), status, sizeof(*status));
    if (err == ESP_OK && (status->radio != radio || status->generation != generation ||
        status->state != CERBERUS_WATCH_COMPLETE || status->stored_count > CERBERUS_WATCH_MAX_OBSERVATIONS ||
        (status->flags & ~(CERBERUS_WATCH_CAPTURE_UNAVAILABLE | CERBERUS_WATCH_CAPTURE_OVERFLOW)) ||
        ((status->flags & CERBERUS_WATCH_CAPTURE_UNAVAILABLE) && status->stored_count))) return ESP_ERR_INVALID_RESPONSE;
    return err;
}

esp_err_t c5_spi_link_get_watch_status(uint8_t radio, uint8_t generation,
    cerberus_watch_scan_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (radio != CERBERUS_WATCH_RADIO_BLE && radio != CERBERUS_WATCH_RADIO_WIFI) return ESP_ERR_INVALID_ARG;
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = watch_capability_locked(CERBERUS_C5_CAP_WATCH_OBSERVATIONS);
    if (err == ESP_OK) err = watch_status_locked(radio, generation, status);
    give_link_mutex();
    if (err != ESP_OK) memset(status, 0, sizeof(*status));
    return err;
}

static bool watch_record_valid(uint8_t radio, const cerberus_watch_observation_t *r)
{
    if (r->address_type > 3U || (r->flags & ~7U) ||
        (r->kind != SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI && r->kind != SENTINEL_WATCH_PROTOCOL_DRONE)) return false;
    if (radio == CERBERUS_WATCH_RADIO_BLE) {
        if (r->kind != SENTINEL_WATCH_PROTOCOL_DRONE || r->transport != SENTINEL_WATCH_TRANSPORT_BLE || r->channel) return false;
    } else if (r->address_type || !r->channel ||
        (r->transport != SENTINEL_WATCH_TRANSPORT_WIFI_BEACON && r->transport != SENTINEL_WATCH_TRANSPORT_WIFI_NAN)) return false;
    if (r->kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI &&
        (r->transport != SENTINEL_WATCH_TRANSPORT_WIFI_BEACON || r->flags != SENTINEL_WATCH_PROTOCOL_HAS_ID)) return false;
    bool terminated = false;
    for (size_t i = 0; i < sizeof(r->id); ++i) {
        uint8_t c = (uint8_t)r->id[i];
        if (!c) terminated = true;
        else if (terminated || c < 32U || c > 126U) return false;
    }
    if (!terminated || ((r->flags & SENTINEL_WATCH_PROTOCOL_HAS_ID) ? !r->id[0] : r->id[0] != 0)) return false;
    if (r->flags & SENTINEL_WATCH_PROTOCOL_HAS_POSITION) {
        if (r->latitude_e7 < -900000000 || r->latitude_e7 > 900000000 ||
            r->longitude_e7 < -1800000000 || r->longitude_e7 > 1800000000) return false;
    } else if (r->latitude_e7 || r->longitude_e7) return false;
    if (r->flags & SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE) {
        if (r->altitude_dm <= -10000 || r->altitude_dm > 317675 ||
            (r->altitude_kind != SENTINEL_WATCH_ALTITUDE_GEODETIC &&
             r->altitude_kind != SENTINEL_WATCH_ALTITUDE_PRESSURE)) return false;
    } else if (r->altitude_dm || r->altitude_kind) return false;
    return true;
}

static esp_err_t watch_timing_locked(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_scan_timing_payload_t *timing)
{
    cerberus_watch_scan_timing_request_t query = {radio, generation, index};
    memset(timing, 0, sizeof(*timing));
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WATCH_SCAN_TIMING,
        &query, sizeof(query), timing, sizeof(*timing));
    cerberus_watch_receive_timing_t decoded;
    if (err == ESP_OK && !cerberus_watch_receive_timing_decode(timing,
            radio, generation, index, 0U, &decoded)) return ESP_ERR_INVALID_RESPONSE;
    return err;
}

static bool watch_peer_equal(const cerberus_c5_status_t *a, const cerberus_c5_status_t *b)
{
    return a->firmware_version == b->firmware_version && a->capabilities == b->capabilities &&
        a->protocol_version == b->protocol_version && a->reset_reason == b->reset_reason &&
        a->boot_count == b->boot_count && b->uptime_ms >= a->uptime_ms;
}

static esp_err_t watch_motion_timing_locked(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_scan_timing_payload_t *timing)
{
    cerberus_watch_scan_data_request_t query = {radio, generation, index, 0U};
    memset(timing, 0, sizeof(*timing));
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING,
        &query, sizeof(query), timing, sizeof(*timing));
    cerberus_watch_receive_timing_t decoded;
    if (err == ESP_OK && !cerberus_watch_receive_timing_decode(timing,
            radio, generation, index, 0U, &decoded)) return ESP_ERR_INVALID_RESPONSE;
    return err;
}

static esp_err_t watch_motion_locked(uint8_t radio, uint8_t generation, uint8_t index,
    uint64_t epoch, sentinel_watch_motion_t *motion)
{
    cerberus_watch_scan_data_request_t query = {radio, generation, index, 0U};
    cerberus_watch_scan_motion_payload_t payload = {0};
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION,
        &query, sizeof(query), &payload, sizeof(payload));
    sentinel_watch_motion_t pending;
    memcpy(&pending, &payload.motion, sizeof(pending));
    if (err == ESP_OK && (payload.radio != radio || payload.generation != generation ||
        payload.index != index || payload.reserved0 || payload.reserved[0] || payload.reserved[1] ||
        !epoch || payload.capture_epoch != epoch || !sentinel_watch_motion_valid(&pending)))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *motion = pending;
    return err;
}

/* Optional outputs select the additive queries. The old wrappers deliberately
 * keep their existing wire cost; completed banks are immutable within an epoch. */
static esp_err_t watch_result_locked_transaction(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion, cerberus_watch_receive_timing_t *motion_timing)
{
    if (timing) memset(timing, 0, sizeof(*timing));
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if ((radio != CERBERUS_WATCH_RADIO_BLE && radio != CERBERUS_WATCH_RADIO_WIFI) ||
        index >= CERBERUS_WATCH_MAX_OBSERVATIONS) return ESP_ERR_INVALID_ARG;
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_c5_status_t peer_before = {0}, peer_after = {0};
    esp_err_t err = timing ? get_status_v2_locked(++sequence, &peer_before) :
        watch_capability_locked(CERBERUS_C5_CAP_WATCH_OBSERVATIONS);
    if (err == ESP_OK && timing &&
        (peer_before.firmware_version < CERBERUS_WATCH_MIN_FIRMWARE ||
         !(peer_before.capabilities & CERBERUS_C5_CAP_WATCH_OBSERVATIONS))) err = ESP_ERR_NOT_SUPPORTED;
    bool supported = timing && peer_before.firmware_version >= CERBERUS_WATCH_TIMING_MIN_FIRMWARE;
    bool motion_supported = motion && peer_before.firmware_version >= CERBERUS_WATCH_MOTION_MIN_FIRMWARE;
    cerberus_watch_scan_status_payload_t before = {0}, after = {0};
    cerberus_watch_scan_timing_payload_t timing_before = {0}, timing_after = {0};
    cerberus_watch_receive_timing_t pending_timing = {0};
    cerberus_watch_scan_timing_payload_t motion_before = {0}, motion_after = {0};
    cerberus_watch_receive_timing_t pending_motion_timing = {0};
    sentinel_watch_motion_t pending_motion = {0};
    cerberus_watch_observation_t pending = {0};
    if (err == ESP_OK) err = watch_status_locked(radio, generation, &before);
    if (err == ESP_OK && index >= before.stored_count) err = ESP_ERR_INVALID_ARG;
    if (err == ESP_OK && supported) {
        int64_t started_us = esp_timer_get_time();
        if (started_us < 0) err = ESP_ERR_INVALID_STATE;
        else {
            pending_timing.request_started_us = (uint64_t)started_us;
            err = watch_timing_locked(radio, generation, index, &timing_before);
        }
    }
    if (err == ESP_OK && motion_supported) {
        int64_t started_us = esp_timer_get_time();
        if (started_us < 0) err = ESP_ERR_INVALID_STATE;
        else {
            pending_motion_timing.request_started_us = (uint64_t)started_us;
            err = watch_motion_timing_locked(radio, generation, index, &motion_before);
        }
        if (err == ESP_OK && motion_before.capture_epoch != timing_before.capture_epoch)
            err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) err = watch_motion_locked(radio, generation, index,
            motion_before.capture_epoch, &pending_motion);
        if (err == ESP_OK && motion_before.flags &&
            !(pending_motion.flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT)) err = ESP_ERR_INVALID_RESPONSE;
    }
    for (size_t offset = 0; err == ESP_OK && offset < sizeof(pending); offset += CERBERUS_WATCH_DATA_CHUNK_SIZE) {
        cerberus_watch_scan_data_request_t query = {radio, generation, index, offset / CERBERUS_WATCH_DATA_CHUNK_SIZE};
        cerberus_watch_scan_data_payload_t chunk = {0};
        err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA, &query, sizeof(query), &chunk, sizeof(chunk));
        size_t count = sizeof(pending) - offset;
        if (count > CERBERUS_WATCH_DATA_CHUNK_SIZE) count = CERBERUS_WATCH_DATA_CHUNK_SIZE;
        if (err == ESP_OK && (chunk.radio != radio || chunk.generation != generation || chunk.index != index ||
            chunk.chunk != query.chunk || chunk.byte_count != count)) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) memcpy((uint8_t *)&pending + offset, chunk.bytes, count);
    }
    if (err == ESP_OK && motion_supported) {
        err = watch_motion_timing_locked(radio, generation, index, &motion_after);
        if (err == ESP_OK && (motion_before.capture_epoch != motion_after.capture_epoch ||
            motion_before.flags != motion_after.flags ||
            motion_after.source_age_ms < motion_before.source_age_ms ||
            (pending.kind != SENTINEL_WATCH_PROTOCOL_DRONE && pending_motion.flags)))
            err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK && motion_before.flags) {
            pending_motion_timing.source_age_ms = motion_before.source_age_ms;
            pending_motion_timing.valid = true;
        } else if (err == ESP_OK) memset(&pending_motion_timing, 0, sizeof(pending_motion_timing));
    }
    if (err == ESP_OK && supported) {
        err = watch_timing_locked(radio, generation, index, &timing_after);
        if (err == ESP_OK && (timing_before.capture_epoch != timing_after.capture_epoch ||
            timing_before.flags != timing_after.flags ||
            timing_after.source_age_ms < timing_before.source_age_ms)) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK && timing_before.flags) {
            pending_timing.source_age_ms = timing_before.source_age_ms;
            pending_timing.valid = true;
        } else if (err == ESP_OK) memset(&pending_timing, 0, sizeof(pending_timing));
    }
    if (err == ESP_OK) err = watch_status_locked(radio, generation, &after);
    if (err == ESP_OK && (memcmp(&before, &after, sizeof(before)) || !watch_record_valid(radio, &pending))) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && timing) {
        err = get_status_v2_locked(++sequence, &peer_after);
        if (err == ESP_OK && !watch_peer_equal(&peer_before, &peer_after)) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        *result = pending;
        if (timing) *timing = pending_timing;
        if (motion) *motion = pending_motion;
        if (motion_timing) *motion_timing = pending_motion_timing;
    }
    give_link_mutex();
    return err;
}

esp_err_t c5_spi_link_get_watch_observed_result(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing)
{
    return watch_result_locked_transaction(radio, generation, index, result, timing, NULL, NULL);
}

esp_err_t c5_spi_link_get_watch_detailed_result(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion, cerberus_watch_receive_timing_t *motion_timing)
{
    if (result) memset(result, 0, sizeof(*result));
    if (timing) memset(timing, 0, sizeof(*timing));
    if (motion) memset(motion, 0, sizeof(*motion));
    if (motion_timing) memset(motion_timing, 0, sizeof(*motion_timing));
    if (!result || !timing || !motion || !motion_timing) return ESP_ERR_INVALID_ARG;
    return watch_result_locked_transaction(radio, generation, index, result, timing, motion, motion_timing);
}

esp_err_t c5_spi_link_get_watch_result(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result)
{
    return c5_spi_link_get_watch_observed_result(radio, generation, index, result, NULL);
}

esp_err_t c5_spi_link_get_ble_activity(uint8_t generation, cerberus_ble_activity_payload_t *activity)
{
    if (!activity) return ESP_ERR_INVALID_ARG;
    memset(activity, 0, sizeof(*activity));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = watch_capability_locked(CERBERUS_C5_CAP_BLE_ACTIVITY);
    cerberus_watch_scan_request_t query = {CERBERUS_WATCH_RADIO_BLE, generation};
    cerberus_ble_activity_payload_t pending = {0};
    if (err == ESP_OK) err = exchange_v2_locked(CERBERUS_SPI_COMMAND_BLE_ACTIVITY,
        &query, sizeof(query), &pending, sizeof(pending));
    if (err == ESP_OK && (pending.generation != generation || pending.state != CERBERUS_WATCH_COMPLETE ||
        (pending.flags & ~7U) || pending.reserved[0] || pending.reserved[1] ||
        pending.valid_reports > pending.reports || pending.identities > CERBERUS_BLE_ACTIVITY_MAX_IDENTITIES ||
        pending.identities > pending.reports || pending.peak_reports_per_second > pending.reports)) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *activity = pending;
    give_link_mutex();
    return err;
}
