#include "cerberus_protocol.h"
#include "cerberus_pins.h"
#ifndef C5_NODE_ID
#define C5_NODE_ID 0
#endif
#ifndef CERBERUS_DISABLE_C5_SPI_IRQ
#define CERBERUS_DISABLE_C5_SPI_IRQ 0
#endif
#ifndef CERBERUS_C5_STATUS_LED_GPIO
#define CERBERUS_C5_STATUS_LED_GPIO -1
#endif
#ifndef CERBERUS_C5_DEFAULT_SCAN_ENABLED
#define CERBERUS_C5_DEFAULT_SCAN_ENABLED 0
#endif

#include "cerberus_spi_protocol.h"
#include "cerberus_config_proof.h"
#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_attr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define C5_SPI_RECORD_QUEUE_SIZE 128
#define C5_OBSERVATION_CACHE_SIZE 512
#define C5_RSSI_FORWARD_MARGIN_DB 3
#define C5_RELOG_ABSENCE_MS (5 * 60 * 1000)
#define C5_MAX_SCAN_CHANNELS 42
#define C5_SCAN_WATCHDOG_MARGIN_MS 1500
#define C5_SCAN_SUPERVISOR_MARGIN_MS 2500

static const char *TAG = "cerberus_c5";
static cerberus_scan_config_t scan_config;
static uint32_t sequence;
static volatile uint32_t records_sent;
static volatile uint32_t scan_cycles;
static wifi_ap_record_t scan_records[32];
static uint8_t scan_channels[C5_MAX_SCAN_CHANNELS];
static uint8_t scan_channel_count;
static uint8_t scan_channel_index;
static volatile bool scan_in_progress;
static volatile bool scan_complete_pending;
static volatile uint8_t scan_in_progress_channel;
static volatile int64_t scan_started_ms;
static volatile int64_t scan_progress_ms;
static volatile uint32_t scan_task_ticks;
static volatile uint8_t scan_phase;
static volatile uint8_t config_result;
static volatile uint32_t applied_config_fingerprint;
static volatile uint32_t scan_rejection_count;
static volatile uint8_t last_rejected_channel;
static volatile uint32_t last_scan_error;

typedef struct {
    uint8_t bssid[6];
    uint32_t last_seen_ms;
    uint32_t generation;
    int8_t forwarded_rssi;
    uint8_t channel;
} c5_observation_cache_entry_t;

static c5_observation_cache_entry_t observation_cache[C5_OBSERVATION_CACHE_SIZE];
static volatile uint32_t observation_cache_generation = 1;

static void clear_observation_cache(void)
{
    observation_cache_generation++;
    if (observation_cache_generation == 0) {
        memset(observation_cache, 0, sizeof(observation_cache));
        observation_cache_generation = 1;
    }
}

static bool should_forward_observation(const wifi_ap_record_t *record)
{
    if (record == NULL) {
        return false;
    }

    uint32_t generation = observation_cache_generation;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
    size_t replacement = 0;
    uint32_t replacement_age = 0;
    bool found_unused = false;

    for (size_t i = 0; i < C5_OBSERVATION_CACHE_SIZE; ++i) {
        c5_observation_cache_entry_t *entry = &observation_cache[i];
        if (entry->generation != generation) {
            if (!found_unused) {
                replacement = i;
                found_unused = true;
            }
            continue;
        }

        if (memcmp(entry->bssid, record->bssid, sizeof(entry->bssid)) == 0) {
            uint32_t absence_ms = now_ms - entry->last_seen_ms;
            entry->last_seen_ms = now_ms;
            if (entry->channel != record->primary || absence_ms >= C5_RELOG_ABSENCE_MS) {
                entry->channel = record->primary;
                entry->forwarded_rssi = record->rssi;
                return true;
            }
            if (record->rssi >= entry->forwarded_rssi + C5_RSSI_FORWARD_MARGIN_DB) {
                entry->forwarded_rssi = record->rssi;
                return true;
            }
            return false;
        }

        if (!found_unused) {
            uint32_t age = now_ms - entry->last_seen_ms;
            if (age >= replacement_age) {
                replacement = i;
                replacement_age = age;
            }
        }
    }

    c5_observation_cache_entry_t *entry = &observation_cache[replacement];
    memcpy(entry->bssid, record->bssid, sizeof(entry->bssid));
    entry->last_seen_ms = now_ms;
    entry->generation = generation;
    entry->forwarded_rssi = record->rssi;
    entry->channel = record->primary;
    return true;
}

