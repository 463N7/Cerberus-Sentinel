#include "s3_ble_scanner.h"
#include "s3_ble_host.h"
#include "s3_ble_snapshot.h"
#include "s3_wifi_owner.h"
#include "ble_flock_oui.h"
#include "sdkconfig.h"
#include <string.h>

static void clear_wardrive_copy(cerberus_c5_ble_result_t *rows,
    cerberus_s3_ble_wardrive_evidence_t *evidence, size_t capacity)
{
    size_t bounded = capacity > CERBERUS_BLE_SCAN_MAX_RESULTS ? CERBERUS_BLE_SCAN_MAX_RESULTS : capacity;
    if (rows) memset(rows, 0, bounded * sizeof(*rows));
    if (evidence) memset(evidence, 0, bounded * sizeof(*evidence));
}

#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include <string.h>

#define S3_BLE_SCAN_DURATION_MS 8000U
#if MYNEWT_VAL(BLE_EXT_ADV)
_Static_assert(CONFIG_BT_NIMBLE_TRANSPORT_EVT_SIZE >= 257,
    "Extended BLE scanning requires full-size HCI event buffers");
#endif

static const char *TAG = "s3_ble_scan";
static SemaphoreHandle_t scan_mutex;
static struct ble_npl_event start_event, stop_event;
static bool events_ready, cancel_requested, stop_event_pending;
static bool start_event_pending;
static uint32_t start_queued_epoch;
static bool active_scan;
static uint32_t scan_epoch, stop_epoch;
/* Watch timing identity must not alias the public generation byte. Never
 * reset or wrap this boot-lifetime counter when the BLE host restarts. */
static uint64_t watch_epoch;
static unsigned scan_host_epoch;
static unsigned tracker_host_epoch;
static unsigned analyzer_host_epoch;
static cerberus_s3_ble_tracker_t tracker;
static cerberus_ble_analyzer_t *analyzer;
static cerberus_s3_ble_mac_monitor_t *mac_monitor;
static uint64_t mac_monitor_epoch;
static uint32_t mac_monitor_job;
static bool discovery_attached;
static uint64_t discovery_epoch, discovery_started_ms;
static uint32_t discovery_scan_epoch;
static cerberus_s3_ble_binding_cache_t *discovery_bindings;
/* NimBLE callbacks run in a task, not an ISR. The bounded inventory can live
 * in PSRAM, preserving internal heap for the controller and Wi-Fi receiver. */
static cerberus_s3_ble_snapshot_t *snapshot;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }

/* A controller reset discards GAP procedures without a DISC_COMPLETE callback.
 * The shared host clears its lease; retire the old snapshot and event epoch. */
static void reconcile_host_locked(void)
{
    unsigned host_epoch = cerberus_s3_ble_host_epoch();
    if (mac_monitor && mac_monitor->host_epoch != host_epoch) {
        mac_monitor->receiving = false;
        mac_monitor->stopping = true;
        mac_monitor->error = ESP_ERR_INVALID_STATE;
        mac_monitor->state = CERBERUS_MAC_ERROR;
    }
    if (tracker.enabled && tracker_host_epoch != host_epoch) {
        cerberus_s3_ble_tracker_host_reset(&tracker);
        tracker_host_epoch = host_epoch;
    }
    if (analyzer && analyzer->active && analyzer_host_epoch != host_epoch) {
        cerberus_ble_analyzer_stop(analyzer);
        analyzer_host_epoch = host_epoch;
    }
    if (snapshot && snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING &&
        scan_host_epoch != host_epoch) {
        cerberus_s3_ble_snapshot_finish(snapshot, false, ESP_ERR_INVALID_STATE, now_ms());
        if (scan_epoch != UINT32_MAX) ++scan_epoch;
        cancel_requested = false;
        stop_event_pending = false;
    }
}

static void finish_scan(uint32_t expected_epoch, bool cancelled, int32_t reason)
{
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool running = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING && scan_epoch == expected_epoch;
    if (running) {
        cerberus_s3_ble_snapshot_finish(snapshot, cancelled || cancel_requested,
            reason, now_ms());
        cancel_requested = false;
        if (mac_monitor && mac_monitor->scan_epoch == expected_epoch) {
            mac_monitor->receiving = false;
            if (reason) {
                mac_monitor->error = reason;
                mac_monitor->stopping = true;
                mac_monitor->state = CERBERUS_MAC_ERROR;
            }
        }
    }
    /* Publish completion and release under the same scanner lock: a new
     * scanner lease cannot be admitted between these operations. */
    if (running) {
        esp_err_t err = cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER);
        if (err != ESP_OK) ESP_LOGE(TAG, "BLE host release failed: %s", esp_err_to_name(err));
    }
    xSemaphoreGive(scan_mutex);
}

/* Generic MAC evidence is a received report header, not an AD classifier.
 * Partial payloads and malformed AD bytes remain explicitly report evidence. */
