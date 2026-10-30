#include "c5_spi_link.h"
#include "c5_ready_wait.h"
#include "radio_bus.h"

#include "cerberus_pins.h"
#include "cerberus_protocol.h"
#include "wifi_packet_monitor_core.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdbool.h>
#include <stdatomic.h>
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
static uint32_t peer_firmware_version; /* Accessed only under link_mutex. */
static uint8_t sequence;
static const char *TAG = "c5_spi";
static DRAM_ATTR atomic_uint ready_fall_epoch;
_Static_assert(sizeof(atomic_uint) == sizeof(uint32_t) && _Alignof(atomic_uint) >= 4U,
    "READY epoch requires an aligned 32-bit atomic object");
/* IDF's S3 -mdisable-hardware-atomics makes the generic macro 1 because RMW
 * may need support code. Our single-writer ISR uses only loads/stores, which
 * GCC emits as inline l32i/s32i + memw under those exact flags. The production
 * object is checked by tools/check_c5_ready_iram.py; do not replace these
 * operations with fetch_add/CAS without repeating that IRAM inspection. */
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "READY host requires lock-free atomics");
#endif
static cerberus_c5_transport_diagnostics_t transport_diagnostics;
static portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t mutex_acquired_us;

static uint32_t transport_now_us(void)
{
    return (uint32_t)esp_timer_get_time();
}

static void diagnostic_increment(uint32_t *counter)
{
    portENTER_CRITICAL(&diagnostics_lock);
    if (*counter != UINT32_MAX) ++*counter;
    portEXIT_CRITICAL(&diagnostics_lock);
}

static void diagnostic_maximum(uint32_t *maximum, uint32_t value)
{
    portENTER_CRITICAL(&diagnostics_lock);
    if (value > *maximum) *maximum = value;
    portEXIT_CRITICAL(&diagnostics_lock);
}

static BaseType_t take_link_mutex(TickType_t ticks)
{
    uint32_t started = transport_now_us();
    BaseType_t acquired = xSemaphoreTake(link_mutex, ticks);
    uint32_t finished = transport_now_us();
    diagnostic_maximum(&transport_diagnostics.max_mutex_wait_us, finished - started);
    if (acquired == pdTRUE) mutex_acquired_us = finished;
    else diagnostic_increment(&transport_diagnostics.mutex_timeouts);
    return acquired;
}

static void give_link_mutex(void)
{
    diagnostic_maximum(&transport_diagnostics.max_mutex_hold_us,
        transport_now_us() - mutex_acquired_us);
    xSemaphoreGive(link_mutex);
}

void c5_spi_link_get_transport_diagnostics(cerberus_c5_transport_diagnostics_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&diagnostics_lock);
    *out = transport_diagnostics;
    portEXIT_CRITICAL(&diagnostics_lock);
    out->ready_fall_edges = atomic_load_explicit(&ready_fall_epoch, memory_order_relaxed);
}

static void IRAM_ATTR ready_falling_isr(void *argument)
{
    (void)argument;
    /* The per-pin ISR is the only writer and cannot reenter itself. Loads and
     * stores are lock-free, aligned, and remain in IRAM with the counter in
     * internal DRAM; no allocator, logging, or task API runs in this ISR. */
    unsigned epoch = atomic_load_explicit(&ready_fall_epoch, memory_order_relaxed);
    atomic_store_explicit(&ready_fall_epoch, epoch + 1U, memory_order_relaxed);
}

static uint32_t ready_epoch(void)
{
    return atomic_load_explicit(&ready_fall_epoch, memory_order_relaxed);
}

static bool ready_high(void)
{
    return gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO) != 0;
}

static const c5_ready_wait_io_t ready_io = {
    .now_us = transport_now_us,
    .pause_us = esp_rom_delay_us,
    .level_high = ready_high,
    .fall_epoch = ready_epoch,
};

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
        diagnostic_increment(&transport_diagnostics.spi_errors);
        ESP_LOGW(TAG, "hardware SPI transaction failed: %s", esp_err_to_name(err));
        if (rx_data != NULL) memset(rx_data, 0, length);
    }
}

static bool wait_for_ready_cycle(uint32_t baseline)
{
    if (!c5_ready_wait_fall(&ready_io, baseline,
            C5_READY_TIMEOUT_US, C5_READY_POLL_US)) {
        diagnostic_increment(&transport_diagnostics.ready_fall_timeouts);
        return false;
    }
    if (ready_high()) diagnostic_increment(&transport_diagnostics.ready_latched_pulses);
    if (!c5_ready_wait_high(&ready_io, C5_READY_TIMEOUT_US, C5_READY_POLL_US)) {
        diagnostic_increment(&transport_diagnostics.ready_high_timeouts);
        return false;
    }
    return true;
}