static const char *region_country_code(uint8_t region)
{
    switch (region) {
    case CERBERUS_REGION_ETSI:
        return "DE";
    case CERBERUS_REGION_JP:
        return "JP";
    case CERBERUS_REGION_US:
    default:
        return "US";
    }
}

static bool is_dfs_channel(uint8_t channel)
{
    return channel >= 52 && channel <= 144;
}

static void rebuild_scan_channels(void)
{
    scan_channel_count = 0;
    memset(scan_channels, 0, sizeof(scan_channels));
    memset(scan_config.channels, 0, sizeof(scan_config.channels));
    scan_config.channel_count = 0;

    for (uint8_t channel = 1; channel <= 14 && scan_channel_count < C5_MAX_SCAN_CHANNELS; ++channel) {
        if ((scan_config.channels_2g_mask & CERBERUS_2G_CHANNEL_BIT(channel)) == 0) {
            continue;
        }
        scan_channels[scan_channel_count++] = channel;
    }
    for (uint8_t bit = 1; bit <= 28 && scan_channel_count < C5_MAX_SCAN_CHANNELS; ++bit) {
        if ((scan_config.channels_5g_mask & (1u << bit)) == 0) {
            continue;
        }
        uint8_t channel = cerberus_5g_bit_to_channel(bit);
        if (channel != 0) {
            scan_channels[scan_channel_count++] = channel;
        }
    }

    scan_config.channel_count = scan_channel_count;
    uint8_t legacy_count = scan_channel_count < sizeof(scan_config.channels) ?
        scan_channel_count : sizeof(scan_config.channels);
    for (uint8_t i = 0; i < legacy_count; ++i) {
        scan_config.channels[i] = scan_channels[i];
    }
    if (scan_channel_index >= scan_channel_count) {
        scan_channel_index = 0;
    }
}

static WORD_ALIGNED_ATTR uint8_t spi_rx_buffer[CERBERUS_SPI_TRANSFER_SIZE];
static WORD_ALIGNED_ATTR uint8_t spi_tx_buffer[CERBERUS_SPI_TRANSFER_SIZE];
static portMUX_TYPE spi_record_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_spi_scan_record_t spi_record_queue[C5_SPI_RECORD_QUEUE_SIZE];
static volatile uint8_t spi_record_head;
static volatile uint8_t spi_record_tail;
static volatile uint8_t spi_record_count;
static volatile uint8_t spi_record_high_water;
static volatile uint32_t spi_records_dropped;
static bool spi_record_response_pending;
static uint8_t spi_record_response_sequence;

static cerberus_auth_mode_t map_auth(wifi_auth_mode_t auth_mode)
{
    switch (auth_mode) {
    case WIFI_AUTH_OPEN:
        return CERBERUS_AUTH_OPEN;
    case WIFI_AUTH_WEP:
        return CERBERUS_AUTH_WEP;
    case WIFI_AUTH_WPA_PSK:
        return CERBERUS_AUTH_WPA;
    case WIFI_AUTH_WPA2_PSK:
        return CERBERUS_AUTH_WPA2;
    case WIFI_AUTH_WPA_WPA2_PSK:
        return CERBERUS_AUTH_WPA_WPA2;
    case WIFI_AUTH_WPA3_PSK:
        return CERBERUS_AUTH_WPA3;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return CERBERUS_AUTH_WPA2_WPA3;
    default:
        return CERBERUS_AUTH_UNKNOWN;
    }
}

