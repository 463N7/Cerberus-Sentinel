#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "esp_err.h"

#define WARDRIVE_STOP_GRACE_MS 9000U
#define WARDRIVE_EXPANSION_STOP_GRACE_MS 3000U
#define CERBERUS_WIFI_SCAN_RUNNING 1U
#define CERBERUS_WIFI_SCAN_COMPLETE 2U
#define CERBERUS_WIFI_SCAN_ERROR 3U
#define CERBERUS_BLE_SCAN_RUNNING 1U
#define CERBERUS_BLE_SCAN_COMPLETE 2U
#define CERBERUS_BLE_SCAN_ERROR 3U
#define pdMS_TO_TICKS(ms) (ms)

typedef struct {
    uint8_t state, generation, stored_count;
    uint16_t total_count;
    int32_t error_code;
} cerberus_wifi_scan_status_payload_t;
typedef struct {
    uint8_t state, generation, stored_count;
    uint16_t dropped_reports;
    int32_t error_code;
} cerberus_ble_scan_status_payload_t;
typedef struct {
    bool requested;
    uint8_t scanning_mask, uncertain_mask;
    uint32_t request_epoch, worker_seen_epoch;
} field_survey_snapshot_t;

static unsigned mode, wifi_queries, ble_queries, wifi_drains, ble_drains;
static unsigned wifi_stops, ble_stops, errors;
static unsigned expansion_mode, expansion_snapshots, expansion_stop_calls;
static unsigned expansion_final_drains;
static unsigned stream_closes, plan_resets, file_closes, gate_resumes;
static esp_err_t last_error;
static uint32_t clock_ms;
static field_survey_snapshot_t expansion_stage;
static struct { bool expansions_requested; } wardrive_status;
static uint32_t open_file_rows;
static bool wardrive_file_open, wigle_file_open;
static uint32_t wigle_file_rows;
static unsigned session_closes, wigle_closes;
static unsigned capture_resets;
static void reset_wifi_capture(void) { ++capture_resets; }
static uint32_t now_ms(void) { return clock_ms; }
static bool before_deadline(uint32_t deadline)
{ return (int32_t)(deadline - now_ms()) > 0; }
static void vTaskDelay(uint32_t ticks) { clock_ms += ticks; }
static void publish_error(esp_err_t error, char radio)
{ (void)radio; ++errors; last_error = error; }
static esp_err_t drain_completed_wifi(const cerberus_wifi_scan_status_payload_t *status,
    uint32_t deadline, bool *captured, char *error_radio)
{
    assert(status->state == CERBERUS_WIFI_SCAN_COMPLETE);
    assert(before_deadline(deadline));
    ++wifi_drains;
    *error_radio = 'C';
    *captured = mode != 6U && (mode != 5U || wifi_drains > 1U);
    return ESP_OK;
}
static void drain_completed_ble(const cerberus_ble_scan_status_payload_t *status)
{ assert(status->state == CERBERUS_BLE_SCAN_COMPLETE); ++ble_drains; }
static esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *status)
{
    ++wifi_queries;
    if ((mode == 2U && wifi_queries == 1U) || mode == 8U)
        return ESP_ERR_TIMEOUT;
    *status = (cerberus_wifi_scan_status_payload_t){
        .generation = mode == 3U ? 9U : mode == 7U ? 5U : 4U,
        .state = mode == 4U && wifi_queries == 1U ?
            CERBERUS_WIFI_SCAN_RUNNING : CERBERUS_WIFI_SCAN_COMPLETE,
        .stored_count = 1U, .total_count = 1U,
    };
    return ESP_OK;
}
static esp_err_t c5_spi_link_stop_wifi_scan(cerberus_wifi_scan_status_payload_t *status)
{ ++wifi_stops; status->state = CERBERUS_WIFI_SCAN_RUNNING; return ESP_OK; }
static esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status)
{
    ++ble_queries;
    *status = (cerberus_ble_scan_status_payload_t){
        .generation = 7U, .state = CERBERUS_BLE_SCAN_COMPLETE,
        .stored_count = 1U,
    };
    return ESP_OK;
}
static esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *status)
{ ++ble_stops; status->state = CERBERUS_BLE_SCAN_RUNNING; return ESP_OK; }
static esp_err_t field_survey_set_running(bool run)
{
    assert(!run);
    ++expansion_stop_calls;
    return expansion_mode == 4U ? ESP_FAIL : ESP_OK;
}
static void field_survey_snapshot(field_survey_snapshot_t *out)
{
    ++expansion_snapshots;
    *out = (field_survey_snapshot_t){
        .request_epoch = 2U,
        .worker_seen_epoch = expansion_mode == 3U || expansion_snapshots < 3U ? 1U : 2U,
        .scanning_mask = expansion_mode == 2U && expansion_snapshots < 5U ? 1U : 0U,
        .uncertain_mask = 0U,
    };
}
static esp_err_t capture_expansions(bool final_drain)
{ assert(final_drain); ++expansion_final_drains; return ESP_OK; }
static void field_survey_stream_close(void) { ++stream_closes; }
static esp_err_t field_survey_set_expansion_plan(const void *plan)
{
    assert(plan == NULL && stream_closes == 1U);
    ++plan_resets;
    return expansion_mode == 3U ? ESP_ERR_INVALID_STATE :
        expansion_mode == 5U ? ESP_FAIL : ESP_OK;
}
static esp_err_t save_open_file(bool keep)
{
    assert(keep && wifi_drains > 0U && errors == 0U);
    ++file_closes;
    return ESP_OK;
}
static esp_err_t save_log_stream(bool located, bool keep)
{ assert(located && keep); ++wigle_closes; return ESP_OK; }
static void finish_storage_session(void) { ++session_closes; }
static esp_err_t field_survey_set_idle_probe_enabled(bool enabled)
{ assert(enabled); ++gate_resumes; return ESP_OK; }

