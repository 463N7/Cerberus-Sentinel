#include "wifi_capture_session.h"
#include "c5_spi_link.h"
#include "storage.h"
#include "wifi_pcap.h"
#include "cerberus_protocol.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static portMUX_TYPE capture_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_s3_capture_status_t capture_status;
static cerberus_wifi_capture_start_request_t capture_request;
static cerberus_s3_capture_options_t capture_options;
static bool capture_worker_active, capture_stop_requested;
static uint32_t capture_started_ms;
/* A failed local start may never reach C5. Retained observers/rows still own
 * that reservation, so do not reuse it while this S3 boot is alive. */
static uint32_t last_reserved_generation;
/* One worker owns these PSRAM buffers. No capture payload remains allocated
 * while idle, and analysis-only sessions do not allocate a PCAP block. */
static uint8_t *capture_frame, *capture_block;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t increment(uint32_t value) { return value == UINT32_MAX ? value : value + 1U; }

static bool stop_requested(void)
{
    portENTER_CRITICAL(&capture_lock);
    bool stop = capture_stop_requested;
    portEXIT_CRITICAL(&capture_lock);
    return stop;
}

static void publish_c5(const cerberus_wifi_capture_status_payload_t *c5)
{
    portENTER_CRITICAL(&capture_lock);
    capture_status.channel = c5->channel;
    capture_status.received = c5->accepted;
    capture_status.dropped_full = c5->dropped_full;
    capture_status.dropped_other = c5->dropped_other;
    capture_status.pending = c5->pending;
    capture_status.elapsed_ms = now_ms() - capture_started_ms;
    portEXIT_CRITICAL(&capture_lock);
}

static void publish_stopping(esp_err_t failure)
{
    portENTER_CRITICAL(&capture_lock);
    capture_status.state = CERBERUS_S3_CAPTURE_STOPPING;
    capture_status.error_code = failure;
    capture_status.elapsed_ms = now_ms() - capture_started_ms;
    portEXIT_CRITICAL(&capture_lock);
}

static void publish_written(size_t bytes)
{
    portENTER_CRITICAL(&capture_lock);
    capture_status.written = increment(capture_status.written);
    capture_status.bytes_written += bytes;
    capture_status.elapsed_ms = now_ms() - capture_started_ms;
    portEXIT_CRITICAL(&capture_lock);
}

static bool status_matches_request(const cerberus_wifi_capture_status_payload_t *status)
{
    return status->generation == capture_request.generation &&
        status->mode_mask == capture_request.mode_mask &&
        (capture_options.hopping || status->channel == capture_request.channel);
}

/* consumed_sequence is independent of SD/ACK success. A lost ACK must never
 * replay an observer or append a second copy, including in the final drain. */
