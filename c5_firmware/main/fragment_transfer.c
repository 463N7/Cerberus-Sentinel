#include "fragment_transfer.h"

#include <string.h>

static uint8_t object_buffer[CERBERUS_FRAGMENT_OBJECT_MAX_SIZE];
static uint16_t object_length;
static uint32_t object_crc32;
static uint8_t object_type;
static uint8_t object_generation;
static bool object_valid;

esp_err_t cerberus_c5_fragment_open(
    uint8_t requested_type,
    const void *data,
    uint16_t length,
    cerberus_fragment_info_payload_t *info)
{
    if (requested_type == CERBERUS_FRAGMENT_OBJECT_NONE || data == NULL ||
        length == 0U || length > CERBERUS_FRAGMENT_OBJECT_MAX_SIZE || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(object_buffer, data, length);
    object_length = length;
    object_crc32 = cerberus_protocol_crc32(object_buffer, object_length);
    object_type = requested_type;
    ++object_generation;
    if (object_generation == 0U) {
        ++object_generation;
    }
    object_valid = true;

    memset(info, 0, sizeof(*info));
    info->object_type = object_type;
    info->generation = object_generation;
    info->total_length = object_length;
    info->crc32 = object_crc32;
    info->fragment_size = CERBERUS_FRAGMENT_DATA_SIZE;
    return ESP_OK;
}

esp_err_t cerberus_c5_fragment_read(
    const cerberus_fragment_read_request_t *request,
    cerberus_fragment_read_payload_t *payload)
{
    if (request == NULL || payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!object_valid || request->object_type != object_type ||
        request->generation != object_generation || request->offset >= object_length) {
        return ESP_ERR_INVALID_STATE;
    }

    uint16_t remaining = object_length - request->offset;
    uint8_t data_length = remaining < CERBERUS_FRAGMENT_DATA_SIZE
        ? (uint8_t)remaining
        : CERBERUS_FRAGMENT_DATA_SIZE;
    memset(payload, 0, sizeof(*payload));
    payload->object_type = object_type;
    payload->generation = object_generation;
    payload->offset = request->offset;
    payload->data_length = data_length;
    if ((uint16_t)(request->offset + data_length) == object_length) {
        payload->flags = CERBERUS_FRAGMENT_FLAG_LAST;
    }
    memcpy(payload->data, &object_buffer[request->offset], data_length);
    return ESP_OK;
}