#include "wardrive_stop_impl.h"

static void reset(unsigned test_mode)
{
    mode = test_mode;
    capture_resets = 0U;
    wifi_queries = ble_queries = wifi_drains = ble_drains = 0U;
    wifi_stops = ble_stops = errors = 0U;
    expansion_mode = expansion_snapshots = expansion_stop_calls = 0U;
    expansion_final_drains = 0U;
    stream_closes = plan_resets = file_closes = gate_resumes = 0U;
    expansion_stage = (field_survey_snapshot_t){0};
    wardrive_status.expansions_requested = false;
    open_file_rows = 0U;
    wardrive_file_open = wigle_file_open = false;
    wigle_file_rows = session_closes = wigle_closes = 0U;
    last_error = ESP_OK;
    clock_ms = 0U;
}
int main(void)
{
    reset(1U);
    stop_own_scans(true, 4U, false, 0U, true, 7U);
    assert(wifi_drains == 1U && ble_drains == 1U && errors == 0U);
    assert(capture_resets == 0U);
    reset(2U);
    stop_own_scans(true, 4U, false, 0U, false, 0U);
    assert(wifi_queries == 2U && wifi_drains == 1U && errors == 0U);
    reset(3U);
    stop_own_scans(true, 4U, false, 0U, false, 0U);
    assert(wifi_drains == 0U && wifi_stops == 0U);
    assert(errors == 1U && last_error == ESP_ERR_INVALID_STATE);
    reset(4U);
    stop_own_scans(true, 4U, false, 0U, false, 0U);
    assert(wifi_stops == 1U && wifi_drains == 1U && errors == 0U);
    reset(5U);
    wardrive_file_open = true;
    open_file_rows = 1U;
    wigle_file_open = true; wigle_file_rows = 1U;
    stop_scans_and_close_file(true, 4U, false, 0U, false, 0U);
    assert(wifi_drains == 2U && file_closes == 1U && gate_resumes == 1U &&
        clock_ms >= 100U && errors == 0U && session_closes == 1U && wigle_closes == 1U);
    reset(6U);
    stop_own_scans(true, 4U, false, 0U, true, 7U);
    assert(wifi_drains > 1U && clock_ms >= WARDRIVE_STOP_GRACE_MS);
    assert(ble_drains == 1U && capture_resets == 0U);
    assert(errors == 1U && last_error == ESP_ERR_TIMEOUT);
    reset(7U);
    stop_own_scans(false, 0U, true, 4U, false, 0U);
    assert(wifi_drains == 1U && errors == 0U);
    assert(capture_resets == 1U);
    reset(8U);
    stop_own_scans(false, 0U, true, 4U, false, 0U);
    assert(clock_ms >= WARDRIVE_STOP_GRACE_MS);
    assert(errors == 1U && last_error == ESP_ERR_TIMEOUT);
    assert(capture_resets == 0U);
    reset(0U);
    expansion_mode = 1U;
    stop_expansions();
    assert(expansion_stop_calls == 1U && expansion_snapshots >= 3U);
    assert(clock_ms >= 100U && errors == 0U && expansion_final_drains == 1U &&
        stream_closes == 1U && plan_resets == 1U);
    reset(0U);
    expansion_mode = 2U;
    stop_expansions();
    assert(expansion_snapshots >= 5U && clock_ms >= 300U && errors == 0U &&
        expansion_final_drains == 1U && stream_closes == 1U && plan_resets == 1U);
    reset(0U);
    expansion_mode = 3U;
    stop_expansions();
    assert(clock_ms >= WARDRIVE_EXPANSION_STOP_GRACE_MS);
    assert(errors == 2U && last_error == ESP_ERR_INVALID_STATE &&
        expansion_final_drains == 1U && stream_closes == 1U && plan_resets == 1U);
    reset(0U);
    expansion_mode = 4U;
    stop_expansions();
    assert(expansion_snapshots == 0U && errors == 1U && last_error == ESP_FAIL &&
        expansion_final_drains == 1U && stream_closes == 1U && plan_resets == 1U);
    reset(0U);
    expansion_mode = 5U;
    stop_expansions();
    assert(errors == 1U && last_error == ESP_FAIL && plan_resets == 1U);
    puts("wardrive Stop drains completed sweeps and waits for expansion acknowledgment: pass");
    return 0;
}