static cerberus_scan_config_t default_scan_config(void)
{
    cerberus_scan_config_t config = {
        .protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION,
        .node_id = C5_NODE_ID,
        .firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION,
        .scan_interval_ms = 3000,
        .dwell_time_ms = 120,
        .passive_dwell_time_ms = 240,
        .channels_2g_mask =
            CERBERUS_2G_CHANNEL_BIT(1) |
            CERBERUS_2G_CHANNEL_BIT(6) |
            CERBERUS_2G_CHANNEL_BIT(11),
        .ble_scan_interval_ms = 100,
        .ble_scan_window_ms = 100,
        .min_rssi = -95,
        .radio_mode = CERBERUS_RADIO_MODE_WIFI,
        .region = CERBERUS_REGION_US,
        .scan_enabled = CERBERUS_C5_DEFAULT_SCAN_ENABLED ? 1 : 0,
    };

    return config;
}

static void prepare_spi_empty_response(void)
{
    memset(spi_tx_buffer, 0, sizeof(spi_tx_buffer));
}

static void prepare_spi_status_response(uint8_t request_sequence)
{
    prepare_spi_empty_response();
    cerberus_spi_status_response_t response = {
        .magic = CERBERUS_SPI_MAGIC,
        .response = CERBERUS_SPI_RESP_STATUS,
        .node_id = C5_NODE_ID,
        .protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION,
        .scan_enabled = scan_config.scan_enabled,
        .sequence = request_sequence,
        .queued_records = spi_record_count,
        .scan_phase = scan_phase,
        .scan_channel = scan_in_progress_channel,
        .firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION,
        .records_sent = records_sent,
        .scan_cycles = scan_cycles,
        .records_dropped = spi_records_dropped,
        .queue_high_water = spi_record_high_water,
    };
    response.reserved[CERBERUS_CONFIG_PROOF_TAG0_AT] = CERBERUS_CONFIG_PROOF_TAG0;
    response.reserved[CERBERUS_CONFIG_PROOF_TAG1_AT] = CERBERUS_CONFIG_PROOF_TAG1;
    response.reserved[CERBERUS_CONFIG_PROOF_VERSION_AT] = CERBERUS_CONFIG_PROOF_VERSION;
    response.reserved[CERBERUS_CONFIG_PROOF_RESULT_AT] = config_result;
    cerberus_config_proof_store_u32(response.reserved + CERBERUS_CONFIG_PROOF_FINGERPRINT_AT,
        applied_config_fingerprint);
    cerberus_config_proof_store_u32(response.reserved + CERBERUS_CONFIG_PROOF_SCAN_REJECTIONS_AT,
        scan_rejection_count);
    response.reserved[CERBERUS_CONFIG_PROOF_LAST_REJECTED_CHANNEL_AT] = last_rejected_channel;
    cerberus_config_proof_store_u32(response.reserved + CERBERUS_CONFIG_PROOF_LAST_SCAN_ERROR_AT,
        last_scan_error);
    memcpy(spi_tx_buffer, &response, sizeof(response) < sizeof(spi_tx_buffer) ? sizeof(response) : sizeof(spi_tx_buffer));
}

static void spi_queue_record(const wifi_ap_record_t *ap_record, uint32_t record_sequence)
{
    if (ap_record == NULL) {
        return;
    }

    cerberus_spi_scan_record_t record = {
        .node_id = C5_NODE_ID,
        .sequence = record_sequence,
        .channel = ap_record->primary,
        .rssi = ap_record->rssi,
        .auth_mode = (uint8_t)map_auth(ap_record->authmode),
    };
    memcpy(record.bssid, ap_record->bssid, sizeof(record.bssid));
    snprintf(record.ssid, sizeof(record.ssid), "%s", ap_record->ssid);

    portENTER_CRITICAL(&spi_record_lock);
    if (spi_record_count >= C5_SPI_RECORD_QUEUE_SIZE) {
        spi_record_tail = (spi_record_tail + 1) % C5_SPI_RECORD_QUEUE_SIZE;
        spi_record_count--;
        spi_records_dropped++;
    }
    spi_record_queue[spi_record_head] = record;
    spi_record_head = (spi_record_head + 1) % C5_SPI_RECORD_QUEUE_SIZE;
    spi_record_count++;
    if (spi_record_count > spi_record_high_water) {
        spi_record_high_water = spi_record_count;
    }
    portEXIT_CRITICAL(&spi_record_lock);
}

