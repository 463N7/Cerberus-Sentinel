#include "wardrive_service.h"

#include "c5_spi_link.h"
#include "field_survey.h"
#include "field_survey_stream.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "s3_wifi_owner.h"
#include "storage.h"
#include "wardrive_core.h"
#include "wardrive_wigle.h"
#include "wardrive_gps_uart.h"
#include "wifi_capture_session.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

#define WARDRIVE_WORKER_STACK 6144U
#define WARDRIVE_POLL_MS 100U
#define WARDRIVE_EXPANSION_POLL_MS 1000U
#define WARDRIVE_SYNC_MS 2000U
#define WARDRIVE_STOP_GRACE_MS 9000U
#define WARDRIVE_EXPANSION_STOP_GRACE_MS 3000U
#define WARDRIVE_C5_STATUS_GRACE_MS 12000U
#define WARDRIVE_C5_CAPTURE_GRACE_MS 12000U

static const char wardrive_header[] = CERBERUS_WARDRIVE_UNLOCATED_HEADER;

static const char wigle_header[] =
    CERBERUS_WIGLE_METADATA_HEADER CERBERUS_WIGLE_COLUMN_HEADER;

static portMUX_TYPE wardrive_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_wardrive_status_t wardrive_status;
static bool worker_active, start_in_progress, stop_requested;
static bool c5_has_channel_survey;
static uint64_t started_ms;
/* These bounded buffers are needed only during a session. Keep the idle S3
 * internal heap available for NimBLE's first controller/host initialization. */
typedef struct {
    cerberus_wardrive_dedupe_t dedupe;
    cerberus_wardrive_dedupe_t wigle_dedupe;
    cerberus_wardrive_probe_dedupe_t probe_dedupe;
    cerberus_wardrive_relation_dedupe_t relation_dedupe;
    cerberus_c5_wifi_result_t wifi_stage[CERBERUS_WIFI_SCAN_MAX_RESULTS];
    cerberus_wifi_probe_row_t probe_stage[CERBERUS_WIFI_PROBE_LIMIT];
    cerberus_c5_wifi_probe_bank_t probe_bank;
    cerberus_wifi_relation_row_t relation_stage[CERBERUS_WIFI_RELATION_LIMIT];
    cerberus_c5_wifi_relation_bank_t relation_bank;
    bool wifi_capture_ready;
    uint8_t wifi_capture_generation, wifi_capture_count, wifi_capture_next, probe_capture_next;
    uint8_t relation_capture_next;
    cerberus_wardrive_probe_state_t probe_capture_state;
    cerberus_wardrive_relation_state_t relation_capture_state;
    cerberus_c5_ble_result_t ble_stage[CERBERUS_BLE_SCAN_MAX_RESULTS];
    cerberus_s3_ble_wardrive_evidence_t ble_evidence_stage[CERBERUS_BLE_SCAN_MAX_RESULTS];
    bool ble_capture_ready;
    uint8_t ble_capture_generation, ble_capture_count, ble_capture_next;
    field_survey_snapshot_t expansion_stage;
    expansion_record_t expansion_event;
    uint32_t expansion_dropped_at_start_by_slot[6];
    char row_buffer[384];
} wardrive_workspace_t;
static wardrive_workspace_t *workspace;
#define dedupe (workspace->dedupe)
#define wigle_dedupe (workspace->wigle_dedupe)
#define probe_dedupe (workspace->probe_dedupe)
#define relation_dedupe (workspace->relation_dedupe)
#define wifi_stage (workspace->wifi_stage)
#define probe_stage (workspace->probe_stage)
#define probe_bank (workspace->probe_bank)
#define relation_stage (workspace->relation_stage)
#define relation_bank (workspace->relation_bank)
#define wifi_capture_ready (workspace->wifi_capture_ready)
#define wifi_capture_generation (workspace->wifi_capture_generation)
#define wifi_capture_count (workspace->wifi_capture_count)
#define wifi_capture_next (workspace->wifi_capture_next)
#define probe_capture_next (workspace->probe_capture_next)
#define probe_capture_state (workspace->probe_capture_state)
#define relation_capture_next (workspace->relation_capture_next)
#define relation_capture_state (workspace->relation_capture_state)
#define ble_stage (workspace->ble_stage)
#define ble_evidence_stage (workspace->ble_evidence_stage)
#define ble_capture_ready (workspace->ble_capture_ready)
#define ble_capture_generation (workspace->ble_capture_generation)
#define ble_capture_count (workspace->ble_capture_count)
#define ble_capture_next (workspace->ble_capture_next)
#define expansion_stage (workspace->expansion_stage)
#define expansion_event (workspace->expansion_event)
#define expansion_dropped_at_start_by_slot (workspace->expansion_dropped_at_start_by_slot)
#define row_buffer (workspace->row_buffer)
static uint32_t open_file_rows;
static bool wardrive_file_open;
static uint32_t wigle_file_rows;
static bool wigle_file_open;