static void mac_report_locked(const cerberus_s3_ble_report_t *report,
    uint32_t expected_epoch, int64_t callback_us, size_t maximum_length, bool metadata_valid)
{
    cerberus_s3_ble_mac_monitor_t *m = mac_monitor;
    if (!m || !m->started || !m->owned || !m->receiving || m->stopping ||
        m->scan_epoch != expected_epoch) return;
    bool event_valid = report->scan_response ?
        (report->event_type == 0U || report->event_type == 3U || report->event_type == 4U) :
        report->event_type <= 3U;
    if (!metadata_valid || callback_us < 0 || report->address_type > 3U || !event_valid ||
        report->length > maximum_length || (report->length && !report->data)) {
        cerberus_mac_observation_reject(&m->capture); return;
    }
    uint8_t type = report->scan_response ? CERBERUS_MAC_BLE_RESPONSE : CERBERUS_MAC_BLE_ORIGINAL;
    if (!report->complete) type |= CERBERUS_MAC_BLE_PARTIAL;
    (void)cerberus_mac_observation_observe(&m->capture, report->address,
        report->address_type, report->rssi, 0U, type, (uint64_t)callback_us);
}

/* Called with scan_mutex held from the current GAP callback. Each controller
 * report contributes one event; no strongest-RSSI inventory dedupe is used. */
static void analyze_report_locked(const cerberus_s3_ble_report_t *report,
    uint32_t observed_ms)
{
    if (!analyzer || !analyzer->active) return;
    cerberus_ble_analyzer_report_t event = {
        .address_type = report->address_type,
        .rssi_dbm = report->rssi,
        .advertisement = report->data,
        .advertisement_length = report->length,
        .advertisement_flags = report->complete ? 0U : 1U,
        .complete = report->complete,
        .scan_response = report->scan_response,
    };
    memcpy(event.address, report->address, sizeof(event.address));
    (void)cerberus_ble_analyzer_observe(analyzer, &event, observed_ms);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    const int64_t callback_us = esp_timer_get_time();
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING &&
        scan_epoch == (uint32_t)(uintptr_t)arg;
    xSemaphoreGive(scan_mutex);
    if (!current) return 0; /* A cancel may queue DISC_COMPLETE after the next sweep starts. */
    if (event->type == BLE_GAP_EVENT_DISC) {
        const struct ble_gap_disc_desc *disc = &event->disc;
        cerberus_s3_ble_report_t report = {0};
        memcpy(report.address, disc->addr.val, 6U);
        report.address_type = disc->addr.type;
        report.rssi = disc->rssi;
        report.event_type = disc->event_type;
        report.connectable = disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
            disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND;
        report.scan_response = disc->event_type == BLE_HCI_ADV_RPT_EVTYPE_SCAN_RSP;
        report.sid_valid = false;
        report.complete = true;
        report.data = disc->data;
        report.length = disc->length_data;
        xSemaphoreTake(scan_mutex, portMAX_DELAY);
        reconcile_host_locked();
        if (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING &&
            scan_epoch == (uint32_t)(uintptr_t)arg) {
            uint32_t observed_ms = now_ms();
            mac_report_locked(&report, (uint32_t)(uintptr_t)arg, callback_us, 31U, true);
            (void)cerberus_s3_ble_tracker_observe(&tracker, report.address,
                report.address_type, report.rssi, observed_ms);
            analyze_report_locked(&report, observed_ms);
            if (callback_us >= 0)
                cerberus_s3_ble_snapshot_observe_timed(snapshot, &report, observed_ms, (uint64_t)callback_us);
            else cerberus_s3_ble_snapshot_observe(snapshot, &report, observed_ms);
        }
        xSemaphoreGive(scan_mutex);
    }
#if MYNEWT_VAL(BLE_EXT_ADV)
    else if (event->type == BLE_GAP_EVENT_EXT_DISC) {
        const struct ble_gap_ext_disc_desc *disc = &event->ext_disc;
        cerberus_s3_ble_report_t report = {0};
        memcpy(report.address, disc->addr.val, 6U);
        report.address_type = disc->addr.type;
        report.rssi = disc->rssi;
        report.event_type = (disc->props & BLE_HCI_ADV_LEGACY_MASK) != 0U ?
            disc->legacy_event_type :
            ((disc->props & BLE_HCI_ADV_CONN_MASK) != 0U ?
                BLE_HCI_ADV_RPT_EVTYPE_ADV_IND : BLE_HCI_ADV_RPT_EVTYPE_NONCONN_IND);
        report.connectable = (disc->props & BLE_HCI_ADV_CONN_MASK) != 0U;
        report.scan_response = (disc->props & BLE_HCI_ADV_SCAN_RSP_MASK) != 0U;
        report.sid_valid = (disc->props & BLE_HCI_ADV_LEGACY_MASK) == 0U &&
            disc->sid <= 15U;
        report.sid_invalid = (disc->props & BLE_HCI_ADV_LEGACY_MASK) == 0U &&
            disc->sid > 15U;
        report.sid = disc->sid;
        report.complete = disc->data_status == BLE_GAP_EXT_ADV_DATA_STATUS_COMPLETE;
        report.data = disc->data;
        report.length = disc->length_data;
        xSemaphoreTake(scan_mutex, portMAX_DELAY);
        reconcile_host_locked();
        if (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING &&
            scan_epoch == (uint32_t)(uintptr_t)arg) {
            uint32_t observed_ms = now_ms();
            mac_report_locked(&report, (uint32_t)(uintptr_t)arg, callback_us,
                (disc->props & BLE_HCI_ADV_LEGACY_MASK) ? 31U : 255U, disc->data_status <= 2U);
            (void)cerberus_s3_ble_tracker_observe(&tracker, report.address,
                report.address_type, report.rssi, observed_ms);
            analyze_report_locked(&report, observed_ms);
            if (callback_us >= 0)
                cerberus_s3_ble_snapshot_observe_timed(snapshot, &report, observed_ms, (uint64_t)callback_us);
            else cerberus_s3_ble_snapshot_observe(snapshot, &report, observed_ms);
        }
        xSemaphoreGive(scan_mutex);
    }
#endif
    else if (event->type == BLE_GAP_EVENT_DISC_COMPLETE) {
        finish_scan((uint32_t)(uintptr_t)arg, false, event->disc_complete.reason);
    }
    return 0;
}

