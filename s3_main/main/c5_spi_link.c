#include "c5_spi_link.h"

#include "cerberus_pins.h"
#include "cerberus_protocol.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <string.h>

#define C5_SPI_HOST SPI3_HOST
#define C5_SPI_CLOCK_HZ 40000000
#define C5_EXCHANGE_ATTEMPTS 3
#define C5_READY_POLL_US 20
#define C5_READY_TIMEOUT_US 100000
#define C5_LEGACY_RESPONSE_DELAY_US 10000

static SemaphoreHandle_t link_mutex;
static spi_device_handle_t c5_spi;
static uint32_t c5_spi_actual_clock_hz;
static uint8_t sequence;
static const char *TAG = "c5_spi";

static void spi_transaction(const void *tx_data, void *rx_data, size_t length)
{
    if (rx_data != NULL) {
        memset(rx_data, 0, length);
    }
    if (c5_spi == NULL || length == 0U) return;
    spi_transaction_t transaction = {
        .length = length * 8U,
        .tx_buffer = tx_data,
        .rx_buffer = rx_data,
    };
    esp_err_t err = spi_device_polling_transmit(c5_spi, &transaction);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "hardware SPI transaction failed: %s", esp_err_to_name(err));
        if (rx_data != NULL) memset(rx_data, 0, length);
    }
}

static bool wait_for_ready_level(bool high)
{
    for (uint32_t elapsed = 0; elapsed < C5_READY_TIMEOUT_US; elapsed += C5_READY_POLL_US) {
        if (gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO) == (high ? 1 : 0)) {
            return true;
        }
        esp_rom_delay_us(C5_READY_POLL_US);
    }
    return false;
}

static void spi_request_response_sized(
    const void *request,
    void *discarded,
    void *response,
    size_t request_length,
    size_t response_length)
{
    bool request_ready = wait_for_ready_level(true);
    spi_transaction(request, discarded, request_length);

    bool response_ready = false;
    if (request_ready && wait_for_ready_level(false)) {
        response_ready = wait_for_ready_level(true);
    }
    if (!response_ready) {
        /* Retains V1 compatibility with C5 firmware that predates READY. */
        esp_rom_delay_us(C5_LEGACY_RESPONSE_DELAY_US);
    }
    spi_transaction(NULL, response, response_length);

    if (response_ready) {
        /* Do not expose the response transaction's stale READY-high level to
           the next command. Wait until the C5 consumes this transfer and
           queues the following transaction. */
        (void)wait_for_ready_level(false);
        (void)wait_for_ready_level(true);
    }
}

static void spi_request_response(
    const void *request,
    void *discarded,
    void *response,
    size_t length)
{
    spi_request_response_sized(request, discarded, response, length, length);
}

esp_err_t c5_spi_link_init(void)
{
    if (!cerberus_protocol_self_test()) {
        return ESP_FAIL;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = CERBERUS_ONBOARD_C5_SPI_MOSI_GPIO,
        .miso_io_num = CERBERUS_ONBOARD_C5_SPI_MISO_GPIO,
        .sclk_io_num = CERBERUS_ONBOARD_C5_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(C5_SPI_HOST, &bus, SPI_DMA_DISABLED),
        TAG, "SPI3 bus setup failed");
    spi_device_interface_config_t device = {
        .clock_speed_hz = C5_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = CERBERUS_ONBOARD_C5_SPI_CS_GPIO,
        .queue_size = 1,
    };
    esp_err_t err = spi_bus_add_device(C5_SPI_HOST, &device, &c5_spi);
    if (err != ESP_OK) {
        (void)spi_bus_free(C5_SPI_HOST);
        return err;
    }
    int actual_clock_khz = 0;
    if (spi_device_get_actual_freq(c5_spi, &actual_clock_khz) == ESP_OK && actual_clock_khz > 0) {
        c5_spi_actual_clock_hz = (uint32_t)actual_clock_khz * 1000U;
    } else {
        c5_spi_actual_clock_hz = C5_SPI_CLOCK_HZ;
    }
    gpio_pullup_en(CERBERUS_ONBOARD_C5_SPI_MISO_GPIO);

    gpio_config_t ready_input = {
        .pin_bit_mask = 1ULL << CERBERUS_ONBOARD_C5_READY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&ready_input), "c5_spi", "READY setup failed");

    link_mutex = xSemaphoreCreateMutex();
    if (link_mutex == NULL) {
        (void)spi_bus_remove_device(c5_spi);
        c5_spi = NULL;
        (void)spi_bus_free(C5_SPI_HOST);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "C5 hardware SPI3 master ready at %lu Hz",
        (unsigned long)c5_spi_actual_clock_hz);
    return ESP_OK;
}

uint32_t c5_spi_link_clock_hz(void)
{
    return c5_spi_actual_clock_hz;
}

