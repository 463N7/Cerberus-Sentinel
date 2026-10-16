/* Private implementation included by c5_spi_link.c after exchange helpers. */
#include "sentinel_watch_protocol.h"

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
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = watch_capability_locked(CERBERUS_C5_CAP_WATCH_OBSERVATIONS);
    if (err == ESP_OK) err = watch_status_locked(radio, generation, status);
    xSemaphoreGive(link_mutex);
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

esp_err_t c5_spi_link_get_watch_result(uint8_t radio, uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result)
{
    if (!result) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if ((radio != CERBERUS_WATCH_RADIO_BLE && radio != CERBERUS_WATCH_RADIO_WIFI) ||
        index >= CERBERUS_WATCH_MAX_OBSERVATIONS) return ESP_ERR_INVALID_ARG;
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = watch_capability_locked(CERBERUS_C5_CAP_WATCH_OBSERVATIONS);
    cerberus_watch_scan_status_payload_t before = {0}, after = {0};
    cerberus_watch_observation_t pending = {0};
    if (err == ESP_OK) err = watch_status_locked(radio, generation, &before);
    if (err == ESP_OK && index >= before.stored_count) err = ESP_ERR_INVALID_ARG;
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
    if (err == ESP_OK) err = watch_status_locked(radio, generation, &after);
    if (err == ESP_OK && (memcmp(&before, &after, sizeof(before)) || !watch_record_valid(radio, &pending))) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *result = pending;
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_get_ble_activity(uint8_t generation, cerberus_ble_activity_payload_t *activity)
{
    if (!activity) return ESP_ERR_INVALID_ARG;
    memset(activity, 0, sizeof(*activity));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
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
    xSemaphoreGive(link_mutex);
    return err;
}