static void spi_request_response_sized(
    const void *request,
    void *discarded,
    void *response,
    size_t request_length,
    size_t response_length)
{
    uint32_t started = transport_now_us();
    diagnostic_increment(&transport_diagnostics.exchanges);
    bool request_ready = c5_ready_wait_high(&ready_io,
        C5_READY_TIMEOUT_US, C5_READY_POLL_US);
    if (!request_ready) diagnostic_increment(&transport_diagnostics.ready_initial_timeouts);
    uint32_t request_epoch = ready_epoch();
    spi_transaction(request, discarded, request_length);

    bool response_ready = request_ready && wait_for_ready_cycle(request_epoch);
    if (!response_ready) {
        /* Retains V1 compatibility with C5 firmware that predates READY. */
        diagnostic_increment(&transport_diagnostics.legacy_fallbacks);
        esp_rom_delay_us(C5_LEGACY_RESPONSE_DELAY_US);
    }
    uint32_t response_epoch = ready_epoch();
    spi_transaction(NULL, response, response_length);

    if (response_ready) {
        /* Do not expose the response transaction's stale READY-high level to
           the next command. Wait until the C5 consumes this transfer and
           queues the following transaction. */
        (void)wait_for_ready_cycle(response_epoch);
    }
    diagnostic_maximum(&transport_diagnostics.max_exchange_us,
        transport_now_us() - started);
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
    if (link_mutex != NULL) return ESP_OK;
    if (!cerberus_protocol_self_test()) {
        return ESP_FAIL;
    }

    ESP_RETURN_ON_ERROR(radio_bus_init(), TAG, "shared SPI3 setup failed");
    c5_spi = radio_bus_onboard();
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

    /* ESP_ERR_INVALID_STATE means an existing per-pin ISR service is shared.
     * Never uninstall it: another driver may own handlers. The READY handler
     * is IRAM-safe whether that service was allocated with IRAM flags or not. */
    esp_err_t isr_error = gpio_install_isr_service(ESP_INTR_FLAG_IRAM);
    if (isr_error != ESP_OK && isr_error != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "READY ISR service failed: %s", esp_err_to_name(isr_error));
        return isr_error;
    }
    ESP_RETURN_ON_ERROR(gpio_set_intr_type(CERBERUS_ONBOARD_C5_READY_GPIO,
        GPIO_INTR_NEGEDGE), TAG, "READY interrupt mode failed");
    isr_error = gpio_isr_handler_add(CERBERUS_ONBOARD_C5_READY_GPIO,
        ready_falling_isr, NULL);
    if (isr_error != ESP_OK) {
        (void)gpio_intr_disable(CERBERUS_ONBOARD_C5_READY_GPIO);
        ESP_LOGE(TAG, "READY ISR handler failed: %s", esp_err_to_name(isr_error));
        return isr_error;
    }

    link_mutex = radio_bus_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS,
        NULL,
        0U,
        status,
        sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(event, 0, sizeof(*event));
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP,
        NULL,
        0U,
        event,
        sizeof(*event));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
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
        give_link_mutex();
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

    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
        give_link_mutex();
        return err;
    }
    if (info.object_type != object_type || info.generation == 0U ||
        info.total_length == 0U || info.total_length > capacity ||
        info.total_length > CERBERUS_FRAGMENT_OBJECT_MAX_SIZE ||
        info.fragment_size != CERBERUS_FRAGMENT_DATA_SIZE || info.flags != 0U) {
        give_link_mutex();
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
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
    peer_firmware_version = err == ESP_OK ? status->firmware_version : 0U;
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(stats, 0, sizeof(*stats));
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_RADIO_STATS,
        NULL, 0U, stats, sizeof(*stats));
    give_link_mutex();
    if (err == ESP_OK && (stats->analyzed_count > CERBERUS_WIFI_SCAN_MAX_RESULTS ||
        stats->open_count + stats->secured_count > stats->analyzed_count ||
        stats->hidden_count > stats->analyzed_count || stats->truncated > 1U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

static esp_err_t wifi_channel_survey_status_command(cerberus_spi_command_t command,
    const void *request, size_t request_length,
    cerberus_wifi_channel_survey_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, request, request_length,
        status, sizeof(*status));
    give_link_mutex();
    if (err == ESP_OK &&
        (status->state > CERBERUS_WIFI_CHANNEL_SURVEY_ERROR ||
         status->band > CERBERUS_WIFI_CHANNEL_SURVEY_BAND_5 ||
         status->result_count > CERBERUS_WIFI_CHANNEL_SURVEY_MAX_RESULTS ||
         status->completed_count > status->result_count ||
         status->dwell_ms != CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) memset(status, 0, sizeof(*status));
    return err;
}

esp_err_t c5_spi_link_wifi_channel_survey_start(uint8_t band,
    cerberus_wifi_channel_survey_status_payload_t *status)
{
    if (band != CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24 &&
        band != CERBERUS_WIFI_CHANNEL_SURVEY_BAND_5) return ESP_ERR_INVALID_ARG;
    cerberus_c5_status_t device;
    esp_err_t err = c5_spi_link_get_status(&device);
    if (err != ESP_OK) return err;
    if (device.firmware_version < CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    cerberus_wifi_channel_survey_start_request_t request = {.band = band};
    err = wifi_channel_survey_status_command(
        CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_START,
        &request, sizeof(request), status);
    if (err == ESP_OK && status->state != CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING)
        err = status->error_code ? status->error_code : ESP_ERR_INVALID_RESPONSE;
    return err;
}

esp_err_t c5_spi_link_wifi_channel_survey_stop(
    cerberus_wifi_channel_survey_status_payload_t *status)
{
    esp_err_t err = wifi_channel_survey_status_command(
        CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STOP, NULL, 0U, status);
    if (err == ESP_OK && status->state == CERBERUS_WIFI_CHANNEL_SURVEY_ERROR)
        err = status->error_code ? status->error_code : ESP_ERR_INVALID_RESPONSE;
    return err;
}

esp_err_t c5_spi_link_wifi_channel_survey_status(
    cerberus_wifi_channel_survey_status_payload_t *status)
{
    return wifi_channel_survey_status_command(
        CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STATUS, NULL, 0U, status);
}

esp_err_t c5_spi_link_wifi_channel_survey_result(uint8_t generation, uint8_t index,
    cerberus_wifi_channel_survey_result_payload_t *result)
{
    if (!result || index >= CERBERUS_WIFI_CHANNEL_SURVEY_MAX_RESULTS)
        return ESP_ERR_INVALID_ARG;
    memset(result, 0, sizeof(*result));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_wifi_channel_survey_result_request_t request = {
        .generation = generation, .index = index};
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_RESULT,
        &request, sizeof(request), result, sizeof(*result));
    give_link_mutex();
    if (err == ESP_OK &&
        (result->generation != generation || result->index != index ||
         result->seen > 1U || result->channel == 0U || result->dwell_ms !=
             CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err != ESP_OK) memset(result, 0, sizeof(*result));
    return err;
}

static esp_err_t wifi_packet_monitor_status_command(cerberus_spi_command_t command,
    const void *request, size_t request_length,
    cerberus_wifi_packet_monitor_status_payload_t *status)
{
    if (!status || !link_mutex) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t error = exchange_v2_locked(command, request, request_length,
        status, sizeof(*status));
    give_link_mutex();
    if (error == ESP_OK &&
        (status->state > CERBERUS_WIFI_PACKET_MONITOR_ERROR ||
         status->stored_count > CERBERUS_WIFI_PACKET_MONITOR_SAMPLES ||
         status->sample_ms != CERBERUS_WIFI_PACKET_MONITOR_SAMPLE_MS ||
         (status->generation != 0U && status->channel == 0U)))
        error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) memset(status, 0, sizeof(*status));
    return error;
}

esp_err_t c5_spi_link_wifi_packet_monitor_start(uint8_t channel,
    cerberus_wifi_packet_monitor_status_payload_t *status)
{
    if (channel == 0U) return ESP_ERR_INVALID_ARG;
    cerberus_c5_status_t device = {0};
    esp_err_t error = c5_spi_link_get_status(&device);
    if (error != ESP_OK) return error;
    if (device.firmware_version < CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    cerberus_wifi_packet_monitor_start_request_t request = {.channel = channel};
    error = wifi_packet_monitor_status_command(
        CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_START,
        &request, sizeof(request), status);
    if (error == ESP_OK && status->state != CERBERUS_WIFI_PACKET_MONITOR_RUNNING)
        error = status->error_code != ESP_OK ? (esp_err_t)status->error_code :
            ESP_ERR_INVALID_RESPONSE;
    return error;
}

esp_err_t c5_spi_link_wifi_packet_monitor_stop(uint8_t generation,
    cerberus_wifi_packet_monitor_status_payload_t *status)
{
    if (generation == 0U) return ESP_ERR_INVALID_ARG;
    cerberus_wifi_packet_monitor_stop_request_t request = {
        .generation = generation,
    };
    esp_err_t error = wifi_packet_monitor_status_command(
        CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STOP,
        &request, sizeof(request), status);
    if (error == ESP_OK && status->state == CERBERUS_WIFI_PACKET_MONITOR_ERROR)
        error = status->error_code != ESP_OK ? (esp_err_t)status->error_code :
            ESP_ERR_INVALID_RESPONSE;
    return error;
}

esp_err_t c5_spi_link_wifi_packet_monitor_status(
    cerberus_wifi_packet_monitor_status_payload_t *status)
{
    return wifi_packet_monitor_status_command(
        CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STATUS,
        NULL, 0U, status);
}

esp_err_t c5_spi_link_wifi_packet_monitor_sample(uint8_t generation,
    uint16_t sequence, cerberus_wifi_packet_monitor_sample_payload_t *sample)
{
    if (!sample || !link_mutex || generation == 0U) return ESP_ERR_INVALID_ARG;
    cerberus_wifi_packet_monitor_sample_request_t request = {
        .generation = generation,
        .sequence = sequence,
    };
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE)
        return ESP_ERR_TIMEOUT;
    esp_err_t error = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_SAMPLE,
        &request, sizeof(request), sample, sizeof(*sample));
    give_link_mutex();
    if (error == ESP_OK && (sample->generation != generation ||
        sample->sequence != sequence || sample->channel == 0U))
        error = ESP_ERR_INVALID_RESPONSE;
    if (error != ESP_OK) memset(sample, 0, sizeof(*sample));
    return error;
}

static bool wifi_capture_status_channel_valid(const cerberus_wifi_capture_status_payload_t *status)
{
    if (!status->channel)
        return status->state != CERBERUS_WIFI_CAPTURE_RUNNING ||
            status->mode_mask == CERBERUS_WIFI_CAPTURE_MODE_BEACON;
    if (status->mode_mask == CERBERUS_WIFI_CAPTURE_MODE_BEACON)
        return cerberus_wifi_packet_monitor_channel_valid(status->channel);
    /* Preserve the existing fixed-channel request range for legacy modes. */
    return status->channel <= 14U || (status->channel >= 32U && status->channel <= 177U);
}

static esp_err_t wifi_capture_status_command(cerberus_spi_command_t command,
    const void *request,size_t request_length,
    cerberus_wifi_capture_status_payload_t *status)
{
    if(!status)return ESP_ERR_INVALID_ARG;
    memset(status,0,sizeof(*status));
    if(!link_mutex)return ESP_ERR_INVALID_STATE;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    esp_err_t err=exchange_v2_locked(command,request,request_length,status,sizeof(*status));
    give_link_mutex();
    if(err==ESP_OK&&(status->state>CERBERUS_WIFI_CAPTURE_ERROR||
       status->pending>2U||
       (status->mode_mask&~CERBERUS_WIFI_CAPTURE_MODE_ALL)!=0U||
       (status->state==CERBERUS_WIFI_CAPTURE_RUNNING&&!status->mode_mask)||
       !wifi_capture_status_channel_valid(status)))
        err=ESP_ERR_INVALID_RESPONSE;
    if(err!=ESP_OK)memset(status,0,sizeof(*status));
    return err;
}

esp_err_t c5_spi_link_wifi_capture_start(
    const cerberus_wifi_capture_start_request_t *request,
    cerberus_wifi_capture_status_payload_t *status)
{
    if(!request||!status)return ESP_ERR_INVALID_ARG;
    memset(status,0,sizeof(*status));
    bool hopping=(request->flags&CERBERUS_WIFI_CAPTURE_FLAG_HOP)!=0U;
    if(hopping&&(request->channel!=0U||request->mode_mask!=CERBERUS_WIFI_CAPTURE_MODE_BEACON||
       (request->flags&CERBERUS_WIFI_CAPTURE_FLAG_BSSID)!=0U))return ESP_ERR_INVALID_ARG;
    cerberus_c5_status_t device;
    esp_err_t err=c5_spi_link_get_status(&device);
    if(err!=ESP_OK)return err;
    if(device.firmware_version<CERBERUS_WIFI_CAPTURE_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    if((request->mode_mask&CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT)!=0U &&
       device.firmware_version<CERBERUS_WIFI_CAPTURE_SAE_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    if((request->mode_mask&CERBERUS_WIFI_CAPTURE_MODE_BEACON)!=0U &&
       device.firmware_version<CERBERUS_WIFI_CAPTURE_BEACON_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    if((request->flags&CERBERUS_WIFI_CAPTURE_FLAG_HOP)!=0U &&
       device.firmware_version<CERBERUS_WIFI_CAPTURE_HOP_MIN_FIRMWARE)
        return ESP_ERR_NOT_SUPPORTED;
    err=wifi_capture_status_command(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START,
        request,sizeof(*request),status);
    if(err!=ESP_OK)return err;
    if(status->generation!=request->generation||
       status->state!=CERBERUS_WIFI_CAPTURE_RUNNING||
       status->mode_mask!=request->mode_mask||
       (!hopping&&status->channel!=request->channel))
        return status->error_code?status->error_code:ESP_ERR_INVALID_RESPONSE;
    return ESP_OK;
}

esp_err_t c5_spi_link_wifi_capture_status(cerberus_wifi_capture_status_payload_t *status)
{
    return wifi_capture_status_command(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STATUS,
        NULL,0U,status);
}

esp_err_t c5_spi_link_wifi_capture_stop(uint32_t generation,
    cerberus_wifi_capture_status_payload_t *status)
{
    if(!status)return ESP_ERR_INVALID_ARG;
    memset(status,0,sizeof(*status));
    if(!generation)return ESP_ERR_INVALID_ARG;
    cerberus_wifi_capture_stop_request_t request={.generation=generation};
    esp_err_t err=wifi_capture_status_command(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP,
        &request,sizeof(request),status);
    if(err==ESP_OK&&status->generation!=generation)
        err=ESP_ERR_INVALID_RESPONSE;
    return err;
}

esp_err_t c5_spi_link_wifi_capture_peek(uint32_t generation,
    cerberus_wifi_capture_peek_payload_t *peek)
{
    if(!peek)return ESP_ERR_INVALID_ARG;
    memset(peek,0,sizeof(*peek));
    if(!generation)return ESP_ERR_INVALID_ARG;
    if(!link_mutex)return ESP_ERR_INVALID_STATE;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_capture_stop_request_t request={.generation=generation};
    esp_err_t err=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK,
        &request,sizeof(request),peek,sizeof(*peek));
    give_link_mutex();
    if(err==ESP_OK&&(peek->generation!=generation||
       (peek->sequence==0U&&peek->length!=0U)||
       (peek->sequence!=0U&&
        (peek->length==0U||peek->length>4096U||peek->channel==0U||peek->rssi_dbm>0))))
        err=ESP_ERR_INVALID_RESPONSE;
    if(err!=ESP_OK)memset(peek,0,sizeof(*peek));
    return err;
}

esp_err_t c5_spi_link_wifi_capture_read(uint32_t generation,uint32_t sequence,
    uint16_t offset,cerberus_wifi_capture_read_payload_t *chunk)
{
    if(!chunk)return ESP_ERR_INVALID_ARG;
    memset(chunk,0,sizeof(*chunk));
    if(!generation||!sequence||offset%CERBERUS_WIFI_CAPTURE_CHUNK_SIZE)
        return ESP_ERR_INVALID_ARG;
    if(!link_mutex)return ESP_ERR_INVALID_STATE;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_capture_read_request_t request={
        .generation=generation,.sequence=sequence,.offset=offset};
    esp_err_t err=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ,
        &request,sizeof(request),chunk,sizeof(*chunk));
    give_link_mutex();
    if(err==ESP_OK&&chunk->offset!=offset)err=ESP_ERR_INVALID_RESPONSE;
    if(err!=ESP_OK)memset(chunk,0,sizeof(*chunk));
    return err;
}

esp_err_t c5_spi_link_wifi_capture_read_frame(
    const cerberus_wifi_capture_peek_payload_t *peek,uint8_t *frame,size_t capacity)
{
    if(!peek||!frame||!peek->generation||!peek->sequence||!peek->length||
       peek->length>4096U||capacity<peek->length)return ESP_ERR_INVALID_ARG;
    for(uint16_t offset=0U;offset<peek->length;
        offset=(uint16_t)(offset+CERBERUS_WIFI_CAPTURE_CHUNK_SIZE)){
        cerberus_wifi_capture_read_payload_t chunk;
        esp_err_t err=c5_spi_link_wifi_capture_read(peek->generation,
            peek->sequence,offset,&chunk);
        if(err!=ESP_OK){memset(frame,0,peek->length);return err;}
        size_t remaining=peek->length-offset;
        size_t count=remaining<CERBERUS_WIFI_CAPTURE_CHUNK_SIZE?
            remaining:CERBERUS_WIFI_CAPTURE_CHUNK_SIZE;
        memcpy(frame+offset,chunk.data,count);
    }
    if(cerberus_protocol_crc32(frame,peek->length)!=peek->crc32){
        memset(frame,0,peek->length);return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK;
}

esp_err_t c5_spi_link_wifi_capture_ack(uint32_t generation,uint32_t sequence)
{
    if(!generation||!sequence)return ESP_ERR_INVALID_ARG;
    if(!link_mutex)return ESP_ERR_INVALID_STATE;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_capture_ack_request_t request={
        .generation=generation,.sequence=sequence};
    int32_t response=ESP_ERR_INVALID_RESPONSE;
    esp_err_t err=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_ACK,
        &request,sizeof(request),&response,sizeof(response));
    give_link_mutex();
    return err==ESP_OK?(esp_err_t)response:err;
}

uint64_t c5_spi_link_wifi_capture_monotonic_us(
    const cerberus_wifi_capture_peek_payload_t *peek)
{
    if(!peek)return 0U;
    uint64_t value=0U;
    for(unsigned i=0;i<6U;++i)value|=(uint64_t)peek->monotonic_us_le48[i]<<(8U*i);
    return value;
}

static esp_err_t wifi_defense_status_command(
    cerberus_spi_command_t command,
    cerberus_wifi_defense_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0U, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    memset(result, 0, sizeof(*result));
    cerberus_wifi_defense_result_request_t request = {
        .generation = generation,
        .index = index,
    };
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_RESULT,
        &request, sizeof(request), result, sizeof(*result));
    give_link_mutex();
    if (err == ESP_OK && (result->generation != generation || result->index != index ||
        result->channel == 0U || (result->assessment_flags & 0xF0U) != 0U)) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t c5_spi_link_get_wifi_scan_observed_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_wifi_result_t *result,
    cerberus_s3_wifi_observation_t *observation)
{
    if (observation) memset(observation, 0, sizeof(*observation));
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (link_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(result, 0, sizeof(*result));
    cerberus_wifi_scan_result_request_t result_request = {
        .generation = generation,
        .index = index,
    };
    cerberus_wifi_scan_result_payload_t metadata;
    uint64_t request_started_us = observation ? (uint64_t)esp_timer_get_time() : 0U;
    esp_err_t err = exchange_v2_locked(
        CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT,
        &result_request,
        sizeof(result_request),
        &metadata,
        sizeof(metadata));
    if (err != ESP_OK || metadata.generation != generation || metadata.index != index ||
        metadata.ssid_length > CERBERUS_WIFI_SSID_MAX_LENGTH ||
        metadata.security > CERBERUS_WIFI_SECURITY_OTHER) {
        give_link_mutex();
        return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
    }

    cerberus_s3_wifi_observation_t pending_observation = {0};
    if (observation && !cerberus_s3_wifi_observation_decode(&metadata,
            peer_firmware_version, request_started_us, &pending_observation)) {
        give_link_mutex();
        return ESP_ERR_INVALID_RESPONSE;
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
        size_t remaining = metadata.ssid_length - copied;
        uint8_t expected_bytes = remaining < CERBERUS_WIFI_SSID_CHUNK_SIZE ?
            (uint8_t)remaining : CERBERUS_WIFI_SSID_CHUNK_SIZE;
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
            ssid_payload.byte_count != expected_bytes ||
            copied + ssid_payload.byte_count > metadata.ssid_length) {
            give_link_mutex();
            return err == ESP_OK ? ESP_ERR_INVALID_RESPONSE : err;
        }
        memcpy(result->ssid + copied, ssid_payload.bytes, ssid_payload.byte_count);
        copied += ssid_payload.byte_count;
    }
    result->ssid[copied] = '\0';

    if (copied == metadata.ssid_length && observation) *observation = pending_observation;
    give_link_mutex();
    return copied == metadata.ssid_length ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation, uint8_t index,
    cerberus_c5_wifi_result_t *result)
{
    return c5_spi_link_get_wifi_scan_observed_result(generation, index, result, NULL);
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(500)) != pdTRUE) return ESP_ERR_TIMEOUT;
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
    give_link_mutex();
    return err;
}