static esp_err_t get_status_v2_locked(uint8_t request_sequence, cerberus_c5_status_t *status)
{
    cerberus_spi_frame_t request;
    if (!cerberus_protocol_prepare_frame(
            &request,
            CERBERUS_SPI_COMMAND_STATUS,
            request_sequence,
            NULL,
            0)) {
        return ESP_FAIL;
    }

    uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
    cerberus_spi_frame_t response = {0};
    spi_request_response(&request, discarded, &response, sizeof(request));

    if (!cerberus_protocol_frame_is_valid(&response) ||
        response.command != CERBERUS_SPI_COMMAND_STATUS ||
        response.sequence != request_sequence ||
        (response.payload_length != sizeof(cerberus_status_payload_t) &&
            response.payload_length != CERBERUS_STATUS_PAYLOAD_LEGACY_SIZE)) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cerberus_status_payload_t payload = {0};
    memcpy(&payload, response.payload, response.payload_length);
    if (!payload.ready) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    status->firmware_version = payload.firmware_version;
    status->uptime_ms = payload.uptime_ms;
    status->capabilities = payload.capabilities;
    status->protocol_version = CERBERUS_PROTOCOL_VERSION;
    status->reset_reason = payload.reset_reason;
    status->boot_count = payload.boot_count;
    status->ble_phase = payload.ble_phase;
    status->free_heap_kib = payload.free_heap_kib;
    status->minimum_free_heap_kib = payload.minimum_free_heap_kib;
    status->radio_state = payload.radio_state;
    return ESP_OK;
}

static esp_err_t exchange_v2_locked(
    cerberus_spi_command_t command,
    const void *request_payload,
    size_t request_payload_length,
    void *response_payload,
    size_t response_payload_length)
{
    if (response_payload_length > 0U && response_payload == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    for (uint8_t attempt = 0; attempt < C5_EXCHANGE_ATTEMPTS; ++attempt) {
        cerberus_spi_frame_t request;
        uint8_t request_sequence = ++sequence;
        if (!cerberus_protocol_prepare_frame(
                &request,
                command,
                request_sequence,
                request_payload,
                request_payload_length)) {
            return ESP_ERR_INVALID_ARG;
        }

        uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
        cerberus_spi_frame_t response = {0};
        spi_request_response(&request, discarded, &response, sizeof(request));

        if (cerberus_protocol_frame_is_valid(&response) &&
            response.command == command &&
            response.sequence == request_sequence &&
            response.payload_length == response_payload_length) {
            if (response_payload_length > 0U) {
                memcpy(response_payload, response.payload, response_payload_length);
            }
            return ESP_OK;
        }
        ESP_LOGW(
            TAG,
            "invalid response attempt=%u want_cmd=%u want_seq=%u magic=%08lx version=%u cmd=%u seq=%u len=%u crc=%c ready=%d",
            (unsigned)(attempt + 1U),
            (unsigned)command,
            (unsigned)request_sequence,
            (unsigned long)response.magic,
            (unsigned)response.protocol_version,
            (unsigned)response.command,
            (unsigned)response.sequence,
            (unsigned)response.payload_length,
            cerberus_protocol_frame_is_valid(&response) ? 'Y' : 'N',
            gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO));
    }
    return ESP_ERR_INVALID_RESPONSE;
}