static void start_scan_event(struct ble_npl_event *event)
{
    uint32_t epoch = (uint32_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING && scan_epoch == epoch;
    bool cancelled = cancel_requested;
    bool active = active_scan;
    xSemaphoreGive(scan_mutex);
    if (!current || cancelled) {
        xSemaphoreTake(scan_mutex, portMAX_DELAY);
        if (start_queued_epoch == epoch) start_event_pending = false;
        xSemaphoreGive(scan_mutex);
        return; /* A queued STOP owns cancelled completion. */
    }
    uint8_t own_address_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_address_type);
    if (rc == 0) {
#if MYNEWT_VAL(BLE_EXT_ADV)
        const struct ble_gap_ext_disc_params uncoded = {
            .itvl = 160U, .window = 80U, .passive = active ? 0U : 1U,
        };
        const struct ble_gap_ext_disc_params coded = {
            .itvl = 160U, .window = 80U, .passive = 1U,
        };
        rc = ble_gap_ext_disc(own_address_type, S3_BLE_SCAN_DURATION_MS / 10U,
            0U, 0U, 0U, 0U,
            &uncoded, &coded, gap_event, (void *)(uintptr_t)epoch);
#else
        const struct ble_gap_disc_params parameters = {
            .itvl = active ? 160U : 0U, .window = active ? 80U : 0U,
            .filter_policy = 0, .limited = 0,
            .passive = active ? 0U : 1U, .filter_duplicates = 0,
        };
        rc = ble_gap_disc(own_address_type, S3_BLE_SCAN_DURATION_MS, &parameters,
            gap_event, (void *)(uintptr_t)epoch);
#endif
    }
    if (rc != 0) {
        ESP_LOGW(TAG, "BLE scan start failed: %d", rc);
        finish_scan(epoch, false, rc);
    } else {
        xSemaphoreTake(scan_mutex, portMAX_DELAY);
        reconcile_host_locked();
        if (mac_monitor && mac_monitor->scan_epoch == epoch && scan_epoch == epoch &&
            snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING && !mac_monitor->stopping)
            mac_monitor->receiving = true;
        xSemaphoreGive(scan_mutex);
    }
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (start_queued_epoch == epoch) start_event_pending = false;
    xSemaphoreGive(scan_mutex);
}