static void spi_clear_records(void)
{
    portENTER_CRITICAL(&spi_record_lock);
    spi_record_head = 0;
    spi_record_tail = 0;
    spi_record_count = 0;
    spi_record_high_water = 0;
    spi_records_dropped = 0;
    spi_record_response_pending = false;
    portEXIT_CRITICAL(&spi_record_lock);
}

static bool spi_pop_record(cerberus_spi_scan_record_t *record, uint8_t *queued_after_pop)
{
    bool has_record = false;
    portENTER_CRITICAL(&spi_record_lock);
    if (spi_record_count > 0) {
        if (record != NULL) {
            *record = spi_record_queue[spi_record_tail];
        }
        spi_record_tail = (spi_record_tail + 1) % C5_SPI_RECORD_QUEUE_SIZE;
        spi_record_count--;
        has_record = true;
    }
    if (queued_after_pop != NULL) {
        *queued_after_pop = spi_record_count;
    }
    portEXIT_CRITICAL(&spi_record_lock);
    return has_record;
}

static bool spi_peek_record(cerberus_spi_scan_record_t *record, uint8_t *queued_records)
{
    bool has_record = false;
    portENTER_CRITICAL(&spi_record_lock);
    if (spi_record_count > 0) {
        if (record != NULL) {
            *record = spi_record_queue[spi_record_tail];
        }
        has_record = true;
    }
    if (queued_records != NULL) {
        *queued_records = spi_record_count;
    }
    portEXIT_CRITICAL(&spi_record_lock);
    return has_record;
}

static uint8_t spi_current_record_count(void)
{
    uint8_t queued_records = 0;
    portENTER_CRITICAL(&spi_record_lock);
    queued_records = spi_record_count;
    portEXIT_CRITICAL(&spi_record_lock);
    return queued_records;
}

static uint8_t compact_record_ssid_len(const cerberus_spi_scan_record_t *record)
{
    if (record == NULL) {
        return 0;
    }

    uint8_t len = 0;
    while (len < CERBERUS_MAX_SSID_LEN && record->ssid[len] != '\0') {
        len++;
    }
    return len;
}

static bool append_compact_record(uint8_t *payload, size_t payload_size, size_t *offset, const cerberus_spi_scan_record_t *record)
{
    if (payload == NULL || offset == NULL || record == NULL) {
        return false;
    }

    uint8_t ssid_len = compact_record_ssid_len(record);
    size_t record_len = CERBERUS_SPI_COMPACT_RECORD_BASE_SIZE + ssid_len;
    if (*offset + record_len > payload_size) {
        return false;
    }

    uint8_t *out = &payload[*offset];
    memcpy(&out[0], &record->sequence, sizeof(record->sequence));
    memcpy(&out[4], record->bssid, sizeof(record->bssid));
    out[10] = record->channel;
    out[11] = (uint8_t)record->rssi;
    out[12] = record->auth_mode;
    out[13] = ssid_len;
    if (ssid_len > 0) {
        memcpy(&out[14], record->ssid, ssid_len);
    }
    *offset += record_len;
    return true;
}