esp_err_t c5_spi_link_get_event_queue_status(
    cerberus_c5_event_queue_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS,
        NULL,
        0U,
        status,
        sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_pop_event(cerberus_c5_event_payload_t *event)
{
    if (event == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(event, 0, sizeof(*event));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP,
        NULL,
        0U,
        event,
        sizeof(*event));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_pop_event_batch(
    uint8_t max_events,
    cerberus_c5_event_batch_frame_t *batch)
{
    if (batch == NULL || max_events == 0U ||
        max_events > CERBERUS_C5_EVENT_BATCH_MAX_EVENTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_c5_event_batch_request_t query = {
        .max_events = max_events,
    };
    cerberus_spi_frame_t request;
    uint8_t request_sequence = ++sequence;
    if (!cerberus_protocol_prepare_frame(
            &request,
            CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH,
            request_sequence,
            &query,
            sizeof(query))) {
        xSemaphoreGive(link_mutex);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_ERR_INVALID_RESPONSE;
    for (uint8_t attempt = 0U; attempt < C5_EXCHANGE_ATTEMPTS; ++attempt) {
        uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
        memset(batch, 0, sizeof(*batch));
        spi_request_response_sized(
            &request,
            discarded,
            batch,
            sizeof(request),
            sizeof(*batch));
        if (cerberus_protocol_event_batch_is_valid(batch) &&
            batch->sequence == request_sequence &&
            batch->event_count <= max_events) {
            result = ESP_OK;
            break;
        }
        ESP_LOGW(
            TAG,
            "invalid batch response attempt=%u seq=%u count=%u crc=%c ready=%d",
            (unsigned)(attempt + 1U),
            (unsigned)request_sequence,
            (unsigned)batch->event_count,
            cerberus_protocol_event_batch_is_valid(batch) ? 'Y' : 'N',
            gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO));
    }

    xSemaphoreGive(link_mutex);
    return result;
}

esp_err_t c5_spi_link_configure_event_flow_control(
    uint8_t low_watermark,
    uint8_t high_watermark,
    bool enabled,
    cerberus_c5_event_queue_status_payload_t *status)
{
    if (status == NULL || low_watermark == 0U || low_watermark >= high_watermark ||
        high_watermark > CERBERUS_C5_EVENT_QUEUE_DEPTH) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_c5_event_flow_control_request_t request = {
        .low_watermark = low_watermark,
        .high_watermark = high_watermark,
        .flow_flags = enabled ? CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED : 0U,
    };
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_EVENT_QUEUE_FLOW_CONTROL,
        &request,
        sizeof(request),
        status,
        sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_read_fragmented_object(
    uint8_t object_type,
    void *buffer,
    size_t capacity,
    uint16_t *length,
    uint32_t *crc32)
{
    if (object_type == CERBERUS_FRAGMENT_OBJECT_NONE || buffer == NULL ||
        capacity == 0U || length == NULL || crc32 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *length = 0U;
    *crc32 = 0U;
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_fragment_open_request_t open_request = {
        .object_type = object_type,
    };
    cerberus_fragment_info_payload_t info;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_FRAGMENT_OPEN,
        &open_request,
        sizeof(open_request),
        &info,
        sizeof(info));
    if (err != ESP_OK) {
        xSemaphoreGive(link_mutex);
        return err;
    }
    if (info.object_type != object_type || info.generation == 0U ||
        info.total_length == 0U || info.total_length > capacity ||
        info.total_length > CERBERUS_FRAGMENT_OBJECT_MAX_SIZE ||
        info.fragment_size != CERBERUS_FRAGMENT_DATA_SIZE || info.flags != 0U) {
        xSemaphoreGive(link_mutex);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t *output = (uint8_t *)buffer;
    uint16_t offset = 0U;
    while (offset < info.total_length) {
        cerberus_fragment_read_request_t read_request = {
            .object_type = object_type,
            .generation = info.generation,
            .offset = offset,
        };
        cerberus_fragment_read_payload_t fragment;
        err = exchange_v2_locked(
            CERBERUS_SPI_COMMAND_FRAGMENT_READ,
            &read_request,
            sizeof(read_request),
            &fragment,
            sizeof(fragment));
        if (err != ESP_OK) {
            break;
        }

        uint16_t remaining = info.total_length - offset;
        uint8_t expected_length = remaining < CERBERUS_FRAGMENT_DATA_SIZE
            ? (uint8_t)remaining
            : CERBERUS_FRAGMENT_DATA_SIZE;
        bool expected_last = remaining <= CERBERUS_FRAGMENT_DATA_SIZE;
        if (fragment.object_type != object_type ||
            fragment.generation != info.generation || fragment.offset != offset ||
            fragment.data_length != expected_length ||
            ((fragment.flags & CERBERUS_FRAGMENT_FLAG_LAST) != 0U) != expected_last ||
            (fragment.flags & ~CERBERUS_FRAGMENT_FLAG_LAST) != 0U) {
            err = ESP_ERR_INVALID_RESPONSE;
            break;
        }

        memcpy(&output[offset], fragment.data, fragment.data_length);
        offset = (uint16_t)(offset + fragment.data_length);
    }

    if (err == ESP_OK) {
        uint32_t calculated_crc32 = cerberus_protocol_crc32(output, info.total_length);
        if (calculated_crc32 != info.crc32) {
            err = ESP_ERR_INVALID_CRC;
        } else {
            *length = info.total_length;
            *crc32 = info.crc32;
        }
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t get_status_v1_locked(uint8_t request_sequence, cerberus_c5_status_t *status)
{
    cerberus_spi_v1_request_t request = {
        .magic = CERBERUS_SPI_MAGIC,
        .protocol_version = CERBERUS_PROTOCOL_VERSION_V1,
        .command = CERBERUS_SPI_COMMAND_STATUS,
        .sequence = request_sequence,
    };
    uint8_t discarded[CERBERUS_SPI_TRANSFER_SIZE] = {0};
    cerberus_spi_v1_response_t response = {0};
    spi_request_response(&request, discarded, &response, sizeof(request));

    if (response.magic != CERBERUS_SPI_MAGIC ||
        response.protocol_version != CERBERUS_PROTOCOL_VERSION_V1 ||
        response.command != CERBERUS_SPI_COMMAND_STATUS ||
        response.sequence != request_sequence ||
        !response.ready) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    status->firmware_version = response.firmware_version;
    status->uptime_ms = response.uptime_ms;
    status->capabilities = 0;
    status->protocol_version = CERBERUS_PROTOCOL_VERSION_V1;
    status->reset_reason = 0;
    status->boot_count = 0;
    status->ble_phase = 0;
    status->free_heap_kib = 0;
    status->minimum_free_heap_kib = 0;
    status->radio_state = 0;
    return ESP_OK;
}

esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = ESP_ERR_INVALID_RESPONSE;
    for (uint8_t attempt = 0; attempt < C5_EXCHANGE_ATTEMPTS && err != ESP_OK; ++attempt) {
        err = get_status_v2_locked(++sequence, status);
    }
    if (err != ESP_OK) {
        err = get_status_v1_locked(++sequence, status);
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t wifi_scan_status_command(
    cerberus_spi_command_t command,
    cerberus_wifi_scan_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_wifi_scan(cerberus_wifi_scan_status_payload_t *status)
{
    return wifi_scan_status_command(CERBERUS_SPI_COMMAND_WIFI_SCAN_START, status);
}

esp_err_t c5_spi_link_stop_wifi_scan(cerberus_wifi_scan_status_payload_t *status)
{
    return wifi_scan_status_command(CERBERUS_SPI_COMMAND_WIFI_SCAN_STOP, status);
}

esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *status)
{
    return wifi_scan_status_command(CERBERUS_SPI_COMMAND_WIFI_SCAN_STATUS, status);
}

esp_err_t c5_spi_link_get_wifi_radio_stats(cerberus_wifi_radio_stats_payload_t *stats)
{
    if (stats == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(stats, 0, sizeof(*stats));
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_RADIO_STATS,
        NULL, 0U, stats, sizeof(*stats));
    xSemaphoreGive(link_mutex);
    if (err == ESP_OK && (stats->analyzed_count > CERBERUS_WIFI_SCAN_MAX_RESULTS ||
        stats->open_count + stats->secured_count > stats->analyzed_count ||
        stats->hidden_count > stats->analyzed_count || stats->truncated > 1U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t wifi_defense_status_command(
    cerberus_spi_command_t command,
    cerberus_wifi_defense_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0U, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    if (err == ESP_OK && (status->state > CERBERUS_WIFI_DEFENSE_ERROR ||
        status->result_count > CERBERUS_WIFI_SCAN_MAX_RESULTS ||
        status->completed_count > status->result_count ||
        status->flagged_count > status->completed_count)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t c5_spi_link_start_wifi_defense_survey(cerberus_wifi_defense_status_payload_t *status)
{
    return wifi_defense_status_command(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_START, status);
}

esp_err_t c5_spi_link_stop_wifi_defense_survey(cerberus_wifi_defense_status_payload_t *status)
{
    return wifi_defense_status_command(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STOP, status);
}

esp_err_t c5_spi_link_get_wifi_defense_status(cerberus_wifi_defense_status_payload_t *status)
{
    return wifi_defense_status_command(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STATUS, status);
}

esp_err_t c5_spi_link_get_wifi_defense_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_defense_result_payload_t *result)
{
    if (result == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(result, 0, sizeof(*result));
    cerberus_wifi_defense_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_RESULT,
        &request, sizeof(request), result, sizeof(*result));
    xSemaphoreGive(link_mutex);
    if (err == ESP_OK && (result->generation != generation || result->index != index ||
        result->channel == 0U || (result->assessment_flags & 0xF0U) != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t c5_spi_link_get_wifi_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_wifi_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(result, 0, sizeof(*result));
    cerberus_wifi_scan_result_request_t result_request = {
        .generation = generation,
        .index = index,
    };
    cerberus_wifi_scan_result_payload_t metadata;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT,
        &result_request,
        sizeof(result_request),
        &metadata,
        sizeof(metadata));
    if (err != ESP_OK || metadata.generation != generation || metadata.index != index ||
        metadata.ssid_length > CERBERUS_WIFI_SSID_MAX_LENGTH ||
        metadata.security > CERBERUS_WIFI_SECURITY_OTHER) {
        xSemaphoreGive(link_mutex);
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }

    result->rssi = metadata.rssi;
    result->channel = metadata.channel;
    result->security = metadata.security;
    memcpy(result->bssid, metadata.bssid, sizeof(result->bssid));

    size_t copied = 0;
    uint8_t chunk_count = (uint8_t)(
        (metadata.ssid_length + CERBERUS_WIFI_SSID_CHUNK_SIZE - 1U) /
        CERBERUS_WIFI_SSID_CHUNK_SIZE);
    for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
        cerberus_wifi_scan_ssid_request_t ssid_request = {
            .generation = generation,
            .index = index,
            .chunk = chunk,
        };
        cerberus_wifi_scan_ssid_payload_t ssid_payload;
        err = exchange_v2_locked(
            CERBERUS_SPI_COMMAND_WIFI_SCAN_SSID,
            &ssid_request,
            sizeof(ssid_request),
            &ssid_payload,
            sizeof(ssid_payload));
        if (err != ESP_OK || ssid_payload.generation != generation ||
            ssid_payload.index != index || ssid_payload.chunk != chunk ||
            ssid_payload.byte_count > CERBERUS_WIFI_SSID_CHUNK_SIZE ||
            copied + ssid_payload.byte_count > metadata.ssid_length) {
            xSemaphoreGive(link_mutex);
            return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
        }
        memcpy(result->ssid + copied, ssid_payload.bytes, ssid_payload.byte_count);
        copied += ssid_payload.byte_count;
    }
    result->ssid[copied] = '\0';

    xSemaphoreGive(link_mutex);
    return copied == metadata.ssid_length ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t c5_spi_link_get_wifi_scan_metadata(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_wifi_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_wifi_scan_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    cerberus_wifi_scan_result_payload_t metadata;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT,
        &request,
        sizeof(request),
        &metadata,
        sizeof(metadata));
    if (err == ESP_OK && (metadata.generation != generation || metadata.index != index ||
        metadata.ssid_length > CERBERUS_WIFI_SSID_MAX_LENGTH ||
        metadata.security > CERBERUS_WIFI_SECURITY_OTHER || metadata.channel == 0U)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        memset(result, 0, sizeof(*result));
        result->rssi = metadata.rssi;
        result->channel = metadata.channel;
        result->security = metadata.security;
        memcpy(result->bssid, metadata.bssid, sizeof(result->bssid));
    }
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_connect_wifi(
    uint8_t generation,
    uint8_t index,
    const char *password,
    cerberus_wifi_connection_status_payload_t *status)
{
    if (password == NULL || status == NULL) return ESP_ERR_INVALID_ARG;
    size_t password_length = strnlen(password, CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 1U);
    if (password_length > CERBERUS_WIFI_PASSWORD_MAX_LENGTH || link_mutex == NULL) return ESP_ERR_INVALID_SIZE;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_wifi_connect_begin_request_t begin = {
        .generation = generation,
        .index = index,
        .password_length = (uint8_t)password_length,
    };
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CONNECT_BEGIN,
        &begin, sizeof(begin), status, sizeof(*status));
    bool begin_accepted = err == ESP_OK;
    for (size_t offset = 0; err == ESP_OK && offset < password_length;
         offset += CERBERUS_WIFI_PASSWORD_CHUNK_SIZE) {
        cerberus_wifi_connect_password_request_t chunk = {
            .offset = (uint8_t)offset,
            .byte_count = (uint8_t)((password_length - offset) > CERBERUS_WIFI_PASSWORD_CHUNK_SIZE
                ? CERBERUS_WIFI_PASSWORD_CHUNK_SIZE : (password_length - offset)),
        };
        memcpy(chunk.bytes, password + offset, chunk.byte_count);
        err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CONNECT_PASSWORD,
            &chunk, sizeof(chunk), status, sizeof(*status));
        memset(&chunk, 0, sizeof(chunk));
    }
    if (err == ESP_OK) {
        err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CONNECT_COMMIT,
            NULL, 0, status, sizeof(*status));
    }
    if (err != ESP_OK && begin_accepted) {
        /* Release only our unfinished credential staging reservation. A lost
         * COMMIT response may already have connected; never tear that down. */
        cerberus_wifi_connection_status_payload_t current = {0};
        if (exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CONNECTION_STATUS,
                NULL, 0, &current, sizeof(current)) == ESP_OK &&
            current.state == CERBERUS_WIFI_CONNECTION_STAGING &&
            current.generation == generation && current.index == index) {
            (void)exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_DISCONNECT,
                NULL, 0, status, sizeof(*status));
        }
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t wifi_connection_status_command(
    cerberus_spi_command_t command,
    cerberus_wifi_connection_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_disconnect_wifi(cerberus_wifi_connection_status_payload_t *status)
{
    return wifi_connection_status_command(CERBERUS_SPI_COMMAND_WIFI_DISCONNECT, status);
}

esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *status)
{
    return wifi_connection_status_command(CERBERUS_SPI_COMMAND_WIFI_CONNECTION_STATUS, status);
}

esp_err_t c5_spi_link_get_wifi_dns_status(cerberus_wifi_dns_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_DNS_STATUS,
        NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t network_diagnostics_command(cerberus_spi_command_t command,
    cerberus_network_diagnostics_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_network_diagnostics(
    cerberus_network_diagnostics_status_payload_t *status)
{
    return network_diagnostics_command(CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_START, status);
}

esp_err_t c5_spi_link_get_network_diagnostics_status(
    cerberus_network_diagnostics_status_payload_t *status)
{
    return network_diagnostics_command(CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_STATUS, status);
}

static esp_err_t mdns_status_command(cerberus_spi_command_t command,
    cerberus_mdns_scan_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_mdns_scan(cerberus_mdns_scan_status_payload_t *status)
{
    return mdns_status_command(CERBERUS_SPI_COMMAND_MDNS_SCAN_START, status);
}

esp_err_t c5_spi_link_get_mdns_scan_status(cerberus_mdns_scan_status_payload_t *status)
{
    return mdns_status_command(CERBERUS_SPI_COMMAND_MDNS_SCAN_STATUS, status);
}

esp_err_t c5_spi_link_get_mdns_result(
    uint8_t generation,
    uint8_t index,
    cerberus_mdns_result_payload_t *result)
{
    if (result == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    cerberus_mdns_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_MDNS_SCAN_RESULT,
        &request, sizeof(request), result, sizeof(*result));
    xSemaphoreGive(link_mutex);
    if (err == ESP_OK && (result->generation != generation || result->index != index)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t ble_scan_status_command(
    cerberus_spi_command_t command,
    cerberus_ble_scan_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ble_scan(cerberus_ble_scan_status_payload_t *status)
{
    return ble_scan_status_command(CERBERUS_SPI_COMMAND_BLE_SCAN_START, status);
}

esp_err_t c5_spi_link_stop_ble_scan(cerberus_ble_scan_status_payload_t *status)
{
    return ble_scan_status_command(CERBERUS_SPI_COMMAND_BLE_SCAN_STOP, status);
}

esp_err_t c5_spi_link_get_ble_scan_status(cerberus_ble_scan_status_payload_t *status)
{
    return ble_scan_status_command(CERBERUS_SPI_COMMAND_BLE_SCAN_STATUS, status);
}

static esp_err_t read_ble_data_locked(
    uint8_t generation,
    uint8_t index,
    uint8_t kind,
    uint8_t length,
    uint8_t *destination)
{
    size_t copied = 0;
    uint8_t chunk_count = (uint8_t)(
        (length + CERBERUS_BLE_DATA_CHUNK_SIZE - 1U) / CERBERUS_BLE_DATA_CHUNK_SIZE);
    for (uint8_t chunk = 0; chunk < chunk_count; ++chunk) {
        cerberus_ble_scan_data_request_t request = {
            .generation = generation,
            .index = index,
            .kind = kind,
            .chunk = chunk,
        };
        cerberus_ble_scan_data_payload_t payload;
        esp_err_t err = exchange_v2_locked(
            CERBERUS_SPI_COMMAND_BLE_SCAN_DATA,
            &request,
            sizeof(request),
            &payload,
            sizeof(payload));
        if (err != ESP_OK || payload.generation != generation || payload.index != index ||
            payload.kind != kind || payload.chunk != chunk ||
            payload.byte_count > CERBERUS_BLE_DATA_CHUNK_SIZE ||
            payload.byte_count != (length - copied > CERBERUS_BLE_DATA_CHUNK_SIZE
                ? CERBERUS_BLE_DATA_CHUNK_SIZE : length - copied)) {
            return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
        }
        memcpy(destination + copied, payload.bytes, payload.byte_count);
        copied += payload.byte_count;
    }
    return copied == length ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t c5_spi_link_get_ble_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(result, 0, sizeof(*result));
    cerberus_ble_scan_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    cerberus_ble_scan_result_payload_t metadata;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT,
        &request,
        sizeof(request),
        &metadata,
        sizeof(metadata));
    if (err != ESP_OK || metadata.generation != generation || metadata.index != index ||
        metadata.name_length > CERBERUS_BLE_NAME_MAX_LENGTH ||
        (metadata.uuid_length != 0U && metadata.uuid_length != 2U &&
         metadata.uuid_length != 4U && metadata.uuid_length != 16U)) {
        xSemaphoreGive(link_mutex);
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }

    result->rssi = metadata.rssi;
    result->address_type = metadata.address_type;
    result->manufacturer_id = metadata.manufacturer_id;
    result->flags = metadata.flags;
    result->uuid_length = metadata.uuid_length;
    memcpy(result->address, metadata.address, sizeof(result->address));

    err = read_ble_data_locked(
        generation, index, CERBERUS_BLE_DATA_NAME, metadata.name_length, (uint8_t *)result->name);
    if (err == ESP_OK) {
        result->name[metadata.name_length] = '\0';
        err = read_ble_data_locked(
            generation, index, CERBERUS_BLE_DATA_SERVICE_UUID, metadata.uuid_length, result->uuid);
    }
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_get_ble_advertisement(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_advertisement_t *result)
{
    if (result == NULL) return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if (link_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;

    cerberus_c5_status_t status = {0};
    esp_err_t err = get_status_v2_locked(++sequence, &status);
    if (err == ESP_OK && (status.firmware_version < CERBERUS_BLE_RAW_ADVERTISEMENT_MIN_FIRMWARE ||
        (status.capabilities & CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT) == 0U)) {
        err = ESP_ERR_NOT_SUPPORTED;
    }
    const cerberus_ble_scan_result_request_t request = { .generation = generation, .index = index };
    cerberus_ble_scan_result_payload_t metadata = {0};
    if (err == ESP_OK) {
        err = exchange_v2_locked(CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT,
            &request, sizeof(request), &metadata, sizeof(metadata));
    }
    if (err == ESP_OK && (metadata.generation != generation || metadata.index != index ||
        metadata.advertisement_length > CERBERUS_BLE_ADVERTISEMENT_MAX_LENGTH ||
        metadata.advertisement_event_type > 4U || metadata.address_type > 3U ||
        (metadata.advertisement_flags & ~(CERBERUS_BLE_ADVERTISEMENT_TRUNCATED |
            CERBERUS_BLE_ADVERTISEMENT_MALFORMED)) != 0U)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    cerberus_c5_ble_advertisement_t observation = {0};
    if (err == ESP_OK) {
        observation.length = metadata.advertisement_length;
        observation.event_type = metadata.advertisement_event_type;
        observation.flags = metadata.advertisement_flags;
        observation.reports_seen = metadata.reports_seen;
        observation.address_type = metadata.address_type;
        memcpy(observation.address, metadata.address, sizeof(observation.address));
        err = read_ble_data_locked(generation, index, CERBERUS_BLE_DATA_RAW_ADVERTISEMENT,
            observation.length, observation.data);
    }
    if (err == ESP_OK) *result = observation;
    xSemaphoreGive(link_mutex);
    return err;
}

#include "c5_watch_link.h"

static esp_err_t ble_connection_command(
    cerberus_spi_command_t command,
    const void *request,
    size_t request_length,
    cerberus_ble_connection_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_connect_ble(
    const cerberus_c5_ble_result_t *result,
    cerberus_ble_connection_status_payload_t *status)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cerberus_ble_connect_request_t request = {
        .address_type = result->address_type,
    };
    memcpy(request.address, result->address, sizeof(request.address));
    return ble_connection_command(
        CERBERUS_SPI_COMMAND_BLE_CONNECT,
        &request,
        sizeof(request),
        status);
}

esp_err_t c5_spi_link_disconnect_ble(cerberus_ble_connection_status_payload_t *status)
{
    return ble_connection_command(CERBERUS_SPI_COMMAND_BLE_DISCONNECT, NULL, 0, status);
}

esp_err_t c5_spi_link_get_ble_connection_status(cerberus_ble_connection_status_payload_t *status)
{
    return ble_connection_command(CERBERUS_SPI_COMMAND_BLE_CONNECTION_STATUS, NULL, 0, status);
}

static esp_err_t ble_service_status_command(
    cerberus_spi_command_t command,
    cerberus_ble_gatt_service_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ble_service_discovery(
    cerberus_ble_gatt_service_status_payload_t *status)
{
    return ble_service_status_command(CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_DISCOVER, status);
}

esp_err_t c5_spi_link_get_ble_service_status(
    cerberus_ble_gatt_service_status_payload_t *status)
{
    return ble_service_status_command(CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_STATUS, status);
}

esp_err_t c5_spi_link_get_ble_service_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_service_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_ble_gatt_service_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    cerberus_ble_gatt_service_result_payload_t payload;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_RESULT,
        &request,
        sizeof(request),
        &payload,
        sizeof(payload));
    if (err == ESP_OK && payload.uuid_length != 2U &&
        payload.uuid_length != 4U && payload.uuid_length != 16U) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        memset(result, 0, sizeof(*result));
        result->uuid_length = payload.uuid_length;
        memcpy(result->uuid, payload.uuid, sizeof(result->uuid));
        result->start_handle = payload.start_handle;
        result->end_handle = payload.end_handle;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t ble_characteristic_status_command(
    cerberus_spi_command_t command,
    const void *request,
    size_t request_length,
    cerberus_ble_gatt_characteristic_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ble_characteristic_discovery(
    const cerberus_c5_ble_service_t *service,
    cerberus_ble_gatt_characteristic_status_payload_t *status)
{
    if (service == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    cerberus_ble_gatt_characteristic_discover_request_t request = {
        .start_handle = service->start_handle,
        .end_handle = service->end_handle,
    };
    return ble_characteristic_status_command(
        CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_DISCOVER,
        &request,
        sizeof(request),
        status);
}

esp_err_t c5_spi_link_get_ble_characteristic_status(
    cerberus_ble_gatt_characteristic_status_payload_t *status)
{
    return ble_characteristic_status_command(
        CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_STATUS,
        NULL,
        0,
        status);
}

esp_err_t c5_spi_link_get_ble_characteristic_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_characteristic_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    cerberus_ble_gatt_characteristic_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    cerberus_ble_gatt_characteristic_result_payload_t payload;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_RESULT,
        &request,
        sizeof(request),
        &payload,
        sizeof(payload));
    if (err == ESP_OK && payload.uuid_length != 2U &&
        payload.uuid_length != 4U && payload.uuid_length != 16U) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err == ESP_OK) {
        memset(result, 0, sizeof(*result));
        result->uuid_length = payload.uuid_length;
        memcpy(result->uuid, payload.uuid, sizeof(result->uuid));
        result->definition_handle = payload.definition_handle;
        result->value_handle = payload.value_handle;
        result->properties = payload.properties;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t ble_read_status_command(
    cerberus_spi_command_t command,
    const void *request,
    size_t request_length,
    cerberus_ble_gatt_read_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ble_characteristic_read(
    const cerberus_c5_ble_characteristic_t *characteristic,
    cerberus_ble_gatt_read_status_payload_t *status)
{
    if (characteristic == NULL || (characteristic->properties & 0x02U) == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    cerberus_ble_gatt_read_request_t request = {
        .value_handle = characteristic->value_handle,
    };
    return ble_read_status_command(
        CERBERUS_SPI_COMMAND_BLE_GATT_READ_START,
        &request,
        sizeof(request),
        status);
}

esp_err_t c5_spi_link_get_ble_characteristic_read_status(
    cerberus_ble_gatt_read_status_payload_t *status)
{
    return ble_read_status_command(
        CERBERUS_SPI_COMMAND_BLE_GATT_READ_STATUS,
        NULL,
        0,
        status);
}

esp_err_t c5_spi_link_get_ble_characteristic_read_data(
    uint8_t generation,
    uint8_t chunk,
    cerberus_ble_gatt_read_data_payload_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    cerberus_ble_gatt_read_data_request_t request = {
        .generation = generation,
        .chunk = chunk,
    };
    memset(result, 0, sizeof(*result));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_BLE_GATT_READ_DATA,
        &request,
        sizeof(request),
        result,
        sizeof(*result));
    if (err == ESP_OK && (result->generation != generation || result->chunk != chunk ||
        result->data_length == 0U || result->data_length > CERBERUS_BLE_GATT_READ_CHUNK_LENGTH)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t ieee802154_energy_status_command(
    cerberus_spi_command_t command,
    cerberus_ieee802154_energy_scan_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    if (err == ESP_OK && (status->state > CERBERUS_IEEE802154_ENERGY_SCAN_ERROR ||
        status->completed_count > CERBERUS_IEEE802154_CHANNEL_COUNT ||
        (status->current_channel != 0U &&
         (status->current_channel < CERBERUS_IEEE802154_FIRST_CHANNEL ||
          status->current_channel >= CERBERUS_IEEE802154_FIRST_CHANNEL +
              CERBERUS_IEEE802154_CHANNEL_COUNT)))) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ieee802154_energy_scan(
    cerberus_ieee802154_energy_scan_status_payload_t *status)
{
    return ieee802154_energy_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_START,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_energy_scan_status(
    cerberus_ieee802154_energy_scan_status_payload_t *status)
{
    return ieee802154_energy_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_STATUS,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_energy_scan_result(
    uint8_t generation,
    cerberus_ieee802154_energy_scan_result_payload_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    cerberus_ieee802154_energy_scan_result_request_t request = {
        .generation = generation,
    };
    memset(result, 0, sizeof(*result));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_RESULT,
        &request,
        sizeof(request),
        result,
        sizeof(*result));
    if (err == ESP_OK && (result->generation != generation ||
        result->first_channel != CERBERUS_IEEE802154_FIRST_CHANNEL ||
        result->channel_count != CERBERUS_IEEE802154_CHANNEL_COUNT)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t ieee802154_beacon_status_command(
    cerberus_spi_command_t command,
    cerberus_ieee802154_beacon_scan_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    if (err == ESP_OK && (status->state > CERBERUS_IEEE802154_BEACON_SCAN_ERROR ||
        status->stored_count > CERBERUS_IEEE802154_BEACON_MAX_RESULTS ||
        status->completed_count > CERBERUS_IEEE802154_CHANNEL_COUNT ||
        (status->current_channel != 0U &&
         (status->current_channel < CERBERUS_IEEE802154_FIRST_CHANNEL ||
          status->current_channel >= CERBERUS_IEEE802154_FIRST_CHANNEL +
              CERBERUS_IEEE802154_CHANNEL_COUNT)))) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ieee802154_beacon_scan(
    cerberus_ieee802154_beacon_scan_status_payload_t *status)
{
    return ieee802154_beacon_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_START,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_beacon_scan_status(
    cerberus_ieee802154_beacon_scan_status_payload_t *status)
{
    return ieee802154_beacon_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_STATUS,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_beacon_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ieee802154_beacon_scan_result_payload_t *result)
{
    if (result == NULL || index >= CERBERUS_IEEE802154_BEACON_MAX_RESULTS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    cerberus_ieee802154_beacon_scan_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    memset(result, 0, sizeof(*result));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_RESULT,
        &request,
        sizeof(request),
        result,
        sizeof(*result));
    if (err == ESP_OK && (result->generation != generation || result->index != index ||
        result->channel < CERBERUS_IEEE802154_FIRST_CHANNEL ||
        result->channel >= CERBERUS_IEEE802154_FIRST_CHANNEL +
            CERBERUS_IEEE802154_CHANNEL_COUNT)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

static esp_err_t ieee802154_activity_status_command(
    cerberus_spi_command_t command,
    cerberus_ieee802154_activity_scan_status_payload_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    if (err == ESP_OK && (status->state > CERBERUS_IEEE802154_ACTIVITY_SCAN_ERROR ||
        status->completed_count > CERBERUS_IEEE802154_CHANNEL_COUNT ||
        (status->current_channel != 0U &&
         (status->current_channel < CERBERUS_IEEE802154_FIRST_CHANNEL ||
          status->current_channel >= CERBERUS_IEEE802154_FIRST_CHANNEL +
              CERBERUS_IEEE802154_CHANNEL_COUNT)))) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}

esp_err_t c5_spi_link_start_ieee802154_activity_scan(
    cerberus_ieee802154_activity_scan_status_payload_t *status)
{
    return ieee802154_activity_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_START,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_activity_scan_status(
    cerberus_ieee802154_activity_scan_status_payload_t *status)
{
    return ieee802154_activity_status_command(
        CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_STATUS,
        status);
}

esp_err_t c5_spi_link_get_ieee802154_activity_scan_result(
    uint8_t generation,
    uint8_t result_index,
    cerberus_ieee802154_activity_scan_result_payload_t *result)
{
    if (result == NULL || result_index >= CERBERUS_IEEE802154_ACTIVITY_RESULT_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(link_mutex, pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    cerberus_ieee802154_activity_scan_result_request_t request = {
        .generation = generation,
        .result_index = result_index,
    };
    memset(result, 0, sizeof(*result));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_RESULT,
        &request,
        sizeof(request),
        result,
        sizeof(*result));
    uint8_t expected_first_channel = CERBERUS_IEEE802154_FIRST_CHANNEL +
        result_index * CERBERUS_IEEE802154_ACTIVITY_CHANNELS_PER_RESULT;
    if (err == ESP_OK && (result->generation != generation ||
        result->first_channel != expected_first_channel)) {
        err = ESP_ERR_INVALID_RESPONSE;
    }
    xSemaphoreGive(link_mutex);
    return err;
}