static void stop_scan_event(struct ble_npl_event *event)
{
    uint32_t epoch = (uint32_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = stop_event_pending && stop_epoch == epoch && scan_epoch == epoch &&
        snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING;
    xSemaphoreGive(scan_mutex);
    if (current) {
        int rc = ble_gap_disc_cancel();
        if (rc == 0 || !ble_gap_disc_active()) finish_scan(epoch, true, 0);
        else {
            /* Keep the host claim on failure. The finite controller scan
             * still ends within eight seconds; a later Stop may retry. */
            ESP_LOGW(TAG, "BLE scan stop failed: %d", rc);
            xSemaphoreTake(scan_mutex, portMAX_DELAY);
            if (snapshot->status.error_code != rc && snapshot->revision < UINT64_MAX - 1U)
                ++snapshot->revision;
            snapshot->status.error_code = rc;
            xSemaphoreGive(scan_mutex);
        }
    }
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (stop_event_pending && stop_epoch == epoch) stop_event_pending = false;
    xSemaphoreGive(scan_mutex);
}

static esp_err_t ensure_mutex(void)
{
    if (!scan_mutex) scan_mutex = xSemaphoreCreateMutex(); /* UI task initializes. */
    if (!scan_mutex) return ESP_ERR_NO_MEM;
    if (!snapshot) snapshot = heap_caps_calloc(1U, sizeof(*snapshot),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return snapshot ? ESP_OK : ESP_ERR_NO_MEM;
}

static bool discovery_start_allowed(bool discovery, cerberus_s3_ble_filter_t family,
    uint64_t previous)
{
    if (!discovery) return !discovery_attached;
    if (tracker.enabled || (analyzer && analyzer->active)) return false;
    if (!previous) return !discovery_attached;
    return discovery_attached && discovery_epoch == previous && snapshot->family == family &&
        snapshot->status.state == CERBERUS_BLE_SCAN_COMPLETE;
}

static bool discovery_meta_locked(cerberus_s3_ble_discovery_meta_t *meta);
static bool discovery_meta_at_locked(cerberus_s3_ble_discovery_meta_t *meta, uint64_t source_now_ms);

static esp_err_t start_scan_mode(bool active, cerberus_s3_ble_mac_monitor_t *monitor,
    cerberus_ble_scan_status_payload_t *status, bool discovery,
    cerberus_s3_ble_filter_t family, uint64_t previous,
    cerberus_s3_ble_discovery_meta_t *meta)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ensure_mutex();
    if (err != ESP_OK) return err;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool running = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING || stop_event_pending || start_event_pending ||
        watch_epoch == UINT64_MAX || scan_epoch == UINT32_MAX || (mac_monitor && monitor != mac_monitor) ||
        (monitor && (monitor != mac_monitor || !monitor->owned || monitor->stopping)) ||
        !discovery_start_allowed(discovery, family, previous);
    bool need_bindings = discovery && family != CERBERUS_S3_BLE_FILTER_ALL && !discovery_bindings;
    xSemaphoreGive(scan_mutex);
    if (running) return ESP_ERR_INVALID_STATE;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        (cerberus_s3_ble_host_initialized() ? 32768U : 98304U)) return ESP_ERR_NO_MEM;
    cerberus_s3_ble_binding_cache_t *allocated = need_bindings ?
        heap_caps_calloc(1U, sizeof(*allocated), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    if (need_bindings && !allocated) return ESP_ERR_NO_MEM;
    err = cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_SCANNER);
    if (err != ESP_OK) {
        heap_caps_free(allocated);
        return err;
    }
    if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER) {
        (void)cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER);
        heap_caps_free(allocated);
        return ESP_ERR_INVALID_STATE;
    }
    if (!events_ready) {
        ble_npl_event_init(&start_event, start_scan_event, NULL);
        ble_npl_event_init(&stop_event, stop_scan_event, NULL);
        events_ready = true;
    }
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    int64_t started_us = esp_timer_get_time();
    if (snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING || stop_event_pending || start_event_pending ||
        watch_epoch == UINT64_MAX || scan_epoch == UINT32_MAX || (mac_monitor && monitor != mac_monitor) ||
        (monitor && (monitor != mac_monitor || !monitor->owned || monitor->stopping)) ||
        !discovery_start_allowed(discovery, family, previous) || started_us < 0) {
        xSemaphoreGive(scan_mutex);
        (void)cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER);
        heap_caps_free(allocated);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t generation = (uint8_t)(snapshot->status.generation + 1U);
    ++scan_epoch; /* Admission above guarantees no SDK callback-token reuse. */
    if (monitor) { monitor->scan_epoch = scan_epoch; monitor->receiving = false; }
    scan_host_epoch = cerberus_s3_ble_host_epoch();
    active_scan = active;
    start_event_pending = true;
    start_queued_epoch = scan_epoch;
    ble_npl_event_set_arg(&start_event, (void *)(uintptr_t)scan_epoch);
    uint64_t started_ms = (uint64_t)started_us / 1000U;
    ++watch_epoch;
    if (discovery) {
        if (allocated) {
            discovery_bindings = allocated;
            allocated = NULL;
        }
        (void)cerberus_s3_ble_snapshot_begin_discovery(snapshot, generation, (uint32_t)started_ms,
            MYNEWT_VAL(BLE_EXT_ADV) != 0, watch_epoch, family, discovery_bindings);
        discovery_attached = true;
        discovery_epoch = watch_epoch;
        discovery_scan_epoch = scan_epoch;
        discovery_started_ms = started_ms;
    } else {
        cerberus_s3_ble_snapshot_begin_with_epoch(snapshot, generation, (uint32_t)started_ms,
            MYNEWT_VAL(BLE_EXT_ADV) != 0, watch_epoch);
    }
    cancel_requested = false;
    *status = snapshot->status;
    if (meta) (void)discovery_meta_at_locked(meta, started_ms);
    xSemaphoreGive(scan_mutex);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &start_event);
    return ESP_OK;
}

static esp_err_t start_scan(bool active, cerberus_s3_ble_mac_monitor_t *monitor,
    cerberus_ble_scan_status_payload_t *status)
{
    return start_scan_mode(active, monitor, status, false, CERBERUS_S3_BLE_FILTER_ALL, 0U, NULL);
}

esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t *status)
{
    return start_scan(false, NULL, status);
}

esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t *status)
{
    return start_scan(true, NULL, status);
}