static void prepare_spi_record_batch_response(uint8_t request_sequence)
{
    prepare_spi_empty_response();

    cerberus_spi_record_batch_response_t *response = (cerberus_spi_record_batch_response_t *)spi_tx_buffer;
    response->magic = CERBERUS_SPI_MAGIC;
    response->response = CERBERUS_SPI_RESP_RECORD_BATCH;
    response->node_id = C5_NODE_ID;
    response->protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION;
    response->sequence = request_sequence;

    size_t payload_offset = 0;
    while (true) {
        cerberus_spi_scan_record_t record;
        uint8_t queued_before_pop = 0;
        if (!spi_peek_record(&record, &queued_before_pop)) {
            response->queued_records = 0;
            break;
        }

        size_t record_len = CERBERUS_SPI_COMPACT_RECORD_BASE_SIZE + compact_record_ssid_len(&record);
        if (payload_offset + record_len > sizeof(response->payload)) {
            response->queued_records = queued_before_pop;
            break;
        }

        uint8_t queued_after_pop = 0;
        if (!spi_pop_record(&record, &queued_after_pop)) {
            response->queued_records = spi_current_record_count();
            break;
        }
        if (!append_compact_record(response->payload, sizeof(response->payload), &payload_offset, &record)) {
            response->queued_records = queued_after_pop;
            break;
        }

        response->record_count++;
        response->queued_records = queued_after_pop;
        if (queued_after_pop == 0) {
            break;
        }
    }

    response->payload_len = (uint8_t)payload_offset;
    if (response->record_count == 0) {
        prepare_spi_empty_response();
        spi_tx_buffer[0] = (uint8_t)(CERBERUS_SPI_MAGIC & 0xFF);
        spi_tx_buffer[1] = (uint8_t)((CERBERUS_SPI_MAGIC >> 8) & 0xFF);
        spi_tx_buffer[2] = (uint8_t)((CERBERUS_SPI_MAGIC >> 16) & 0xFF);
        spi_tx_buffer[3] = (uint8_t)((CERBERUS_SPI_MAGIC >> 24) & 0xFF);
        spi_tx_buffer[4] = CERBERUS_SPI_RESP_EMPTY;
        spi_tx_buffer[5] = C5_NODE_ID;
        spi_tx_buffer[6] = CERBERUS_EXPANSION_PROTOCOL_VERSION;
        spi_tx_buffer[7] = request_sequence;
        spi_tx_buffer[8] = spi_current_record_count();
    }
}

static uint32_t read_u32_unaligned(const void *value)
{
    uint32_t out = 0;
    memcpy(&out, value, sizeof(out));
    return out;
}

static bool spi_request_valid(const cerberus_spi_request_t *request)
{
    if (request == NULL ||
        read_u32_unaligned(&request->footer.request_magic) != CERBERUS_SPI_REQUEST_MAGIC) {
        return false;
    }
    uint16_t request_guard = 0;
    memcpy(&request_guard, &request->footer.request_guard, sizeof(request_guard));
    return request_guard ==
        CERBERUS_SPI_REQUEST_GUARD_VALUE(request->footer.command, request->footer.sequence);
}

static bool spi_config_guard_valid(const cerberus_spi_config_request_t *request)
{
    return request != NULL &&
        read_u32_unaligned(&request->config_guard) == CERBERUS_SPI_CONFIG_GUARD;
}

static bool apply_spi_config(const cerberus_scan_config_t *config)
{
    if (config == NULL || config->protocol_version != CERBERUS_EXPANSION_PROTOCOL_VERSION) {
        return false;
    }

    uint8_t old_region = scan_config.region;
    if (config->region != old_region) {
        esp_err_t country_err = esp_wifi_set_country_code(region_country_code(config->region), false);
        if (country_err != ESP_OK) {
            ESP_LOGW(TAG, "country update failed: %s", esp_err_to_name(country_err));
            config_result = CERBERUS_CONFIG_RESULT_COUNTRY_FAILED;
            return false;
        }
    }
    scan_config = *config;
    scan_config.scan_enabled = config->scan_enabled ? 1 : 0;
    rebuild_scan_channels();
    if (!scan_config.scan_enabled) {
        scan_channel_index = 0;
        clear_observation_cache();
        spi_clear_records();
    }

    applied_config_fingerprint = cerberus_config_fingerprint(&scan_config);
    config_result = CERBERUS_CONFIG_RESULT_APPLIED;

    ESP_LOGI(TAG, "spi config scan=%u active=%u passive=%u min_rssi=%d channels=%u 2g=0x%04x 5g=0x%08lx",
        scan_config.scan_enabled,
        scan_config.dwell_time_ms,
        scan_config.passive_dwell_time_ms,
        scan_config.min_rssi,
        scan_channel_count,
        scan_config.channels_2g_mask,
        (unsigned long)scan_config.channels_5g_mask);
    return true;
}

static bool spi_config_payload_valid(const cerberus_scan_config_t *config)
{
    return cerberus_config_payload_valid(config, C5_NODE_ID);
}