static esp_err_t read_record(const cerberus_wifi_capture_peek_payload_t *peek,
    uint32_t *consumed_sequence, bool write_pcap, bool *storage_failed)
{
    *storage_failed = false;
    if (*consumed_sequence == peek->sequence) return ESP_OK;
    esp_err_t error = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 3U; ++attempt) {
        error = c5_spi_link_wifi_capture_read_frame(peek, capture_frame,
            CERBERUS_WIFI_PCAP_FRAME_MAX);
        if (error == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (error != ESP_OK) return error;
    *consumed_sequence = peek->sequence;
    const cerberus_s3_capture_observation_t observation = {
        .generation = peek->generation, .sequence = peek->sequence,
        .channel = peek->channel, .rssi_dbm = peek->rssi_dbm,
        .observed_us = c5_spi_link_wifi_capture_monotonic_us(peek),
    };
    if (capture_options.observer)
        capture_options.observer(&observation, capture_frame, peek->length,
            capture_options.observer_context);
    if (write_pcap) {
        const cerberus_wifi_pcap_metadata_t metadata = {
            .channel = observation.channel, .rssi_dbm = observation.rssi_dbm,
            .rssi_known = true, .frame_has_fcs = false,
            .time_kind = CERBERUS_WIFI_PCAP_TIME_MONOTONIC,
            .capture_time_us = observation.observed_us,
        };
        size_t size = cerberus_wifi_pcapng_record(&metadata, capture_frame,
            peek->length, peek->length, capture_block, CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE);
        if (!size) error = ESP_ERR_INVALID_SIZE;
        else {
            error = cerberus_storage_wifi_capture_append(capture_block, size);
            memset(capture_block, 0, size);
            if (error != ESP_OK) *storage_failed = true;
            else publish_written(size);
        }
    }
    memset(capture_frame, 0, peek->length);
    return error;
}

static esp_err_t ack_record(uint32_t generation, uint32_t sequence)
{
    esp_err_t error = ESP_FAIL;
    for (unsigned attempt = 0; attempt < 10U; ++attempt) {
        error = c5_spi_link_wifi_capture_ack(generation, sequence);
        if (error == ESP_OK) break;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    return error;
}

static void consume_record(const cerberus_wifi_capture_peek_payload_t *peek,
    uint32_t *consumed_sequence, bool *storage_writable, bool *publish_file,
    esp_err_t *failure)
{
    bool storage_failed = false;
    esp_err_t error = read_record(peek, consumed_sequence,
        capture_options.save_pcap && *storage_writable, &storage_failed);
    if (error != ESP_OK && *failure == ESP_OK) *failure = error;
    if (storage_failed) {
        *storage_writable = false;
        /* A rejected size has not written a partial block. Other write
         * failures can leave a torn tail and must not publish a PCAPNG. */
        if (error != ESP_ERR_INVALID_SIZE) *publish_file = false;
    }
}

static void free_buffers(void)
{
    if (capture_frame) {
        memset(capture_frame, 0, CERBERUS_WIFI_PCAP_FRAME_MAX);
        heap_caps_free(capture_frame);
        capture_frame = NULL;
    }
    if (capture_block) {
        memset(capture_block, 0, CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE);
        heap_caps_free(capture_block);
        capture_block = NULL;
    }
}

static void capture_worker(void *unused)
{
    (void)unused;
    const uint32_t generation = capture_request.generation;
    bool file_open = false, c5_started = false, radio_attempted = false;
    bool storage_writable = true, publish_file = true;
    uint32_t consumed_sequence = 0U;
    esp_err_t failure = ESP_OK;
    if (capture_options.save_pcap && !stop_requested()) {
        uint8_t header[CERBERUS_WIFI_PCAP_FILE_HEADER_SIZE];
        size_t size = cerberus_wifi_pcapng_file_header(
            CERBERUS_WIFI_PCAP_RADIO_C5, header, sizeof(header));
        failure = size ? cerberus_storage_wifi_capture_begin(header, size) : ESP_ERR_INVALID_SIZE;
        file_open = failure == ESP_OK;
    }
    if (failure == ESP_OK && !stop_requested()) {
        cerberus_wifi_capture_status_payload_t c5 = {0};
        radio_attempted = true; /* A lost START reply may still own the radio. */
        failure = c5_spi_link_wifi_capture_start(&capture_request, &c5);
        if (failure == ESP_OK && (!status_matches_request(&c5) ||
            c5.state != CERBERUS_WIFI_CAPTURE_RUNNING)) failure = ESP_ERR_INVALID_RESPONSE;
        if (failure != ESP_OK) {
            cerberus_wifi_capture_status_payload_t check = {0};
            if (c5_spi_link_wifi_capture_status(&check) == ESP_OK &&
                status_matches_request(&check) && check.state == CERBERUS_WIFI_CAPTURE_RUNNING) {
                c5 = check;
                failure = ESP_OK;
            }
        }
        c5_started = failure == ESP_OK;
        if (c5_started) {
            publish_c5(&c5);
            portENTER_CRITICAL(&capture_lock);
            if (!capture_stop_requested) capture_status.state = CERBERUS_S3_CAPTURE_RUNNING;
            portEXIT_CRITICAL(&capture_lock);
        }
    }
    uint32_t last_status = now_ms();
    unsigned link_errors = 0U;
    while (c5_started && failure == ESP_OK && !stop_requested()) {
        if ((uint32_t)(now_ms() - last_status) >= 500U) {
            cerberus_wifi_capture_status_payload_t c5 = {0};
            esp_err_t error = c5_spi_link_wifi_capture_status(&c5);
            last_status = now_ms();
            if (error == ESP_OK && status_matches_request(&c5)) {
                publish_c5(&c5);
                if (c5.state != CERBERUS_WIFI_CAPTURE_RUNNING)
                    failure = c5.error_code ? (esp_err_t)c5.error_code : ESP_ERR_TIMEOUT;
            } else if (error == ESP_OK) failure = ESP_ERR_INVALID_RESPONSE;
            if (failure != ESP_OK) break;
        }
        cerberus_wifi_capture_peek_payload_t peek = {0};
        esp_err_t error = c5_spi_link_wifi_capture_peek(generation, &peek);
        if (error != ESP_OK) {
            if (++link_errors >= 3U) failure = error;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        link_errors = 0U;
        if (!peek.sequence) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        consume_record(&peek, &consumed_sequence, &storage_writable, &publish_file, &failure);
        /* Even an unreadable/unsavable record must release its pinned slot.
         * Failure is retained and causes Stop; counters never claim it saved. */
        error = ack_record(generation, peek.sequence);
        if (error != ESP_OK && failure == ESP_OK) failure = error;
    }

    /* Keep the worker/context alive until C5 really stops and drains. There
     * are only two slots; retrying the same sequence never replays ingestion.
     * A different generation belongs to another session/reset and is never
     * stopped or acknowledged here. */
    unsigned cleanup_errors = 0U;
    while (radio_attempted) {
        publish_stopping(failure);
        cerberus_wifi_capture_status_payload_t c5 = {0};
        esp_err_t error = c5_spi_link_wifi_capture_stop(generation, &c5);
        if (error != ESP_OK) error = c5_spi_link_wifi_capture_status(&c5);
        if (error == ESP_OK && c5.generation != generation) {
            if (failure == ESP_OK) failure = ESP_ERR_INVALID_RESPONSE;
            break; /* Our radio generation no longer exists. */
        }
        if (error == ESP_OK) {
            publish_c5(&c5);
            if (c5.state == CERBERUS_WIFI_CAPTURE_STOPPED) {
                if (c5.error_code != ESP_OK && failure == ESP_OK)
                    failure = (esp_err_t)c5.error_code;
                cerberus_wifi_capture_peek_payload_t peek = {0};
                error = c5_spi_link_wifi_capture_peek(generation, &peek);
                if (error == ESP_OK && !peek.sequence && c5.pending == 0U) break;
                if (error == ESP_OK && peek.sequence) {
                    consume_record(&peek, &consumed_sequence, &storage_writable,
                        &publish_file, &failure);
                    error = ack_record(generation, peek.sequence);
                } else if (error == ESP_OK) error = ESP_ERR_INVALID_RESPONSE;
            } else error = c5.error_code ? (esp_err_t)c5.error_code : ESP_ERR_INVALID_RESPONSE;
        }
        if (error != ESP_OK) {
            if (++cleanup_errors >= 3U && failure == ESP_OK) failure = error;
        } else cleanup_errors = 0U;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    char name[32] = {0};
    uint64_t final_bytes = 0U;
    if (file_open) {
        esp_err_t error;
        do {
            publish_stopping(failure);
            error = cerberus_storage_wifi_capture_finish(c5_started && publish_file,
                name, sizeof(name), &final_bytes);
            /* Timeout leaves FILE/USB ownership held. Never publish idle or
             * release caller-owned observer state before the close occurs. */
            if (error == ESP_ERR_TIMEOUT) {
                publish_stopping(failure != ESP_OK ? failure : error);
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        } while (error == ESP_ERR_TIMEOUT);
        if (error != ESP_OK && failure == ESP_OK) failure = error;
    }
    free_buffers();
    portENTER_CRITICAL(&capture_lock);
    capture_status.error_code = failure;
    capture_status.elapsed_ms = now_ms() - capture_started_ms;
    if (name[0]) {
        memcpy(capture_status.file_name, name, sizeof(capture_status.file_name));
        capture_status.bytes_written = final_bytes;
    }
    capture_status.state = failure != ESP_OK ? CERBERUS_S3_CAPTURE_ERROR :
        !c5_started ? CERBERUS_S3_CAPTURE_IDLE : capture_options.save_pcap ?
        CERBERUS_S3_CAPTURE_SAVED : CERBERUS_S3_CAPTURE_COMPLETE;
    capture_worker_active = false;
    capture_status.busy = false;
    portEXIT_CRITICAL(&capture_lock);
    vTaskDelete(NULL);
}

esp_err_t cerberus_s3_capture_start_options_generation(
    const cerberus_s3_capture_options_t *options, uint32_t *generation_out)
{
    if (generation_out) *generation_out = 0U;
    if (!options || !options->mode_mask ||
        (options->mode_mask & ~CERBERUS_WIFI_CAPTURE_MODE_ALL) != 0U ||
        (options->hopping ? (options->channel != 0U || options->filter_bssid ||
            options->mode_mask != CERBERUS_WIFI_CAPTURE_MODE_BEACON) :
            (options->channel == 0U || (options->channel > 14U &&
                (options->channel < 32U || options->channel > 177U)))) ||
        (options->filter_bssid && ((options->bssid[0] & 1U) != 0U ||
            !(options->bssid[0] | options->bssid[1] | options->bssid[2] |
              options->bssid[3] | options->bssid[4] | options->bssid[5]))))
        return ESP_ERR_INVALID_ARG;
    /* Reserve before querying C5: concurrent starts cannot both preflight the
     * same old generation, and Stop during this query is not discarded. */
    portENTER_CRITICAL(&capture_lock);
    if (capture_worker_active) {
        portEXIT_CRITICAL(&capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    capture_options = *options;
    capture_started_ms = now_ms();
    capture_stop_requested = false;
    capture_worker_active = true;
    capture_status = (cerberus_s3_capture_status_t){
        .state = CERBERUS_S3_CAPTURE_STARTING,
        .channel = options->channel, .mode_mask = options->mode_mask,
        .filter_bssid = options->filter_bssid, .busy = true,
        .hopping = options->hopping, .save_pcap = options->save_pcap,
    };
    portEXIT_CRITICAL(&capture_lock);
    cerberus_wifi_capture_status_payload_t remote = {0};
    esp_err_t error = c5_spi_link_wifi_capture_status(&remote);
    if (error == ESP_OK && (remote.generation == UINT32_MAX ||
        remote.state == CERBERUS_WIFI_CAPTURE_RUNNING || remote.pending != 0U))
        error = ESP_ERR_INVALID_STATE;
    if (error != ESP_OK) {
        portENTER_CRITICAL(&capture_lock);
        capture_worker_active = false;
        capture_status.busy = false;
        capture_status.state = CERBERUS_S3_CAPTURE_ERROR;
        capture_status.error_code = error;
        portEXIT_CRITICAL(&capture_lock);
        return error;
    }
    portENTER_CRITICAL(&capture_lock);
    uint32_t floor = remote.generation > last_reserved_generation ?
        remote.generation : last_reserved_generation;
    if (floor == UINT32_MAX) {
        capture_worker_active = false;
        capture_status.busy = false;
        capture_status.state = CERBERUS_S3_CAPTURE_ERROR;
        capture_status.error_code = ESP_ERR_INVALID_STATE;
        portEXIT_CRITICAL(&capture_lock);
        return ESP_ERR_INVALID_STATE;
    }
    const uint32_t generation = floor + 1U;
    last_reserved_generation = generation;
    capture_request = (cerberus_wifi_capture_start_request_t){
        .generation = generation, .channel = options->channel,
        .mode_mask = options->mode_mask,
        .flags = (options->filter_bssid ? CERBERUS_WIFI_CAPTURE_FLAG_BSSID : 0U) |
            (options->hopping ? CERBERUS_WIFI_CAPTURE_FLAG_HOP : 0U),
        .lease_ms = 5000U,
    };
    if (options->filter_bssid) memcpy(capture_request.bssid, options->bssid, 6U);
    capture_status.generation = capture_request.generation;
    portEXIT_CRITICAL(&capture_lock);
    capture_frame = heap_caps_malloc(CERBERUS_WIFI_PCAP_FRAME_MAX,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (capture_frame && options->save_pcap)
        capture_block = heap_caps_malloc(CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!capture_frame || (options->save_pcap && !capture_block) ||
        xTaskCreate(capture_worker, "wifi_capture", 6144U, NULL, 3U, NULL) != pdPASS) {
        free_buffers();
        portENTER_CRITICAL(&capture_lock);
        capture_worker_active = false;
        capture_status.busy = false;
        capture_status.state = CERBERUS_S3_CAPTURE_ERROR;
        capture_status.error_code = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&capture_lock);
        return ESP_ERR_NO_MEM;
    }
    /* xTaskCreate can schedule the worker before returning. It may already
     * finish and be replaced; never reread the shared request/status here. */
    if (generation_out) *generation_out = generation;
    return ESP_OK;
}

esp_err_t cerberus_s3_capture_start_options(const cerberus_s3_capture_options_t *options)
{
    return cerberus_s3_capture_start_options_generation(options, NULL);
}

esp_err_t cerberus_s3_capture_start(uint8_t channel, uint8_t mode_mask,
    const uint8_t bssid[6], bool filter_bssid)
{
    if (filter_bssid && !bssid) return ESP_ERR_INVALID_ARG;
    cerberus_s3_capture_options_t options = {
        .channel = channel, .mode_mask = mode_mask,
        .filter_bssid = filter_bssid, .save_pcap = true,
    };
    if (filter_bssid) memcpy(options.bssid, bssid, sizeof(options.bssid));
    return cerberus_s3_capture_start_options(&options);
}

void cerberus_s3_capture_stop(void)
{
    portENTER_CRITICAL(&capture_lock);
    if (capture_worker_active) {
        capture_stop_requested = true;
        capture_status.state = CERBERUS_S3_CAPTURE_STOPPING;
    }
    portEXIT_CRITICAL(&capture_lock);
}

bool cerberus_s3_capture_stop_generation(uint32_t generation)
{
    portENTER_CRITICAL(&capture_lock);
    bool owned = generation != 0U && capture_worker_active &&
        capture_status.generation == generation;
    if (owned) {
        capture_stop_requested = true;
        capture_status.state = CERBERUS_S3_CAPTURE_STOPPING;
    }
    portEXIT_CRITICAL(&capture_lock);
    return owned;
}

void cerberus_s3_capture_status(cerberus_s3_capture_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&capture_lock);
    *out = capture_status;
    if (capture_worker_active) out->elapsed_ms = now_ms() - capture_started_ms;
    portEXIT_CRITICAL(&capture_lock);
}