static esp_err_t stop_scan(cerberus_s3_ble_mac_monitor_t *monitor,
    cerberus_ble_scan_status_payload_t *status)
{
    if (!status || !scan_mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (snapshot->status.state != CERBERUS_BLE_SCAN_RUNNING ||
        discovery_attached ||
        (mac_monitor && monitor != mac_monitor) ||
        (monitor && (monitor != mac_monitor || monitor->scan_epoch != scan_epoch))) {
        *status = snapshot->status;
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    cancel_requested = true;
    bool queue_stop = false;
    if (!stop_event_pending) {
        stop_event_pending = true;
        stop_epoch = scan_epoch;
        ble_npl_event_set_arg(&stop_event, (void *)(uintptr_t)scan_epoch);
        queue_stop = true;
    }
    *status = snapshot->status;
    xSemaphoreGive(scan_mutex);
    if (queue_stop) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &stop_event);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *status)
{ return stop_scan(NULL, status); }

esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (!snapshot) return ESP_OK;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    *status = snapshot->status;
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_get_inventory_status(uint8_t generation,
    cerberus_ble_scan_status_payload_t *status, uint16_t *replacements)
{
    if (status) memset(status, 0, sizeof(*status));
    if (replacements) *replacements = 0U;
    if (!status || !replacements) return ESP_ERR_INVALID_ARG;
    if (!snapshot || !scan_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = snapshot->status.state == CERBERUS_BLE_SCAN_COMPLETE &&
        snapshot->status.generation == generation;
    if (valid) {
        *status = snapshot->status;
        *replacements = snapshot->inventory_replacements;
    }
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_get_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_result_t *result)
{
    if (!result || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (snapshot->status.state != CERBERUS_BLE_SCAN_COMPLETE ||
        snapshot->status.generation != generation || index >= snapshot->status.stored_count) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    const cerberus_s3_ble_record_t *record = &snapshot->records[index];
    memset(result, 0, sizeof(*result));
    memcpy(result->name, record->name, record->name_length);
    memcpy(result->address, record->address, 6U);
    result->rssi = record->rssi;
    result->address_type = record->address_type;
    result->manufacturer_id = record->manufacturer_id;
    result->flags = record->flags;
    result->uuid_length = record->uuid_length;
    memcpy(result->uuid, record->uuid, record->uuid_length);
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

static cerberus_s3_ble_wardrive_evidence_t wardrive_evidence(
    const cerberus_s3_ble_record_t *record, uint8_t generation)
{
    cerberus_s3_ble_wardrive_evidence_t out = {0};
    const cerberus_s3_ble_discovery_evidence_t *source = &record->focus.discovery;
    if (record->focus.flock_oui_clue && cerberus_ble_flock_oui_hint(record->address, record->address_type))
        out.flags |= CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE;
    const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
    if (source->generation != generation || source->address_type != record->address_type ||
        memcmp(source->address, record->address, sizeof(source->address)) ||
        (source->flags & required) != required || (source->flags & ~63U) ||
        (source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID) ||
        ((source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) && source->sid > 15U) ||
        !source->advertisement_sequence ||
        !cerberus_s3_ble_wardrive_hints_valid(&source->flock_advertisement)) return out;
    out.flags |= CERBERUS_S3_BLE_WARDRIVE_ADV_VALID;
    out.advertisement = source->flock_advertisement;
    out.advertisement_sequence = source->advertisement_sequence;
    out.advertisement_observed_ms = source->advertisement_observed_ms;
    if (source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) {
        out.flags |= CERBERUS_S3_BLE_WARDRIVE_SID_VALID;
        out.sid = source->sid;
    }
    if ((source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) &&
        source->scan_response_sequence > source->advertisement_sequence &&
        (uint32_t)(source->scan_response_observed_ms - source->advertisement_observed_ms) <= 2000U &&
        cerberus_s3_ble_wardrive_hints_valid(&source->flock_scan_response)) {
        out.flags |= CERBERUS_S3_BLE_WARDRIVE_RSP_VALID;
        out.scan_response = source->flock_scan_response;
        out.scan_response_sequence = source->scan_response_sequence;
        out.scan_response_observed_ms = source->scan_response_observed_ms;
    }
    return out;
}

esp_err_t cerberus_s3_ble_scanner_copy_wardrive(uint8_t generation, uint8_t count,
    cerberus_c5_ble_result_t *rows, cerberus_s3_ble_wardrive_evidence_t *evidence,
    size_t capacity)
{
    clear_wardrive_copy(rows, evidence, capacity);
    if (capacity > CERBERUS_BLE_SCAN_MAX_RESULTS || count > capacity ||
        (capacity && (!rows || !evidence))) return ESP_ERR_INVALID_ARG;
    if (!snapshot || !scan_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = snapshot->status.state == CERBERUS_BLE_SCAN_COMPLETE &&
        snapshot->status.generation == generation && snapshot->status.stored_count == count;
    /* Reject corrupt lengths before copying any row; no partial bank escapes. */
    for (size_t i = 0U; valid && i < count; ++i)
        valid = snapshot->records[i].name_length <= CERBERUS_BLE_NAME_MAX_LENGTH &&
            snapshot->records[i].uuid_length <= CERBERUS_BLE_UUID_MAX_LENGTH;
    for (size_t i = 0U; valid && i < count; ++i) {
        const cerberus_s3_ble_record_t *record = &snapshot->records[i];
        memcpy(rows[i].address, record->address, sizeof(rows[i].address));
        memcpy(rows[i].name, record->name, record->name_length);
        rows[i].rssi = record->rssi;
        rows[i].address_type = record->address_type;
        rows[i].manufacturer_id = record->manufacturer_id;
        rows[i].flags = record->flags;
        rows[i].uuid_length = record->uuid_length;
        memcpy(rows[i].uuid, record->uuid, record->uuid_length);
        evidence[i] = wardrive_evidence(record, generation);
    }
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_get_advertisement(uint8_t generation, uint8_t index,
    cerberus_c5_ble_advertisement_t *result)
{
    if (!result || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (snapshot->status.state != CERBERUS_BLE_SCAN_COMPLETE ||
        snapshot->status.generation != generation || index >= snapshot->status.stored_count) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    const cerberus_s3_ble_record_t *record = &snapshot->records[index];
    memset(result, 0, sizeof(*result));
    memcpy(result->address, record->address, 6U);
    result->address_type = record->address_type;
    result->length = record->advertisement_length;
    result->event_type = record->advertisement_event_type;
    result->flags = record->advertisement_flags;
    result->reports_seen = record->reports_seen;
    memcpy(result->data, record->advertisement, record->advertisement_length);
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_get_advertisement_time(uint8_t generation,
    uint8_t index, uint32_t *observed_ms)
{
    if (!observed_ms || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (snapshot->status.state != CERBERUS_BLE_SCAN_COMPLETE ||
        snapshot->status.generation != generation ||
        index >= snapshot->status.stored_count) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *observed_ms = snapshot->records[index].last_advertisement_ms;
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_get_focus(uint8_t generation, uint8_t index,
    cerberus_s3_ble_focus_t *result)
{
    if (!result || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    if (snapshot->status.state != CERBERUS_BLE_SCAN_COMPLETE ||
        snapshot->status.generation != generation || index >= snapshot->status.stored_count) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *result = snapshot->records[index].focus;
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_get_watch_status(uint8_t generation,
    cerberus_watch_scan_status_payload_t *status)
{
    if (!status || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    bool valid = snapshot->watch.status.state == CERBERUS_WATCH_COMPLETE &&
        snapshot->watch.status.generation == generation;
    if (valid) *status = snapshot->watch.status;
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_get_watch_result(uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result)
{
    if (!result || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    bool valid = snapshot->watch.status.state == CERBERUS_WATCH_COMPLETE &&
        snapshot->watch.status.generation == generation && index < snapshot->watch.status.stored_count;
    if (valid) *result = snapshot->watch.records[index];
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t get_watch_observed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result,
    cerberus_watch_receive_timing_t *timing, sentinel_watch_motion_t *motion,
    cerberus_watch_receive_timing_t *motion_timing)
{
    const int64_t request_started_us = esp_timer_get_time();
    if (result) memset(result, 0, sizeof(*result));
    if (timing) memset(timing, 0, sizeof(*timing));
    if (!result || !timing || !snapshot) return ESP_ERR_INVALID_ARG;
    cerberus_watch_observation_t pending = {0};
    cerberus_watch_receive_timing_t pending_timing = {0};
    sentinel_watch_motion_t pending_motion = {0};
    cerberus_watch_receive_timing_t pending_motion_timing = {0};
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    bool valid = snapshot->watch.status.state == CERBERUS_WATCH_COMPLETE &&
        snapshot->watch.status.generation == generation && index < snapshot->watch.status.stored_count &&
        index < CERBERUS_WATCH_MAX_OBSERVATIONS;
    if (valid) {
        pending = snapshot->watch.records[index];
        const int64_t source_now_us = esp_timer_get_time();
        if (request_started_us >= 0 && source_now_us >= request_started_us &&
            snapshot->watch.capture_epoch != 0U) {
            cerberus_watch_scan_timing_payload_t source = {0};
            valid = cerberus_watch_capture_get_timing(&snapshot->watch, CERBERUS_WATCH_RADIO_BLE,
                generation, index, (uint64_t)source_now_us, &source) &&
                source.capture_epoch == snapshot->watch.capture_epoch &&
                cerberus_watch_receive_timing_decode(&source, CERBERUS_WATCH_RADIO_BLE,
                    generation, index, (uint64_t)request_started_us, &pending_timing);
        }
        if (valid && motion) {
            cerberus_watch_scan_timing_payload_t source = {0};
            const bool clock_valid = request_started_us >= 0 && source_now_us >= request_started_us;
            valid = cerberus_watch_capture_get_motion(&snapshot->watch, CERBERUS_WATCH_RADIO_BLE,
                generation, index, clock_valid ? (uint64_t)source_now_us : 0U, &pending_motion, &source) &&
                source.capture_epoch == snapshot->watch.capture_epoch &&
                sentinel_watch_motion_valid(&pending_motion) &&
                (!pending_motion.flags || pending.kind == SENTINEL_WATCH_PROTOCOL_DRONE) &&
                (!source.flags || (pending_motion.flags & SENTINEL_WATCH_MOTION_LOCATION_PRESENT));
            if (valid && clock_valid) valid = cerberus_watch_receive_timing_decode(&source,
                CERBERUS_WATCH_RADIO_BLE, generation, index, (uint64_t)request_started_us,
                &pending_motion_timing);
        }
    }
    xSemaphoreGive(scan_mutex);
    if (!valid) return ESP_ERR_INVALID_STATE;
    *result = pending;
    *timing = pending_timing;
    if (motion) *motion = pending_motion;
    if (motion_timing) *motion_timing = pending_motion_timing;
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_scanner_get_watch_observed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing)
{
    return get_watch_observed_result(generation, index, result, timing, NULL, NULL);
}

esp_err_t cerberus_s3_ble_scanner_get_watch_detailed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion, cerberus_watch_receive_timing_t *motion_timing)
{
    if (result) memset(result, 0, sizeof(*result));
    if (timing) memset(timing, 0, sizeof(*timing));
    if (motion) memset(motion, 0, sizeof(*motion));
    if (motion_timing) memset(motion_timing, 0, sizeof(*motion_timing));
    if (!result || !timing || !motion || !motion_timing) return ESP_ERR_INVALID_ARG;
    return get_watch_observed_result(generation, index, result, timing, motion, motion_timing);
}

esp_err_t cerberus_s3_ble_scanner_get_activity(uint8_t generation,
    cerberus_ble_activity_payload_t *status)
{
    if (!status || !snapshot) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    bool valid = snapshot->activity.status.state == CERBERUS_WATCH_COMPLETE &&
        snapshot->activity.status.generation == generation;
    if (valid) *status = snapshot->activity.status;
    xSemaphoreGive(scan_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_track_begin(const uint8_t address[6],
    uint8_t address_type)
{
    if (!address || address_type > 3U) return ESP_ERR_INVALID_ARG;
    esp_err_t error = ensure_mutex();
    if (error != ESP_OK) return error;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool busy = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING ||
        stop_event_pending || start_event_pending || discovery_attached || mac_monitor || (analyzer && analyzer->active) ||
        cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_NONE;
    if (!busy) {
        cerberus_s3_ble_tracker_begin(&tracker, address, address_type);
        tracker_host_epoch = cerberus_s3_ble_host_epoch();
    }
    xSemaphoreGive(scan_mutex);
    return busy ? ESP_ERR_INVALID_STATE : ESP_OK;
}

void cerberus_s3_ble_scanner_track_end(void)
{
    if (!scan_mutex) return;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    cerberus_s3_ble_tracker_end(&tracker);
    xSemaphoreGive(scan_mutex);
}

esp_err_t cerberus_s3_ble_scanner_track_status(
    cerberus_s3_ble_track_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    memset(status, 0, sizeof(*status));
    if (!scan_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    cerberus_s3_ble_tracker_snapshot(&tracker, now_ms(), status);
    xSemaphoreGive(scan_mutex);
    return status->enabled ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_scanner_analyzer_begin(void)
{
    esp_err_t error = ensure_mutex();
    if (error != ESP_OK) return error;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool busy = snapshot->status.state == CERBERUS_BLE_SCAN_RUNNING ||
        stop_event_pending || start_event_pending || discovery_attached || mac_monitor || tracker.enabled ||
        cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_NONE;
    if (busy) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    if (!analyzer) analyzer = heap_caps_calloc(1U, sizeof(*analyzer),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!analyzer) {
        xSemaphoreGive(scan_mutex);
        return ESP_ERR_NO_MEM;
    }
    cerberus_ble_analyzer_begin(analyzer, now_ms());
    analyzer_host_epoch = cerberus_s3_ble_host_epoch();
    xSemaphoreGive(scan_mutex);
    return ESP_OK;
}

void cerberus_s3_ble_scanner_analyzer_end(void)
{
    if (!scan_mutex) return;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    cerberus_ble_analyzer_t *old = analyzer;
    analyzer = NULL;
    xSemaphoreGive(scan_mutex);
    heap_caps_free(old);
}

esp_err_t cerberus_s3_ble_scanner_analyzer_status(
    cerberus_ble_analyzer_view_t *view)
{
    if (!view) return ESP_ERR_INVALID_ARG;
    memset(view, 0, sizeof(*view));
    if (!scan_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scan_mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool active = analyzer && analyzer->active;
    if (active) cerberus_ble_analyzer_snapshot(analyzer, now_ms(), view);
    xSemaphoreGive(scan_mutex);
    return active ? ESP_OK : ESP_ERR_INVALID_STATE;
}
#include "s3_ble_discovery_impl.h"
#include "s3_ble_mac_monitor_impl.h"
#else
#include <string.h>
esp_err_t cerberus_s3_ble_scanner_discovery_diagnostics(cerberus_s3_ble_discovery_meta_t *meta)
{
    if (!meta) return ESP_ERR_INVALID_ARG;
    memset(meta, 0, sizeof(*meta));
    return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_start_discovery(cerberus_s3_ble_filter_t family,
    uint64_t previous, cerberus_s3_ble_discovery_meta_t *meta)
{
    (void)family;
    (void)previous;
    if (meta) memset(meta, 0, sizeof(*meta));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_discovery_status(uint64_t epoch,
    cerberus_s3_ble_discovery_meta_t *meta)
{
    (void)epoch;
    if (meta) memset(meta, 0, sizeof(*meta));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_copy_discovery(uint64_t epoch,
    cerberus_s3_ble_discovery_bank_t *bank)
{
    (void)epoch;
    if (bank) memset(bank, 0, sizeof(*bank));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_stop_discovery(uint64_t epoch,
    cerberus_s3_ble_discovery_meta_t *meta)
{
    return cerberus_s3_ble_scanner_discovery_status(epoch, meta);
}
esp_err_t cerberus_s3_ble_scanner_release_discovery(uint64_t epoch)
{
    (void)epoch;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_inventory_status(uint8_t generation,
    cerberus_ble_scan_status_payload_t *status, uint16_t *replacements)
{
    (void)generation;
    if (status) memset(status, 0, sizeof(*status));
    if (replacements) *replacements = 0U;
    if (!status || !replacements) return ESP_ERR_INVALID_ARG;
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_get_result(uint8_t generation, uint8_t index, cerberus_c5_ble_result_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_copy_wardrive(uint8_t generation, uint8_t count,
    cerberus_c5_ble_result_t *rows, cerberus_s3_ble_wardrive_evidence_t *evidence, size_t capacity)
{
    (void)generation;
    (void)count;
    clear_wardrive_copy(rows, evidence, capacity);
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_get_advertisement(uint8_t generation, uint8_t index, cerberus_c5_ble_advertisement_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_advertisement_time(uint8_t generation, uint8_t index, uint32_t *observed_ms)
{ (void)generation; (void)index; (void)observed_ms; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_focus(uint8_t generation, uint8_t index, cerberus_s3_ble_focus_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_watch_status(uint8_t generation, cerberus_watch_scan_status_payload_t *status)
{ (void)generation; (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_watch_result(uint8_t generation, uint8_t index, cerberus_watch_observation_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_get_watch_observed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing)
{
    (void)generation;
    (void)index;
    if (result) memset(result, 0, sizeof(*result));
    if (timing) memset(timing, 0, sizeof(*timing));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_get_watch_detailed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result, cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion, cerberus_watch_receive_timing_t *motion_timing)
{
    (void)generation;
    (void)index;
    if (result) memset(result, 0, sizeof(*result));
    if (timing) memset(timing, 0, sizeof(*timing));
    if (motion) memset(motion, 0, sizeof(*motion));
    if (motion_timing) memset(motion_timing, 0, sizeof(*motion_timing));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_scanner_get_activity(uint8_t generation, cerberus_ble_activity_payload_t *status)
{ (void)generation; (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_track_begin(const uint8_t address[6], uint8_t address_type)
{ (void)address; (void)address_type; return ESP_ERR_NOT_SUPPORTED; }
void cerberus_s3_ble_scanner_track_end(void) { }
esp_err_t cerberus_s3_ble_scanner_track_status(cerberus_s3_ble_track_status_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_scanner_analyzer_begin(void)
{ return ESP_ERR_NOT_SUPPORTED; }
void cerberus_s3_ble_scanner_analyzer_end(void) { }
esp_err_t cerberus_s3_ble_scanner_analyzer_status(cerberus_ble_analyzer_view_t *view)
{ if (view) memset(view, 0, sizeof(*view)); return ESP_ERR_NOT_SUPPORTED; }

static uint64_t mac_disabled_now(void *c) { (void)c;return 0; }
static int32_t mac_disabled_reserve(void *c,uint8_t radio,cerberus_mac_binding_t *out)
{(void)c;(void)radio;if(out)memset(out,0,sizeof(*out));return ESP_ERR_NOT_SUPPORTED;}
static cerberus_mac_monitor_operation_t mac_disabled_status(void *c,uint8_t radio,
    const cerberus_mac_binding_t *b,cerberus_mac_monitor_observation_t *out)
{(void)c;(void)radio;(void)b;if(out)memset(out,0,sizeof(*out));return (cerberus_mac_monitor_operation_t){ESP_ERR_NOT_SUPPORTED,true};}
static cerberus_mac_monitor_operation_t mac_disabled_start(void *c,uint8_t radio,
    const cerberus_mac_start_request_t *r,cerberus_mac_monitor_observation_t *out)
{(void)r;return mac_disabled_status(c,radio,NULL,out);}
static cerberus_mac_monitor_operation_t mac_disabled_renew(void *c,uint8_t radio,
    const cerberus_mac_renew_request_t *r,cerberus_mac_monitor_observation_t *out)
{(void)r;return mac_disabled_status(c,radio,NULL,out);}
static cerberus_mac_monitor_operation_t mac_disabled_read(void *c,uint8_t radio,
    const cerberus_mac_binding_t *b,cerberus_mac_snapshot_t *out,cerberus_mac_monitor_transfer_t *t)
{
    if(out)memset(out,0,sizeof(*out));
    if(t)memset(t,0,sizeof(*t));
    return mac_disabled_status(c,radio,b,NULL);
}
static cerberus_mac_monitor_operation_t mac_disabled_ack(void *c,uint8_t radio,const cerberus_mac_ack_request_t *r)
{(void)r;return mac_disabled_status(c,radio,NULL,NULL);}
void cerberus_s3_ble_scanner_mac_ops(cerberus_s3_ble_mac_monitor_t *c,cerberus_mac_monitor_ops_t *out)
{if(out)*out=(cerberus_mac_monitor_ops_t){.context=c,.now_us=mac_disabled_now,
 .reserve=mac_disabled_reserve,.start=mac_disabled_start,.poll=mac_disabled_status,
 .renew=mac_disabled_renew,.read_step=mac_disabled_read,.ack=mac_disabled_ack,.stop=mac_disabled_status};}
void cerberus_s3_ble_scanner_mac_diagnostics(cerberus_mac_status_t *status,cerberus_mac_counters_t *counters)
{if(status)*status=(cerberus_mac_status_t){.version=CERBERUS_MAC_VERSION};
 if(counters)*counters=(cerberus_mac_counters_t){.version=CERBERUS_MAC_VERSION};}
#endif