static esp_err_t allocate_workspace(void)
{
    if (workspace != NULL) return ESP_ERR_INVALID_STATE;
    workspace = heap_caps_calloc(1U, sizeof(*workspace),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return workspace != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static void release_workspace(void)
{
    heap_caps_free(workspace);
    workspace = NULL;
}

static size_t post_ble_reserve_bytes(bool enable_gps)
{
    return 32768U + WARDRIVE_WORKER_STACK +
        (enable_gps ? CERBERUS_WARDRIVE_GPS_RX_BUFFER_BYTES +
            CERBERUS_WARDRIVE_GPS_TASK_STACK_BYTES + 2048U : 4096U);
}

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }
static uint64_t now_ms64(void) { return (uint64_t)(esp_timer_get_time() / 1000LL); }
static bool before_deadline(uint32_t deadline)
{
    return (int32_t)(deadline - now_ms()) > 0;
}
static esp_err_t start_optional_gps(bool enabled)
{
    return enabled ? cerberus_wardrive_gps_uart_start() : ESP_OK;
}

static esp_err_t stop_optional_gps(bool enabled)
{
    return enabled ? cerberus_wardrive_gps_uart_stop() : ESP_OK;
}

static void snapshot_optional_gps(bool enabled,
    cerberus_wardrive_gps_snapshot_t *snapshot, esp_err_t *uart_error)
{
    if (enabled) {
        cerberus_wardrive_gps_uart_snapshot(snapshot, uart_error);
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->sentence_age_ms = UINT32_MAX;
    snapshot->fix_age_ms = UINT32_MAX;
    snapshot->utc_age_ms = UINT32_MAX;
    if (uart_error != NULL) *uart_error = ESP_OK;
}

static uint32_t plus_one(uint32_t value) { return value == UINT32_MAX ? value : value + 1U; }
static uint32_t plus_saturated(uint32_t value, uint32_t amount)
{
    return amount > UINT32_MAX - value ? UINT32_MAX : value + amount;
}

static bool wants_stop(void)
{
    portENTER_CRITICAL(&wardrive_lock);
    bool wanted = stop_requested;
    portEXIT_CRITICAL(&wardrive_lock);
    return wanted;
}

static void publish_error(esp_err_t error, char radio)
{
    portENTER_CRITICAL(&wardrive_lock);
    if (radio == 'C') wardrive_status.c5_error = error;
    else if (radio == 'B') wardrive_status.s3_ble_error = error;
    else if (radio == 'E') wardrive_status.expansion_error = error;
    else wardrive_status.sd_error = error;
    if (wardrive_status.error_code == ESP_OK) wardrive_status.error_code = error;
    portEXIT_CRITICAL(&wardrive_lock);
}

static void publish_sighting(const cerberus_wardrive_observation_t *observation,
    bool logged)
{
    portENTER_CRITICAL(&wardrive_lock);
    if (observation->type == CERBERUS_WARDRIVE_WIFI_RELATION) {
        wardrive_status.c5_relation_observations = plus_one(wardrive_status.c5_relation_observations);
        if (logged) wardrive_status.c5_relation_logged = plus_one(wardrive_status.c5_relation_logged);
    } else if (observation->type == CERBERUS_WARDRIVE_WIFI_PROBE) {
        wardrive_status.c5_probe_observations = plus_one(wardrive_status.c5_probe_observations);
        if (logged) wardrive_status.c5_probe_logged = plus_one(wardrive_status.c5_probe_logged);
    } else if (observation->radio == CERBERUS_WARDRIVE_RADIO_C5) {
        wardrive_status.c5_wifi_observations = plus_one(wardrive_status.c5_wifi_observations);
        if (logged) wardrive_status.c5_wifi_logged = plus_one(wardrive_status.c5_wifi_logged);
    } else if (observation->radio == CERBERUS_WARDRIVE_RADIO_S3) {
        wardrive_status.s3_ble_observations = plus_one(wardrive_status.s3_ble_observations);
        if (logged) wardrive_status.s3_ble_logged = plus_one(wardrive_status.s3_ble_logged);
        cerberus_ble_flock_level_t level =
            cerberus_s3_ble_wardrive_evidence_level(&observation->ble_evidence);
        if (level != CERBERUS_BLE_FLOCK_LEVEL_NONE)
            wardrive_status.s3_ble_flock_observations = plus_one(wardrive_status.s3_ble_flock_observations);
        if (level == CERBERUS_BLE_FLOCK_LEVEL_CLUE)
            wardrive_status.s3_ble_flock_clue = plus_one(wardrive_status.s3_ble_flock_clue);
        else if (level == CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE)
            wardrive_status.s3_ble_flock_signature = plus_one(wardrive_status.s3_ble_flock_signature);
        else if (level == CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED)
            wardrive_status.s3_ble_flock_corroborated = plus_one(wardrive_status.s3_ble_flock_corroborated);
    } else {
        wardrive_status.expansion_observations = plus_one(wardrive_status.expansion_observations);
        if (logged) wardrive_status.expansion_logged = plus_one(wardrive_status.expansion_logged);
    }
    if (logged) wardrive_status.sd_rows = plus_one(wardrive_status.sd_rows);
    else wardrive_status.duplicate_sightings = plus_one(wardrive_status.duplicate_sightings);
    wardrive_status.dedupe_evicted = plus_saturated(
        plus_saturated(dedupe.evicted, wigle_dedupe.evicted),
        plus_saturated(probe_dedupe.evicted, relation_dedupe.evicted));
    portEXIT_CRITICAL(&wardrive_lock);
}

static void publish_invalid(void)
{
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.invalid_sightings = plus_one(wardrive_status.invalid_sightings);
    portEXIT_CRITICAL(&wardrive_lock);
}

static esp_err_t begin_log_stream(bool located)
{
    const char *header = located ? wigle_header : wardrive_header;
    size_t length = located ? sizeof(wigle_header) - 1U : sizeof(wardrive_header) - 1U;
    esp_err_t error = located ? cerberus_storage_wardrive_wigle_begin(header, length) :
        cerberus_storage_wardrive_begin(header, length);
    if (error == ESP_OK) {
        if (located) wigle_file_open = true;
        else wardrive_file_open = true;
        portENTER_CRITICAL(&wardrive_lock);
        wardrive_status.sd_bytes += length;
        portEXIT_CRITICAL(&wardrive_lock);
    }
    return error;
}

static esp_err_t save_log_stream(bool located, bool keep)
{
    char name[24] = {0};
    uint64_t bytes = 0U;
    esp_err_t error;
    do {
        error = located ? cerberus_storage_wardrive_wigle_finish(keep, name, sizeof(name), &bytes) :
            cerberus_storage_wardrive_finish(keep, name, sizeof(name), &bytes);
        /* A mutex timeout leaves the FILE open. Keep ownership until close. */
        if (error == ESP_ERR_TIMEOUT) vTaskDelay(pdMS_TO_TICKS(100));
    } while (error == ESP_ERR_TIMEOUT);
    if (located) { wigle_file_open = false; wigle_file_rows = 0U; }
    else { wardrive_file_open = false; open_file_rows = 0U; }
    if (error != ESP_OK) { publish_error(error, 'S'); return error; }
    portENTER_CRITICAL(&wardrive_lock);
    if (keep) {
        wardrive_status.files_saved = plus_one(wardrive_status.files_saved);
        memcpy(wardrive_status.last_file_name, name, sizeof(name));
    } else {
        wardrive_status.sd_bytes -= located ? sizeof(wigle_header) - 1U :
            sizeof(wardrive_header) - 1U;
    }
    portEXIT_CRITICAL(&wardrive_lock);
    return ESP_OK;
}

static esp_err_t save_open_file(bool keep) { return save_log_stream(false, keep); }

static void finish_storage_session(void)
{
    esp_err_t error;
    do {
        error = cerberus_storage_wardrive_session_finish();
        if (error == ESP_ERR_TIMEOUT) vTaskDelay(pdMS_TO_TICKS(100));
    } while (error == ESP_ERR_TIMEOUT);
    if (error != ESP_OK) publish_error(error, 'S');
}

static esp_err_t append_observation(const cerberus_wardrive_observation_t *observation)
{
    const uint64_t uptime = now_ms64();
    if (!cerberus_wardrive_observation_valid(observation)) {
        publish_invalid();
        return ESP_OK;
    }
    cerberus_wardrive_gps_snapshot_t gps = {0};
    snapshot_optional_gps(wardrive_status.gps_requested, &gps, NULL);
    size_t row_length = 0U;
    bool located = false;
    bool probe = observation->type == CERBERUS_WARDRIVE_WIFI_PROBE;
    bool relation = observation->type == CERBERUS_WARDRIVE_WIFI_RELATION;
    if (wardrive_status.gps_requested && !probe && !relation) {
        /* Radio scan results do not carry synchronized RF timestamps. Match
         * the preserved firmware's GPS-at-result-receipt semantics and label
         * it in WiGLE metadata; never invent the original observation time. */
        cerberus_wigle_time_provenance_t time = {
            .source = CERBERUS_WIGLE_TIME_RESULT_RECEIPT,
            .result_received_monotonic_ms = uptime,
            .gps_snapshot_monotonic_ms = now_ms64(),
        };
        located = cerberus_wardrive_wigle_format_observation(observation, &gps,
            &time, row_buffer, sizeof(row_buffer), &row_length) == CERBERUS_WIGLE_OK;
    }
    if (!located) {
        row_length = cerberus_wardrive_format_unlocated_row(observation, &gps,
            uptime, uptime - started_ms, row_buffer, sizeof(row_buffer));
        if (row_length == 0U) return ESP_ERR_INVALID_SIZE;
    }
    /* Separate histories let the first fresh fix record an identity that was
     * previously written without location. A failed append never commits. */
    cerberus_wardrive_dedupe_t *history = located ? &wigle_dedupe : &dedupe;
    cerberus_wardrive_decision_t decision = relation ?
        cerberus_wardrive_relation_decide(&relation_dedupe, observation, (uint32_t)uptime) : probe ?
        cerberus_wardrive_probe_decide(&probe_dedupe, observation, (uint32_t)uptime) : located ?
        cerberus_wardrive_decide(history, observation, (uint32_t)uptime) :
        cerberus_wardrive_decide_evidence(history, observation, (uint32_t)uptime);
    if (!decision.should_log) {
        publish_sighting(observation, false);
        return ESP_OK;
    }
    esp_err_t error = ESP_OK;
    if (!(located ? wigle_file_open : wardrive_file_open))
        error = begin_log_stream(located);
    if (error == ESP_OK)
        error = located ? cerberus_storage_wardrive_wigle_append(row_buffer, row_length) :
            cerberus_storage_wardrive_append(row_buffer, row_length);
    if (error == ESP_ERR_INVALID_SIZE &&
        (located ? wigle_file_open : wardrive_file_open)) {
        error = save_log_stream(located, true);
        if (error == ESP_OK) error = begin_log_stream(located);
        if (error == ESP_OK)
            error = located ? cerberus_storage_wardrive_wigle_append(row_buffer, row_length) :
                cerberus_storage_wardrive_append(row_buffer, row_length);
    }
    if (error != ESP_OK) { publish_error(error, 'S'); return error; }
    if (relation) cerberus_wardrive_relation_commit(&relation_dedupe, observation, decision, (uint32_t)uptime);
    else if (probe) cerberus_wardrive_probe_commit(&probe_dedupe, observation, decision, (uint32_t)uptime);
    else cerberus_wardrive_commit(history, observation, decision, (uint32_t)uptime);
    if (located) ++wigle_file_rows;
    else ++open_file_rows;
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.sd_bytes += row_length;
    if (located) wardrive_status.geotagged_rows = plus_one(wardrive_status.geotagged_rows);
    else {
        wardrive_status.unlocated_rows = plus_one(wardrive_status.unlocated_rows);
        if (wardrive_status.gps_requested && !probe && !relation)
            wardrive_status.gps_fallback_rows = plus_one(wardrive_status.gps_fallback_rows);
    }
    portEXIT_CRITICAL(&wardrive_lock);
    publish_sighting(observation, true);
    return ESP_OK;
}

static void reset_wifi_capture(void)
{
    wifi_capture_ready = false;
    wifi_capture_generation = wifi_capture_count = wifi_capture_next = probe_capture_next = 0U;
    relation_capture_next = 0U;
    probe_capture_state = CERBERUS_WARDRIVE_PROBE_WAITING;
    relation_capture_state = CERBERUS_WARDRIVE_RELATION_WAITING;
    memset(&probe_bank, 0, sizeof(probe_bank));
    memset(&relation_bank, 0, sizeof(relation_bank));
}

static bool continue_wifi_capture(void *context)
{
    const bool *final_drain = context;
    return *final_drain || !wants_stop();
}

static esp_err_t capture_wifi(uint8_t generation, uint8_t count, bool final_drain,
    uint32_t deadline, char *error_radio)
{
    *error_radio = 'C';
    if (count > CERBERUS_WIFI_SCAN_MAX_RESULTS) return ESP_ERR_INVALID_SIZE;
    if (!final_drain && wants_stop()) return ESP_OK;
    esp_err_t error = ESP_OK;
    if (wifi_capture_ready) {
        if (wifi_capture_generation != generation || wifi_capture_count != count)
            return ESP_ERR_INVALID_STATE;
    } else {
        if (!before_deadline(deadline)) return ESP_ERR_TIMEOUT;
        error = c5_spi_link_copy_wifi_probe_results(generation, deadline,
            &probe_bank, probe_stage, CERBERUS_WIFI_PROBE_LIMIT,
            continue_wifi_capture, &final_drain);
        if (error == ESP_ERR_NOT_SUPPORTED) {
            /* An older peer still supports the AP/BLE session. Do not report
             * unavailable probe collection as a sweep with zero detections. */
            memset(&probe_bank, 0, sizeof(probe_bank));
            probe_capture_state = CERBERUS_WARDRIVE_PROBE_UNSUPPORTED;
        } else {
            if (error == ESP_ERR_NOT_FINISHED && !final_drain && wants_stop()) return ESP_OK;
            if (error != ESP_OK) return error;
            probe_capture_state = probe_bank.status.flags & CERBERUS_WIFI_PROBE_AVAILABLE ?
                CERBERUS_WARDRIVE_PROBE_AVAILABLE : CERBERUS_WARDRIVE_PROBE_UNAVAILABLE;
        }
        error = c5_spi_link_copy_wifi_relations(generation, deadline,
            &relation_bank, relation_stage, CERBERUS_WIFI_RELATION_LIMIT,
            continue_wifi_capture, &final_drain);
        if (error == ESP_ERR_NOT_SUPPORTED) {
            memset(&relation_bank, 0, sizeof(relation_bank));
            relation_capture_state = CERBERUS_WARDRIVE_RELATION_UNSUPPORTED;
        } else {
            if (error == ESP_ERR_NOT_FINISHED && !final_drain && wants_stop()) return ESP_OK;
            if (error != ESP_OK) return error;
            relation_capture_state = relation_bank.status.flags & CERBERUS_WIFI_RELATION_AVAILABLE ?
                CERBERUS_WARDRIVE_RELATION_AVAILABLE : CERBERUS_WARDRIVE_RELATION_UNAVAILABLE;
            if (relation_bank.status.count > CERBERUS_WIFI_RELATION_LIMIT)
                return ESP_ERR_INVALID_RESPONSE;
            for (uint8_t i = 0U; i < relation_bank.status.count; ++i)
                if (!cerberus_wifi_relation_row_valid(&relation_stage[i]))
                    return ESP_ERR_INVALID_RESPONSE;
        }
        for (uint8_t i = 0U; i < count; ++i) {
            if (!final_drain && wants_stop()) return ESP_OK;
            if (!before_deadline(deadline)) return ESP_ERR_TIMEOUT;
            error = c5_spi_link_get_wifi_scan_result(generation, i, &wifi_stage[i]);
            if (error != ESP_OK) return error;
        }
        cerberus_wifi_scan_status_payload_t end = {0};
        if (!before_deadline(deadline)) return ESP_ERR_TIMEOUT;
        error = c5_spi_link_get_wifi_scan_status(&end);
        if (error != ESP_OK) return error;
        if (end.state != CERBERUS_WIFI_SCAN_COMPLETE || end.generation != generation ||
            end.stored_count != count) return ESP_ERR_INVALID_STATE;
        if (probe_capture_state != CERBERUS_WARDRIVE_PROBE_UNSUPPORTED) {
            error = c5_spi_link_check_wifi_probe_bank(&probe_bank, deadline);
            if (error != ESP_OK) return error;
        }
        if (relation_capture_state != CERBERUS_WARDRIVE_RELATION_UNSUPPORTED) {
            error = c5_spi_link_check_wifi_relation_bank(&relation_bank, deadline);
            if (error != ESP_OK) return error;
        }
        /* No SD write precedes these proofs. Keep the AP/probe/relation bank and
         * cursors immutable until the next accepted START, including Stop. */
        wifi_capture_generation = generation;
        wifi_capture_count = count;
        wifi_capture_next = probe_capture_next = relation_capture_next = 0U;
        wifi_capture_ready = true;
    }
    *error_radio = 'S';
    for (uint8_t i = wifi_capture_next; i < count && (final_drain || !wants_stop()); ++i) {
        const cerberus_c5_wifi_result_t *source = &wifi_stage[i];
        cerberus_wardrive_observation_t observation = {
            .type = CERBERUS_WARDRIVE_WIFI,
            .radio = CERBERUS_WARDRIVE_RADIO_C5,
            .channel = source->channel,
            .rssi_dbm = source->rssi,
            .security = source->security,
        };
        memcpy(observation.address, source->bssid, 6U);
        size_t label_length = strnlen(source->ssid, CERBERUS_WARDRIVE_LABEL_CAPACITY);
        observation.label_length = (uint8_t)label_length;
        memcpy(observation.label, source->ssid, label_length);
        error = append_observation(&observation);
        if (error != ESP_OK) return error;
        wifi_capture_next = (uint8_t)(i + 1U);
    }
    for (uint8_t i = probe_capture_next; i < probe_bank.status.count &&
            (final_drain || !wants_stop()); ++i) {
        cerberus_wardrive_observation_t observation = {0};
        if (!cerberus_wardrive_observation_from_probe(&probe_stage[i], &observation)) {
            *error_radio = 'C';
            return ESP_ERR_INVALID_RESPONSE;
        }
        error = append_observation(&observation);
        if (error != ESP_OK) return error;
        probe_capture_next = (uint8_t)(i + 1U);
    }
    for (uint8_t i = relation_capture_next; i < relation_bank.status.count &&
            (final_drain || !wants_stop()); ++i) {
        cerberus_wardrive_observation_t observation = {0};
        if (!cerberus_wardrive_observation_from_relation(&relation_stage[i], &observation)) {
            *error_radio = 'C';
            return ESP_ERR_INVALID_RESPONSE;
        }
        error = append_observation(&observation);
        if (error != ESP_OK) return error;
        relation_capture_next = (uint8_t)(i + 1U);
    }
    return ESP_OK;
}

/* Only C5 result/status timeouts are retryable. Storage uses the same error
 * code for a contended SD lease, so the caller must retain the error source. */
static esp_err_t capture_wifi_with_retry(uint8_t generation, uint8_t count,
    bool final_drain, uint32_t deadline, bool *captured, char *error_radio)
{
    *captured = false;
    *error_radio = 'C';
    if (!before_deadline(deadline)) return ESP_ERR_TIMEOUT;
    esp_err_t error = capture_wifi(generation, count, final_drain,
        deadline, error_radio);
    if (error == ESP_ERR_TIMEOUT && *error_radio == 'C' &&
        before_deadline(deadline)) return ESP_OK;
    if (error == ESP_OK) *captured = wifi_capture_ready &&
        wifi_capture_next == count && probe_capture_next == probe_bank.status.count &&
        relation_capture_next == relation_bank.status.count;
    return error;
}

/* A survey-status timeout is a pre-START SPI mutex miss. Defer the scan so
 * BLE can continue, but fail a continuously contended bus after 12 seconds. */
static esp_err_t wifi_survey_ready(bool *ready, bool *timeout_pending,
    uint32_t *timeout_deadline)
{
    *ready = false;
    cerberus_wifi_channel_survey_status_payload_t survey = {0};
    esp_err_t error = c5_spi_link_wifi_channel_survey_status(&survey);
    if (error == ESP_ERR_TIMEOUT) {
        if (!*timeout_pending) {
            *timeout_deadline = now_ms() + WARDRIVE_C5_STATUS_GRACE_MS;
            *timeout_pending = true;
        }
        return before_deadline(*timeout_deadline) ? ESP_OK : ESP_ERR_TIMEOUT;
    }
    *timeout_pending = false;
    if (error != ESP_OK) return error;
    if (survey.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING)
        return ESP_ERR_INVALID_STATE;
    *ready = true;
    return ESP_OK;
}

static esp_err_t capture_ble(uint8_t generation, uint8_t count, bool final_drain)
{
    if (count > CERBERUS_BLE_SCAN_MAX_RESULTS) return ESP_ERR_INVALID_SIZE;
    if (!final_drain && wants_stop()) return ESP_OK;
    /* One completed-bank copy binds each result and its independent packet
     * evidence under the scanner mutex. No row getter can race a new sweep. */
    esp_err_t error = ESP_OK;
    if (ble_capture_ready) {
        if (ble_capture_generation != generation || ble_capture_count != count)
            return ESP_ERR_INVALID_STATE;
    } else {
        error = cerberus_s3_ble_scanner_copy_wardrive(generation, count,
            ble_stage, ble_evidence_stage, CERBERUS_BLE_SCAN_MAX_RESULTS);
        if (error != ESP_OK) return error;
        ble_capture_generation = generation;
        ble_capture_count = count;
        ble_capture_next = 0U;
        ble_capture_ready = true;
    }
    /* Cleanup resumes this immutable bank after a mid-row Stop. Successful
     * rows must not be counted again when the final drain finishes the sweep. */
    for (uint8_t i = ble_capture_next; i < count && (final_drain || !wants_stop()); ++i) {
        const cerberus_c5_ble_result_t *source = &ble_stage[i];
        cerberus_wardrive_observation_t observation = {
            .type = CERBERUS_WARDRIVE_BLE,
            .radio = CERBERUS_WARDRIVE_RADIO_S3,
            .address_type = source->address_type,
            .rssi_dbm = source->rssi,
            .manufacturer_id = source->manufacturer_id,
            .ble_evidence = ble_evidence_stage[i],
        };
        /* NimBLE stores address bytes in little-endian order. CSV uses the
         * usual display order so it matches the device detail view. */
        for (uint8_t j = 0U; j < 6U; ++j)
            observation.address[j] = source->address[5U - j];
        size_t label_length = strnlen(source->name, CERBERUS_WARDRIVE_LABEL_CAPACITY);
        observation.label_length = (uint8_t)label_length;
        memcpy(observation.label, source->name, label_length);
        error = append_observation(&observation);
        if (error != ESP_OK) return error;
        ble_capture_next = (uint8_t)(i + 1U);
    }
    return ESP_OK;
}

static esp_err_t capture_expansions(bool final_drain)
{
    field_survey_snapshot(&expansion_stage);
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.expansion_present_mask =
        expansion_stage.present_mask & expansion_stage.scanning_mask;
    wardrive_status.expansion_radio_dropped = 0U;
    for (unsigned slot = 0U; slot < 6U; ++slot) {
        uint32_t current = expansion_stage.radio_dropped_by_slot[slot];
        uint32_t baseline = expansion_dropped_at_start_by_slot[slot];
        uint32_t delta = current >= baseline ? current - baseline : current;
        wardrive_status.expansion_radio_dropped_by_slot[slot] = delta;
        wardrive_status.expansion_radio_dropped += delta;
    }
    portEXIT_CRITICAL(&wardrive_lock);
    /* A direct FIFO preserves fresh reports even when the survey's separate
     * 32-identity viewer cache replaces entries between one-second polls. */
    uint16_t pending = 0U;
    for (unsigned processed = 0U; processed < FIELD_SURVEY_STREAM_CAPACITY;
            ++processed) {
        if (!final_drain && wants_stop()) return ESP_ERR_NOT_FINISHED;
        uint32_t overflow = 0U, invalid = 0U, by_slot[6] = {0};
        uint16_t queued = 0U;
        size_t count = field_survey_stream_read(&expansion_event, 1U,
            &overflow, &invalid, &queued, by_slot);
        pending = queued;
        portENTER_CRITICAL(&wardrive_lock);
        wardrive_status.expansion_queue_overflow = overflow;
        wardrive_status.expansion_queue_invalid = invalid;
        wardrive_status.expansion_queue_pending = queued;
        memcpy(wardrive_status.expansion_queue_overflow_by_slot, by_slot,
            sizeof(by_slot));
        portEXIT_CRITICAL(&wardrive_lock);
        if (count == 0U) return ESP_OK;
        const expansion_record_t *source = &expansion_event;
        cerberus_wardrive_observation_t observation = {
            .type = CERBERUS_WARDRIVE_WIFI,
            .radio = (uint8_t)(source->radio + 1U),
            .channel = source->channel,
            .rssi_dbm = source->rssi,
            .security = source->security,
        };
        memcpy(observation.address, source->bssid, 6U);
        size_t label_length = source->ssid_length;
        if (label_length > CERBERUS_WARDRIVE_LABEL_CAPACITY)
            label_length = CERBERUS_WARDRIVE_LABEL_CAPACITY;
        observation.label_length = (uint8_t)label_length;
        memcpy(observation.label, source->ssid, label_length);
        esp_err_t error = append_observation(&observation);
        if (error != ESP_OK) return error;
    }
    /* A healthy acknowledged Stop has no producer and can drain at most the
     * FIFO capacity. A still-producing expansion cannot hold Stop forever. */
    return final_drain && pending != 0U ? ESP_ERR_TIMEOUT : ESP_OK;
}

static void publish_completed_wifi(const cerberus_wifi_scan_status_payload_t *status)
{
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.c5_wifi_sweeps = plus_one(wardrive_status.c5_wifi_sweeps);
    wardrive_status.c5_wifi_scan_dropped = plus_saturated(wardrive_status.c5_wifi_scan_dropped,
        status->total_count > status->stored_count ? status->total_count - status->stored_count : 0U);
    wardrive_status.c5_probe_state = probe_capture_state;
    if (probe_capture_state == CERBERUS_WARDRIVE_PROBE_AVAILABLE) {
        wardrive_status.c5_probe_frames = plus_saturated(wardrive_status.c5_probe_frames,
            probe_bank.status.matched_frames);
        wardrive_status.c5_probe_dropped = plus_saturated(wardrive_status.c5_probe_dropped,
            probe_bank.status.dropped_frames);
    } else {
        wardrive_status.c5_probe_unavailable_sweeps = plus_one(wardrive_status.c5_probe_unavailable_sweeps);
    }
    wardrive_status.c5_relation_state = relation_capture_state;
    if (relation_capture_state == CERBERUS_WARDRIVE_RELATION_AVAILABLE) {
        wardrive_status.c5_relation_frames = plus_saturated(wardrive_status.c5_relation_frames,
            relation_bank.status.matched_frames);
        wardrive_status.c5_relation_dropped = plus_saturated(wardrive_status.c5_relation_dropped,
            relation_bank.status.dropped_frames);
    } else {
        wardrive_status.c5_relation_unavailable_sweeps = plus_one(wardrive_status.c5_relation_unavailable_sweeps);
    }
    portEXIT_CRITICAL(&wardrive_lock);
}

static esp_err_t drain_completed_wifi(const cerberus_wifi_scan_status_payload_t *status,
    uint32_t deadline, bool *captured, char *error_radio)
{
    esp_err_t error = capture_wifi_with_retry(status->generation,
        status->stored_count, true, deadline, captured, error_radio);
    if (error != ESP_OK || !*captured) return error;
    publish_completed_wifi(status);
    return ESP_OK;
}

static void drain_completed_ble(const cerberus_ble_scan_status_payload_t *status)
{
    esp_err_t error = capture_ble(status->generation, status->stored_count, true);
    if (error != ESP_OK) { publish_error(error, 'B'); return; }
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.s3_ble_sweeps = plus_one(wardrive_status.s3_ble_sweeps);
    wardrive_status.s3_ble_scan_dropped += status->dropped_reports;
    portEXIT_CRITICAL(&wardrive_lock);
}

static void stop_own_scans(bool wifi_own, uint8_t wifi_generation,
    bool wifi_start_uncertain, uint8_t wifi_start_baseline,
    bool ble_own, uint8_t ble_generation)
{
    uint32_t deadline = now_ms() + WARDRIVE_STOP_GRACE_MS;
    esp_err_t wifi_last_error = ESP_OK, ble_last_error = ESP_OK;
    while ((wifi_own || wifi_start_uncertain || ble_own) &&
        before_deadline(deadline)) {
        if (wifi_own || wifi_start_uncertain) {
            cerberus_wifi_scan_status_payload_t status = {0};
            esp_err_t error = c5_spi_link_get_wifi_scan_status(&status);
            if (error != ESP_OK) wifi_last_error = error;
            else if (wifi_start_uncertain) {
                uint8_t expected = (uint8_t)(wifi_start_baseline + 1U);
                if (status.generation == expected) {
                    reset_wifi_capture();
                    wifi_own = true;
                    wifi_generation = expected;
                    wifi_start_uncertain = false;
                } else if (status.generation == wifi_start_baseline &&
                    status.state != CERBERUS_WIFI_SCAN_RUNNING) {
                    /* The unanswered START did not acquire a generation. */
                    wifi_start_uncertain = false;
                } else {
                    publish_error(ESP_ERR_INVALID_STATE, 'C');
                    wifi_start_uncertain = false;
                }
            }
            if (error == ESP_OK && wifi_own &&
                status.generation != wifi_generation) {
                /* Another owner replaced our generation. Never stop it. */
                publish_error(ESP_ERR_INVALID_STATE, 'C');
                wifi_own = false;
            } else if (error == ESP_OK && wifi_own) {
                if (status.state == CERBERUS_WIFI_SCAN_RUNNING) {
                    error = c5_spi_link_stop_wifi_scan(&status);
                    if (error != ESP_OK) wifi_last_error = error;
                    else if (status.generation != wifi_generation) {
                        publish_error(ESP_ERR_INVALID_STATE, 'C');
                        wifi_own = false;
                    }
                }
                if (error == ESP_OK && wifi_own &&
                    status.state == CERBERUS_WIFI_SCAN_COMPLETE) {
                    bool captured = false;
                    char error_radio = 'C';
                    error = drain_completed_wifi(&status, deadline,
                        &captured, &error_radio);
                    if (error != ESP_OK) {
                        publish_error(error, error_radio);
                        wifi_own = false;
                    } else if (captured) wifi_own = false;
                    else wifi_last_error = ESP_ERR_TIMEOUT;
                } else if (error == ESP_OK && wifi_own &&
                    status.state == CERBERUS_WIFI_SCAN_ERROR) {
                    publish_error(status.error_code != ESP_OK ?
                        (esp_err_t)status.error_code : ESP_FAIL, 'C');
                    wifi_own = false;
                } else if (error == ESP_OK && wifi_own &&
                    status.state != CERBERUS_WIFI_SCAN_RUNNING) {
                    wifi_own = false;
                }
            }
        }
        if (ble_own) {
            cerberus_ble_scan_status_payload_t status = {0};
            esp_err_t error = cerberus_s3_ble_scanner_get_status(&status);
            if (error != ESP_OK) ble_last_error = error;
            else if (status.generation != ble_generation) {
                publish_error(ESP_ERR_INVALID_STATE, 'B');
                ble_own = false;
            } else if (status.state == CERBERUS_BLE_SCAN_RUNNING) {
                error = cerberus_s3_ble_scanner_stop(&status);
                if (error != ESP_OK) ble_last_error = error;
                else if (status.generation != ble_generation) {
                    publish_error(ESP_ERR_INVALID_STATE, 'B');
                    ble_own = false;
                } else if (status.state != CERBERUS_BLE_SCAN_RUNNING) {
                    if (status.state == CERBERUS_BLE_SCAN_COMPLETE)
                        drain_completed_ble(&status);
                    ble_own = false;
                }
            } else {
                if (status.state == CERBERUS_BLE_SCAN_COMPLETE)
                    drain_completed_ble(&status);
                if (status.state == CERBERUS_BLE_SCAN_ERROR)
                    publish_error(status.error_code != ESP_OK ?
                        (esp_err_t)status.error_code : ESP_FAIL, 'B');
                ble_own = false;
            }
        }
        if (wifi_own || wifi_start_uncertain || ble_own)
            vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (wifi_own) publish_error(wifi_last_error != ESP_OK ? wifi_last_error : ESP_ERR_TIMEOUT, 'C');
    if (wifi_start_uncertain)
        publish_error(wifi_last_error != ESP_OK ? wifi_last_error : ESP_ERR_TIMEOUT, 'C');
    if (ble_own) publish_error(ble_last_error != ESP_OK ? ble_last_error : ESP_ERR_TIMEOUT, 'B');
}

static void stop_expansions(void)
{
    esp_err_t error = field_survey_set_running(false);
    if (error != ESP_OK) publish_error(error, 'E');
    else {
        field_survey_snapshot(&expansion_stage);
        uint32_t stop_epoch = expansion_stage.request_epoch;
        uint32_t deadline = now_ms() + WARDRIVE_EXPANSION_STOP_GRACE_MS;
        bool acknowledged = false;
        do {
            field_survey_snapshot(&expansion_stage);
            if (!expansion_stage.requested &&
                expansion_stage.worker_seen_epoch == stop_epoch &&
                expansion_stage.scanning_mask == 0U &&
                expansion_stage.uncertain_mask == 0U) {
                acknowledged = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        } while ((int32_t)(deadline - now_ms()) > 0);
        if (!acknowledged) {
            /* The field worker continues its own STOP retries. */
            publish_error(ESP_ERR_TIMEOUT, 'E');
        }
    }
    /* Stop acknowledgment means no further R1-R6 queue push is in flight.
     * Drain every retained report before closing the SD file or USB handoff. */
    error = capture_expansions(true);
    if (error != ESP_OK) publish_error(error, 'E');
    field_survey_stream_close();
    /* Manual Field Survey must not inherit Wardriving's channel allocation. */
    error = field_survey_set_expansion_plan(NULL);
    if (error != ESP_OK) publish_error(error, 'E');
}

static void stop_scans_and_close_file(bool wifi_own, uint8_t wifi_generation,
    bool wifi_start_uncertain, uint8_t wifi_start_baseline,
    bool ble_own, uint8_t ble_generation)
{
    if (wardrive_status.expansions_requested)
        (void)field_survey_set_running(false);
    stop_own_scans(wifi_own, wifi_generation, wifi_start_uncertain,
        wifi_start_baseline, ble_own, ble_generation);
    if (wardrive_status.expansions_requested) stop_expansions();
    bool keep = open_file_rows != 0U;
    if (wardrive_file_open) (void)save_open_file(keep);
    if (wigle_file_open) (void)save_log_stream(true, wigle_file_rows != 0U);
    finish_storage_session();
    esp_err_t error = field_survey_set_idle_probe_enabled(true);
    if (error != ESP_OK) publish_error(error, 'E');
}

static void wardrive_worker(void *unused)
{
    (void)unused;
    /* An unwired receiver is the default. GPS owns no UART/task/heap until
     * explicitly enabled for this session. It never gates C5/S3 scanning. */
    bool gps_enabled = wardrive_status.gps_requested;
    esp_err_t gps_error = start_optional_gps(gps_enabled);
    bool wifi_own = false, ble_own = false;
    uint8_t wifi_generation = 0U, ble_generation = 0U;
    uint32_t last_expansion = now_ms(), last_sync = now_ms();
    uint32_t last_c5_status_ok = now_ms();
    uint32_t wifi_capture_deadline = 0U;
    bool wifi_capture_pending = false;
    uint32_t wifi_survey_timeout_deadline = 0U;
    bool wifi_survey_timeout_pending = false;
    uint32_t wifi_start_retry_deadline = 0U;
    bool wifi_start_retry_active = false, wifi_start_uncertain = false;
    uint8_t wifi_start_baseline = 0U;
    uint8_t ble_release_retries = 0U;
    esp_err_t failure = ESP_OK;
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.gps_error = gps_error;
    wardrive_status.state = CERBERUS_WARDRIVE_RUNNING;
    portEXIT_CRITICAL(&wardrive_lock);
    while (!wants_stop() && failure == ESP_OK) {
        if (wifi_start_retry_active &&
            !before_deadline(wifi_start_retry_deadline)) {
            failure = ESP_ERR_TIMEOUT;
            publish_error(failure, 'C');
            break;
        }
        cerberus_wifi_scan_status_payload_t wifi = {0};
        esp_err_t wifi_poll_error = c5_spi_link_get_wifi_scan_status(&wifi);
        if (wifi_poll_error == ESP_ERR_TIMEOUT &&
            (uint32_t)(now_ms() - last_c5_status_ok) < WARDRIVE_C5_STATUS_GRACE_MS &&
            (!wifi_capture_pending || before_deadline(wifi_capture_deadline))) {
            /* A status transaction can time out while a C5 scan is running.
             * Keep the owned generation and let the independent S3 BLE sweep
             * progress. A sustained outage still fails after the grace period. */
            portENTER_CRITICAL(&wardrive_lock);
            wardrive_status.c5_wifi_status_timeouts =
                plus_one(wardrive_status.c5_wifi_status_timeouts);
            portEXIT_CRITICAL(&wardrive_lock);
        } else if (wifi_poll_error != ESP_OK) {
            failure = wifi_poll_error;
            publish_error(failure, 'C');
            break;
        } else {
            last_c5_status_ok = now_ms();
            if (wifi_start_uncertain) {
                uint8_t expected = (uint8_t)(wifi_start_baseline + 1U);
                if (wifi.generation == expected) {
                    /* START reached C5 even though its SPI response was lost. */
                    reset_wifi_capture();
                    wifi_own = true;
                    wifi_generation = expected;
                    wifi_start_retry_active = false;
                    wifi_start_uncertain = false;
                } else if (wifi.generation == wifi_start_baseline &&
                    wifi.state != CERBERUS_WIFI_SCAN_RUNNING) {
                    wifi_start_uncertain = false;
                } else {
                    failure = ESP_ERR_INVALID_STATE;
                    publish_error(failure, 'C');
                    break;
                }
            }
            if (wifi_own) {
                if (wifi.generation != wifi_generation) failure = ESP_ERR_INVALID_STATE;
                else if (wifi.state == CERBERUS_WIFI_SCAN_COMPLETE) {
                    if (!wifi_capture_pending) {
                        wifi_capture_deadline = now_ms() + WARDRIVE_C5_CAPTURE_GRACE_MS;
                        wifi_capture_pending = true;
                    }
                    bool captured = false;
                    char error_radio = 'C';
                    failure = capture_wifi_with_retry(wifi_generation,
                        wifi.stored_count, false, wifi_capture_deadline,
                        &captured, &error_radio);
                    /* Stop may arrive while fetching the bounded generation.
                     * Leave ownership set so cleanup can drain it in full. */
                    if (failure == ESP_OK && wants_stop()) break;
                    if (failure != ESP_OK) {
                        publish_error(failure, error_radio);
                        /* A failed append may have reached the SD stream.
                         * Do not replay that generation during Stop. */
                        if (error_radio == 'S') wifi_own = false;
                        break;
                    }
                    if (captured) {
                        publish_completed_wifi(&wifi);
                        wifi_own = false;
                        wifi_capture_pending = false;
                    }
                } else if (wifi_capture_pending)
                    failure = ESP_ERR_INVALID_STATE;
                else if (wifi.state != CERBERUS_WIFI_SCAN_RUNNING)
                    failure = wifi.error_code != ESP_OK ? (esp_err_t)wifi.error_code : ESP_FAIL;
            } else if (wifi.state == CERBERUS_WIFI_SCAN_RUNNING) failure = ESP_ERR_INVALID_STATE;
            if (failure != ESP_OK) { publish_error(failure, 'C'); break; }
            if (!wifi_own && !wants_stop()) {
                bool start_ready = true;
                if (c5_has_channel_survey) {
                    failure = wifi_survey_ready(&start_ready,
                        &wifi_survey_timeout_pending,
                        &wifi_survey_timeout_deadline);
                    if (failure != ESP_OK) { publish_error(failure, 'C'); break; }
                }
                if (start_ready) {
                    if (wifi_start_retry_active &&
                        !before_deadline(wifi_start_retry_deadline)) {
                        failure = ESP_ERR_TIMEOUT;
                        publish_error(failure, 'C');
                        break;
                    }
                    uint8_t start_baseline = wifi.generation;
                    failure = c5_spi_link_start_wifi_scan(&wifi);
                    uint8_t expected = (uint8_t)(start_baseline + 1U);
                    if (failure == ESP_OK && wifi.generation == expected &&
                        (wifi.state == CERBERUS_WIFI_SCAN_RUNNING ||
                         wifi.state == CERBERUS_WIFI_SCAN_COMPLETE)) {
                        reset_wifi_capture();
                        wifi_own = true;
                        wifi_generation = wifi.generation;
                        wifi_capture_pending = false;
                        wifi_start_retry_active = false;
                    } else if (failure == ESP_ERR_TIMEOUT ||
                        failure == ESP_ERR_INVALID_RESPONSE || failure == ESP_OK) {
                        /* A lock timeout issued no command. An invalid SPI
                         * response may hide a successful START, so reconcile
                         * the generation before considering another START. */
                        if (!wifi_start_retry_active) {
                            wifi_start_retry_deadline = now_ms() +
                                WARDRIVE_C5_STATUS_GRACE_MS;
                            wifi_start_retry_active = true;
                        }
                        wifi_start_uncertain = failure != ESP_ERR_TIMEOUT;
                        wifi_start_baseline = start_baseline;
                        if (!before_deadline(wifi_start_retry_deadline)) {
                            if (failure == ESP_OK) failure = ESP_ERR_INVALID_RESPONSE;
                            publish_error(failure, 'C');
                            break;
                        }
                        failure = ESP_OK;
                    } else {
                        publish_error(failure, 'C');
                        break;
                    }
                }
            }
        }
        cerberus_ble_scan_status_payload_t ble = {0};
        failure = cerberus_s3_ble_scanner_get_status(&ble);
        if (failure != ESP_OK) { publish_error(failure, 'B'); break; }
        if (ble_own) {
            if (ble.generation != ble_generation) failure = ESP_ERR_INVALID_STATE;
            else if (ble.state == CERBERUS_BLE_SCAN_COMPLETE) {
                failure = capture_ble(ble_generation, ble.stored_count, false);
                if (failure == ESP_OK && wants_stop()) break;
                if (failure == ESP_OK) {
                    portENTER_CRITICAL(&wardrive_lock);
                    wardrive_status.s3_ble_sweeps = plus_one(wardrive_status.s3_ble_sweeps);
                    wardrive_status.s3_ble_scan_dropped += ble.dropped_reports;
                    portEXIT_CRITICAL(&wardrive_lock);
                }
                ble_own = false;
            } else if (ble.state != CERBERUS_BLE_SCAN_RUNNING)
                failure = ble.error_code != ESP_OK ? (esp_err_t)ble.error_code : ESP_FAIL;
        } else if (ble.state == CERBERUS_BLE_SCAN_RUNNING) failure = ESP_ERR_INVALID_STATE;
        if (failure != ESP_OK) { publish_error(failure, 'B'); break; }
        if (!ble_own && !wants_stop()) {
            cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
            if (owner == CERBERUS_S3_BLE_OWNER_SCANNER &&
                ble.state == CERBERUS_BLE_SCAN_COMPLETE &&
                ble_release_retries < 20U) {
                /* finish_scan publishes COMPLETE before releasing the host.
                 * Give that callback time to finish before claiming it again. */
                ++ble_release_retries;
                vTaskDelay(pdMS_TO_TICKS(WARDRIVE_POLL_MS));
                continue;
            }
            ble_release_retries = 0U;
            if (owner != CERBERUS_S3_BLE_OWNER_NONE) {
                publish_error(ESP_ERR_INVALID_STATE, 'B');
                break;
            }
            failure = cerberus_s3_ble_scanner_start(&ble);
            if (failure == ESP_OK && ble.state == CERBERUS_BLE_SCAN_RUNNING) {
                /* Accepted START distinguishes a new bank even after the
                 * public generation byte wraps. */
                ble_capture_ready = false;
                ble_capture_next = 0U;
                ble_own = true;
                ble_generation = ble.generation;
            } else {
                if (failure == ESP_OK) failure = ESP_ERR_INVALID_RESPONSE;
                publish_error(failure, 'B');
                break;
            }
        }
        uint32_t now = now_ms();
        if (wardrive_status.expansions_requested &&
            (uint32_t)(now - last_expansion) >= WARDRIVE_EXPANSION_POLL_MS) {
            failure = capture_expansions(false);
            last_expansion = now;
            if (failure == ESP_ERR_NOT_FINISHED && wants_stop()) {
                failure = ESP_OK;
                break;
            }
            if (failure != ESP_OK) { publish_error(failure, 'E'); break; }
        }
        if ((uint32_t)(now - last_sync) >= WARDRIVE_SYNC_MS) {
            failure = wardrive_file_open ? cerberus_storage_wardrive_sync() : ESP_OK;
            if (failure == ESP_OK && wigle_file_open)
                failure = cerberus_storage_wardrive_wigle_sync();
            last_sync = now;
            if (failure != ESP_OK) { publish_error(failure, 'S'); break; }
        }
        portENTER_CRITICAL(&wardrive_lock);
        wardrive_status.elapsed_ms = (uint32_t)(now_ms64() - started_ms);
        portEXIT_CRITICAL(&wardrive_lock);
        vTaskDelay(pdMS_TO_TICKS(WARDRIVE_POLL_MS));
    }
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.state = CERBERUS_WARDRIVE_STOPPING;
    portEXIT_CRITICAL(&wardrive_lock);
    stop_scans_and_close_file(wifi_own, wifi_generation,
        wifi_start_uncertain, wifi_start_baseline, ble_own, ble_generation);
    gps_error = stop_optional_gps(gps_enabled);
    if (gps_error != ESP_OK) {
        portENTER_CRITICAL(&wardrive_lock);
        wardrive_status.gps_error = gps_error;
        portEXIT_CRITICAL(&wardrive_lock);
    }
    release_workspace();
    portENTER_CRITICAL(&wardrive_lock);
    wardrive_status.elapsed_ms = (uint32_t)(now_ms64() - started_ms);
    wardrive_status.state = wardrive_status.error_code == ESP_OK ?
        CERBERUS_WARDRIVE_STOPPED : CERBERUS_WARDRIVE_ERROR;
    worker_active = false;
    portEXIT_CRITICAL(&wardrive_lock);
    vTaskDelete(NULL);
}

static esp_err_t prepare_expansion_plan(uint8_t present_mask)
{
    present_mask &= 0x3fU;
    if (present_mask == 0U) {
        /* Keep the legacy fallback so a first session can discover R1-R6. */
        return field_survey_set_expansion_plan(NULL);
    }
    cerberus_wardrive_plan_t plan;
    uint8_t available = (uint8_t)(1U | (present_mask << 1U));
    if (!cerberus_wardrive_plan_build(available, CERBERUS_REGION_US,
            CERBERUS_WARDRIVE_BAND_MIXED,
            CERBERUS_WARDRIVE_TIMING_BALANCED, &plan))
        return ESP_ERR_INVALID_ARG;
    return field_survey_set_expansion_plan(&plan);
}

static esp_err_t wardrive_start_with_idle_probes_paused(bool include_expansions,
    bool enable_gps)
{
    cerberus_c5_status_t c5 = {0};
    esp_err_t error = c5_spi_link_get_status(&c5);
    if (error != ESP_OK) return error;
    if ((c5.capabilities & (CERBERUS_C5_CAP_WIFI_2_4_GHZ |
         CERBERUS_C5_CAP_WIFI_5_GHZ)) == 0U || c5.radio_state != 0U)
        return ESP_ERR_INVALID_STATE;
    cerberus_wifi_scan_status_payload_t wifi = {0};
    error = c5_spi_link_get_wifi_scan_status(&wifi);
    if (error != ESP_OK) return error;
    if (wifi.state == CERBERUS_WIFI_SCAN_RUNNING) return ESP_ERR_INVALID_STATE;
    cerberus_wifi_capture_status_payload_t capture = {0};
    error = c5_spi_link_wifi_capture_status(&capture);
    if (error != ESP_OK) return error;
    if (capture.state == CERBERUS_WIFI_CAPTURE_RUNNING) return ESP_ERR_INVALID_STATE;
    c5_has_channel_survey = c5.firmware_version >= CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE;
    if (c5_has_channel_survey) {
        cerberus_wifi_channel_survey_status_payload_t survey = {0};
        error = c5_spi_link_wifi_channel_survey_status(&survey);
        if (error != ESP_OK) return error;
        if (survey.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING)
            return ESP_ERR_INVALID_STATE;
    }
    cerberus_s3_capture_status_t s3_capture = {0};
    cerberus_s3_capture_status(&s3_capture);
    if (s3_capture.state == CERBERUS_S3_CAPTURE_STARTING ||
        s3_capture.state == CERBERUS_S3_CAPTURE_RUNNING ||
        s3_capture.state == CERBERUS_S3_CAPTURE_STOPPING) return ESP_ERR_INVALID_STATE;
    if (cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_NONE ||
        cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER)
        return ESP_ERR_INVALID_STATE;
    cerberus_ble_scan_status_payload_t ble = {0};
    error = cerberus_s3_ble_scanner_get_status(&ble);
    if (error != ESP_OK) return error;
    if (ble.state == CERBERUS_BLE_SCAN_RUNNING) return ESP_ERR_INVALID_STATE;
    /* The worker and optional GPS UART consume internal heap before the first
     * BLE sweep. Initialize NimBLE while the idle heap still satisfies the
     * scanner's first-use reserve, then release ownership for that sweep. */
    if (!cerberus_s3_ble_host_initialized()) {
        if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 98304U)
            return ESP_ERR_NO_MEM;
        error = cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_SCANNER);
        if (error != ESP_OK) return error;
        error = cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER);
        if (error != ESP_OK) return error;
    }
    /* GPS OFF allocates no UART. Its reserve is the scanner's 32 KiB gate,
     * the 6 KiB worker stack, and 4 KiB for task bookkeeping/other allocations.
     * The measured no-GPS Stop state has 45980 bytes free; excess reserve
     * must not reject a known working restart. GPS adds a 1 KiB UART ring and
     * its existing 3.5 KiB reader stack; 2 KiB covers UART/task metadata. */
    const size_t post_ble_reserve = post_ble_reserve_bytes(enable_gps);
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        post_ble_reserve)
        return ESP_ERR_NO_MEM;
    error = allocate_workspace();
    if (error != ESP_OK) return error;
    field_survey_snapshot(&expansion_stage);
    if (expansion_stage.requested ||
        (include_expansions && (expansion_stage.scanning_mask != 0U ||
            expansion_stage.uncertain_mask != 0U))) {
        release_workspace();
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(expansion_dropped_at_start_by_slot,
        expansion_stage.radio_dropped_by_slot,
        sizeof(expansion_stage.radio_dropped_by_slot));
    if (include_expansions) {
        error = prepare_expansion_plan(expansion_stage.present_mask);
        if (error != ESP_OK) {
            release_workspace();
            return error;
        }
    }
    error = cerberus_storage_wardrive_session_begin();
    if (error != ESP_OK) {
        if (include_expansions) {
            esp_err_t reset_error = field_survey_set_expansion_plan(NULL);
            if (reset_error != ESP_OK) error = reset_error;
        }
        release_workspace();
        return error;
    }
    wardrive_file_open = false;
    wigle_file_open = false;
    if (include_expansions) {
        error = field_survey_stream_open();
        if (error != ESP_OK) {
            finish_storage_session();
            wardrive_file_open = false;
            esp_err_t reset_error = field_survey_set_expansion_plan(NULL);
            if (reset_error != ESP_OK) error = reset_error;
            release_workspace();
            return error;
        }
        error = field_survey_set_running(true);
        if (error != ESP_OK) {
            field_survey_stream_close();
            esp_err_t reset_error = field_survey_set_expansion_plan(NULL);
            if (reset_error != ESP_OK) error = reset_error;
            finish_storage_session();
            wardrive_file_open = false;
            release_workspace();
            return error;
        }
    }
    cerberus_wardrive_dedupe_reset(&dedupe);
    cerberus_wardrive_dedupe_reset(&wigle_dedupe);
    cerberus_wardrive_probe_dedupe_reset(&probe_dedupe);
    cerberus_wardrive_relation_dedupe_reset(&relation_dedupe);
    reset_wifi_capture();
    open_file_rows = 0U;
    wigle_file_rows = 0U;
    started_ms = now_ms64();
    portENTER_CRITICAL(&wardrive_lock);
    memset(&wardrive_status, 0, sizeof(wardrive_status));
    wardrive_status.state = CERBERUS_WARDRIVE_STARTING;
    wardrive_status.gps_requested = enable_gps;
    wardrive_status.expansions_requested = include_expansions;
    wardrive_status.sd_bytes = 0U;
    stop_requested = false;
    worker_active = true;
    portEXIT_CRITICAL(&wardrive_lock);
    if (xTaskCreate(wardrive_worker, "wardrive", WARDRIVE_WORKER_STACK,
        NULL, 2, NULL) != pdPASS) {
        if (include_expansions) stop_expansions();
        finish_storage_session();
        wardrive_file_open = false;
        portENTER_CRITICAL(&wardrive_lock);
        worker_active = false;
        wardrive_status.state = CERBERUS_WARDRIVE_ERROR;
        wardrive_status.error_code = ESP_ERR_NO_MEM;
        portEXIT_CRITICAL(&wardrive_lock);
        release_workspace();
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cerberus_wardrive_start(bool include_expansions, bool enable_gps)
{
    portENTER_CRITICAL(&wardrive_lock);
    bool busy = worker_active || start_in_progress;
    if (!busy) start_in_progress = true;
    portEXIT_CRITICAL(&wardrive_lock);
    if (busy) return ESP_ERR_INVALID_STATE;

    /* Idle R1-R6 presence probes share the SPI bus with onboard C5 scan
     * commands. Drain any in-flight probe before Wardriving takes the bus. */
    esp_err_t error = field_survey_set_idle_probe_enabled(false);
    bool gate_closed = error == ESP_OK || error == ESP_ERR_TIMEOUT;
    if (error == ESP_OK)
        error = wardrive_start_with_idle_probes_paused(include_expansions,
            enable_gps);
    if (error != ESP_OK && gate_closed) {
        /* A gate timeout leaves probes disabled until explicitly resumed. */
        (void)field_survey_set_idle_probe_enabled(true);
    }
    portENTER_CRITICAL(&wardrive_lock);
    start_in_progress = false;
    portEXIT_CRITICAL(&wardrive_lock);
    return error;
}

void cerberus_wardrive_request_stop(void)
{
    portENTER_CRITICAL(&wardrive_lock);
    if (worker_active) {
        stop_requested = true;
        wardrive_status.state = CERBERUS_WARDRIVE_STOPPING;
    }
    portEXIT_CRITICAL(&wardrive_lock);
}

esp_err_t cerberus_wardrive_wait_stopped(uint32_t timeout_ms)
{
    uint32_t start = now_ms();
    for (;;) {
        portENTER_CRITICAL(&wardrive_lock);
        bool active = worker_active;
        portEXIT_CRITICAL(&wardrive_lock);
        if (!active) return ESP_OK;
        if ((uint32_t)(now_ms() - start) >= timeout_ms) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void cerberus_wardrive_get_status(cerberus_wardrive_status_t *out)
{
    if (out == NULL) return;
    portENTER_CRITICAL(&wardrive_lock);
    *out = wardrive_status;
    if (worker_active) out->elapsed_ms = (uint32_t)(now_ms64() - started_ms);
    portEXIT_CRITICAL(&wardrive_lock);
    esp_err_t uart_error = ESP_OK;
    snapshot_optional_gps(out->gps_requested, &out->gps, &uart_error);
    if (uart_error != ESP_OK) out->gps_error = uart_error;
    out->gps_present = out->gps.state != CERBERUS_WARDRIVE_GPS_ABSENT;
}