static esp_err_t wifi_connection_status_command(
    cerberus_spi_command_t command,
    cerberus_wifi_connection_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_DNS_STATUS,
        NULL, 0, status, sizeof(*status));
    give_link_mutex();
    return err;
}

static esp_err_t network_scan_command(cerberus_spi_command_t command,
    const void *request, size_t request_size, cerberus_net_scan_status_t *status)
{
    if (!status || !link_mutex) return ESP_ERR_INVALID_ARG;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_net_scan_status_t reply;
    esp_err_t err = exchange_v2_locked(command, request, request_size, &reply, sizeof(reply));
    give_link_mutex();
    if (err != ESP_OK) return err;
    if (reply.state > CERBERUS_NET_SCAN_ERROR || reply.result_count > CERBERUS_NET_SCAN_MAX_RESULTS ||
        reply.tested > reply.total || (uint32_t)reply.open + reply.closed + reply.filtered != reply.tested ||
        reply.result_count > reply.open) return ESP_ERR_INVALID_RESPONSE;
    *status = reply;
    return command != CERBERUS_SPI_COMMAND_NET_SCAN_STATUS && reply.error_code != ESP_OK ? reply.error_code : ESP_OK;
}
esp_err_t c5_spi_link_network_start(const cerberus_net_scan_request_t *request, cerberus_net_scan_status_t *status)
{
    if (!request) return ESP_ERR_INVALID_ARG;
    esp_err_t err = network_scan_command(CERBERUS_SPI_COMMAND_NET_SCAN_START, request, sizeof(*request), status);
    return err == ESP_OK && status->job_id != request->job_id ? ESP_ERR_INVALID_RESPONSE : err;
}
esp_err_t c5_spi_link_network_status(cerberus_net_scan_status_t *status)
{
    return network_scan_command(CERBERUS_SPI_COMMAND_NET_SCAN_STATUS, NULL, 0, status);
}
esp_err_t c5_spi_link_network_stop(uint16_t job_id, cerberus_net_scan_status_t *status)
{
    esp_err_t err = network_scan_command(CERBERUS_SPI_COMMAND_NET_SCAN_STOP, &job_id, sizeof(job_id), status);
    return err == ESP_OK && status->job_id != job_id ? ESP_ERR_INVALID_RESPONSE : err;
}
esp_err_t c5_spi_link_network_result(uint16_t job_id, uint8_t index, cerberus_net_scan_result_t *result)
{
    if (!result || !link_mutex || index >= CERBERUS_NET_SCAN_MAX_RESULTS) return ESP_ERR_INVALID_ARG;
    cerberus_net_scan_query_t query = {.job_id = job_id, .index = index};
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    cerberus_net_scan_result_t reply;
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_NET_SCAN_RESULT, &query, sizeof(query), &reply, sizeof(reply));
    give_link_mutex();
    if (err != ESP_OK) return err;
    if (!cerberus_net_scan_result_valid(&reply, job_id, index)) return ESP_ERR_INVALID_RESPONSE;
    *result = reply;
    return ESP_OK;
}