static void advance_scan_channel(void)
{
    if (scan_channel_count > 0) {
        scan_channel_index = (scan_channel_index + 1) % scan_channel_count;
    } else {
        scan_channel_index = 0;
    }
    scan_cycles++;
    scan_progress_ms = esp_timer_get_time() / 1000;
}

static void spi_post_setup_cb(spi_slave_transaction_t *transaction)
{
    (void)transaction;
#if !CERBERUS_DISABLE_C5_SPI_IRQ
    gpio_set_level(CERBERUS_C5_SLAVE_IRQ_GPIO, 1);
#endif
}

static void spi_post_trans_cb(spi_slave_transaction_t *transaction)
{
    (void)transaction;
#if !CERBERUS_DISABLE_C5_SPI_IRQ
    gpio_set_level(CERBERUS_C5_SLAVE_IRQ_GPIO, 0);
#endif
}

static void spi_slave_task(void *arg)
{
    (void)arg;

    spi_bus_config_t bus_config = {
        .mosi_io_num = CERBERUS_C5_SLAVE_SPI_MOSI_GPIO,
        .miso_io_num = CERBERUS_C5_SLAVE_SPI_MISO_GPIO,
        .sclk_io_num = CERBERUS_C5_SLAVE_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CERBERUS_SPI_TRANSFER_SIZE,
    };

    spi_slave_interface_config_t slave_config = {
        .mode = 0,
        .spics_io_num = CERBERUS_C5_SLAVE_SPI_CS_GPIO,
        .queue_size = 1,
        .flags = 0,
        .post_setup_cb = spi_post_setup_cb,
        .post_trans_cb = spi_post_trans_cb,
    };

    esp_err_t err = ESP_OK;
#if !CERBERUS_DISABLE_C5_SPI_IRQ
    gpio_config_t irq_config = {
        .pin_bit_mask = 1ULL << CERBERUS_C5_SLAVE_IRQ_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    err = gpio_config(&irq_config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI IRQ setup failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    gpio_set_level(CERBERUS_C5_SLAVE_IRQ_GPIO, 0);
#endif

    err = spi_slave_initialize(SPI2_HOST, &bus_config, &slave_config, SPI_DMA_DISABLED);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SPI slave disabled: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "SPI slave ready");
    prepare_spi_empty_response();

    while (true) {
        memset(spi_rx_buffer, 0, sizeof(spi_rx_buffer));

        spi_slave_transaction_t transaction = {
            .length = CERBERUS_SPI_TRANSFER_SIZE * 8,
            .tx_buffer = spi_tx_buffer,
            .rx_buffer = spi_rx_buffer,
        };

        err = spi_slave_transmit(SPI2_HOST, &transaction, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
            continue;
        }

        const cerberus_spi_request_t *request = (const cerberus_spi_request_t *)spi_rx_buffer;
        bool request_valid = spi_request_valid(request);

        if (request_valid && request->footer.command == CERBERUS_SPI_CMD_STATUS) {
            spi_record_response_pending = false;
            prepare_spi_status_response(request->footer.sequence);
        } else if (request_valid && request->footer.command == CERBERUS_SPI_CMD_CONFIG_ARM) {
            spi_record_response_pending = false;
            prepare_spi_empty_response();
        } else if (request_valid && request->footer.command == CERBERUS_SPI_CMD_CONFIG) {
            spi_record_response_pending = false;
            const cerberus_spi_config_request_t *config_request = (const cerberus_spi_config_request_t *)spi_rx_buffer;
            cerberus_scan_config_t received_config;
            memcpy(&received_config, &config_request->config, sizeof(received_config));
            if (received_config.protocol_version == CERBERUS_EXPANSION_PROTOCOL_VERSION &&
                spi_config_guard_valid(config_request) &&
                spi_config_payload_valid(&received_config)) {
                (void)apply_spi_config(&received_config);
                prepare_spi_status_response(request->footer.sequence);
            } else {
                config_result = CERBERUS_CONFIG_RESULT_REJECTED;
                ESP_LOGD(TAG, "ignored SPI config with bad guard 0x%08lx", (unsigned long)config_request->config_guard);
                prepare_spi_status_response(request->footer.sequence);
            }
        } else if (request_valid && request->footer.command == CERBERUS_SPI_CMD_READ_RECORDS) {
            if (!spi_record_response_pending || spi_record_response_sequence != request->footer.sequence) {
                prepare_spi_record_batch_response(request->footer.sequence);
                spi_record_response_sequence = request->footer.sequence;
                spi_record_response_pending = true;
            }
        } else if (request_valid && request->footer.command == CERBERUS_SPI_CMD_NOP) {
            // Keep the prepared response available if the master needs another fetch.
        } else {
            if (!spi_record_response_pending) {
                prepare_spi_empty_response();
            }
        }
    }
}

#if CERBERUS_C5_STATUS_LED_GPIO >= 0
static void status_led_task(void *arg)
{
    (void)arg;

    gpio_config_t led_config = {
        .pin_bit_mask = 1ULL << CERBERUS_C5_STATUS_LED_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    if (gpio_config(&led_config) != ESP_OK) {
        vTaskDelete(NULL);
    }

    bool on = false;
    while (true) {
        gpio_set_level(CERBERUS_C5_STATUS_LED_GPIO, on ? 1 : 0);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(scan_config.scan_enabled ? 150 : 750));
    }
}
#endif

static void scan_watchdog_task(void *arg)
{
    (void)arg;

    while (true) {
        if (scan_in_progress) {
            int64_t now_ms = esp_timer_get_time() / 1000;
            uint16_t dwell_ms = is_dfs_channel(scan_in_progress_channel) ?
                scan_config.passive_dwell_time_ms : scan_config.dwell_time_ms;
            uint32_t timeout_ms = dwell_ms + C5_SCAN_WATCHDOG_MARGIN_MS;
            if (now_ms - scan_started_ms > timeout_ms) {
                ESP_LOGW(TAG, "scan ch%u watchdog stop after %ld ms",
                    scan_in_progress_channel,
                    (long)(now_ms - scan_started_ms));
                (void)esp_wifi_scan_stop();
                scan_in_progress = false;
                scan_complete_pending = false;
                advance_scan_channel();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

static void scan_supervisor_task(void *arg)
{
    (void)arg;

    uint32_t last_cycles = scan_cycles;
    uint32_t last_ticks = scan_task_ticks;
    int64_t quiet_since_ms = esp_timer_get_time() / 1000;

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (!scan_config.scan_enabled) {
            last_cycles = scan_cycles;
            last_ticks = scan_task_ticks;
            quiet_since_ms = now_ms;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (scan_cycles != last_cycles) {
            last_cycles = scan_cycles;
            last_ticks = scan_task_ticks;
            quiet_since_ms = now_ms;
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        uint32_t channel_count = scan_channel_count > 0 ? scan_channel_count : 1;
        uint16_t max_dwell_ms = scan_config.passive_dwell_time_ms > scan_config.dwell_time_ms ?
            scan_config.passive_dwell_time_ms : scan_config.dwell_time_ms;
        uint32_t quiet_limit_ms = ((uint32_t)max_dwell_ms + C5_SCAN_SUPERVISOR_MARGIN_MS) * channel_count;
        if (quiet_limit_ms < 6000) {
            quiet_limit_ms = 6000;
        }

        if (now_ms - quiet_since_ms > quiet_limit_ms) {
            ESP_LOGW(TAG, "scanner wedged after %ld ms phase=%u in_progress=%u complete=%u ticks=%lu->%lu; restarting C5",
                (long)(now_ms - quiet_since_ms),
                scan_phase,
                scan_in_progress ? 1 : 0,
                scan_complete_pending ? 1 : 0,
                (unsigned long)last_ticks,
                (unsigned long)scan_task_ticks);
            vTaskDelay(pdMS_TO_TICKS(50));
            esp_restart();
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_SCAN_DONE) {
        scan_complete_pending = true;
    }
}

static void finish_wifi_scan(void)
{
    uint8_t channel = scan_in_progress_channel;

    scan_phase = 20;
    scan_in_progress = false;
    scan_complete_pending = false;

    uint16_t ap_count = 0;
    scan_phase = 21;
    esp_err_t scan_err = esp_wifi_scan_get_ap_num(&ap_count);
    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "scan ch%u count failed: %s", channel, esp_err_to_name(scan_err));
        goto next_channel;
    }

    uint16_t record_count = ap_count < 32 ? ap_count : 32;
    scan_phase = 22;
    scan_err = esp_wifi_scan_get_ap_records(&record_count, scan_records);
    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "scan ch%u records failed: %s", channel, esp_err_to_name(scan_err));
        goto next_channel;
    }

    scan_phase = 23;
    for (uint16_t i = 0; i < record_count; ++i) {
        if (scan_records[i].rssi < scan_config.min_rssi) {
            continue;
        }
        if (!should_forward_observation(&scan_records[i])) {
            continue;
        }

        spi_queue_record(&scan_records[i], sequence++);
        records_sent++;
    }

next_channel:
    scan_phase = 24;
    advance_scan_channel();
}

static void wifi_scan_step(void)
{
    scan_task_ticks++;
    scan_phase = 1;
    if (!scan_config.scan_enabled) {
        scan_phase = 2;
        if (scan_in_progress) {
            (void)esp_wifi_scan_stop();
        }
        scan_in_progress = false;
        scan_complete_pending = false;
        return;
    }
    if (scan_channel_count == 0) {
        scan_phase = 3;
        return;
    }
    if (scan_complete_pending) {
        scan_phase = 4;
        finish_wifi_scan();
    }
    if (scan_in_progress) {
        scan_phase = 5;
        return;
    }
    if (scan_channel_index >= scan_channel_count) {
        scan_channel_index = 0;
    }

    uint8_t channel = scan_channels[scan_channel_index];
    bool passive_scan = is_dfs_channel(channel);

    wifi_scan_config_t wifi_scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = channel,
        .show_hidden = true,
        .scan_type = passive_scan ? WIFI_SCAN_TYPE_PASSIVE : WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active = {
            .min = scan_config.dwell_time_ms,
            .max = scan_config.dwell_time_ms,
        },
        .scan_time.passive = scan_config.passive_dwell_time_ms,
    };

    scan_in_progress_channel = channel;
    scan_started_ms = esp_timer_get_time() / 1000;
    scan_in_progress = true;
    scan_complete_pending = false;
    scan_phase = 6;
    esp_err_t scan_err = esp_wifi_scan_start(&wifi_scan_config, false);
    if (scan_err != ESP_OK) {
        scan_phase = 7;
        scan_in_progress = false;
        ++scan_rejection_count;
        last_rejected_channel = channel;
        last_scan_error = (uint32_t)scan_err;
        ESP_LOGW(TAG, "scan ch%u failed: %s", channel, esp_err_to_name(scan_err));
        advance_scan_channel();
    } else {
        scan_phase = 8;
    }
}

static void scanner_task(void *arg)
{
    (void)arg;

    ESP_LOGI(TAG, "scanner task ready");
    while (true) {
        wifi_scan_step();
        vTaskDelay(pdMS_TO_TICKS(scan_config.scan_enabled ? 10 : scan_config.scan_interval_ms));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Cerberus C5 scanner starting");
    scan_config = default_scan_config();
    rebuild_scan_channels();
    xTaskCreate(spi_slave_task, "spi_slave", 4096, NULL, 7, NULL);
#if CERBERUS_C5_STATUS_LED_GPIO >= 0
    xTaskCreate(status_led_task, "status_led", 2048, NULL, 3, NULL);
#endif

    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_SCAN_DONE, &wifi_event_handler, NULL));

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_wifi_set_country_code(region_country_code(scan_config.region), false));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO));
    scan_progress_ms = esp_timer_get_time() / 1000;
    xTaskCreate(scanner_task, "scanner", 4096, NULL, 6, NULL);
    xTaskCreate(scan_watchdog_task, "scan_watchdog", 2048, NULL, 4, NULL);
    xTaskCreate(scan_supervisor_task, "scan_supervisor", 3072, NULL, 3, NULL);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