static esp_err_t radio_test_command(cerberus_spi_command_t command,const void *data,size_t length,
    cerberus_radio_test_status_t *status)
{
    if(!status||!link_mutex)return ESP_ERR_INVALID_ARG;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_radio_test_status_t reply;
    esp_err_t err=exchange_v2_locked(command,data,length,&reply,sizeof(reply));
    give_link_mutex();
    if(err!=ESP_OK)return err;
    if(reply.state>CERBERUS_TEST_ERROR||reply.mode>CERBERUS_TEST_WIFI_AP||reply.seconds>30||reply.name_length>32)return ESP_ERR_INVALID_RESPONSE;
    if(reply.mode==CERBERUS_TEST_WIFI_AP&&
       (reply.name_length>CERBERUS_AP_STAGE_SAE_MATERIAL||reply.counts_valid>1))return ESP_ERR_INVALID_RESPONSE;
    *status=reply;return ESP_OK;
}

static esp_err_t station_command(cerberus_spi_command_t command,const void *data,size_t length,
    cerberus_wifi_station_status_t *status)
{
    if(!status||!link_mutex)return ESP_ERR_INVALID_ARG;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_station_status_t reply;
    esp_err_t error=exchange_v2_locked(command,data,length,&reply,sizeof(reply));
    give_link_mutex();
    if(error!=ESP_OK)return error;
    if(reply.state>CERBERUS_STATION_ERROR||reply.count>CERBERUS_WIFI_STATION_LIMIT||
        reply.matched_frames>reply.observed_frames||reply.overflow_frames>reply.matched_frames||
        reply.count>reply.matched_frames)return ESP_ERR_INVALID_RESPONSE;
    *status=reply;
    return command!=CERBERUS_SPI_COMMAND_WIFI_STATION_STATUS&&reply.error_code?reply.error_code:ESP_OK;
}
esp_err_t c5_spi_link_station_start(const cerberus_wifi_station_request_t *request,cerberus_wifi_station_status_t *status)
{
    if(!cerberus_wifi_station_request_valid(request))return ESP_ERR_INVALID_ARG;
    esp_err_t error=station_command(CERBERUS_SPI_COMMAND_WIFI_STATION_START,request,sizeof(*request),status);
    return error==ESP_OK&&status->job_id!=request->job_id?ESP_ERR_INVALID_RESPONSE:error;
}
esp_err_t c5_spi_link_station_status(uint16_t id,cerberus_wifi_station_status_t *status)
{return station_command(CERBERUS_SPI_COMMAND_WIFI_STATION_STATUS,&id,sizeof(id),status);}
esp_err_t c5_spi_link_station_stop(uint16_t id,cerberus_wifi_station_status_t *status)
{
    esp_err_t error=station_command(CERBERUS_SPI_COMMAND_WIFI_STATION_STOP,&id,sizeof(id),status);
    return error==ESP_OK&&status->job_id!=id?ESP_ERR_INVALID_RESPONSE:error;
}
esp_err_t c5_spi_link_station_result(uint16_t id,uint8_t index,cerberus_wifi_station_result_t *result)
{
    if(!result||!link_mutex||!id||index>=CERBERUS_WIFI_STATION_LIMIT)return ESP_ERR_INVALID_ARG;
    cerberus_wifi_station_query_t query={.job_id=id,.index=index};
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_station_result_t reply;
    esp_err_t error=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_STATION_RESULT,&query,sizeof(query),&reply,sizeof(reply));
    give_link_mutex();
    if(error!=ESP_OK)return error;
    if(!cerberus_wifi_station_result_valid(&reply,id,index))return ESP_ERR_INVALID_RESPONSE;
    *result=reply;return ESP_OK;
}
esp_err_t c5_spi_link_test_status(uint16_t id,cerberus_radio_test_status_t *status)
{return radio_test_command(CERBERUS_SPI_COMMAND_RADIO_TEST_STATUS,&id,sizeof(id),status);}
esp_err_t c5_spi_link_ap_start(const cerberus_wifi_ap_job_t *r,cerberus_radio_test_status_t *status)
{
    if(!cerberus_wifi_ap_job_valid(r))return ESP_ERR_INVALID_ARG;
    esp_err_t error=radio_test_command(CERBERUS_SPI_COMMAND_WIFI_AP_START,r,sizeof(*r),status);
    if(error==ESP_OK&&(status->job_id!=r->job_id||status->mode!=CERBERUS_TEST_WIFI_AP))return ESP_ERR_INVALID_RESPONSE;
    return error==ESP_OK&&status->error_code!=ESP_OK?status->error_code:error;
}
esp_err_t c5_spi_link_ap_validate(const cerberus_wifi_ap_job_t *r)
{
    if(!cerberus_wifi_ap_job_valid(r)||!link_mutex)return ESP_ERR_INVALID_ARG;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    int32_t reply=ESP_FAIL;
    esp_err_t error=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_AP_VALIDATE,r,sizeof(*r),&reply,sizeof(reply));
    give_link_mutex();return error==ESP_OK?reply:error;
}
esp_err_t c5_spi_link_ap_stage(const cerberus_wifi_ap_job_t *r,
    const cerberus_wifi_ap_station_set_t *stations)
{
    if(!cerberus_wifi_ap_station_set_valid(r,stations)||!link_mutex)return ESP_ERR_INVALID_ARG;
    if(take_link_mutex(pdMS_TO_TICKS(250))!=pdTRUE)return ESP_ERR_TIMEOUT;
    cerberus_wifi_ap_station_begin_t begin={.job=*r,.count=stations->count};
    int32_t reply=ESP_FAIL;
    esp_err_t error=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_AP_STATION_BEGIN,
        &begin,sizeof(begin),&reply,sizeof(reply));
    if(error==ESP_OK)error=reply;
    for(unsigned offset=0;error==ESP_OK&&offset<stations->count;offset+=3){
        cerberus_wifi_ap_station_chunk_t chunk={.job_id=r->job_id,.offset=(uint8_t)offset,
            .count=(uint8_t)((stations->count-offset)<3?stations->count-offset:3)};
        for(unsigned i=0;i<chunk.count;++i)memcpy(chunk.mac[i],stations->mac[offset+i],6);
        reply=ESP_FAIL;
        error=exchange_v2_locked(CERBERUS_SPI_COMMAND_WIFI_AP_STATION_CHUNK,
            &chunk,sizeof(chunk),&reply,sizeof(reply));
        if(error==ESP_OK)error=reply;
    }
    give_link_mutex();return error;
}
esp_err_t c5_spi_link_test_stop(uint16_t id,cerberus_radio_test_status_t *status)
{return radio_test_command(CERBERUS_SPI_COMMAND_RADIO_TEST_STOP,&id,sizeof(id),status);}
esp_err_t c5_spi_link_test_start(const cerberus_radio_test_config_t *c,cerberus_radio_test_status_t *status)
{
    if(!cerberus_radio_test_config_valid(c)||c->mode!=CERBERUS_TEST_WIFI_BEACON)return ESP_ERR_INVALID_ARG;
    cerberus_radio_test_begin_t begin={.job_id=c->job_id,.mode=c->mode,.channel=c->channel,.seconds=c->seconds,.name_length=c->name_length};
    memcpy(begin.name,c->name,16);
    esp_err_t err=radio_test_command(CERBERUS_SPI_COMMAND_RADIO_TEST_BEGIN,&begin,sizeof(begin),status);
    if(err==ESP_OK&&status->error_code)err=status->error_code;
    if(err==ESP_OK&&status->job_id!=c->job_id)err=ESP_ERR_INVALID_RESPONSE;
    if(err==ESP_OK&&c->name_length>16){
        cerberus_radio_test_chunk_t chunk={.job_id=c->job_id,.offset=16,.length=c->name_length-16};
        memcpy(chunk.data,c->name+16,chunk.length);
        err=radio_test_command(CERBERUS_SPI_COMMAND_RADIO_TEST_CHUNK,&chunk,sizeof(chunk),status);
        if(err==ESP_OK&&status->error_code)err=status->error_code;
    }
    if(err==ESP_OK){
        err=radio_test_command(CERBERUS_SPI_COMMAND_RADIO_TEST_START,&c->job_id,sizeof(c->job_id),status);
        if(err==ESP_OK&&status->error_code)err=status->error_code;
    }
    return err;
}

static esp_err_t network_diagnostics_command(cerberus_spi_command_t command,
    cerberus_network_diagnostics_status_payload_t *status)
{
    if (status == NULL || link_mutex == NULL) return ESP_ERR_INVALID_ARG;
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = exchange_v2_locked(CERBERUS_SPI_COMMAND_MDNS_SCAN_RESULT,
        &request, sizeof(request), result, sizeof(*result));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
        give_link_mutex();
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) return ESP_ERR_TIMEOUT;

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
    give_link_mutex();
    return err;
}


/* BEGIN PASSIVE WIFI PROBE READER: also compiled by the focused host harness. */
typedef struct {
    uint32_t deadline_ms;
    cerberus_c5_probe_continue_fn continue_fn;
    void *context;
} wifi_probe_read_control_t;

static esp_err_t wifi_probe_read_guard(const wifi_probe_read_control_t *control,
    bool check_continue, uint32_t *remaining_ms)
{
    if (check_continue && control->continue_fn && !control->continue_fn(control->context))
        return ESP_ERR_NOT_FINISHED;
    int64_t now_us = esp_timer_get_time();
    if (now_us < 0) return ESP_ERR_INVALID_STATE;
    uint32_t now_ms = (uint32_t)((uint64_t)now_us / 1000U);
    uint32_t remaining = control->deadline_ms - now_ms;
    if (!remaining || remaining > INT32_MAX) return ESP_ERR_TIMEOUT;
    if (remaining_ms) *remaining_ms = remaining;
    return ESP_OK;
}

static bool wifi_probe_peer_equal(const cerberus_c5_status_t *before,
    const cerberus_c5_status_t *after)
{
    return before->firmware_version == after->firmware_version &&
        before->capabilities == after->capabilities &&
        before->protocol_version == after->protocol_version &&
        before->reset_reason == after->reset_reason &&
        before->boot_count == after->boot_count && after->uptime_ms >= before->uptime_ms;
}

/* Release after every exchange, including each row chunk, so Stop and other
 * radio clients can progress. Full bank epochs make interleaving detectable. */
static esp_err_t wifi_probe_transfer(const wifi_probe_read_control_t *control,
    bool peer_status, cerberus_spi_command_t command, const void *request,
    size_t request_size, void *response, size_t response_size)
{
    uint32_t remaining = 0U;
    esp_err_t err = wifi_probe_read_guard(control, true, &remaining);
    if (err != ESP_OK) return err;
    if (remaining > 100U) remaining = 100U;
    if (take_link_mutex(pdMS_TO_TICKS(remaining)) != pdTRUE) return ESP_ERR_TIMEOUT;
    err = wifi_probe_read_guard(control, false, NULL);
    if (err == ESP_OK) {
        err = peer_status ? get_status_v2_locked(++sequence, response) :
            exchange_v2_locked(command, request, request_size, response, response_size);
    }
    give_link_mutex();
    if (err == ESP_OK) err = wifi_probe_read_guard(control, true, NULL);
    return err;
}

static esp_err_t wifi_probe_bank_read(uint8_t generation,
    const wifi_probe_read_control_t *control, cerberus_c5_wifi_probe_bank_t *proof)
{
    cerberus_c5_wifi_probe_bank_t pending = {0};
    cerberus_c5_status_t before = {0};
    esp_err_t err = wifi_probe_transfer(control, true, CERBERUS_SPI_COMMAND_STATUS,
        NULL, 0U, &before, sizeof(before));
    if (err == ESP_OK && before.firmware_version < CERBERUS_WIFI_PROBE_MIN_FIRMWARE)
        err = ESP_ERR_NOT_SUPPORTED;
    if (err == ESP_OK && before.protocol_version != CERBERUS_PROTOCOL_VERSION)
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && !(before.capabilities &
        (CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ)))
        err = ESP_ERR_INVALID_RESPONSE;
    cerberus_wifi_probe_status_query_t query = {generation};
    if (err == ESP_OK) err = wifi_probe_transfer(control, false,
        CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS, &query, sizeof(query),
        &pending.status, sizeof(pending.status));
    if (err == ESP_OK && (!cerberus_wifi_probe_status_valid(&pending.status) ||
        pending.status.generation != generation || pending.status.state != CERBERUS_WIFI_PROBE_COMPLETE))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = wifi_probe_transfer(control, true, CERBERUS_SPI_COMMAND_STATUS,
        NULL, 0U, &pending.peer, sizeof(pending.peer));
    if (err == ESP_OK && !wifi_probe_peer_equal(&before, &pending.peer))
        err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *proof = pending;
    return err;
}

static esp_err_t wifi_probe_bank_check(const cerberus_c5_wifi_probe_bank_t *proof,
    const wifi_probe_read_control_t *control)
{
    cerberus_c5_wifi_probe_bank_t current = {0};
    esp_err_t err = wifi_probe_bank_read(proof->status.generation, control, &current);
    /* A supported proof becoming an older peer is a failed identity fence,
     * never permission for the caller to fall back to a legacy AP-only bank. */
    if (err == ESP_ERR_NOT_SUPPORTED) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && (memcmp(&proof->status, &current.status, sizeof(proof->status)) ||
        !wifi_probe_peer_equal(&proof->peer, &current.peer))) err = ESP_ERR_INVALID_RESPONSE;
    return err;
}

esp_err_t c5_spi_link_get_wifi_probe_bank(uint8_t generation,
    uint32_t deadline_ms, cerberus_c5_wifi_probe_bank_t *proof)
{
    if (!proof) return ESP_ERR_INVALID_ARG;
    memset(proof, 0, sizeof(*proof));
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    const wifi_probe_read_control_t control = {.deadline_ms = deadline_ms};
    return wifi_probe_bank_read(generation, &control, proof);
}

esp_err_t c5_spi_link_check_wifi_probe_bank(const cerberus_c5_wifi_probe_bank_t *proof,
    uint32_t deadline_ms)
{
    if (!proof || !cerberus_wifi_probe_status_valid(&proof->status) ||
        proof->status.state != CERBERUS_WIFI_PROBE_COMPLETE ||
        proof->peer.firmware_version < CERBERUS_WIFI_PROBE_MIN_FIRMWARE ||
        proof->peer.protocol_version != CERBERUS_PROTOCOL_VERSION) return ESP_ERR_INVALID_ARG;
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    const wifi_probe_read_control_t control = {.deadline_ms = deadline_ms};
    return wifi_probe_bank_check(proof, &control);
}

static bool wifi_probe_same_row(const cerberus_wifi_probe_row_t *a,
    const cerberus_wifi_probe_row_t *b)
{
    return !memcmp(a->transmitter, b->transmitter, sizeof(a->transmitter)) &&
        a->channel == b->channel && a->ssid_length == b->ssid_length &&
        !memcmp(a->ssid, b->ssid, a->ssid_length);
}

esp_err_t c5_spi_link_copy_wifi_probe_results(uint8_t generation,
    uint32_t deadline_ms, cerberus_c5_wifi_probe_bank_t *proof,
    cerberus_wifi_probe_row_t *rows, size_t capacity,
    cerberus_c5_probe_continue_fn continue_fn, void *context)
{
    size_t clear_count = capacity < CERBERUS_WIFI_PROBE_LIMIT ? capacity : CERBERUS_WIFI_PROBE_LIMIT;
    if (proof) memset(proof, 0, sizeof(*proof));
    if (rows) memset(rows, 0, clear_count * sizeof(*rows));
    if (!proof || capacity > CERBERUS_WIFI_PROBE_LIMIT || (!rows && capacity)) return ESP_ERR_INVALID_ARG;
    if (!link_mutex) return ESP_ERR_INVALID_STATE;
    const wifi_probe_read_control_t control = {deadline_ms, continue_fn, context};
    cerberus_c5_wifi_probe_bank_t pending = {0};
    esp_err_t err = wifi_probe_bank_read(generation, &control, &pending);
    if (err == ESP_OK && pending.status.count > capacity) err = ESP_ERR_INVALID_SIZE;
    uint64_t frames = pending.status.dropped_frames;
    for (uint8_t index = 0U; err == ESP_OK && index < pending.status.count; ++index) {
        cerberus_wifi_probe_row_t row = {0};
        for (uint8_t part = 0U; err == ESP_OK && part < CERBERUS_WIFI_PROBE_CHUNKS; ++part) {
            cerberus_wifi_probe_query_t query = {pending.status.epoch, generation, index, part};
            cerberus_wifi_probe_chunk_t chunk = {0};
            err = wifi_probe_transfer(&control, false, CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK,
                &query, sizeof(query), &chunk, sizeof(chunk));
            size_t offset = (size_t)part * CERBERUS_WIFI_PROBE_CHUNK_BYTES;
            size_t length = sizeof(row) - offset;
            if (length > CERBERUS_WIFI_PROBE_CHUNK_BYTES) length = CERBERUS_WIFI_PROBE_CHUNK_BYTES;
            if (err == ESP_OK && (chunk.epoch != pending.status.epoch || chunk.generation != generation ||
                chunk.index != index || chunk.chunk != part || chunk.length != length)) err = ESP_ERR_INVALID_RESPONSE;
            for (size_t i = length; err == ESP_OK && i < sizeof(chunk.data); ++i)
                if (chunk.data[i]) err = ESP_ERR_INVALID_RESPONSE;
            if (err == ESP_OK) memcpy((uint8_t *)&row + offset, chunk.data, length);
        }
        if (err == ESP_OK && (!cerberus_wifi_probe_row_valid(&row) ||
            !(pending.peer.capabilities & (row.channel <= 14U ?
                CERBERUS_C5_CAP_WIFI_2_4_GHZ : CERBERUS_C5_CAP_WIFI_5_GHZ))))
            err = ESP_ERR_INVALID_RESPONSE;
        for (uint8_t i = 0U; err == ESP_OK && i < index; ++i)
            if (wifi_probe_same_row(&rows[i], &row)) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) { rows[index] = row; frames += row.frames; }
    }
    if (frames > UINT32_MAX) frames = UINT32_MAX;
    if (err == ESP_OK && frames != pending.status.matched_frames) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) err = wifi_probe_bank_check(&pending, &control);
    if (err == ESP_OK) *proof = pending;
    else if (rows) memset(rows, 0, clear_count * sizeof(*rows));
    return err;
}
/* END PASSIVE WIFI PROBE READER. */

#include "c5_watch_link.h"
#include "c5_remote_id_link.h"

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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(command, NULL, 0, status, sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    memset(status, 0, sizeof(*status));
    esp_err_t err = exchange_v2_locked(
        command,
        request,
        request_length,
        status,
        sizeof(*status));
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
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
    if (take_link_mutex(pdMS_TO_TICKS(250)) != pdTRUE) {
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
    give_link_mutex();
    return err;
}

#include "c5_csi_link_impl.h"
#include "c5_mac_monitor_link_impl.h"
#include "c5_wifi_relation_link_impl.h"
