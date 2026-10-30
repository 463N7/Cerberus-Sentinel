#include "wifi_beacon_session_private.h"
#include "cerberus_protocol.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

/* Initialized before any UI/observer task. One mutex protects the PSRAM
 * evidence, including atomic page copies; no PSRAM access in an ISR. */
static SemaphoreHandle_t analysis_mutex;
typedef struct {
    cerberus_beacon_session_mode_t mode;
    union {
        struct {
            cerberus_wifi_multissid_t table;
            cerberus_wifi_multissid_record_t records[CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS];
        } multi;
        struct {
            cerberus_wifi_pinescan_t table;
            cerberus_wifi_pinescan_record_t records[CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS];
        } pine;
    } evidence;
    uint64_t first_received_ms[CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS];
    uint64_t last_received_ms[CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS];
    uint32_t generation;
    uint32_t capture_generation;
    uint32_t last_sequence;
    uint32_t received_24ghz;
    uint32_t received_5ghz;
    bool following;
    uint8_t target[6];
    uint8_t target_channel;
    cerberus_s3_beacon_follow_t follow;
    uint64_t follow_received_ms;
    cerberus_s3_capture_status_t last_capture;
} analysis_store_t;
static analysis_store_t *analysis_store;
_Static_assert(CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS == CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS,
    "Shared delivery-age arrays must cover both analysis tables");

static bool valid_mode(cerberus_beacon_session_mode_t mode)
{ return mode == CERBERUS_BEACON_SESSION_MULTISSID || mode == CERBERUS_BEACON_SESSION_PINESCAN; }
static uint16_t mode_capacity(cerberus_beacon_session_mode_t mode)
{ return mode == CERBERUS_BEACON_SESSION_MULTISSID ? CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS : CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS; }
static bool owns_mode(cerberus_beacon_session_mode_t mode)
{ return analysis_store && analysis_store->mode == mode; }
static uint16_t stored_count(const analysis_store_t *store)
{ return store->mode == CERBERUS_BEACON_SESSION_MULTISSID ? store->evidence.multi.table.count : store->evidence.pine.table.count; }
static bool matching_row(const analysis_store_t *store, uint16_t index)
{
    return store->mode == CERBERUS_BEACON_SESSION_PINESCAN ||
        store->evidence.multi.records[index].variant_count == CERBERUS_WIFI_MULTISSID_THRESHOLD;
}
static const cerberus_wifi_beacon_observation_t *row_latest(const analysis_store_t *store, uint16_t index)
{
    return store->mode == CERBERUS_BEACON_SESSION_MULTISSID ?
        &store->evidence.multi.records[index].latest : &store->evidence.pine.records[index].latest;
}

static uint64_t local_ms(void) { return (uint64_t)(esp_timer_get_time() / 1000LL); }
static uint32_t delivery_age(uint64_t now, uint64_t then)
{
    uint64_t age = now >= then ? now - then : 0U;
    return age > UINT32_MAX ? UINT32_MAX : (uint32_t)age;
}
static uint32_t count_one(uint32_t n) { return n == UINT32_MAX ? n : n + 1U; }
static esp_err_t take_analysis(void)
{
    if (!analysis_mutex) return ESP_ERR_INVALID_STATE;
    return xSemaphoreTake(analysis_mutex, pdMS_TO_TICKS(250)) == pdTRUE ?
        ESP_OK : ESP_ERR_TIMEOUT;
}

/* The UI can inspect retained results after another capture has started, but
 * must never inherit that capture's state or issue Stop to its generation. */
static void refresh_owned_capture(analysis_store_t *store)
{
    cerberus_s3_capture_status_t current;
    cerberus_s3_capture_status(&current);
    if (current.generation == store->capture_generation) store->last_capture = current;
    else if (store->last_capture.busy) {
        store->last_capture.busy = false;
        store->last_capture.state = CERBERUS_S3_CAPTURE_ERROR;
        store->last_capture.error_code = ESP_ERR_INVALID_STATE;
    }
}

static void adopt_started_capture(analysis_store_t *store, uint32_t generation,
    const cerberus_s3_capture_options_t *options)
{
    store->capture_generation = generation;
    store->last_capture = (cerberus_s3_capture_status_t){
        .state = CERBERUS_S3_CAPTURE_STARTING, .busy = true,
        .generation = generation, .channel = options->channel,
        .mode_mask = options->mode_mask, .hopping = options->hopping,
        .save_pcap = options->save_pcap,
    };
    /* A very short failed worker can finish before Start returns. Only adopt
     * a status for the exact generation returned atomically by Start. */
    refresh_owned_capture(store);
}

static void receive_beacon(const cerberus_s3_capture_observation_t *observation,
    const uint8_t *frame, size_t length, void *context)
{
    analysis_store_t *store = context;
    if (!store || !observation || !frame || !observation->generation ||
        !observation->sequence || !analysis_mutex) return;
    /* No lossy lock timeout: the only other holders copy at most four rows or
     * configure a session while its worker is inactive. Observer completes
     * before the capture core acknowledges this pinned record. */
    if (xSemaphoreTake(analysis_mutex, portMAX_DELAY) != pdTRUE) return;
    if (store != analysis_store || observation->generation != store->capture_generation ||
        observation->sequence <= store->last_sequence) {
        xSemaphoreGive(analysis_mutex);
        return;
    }
    store->last_sequence = observation->sequence;
    uint64_t received = local_ms();
    if (store->following) {
        cerberus_wifi_beacon_observation_t beacon;
        if (cerberus_wifi_beacon_decode(frame, length, observation->channel,
                observation->rssi_dbm, observation->observed_us, &beacon) &&
            !memcmp(beacon.transmitter, store->target, sizeof(store->target)) &&
            beacon.rx_channel == store->target_channel &&
            (!store->follow.valid || beacon.observed_us >= store->follow.latest.observed_us)) {
            if (!store->follow.valid || beacon.rssi > store->follow.peak_rssi)
                store->follow.peak_rssi = beacon.rssi;
            store->follow.valid = true;
            store->follow.generation = observation->generation;
            store->follow.matching_beacons = count_one(store->follow.matching_beacons);
            store->follow.latest = beacon;
            store->follow_received_ms = received;
        }
    } else {
        uint16_t index = UINT16_MAX;
        bool valid, added;
        if (store->mode == CERBERUS_BEACON_SESSION_MULTISSID) {
            cerberus_wifi_multissid_event_t event = cerberus_wifi_multissid_observe(
                &store->evidence.multi.table, frame, length, observation->channel,
                observation->rssi_dbm, observation->observed_us, &index);
            valid = event != CERBERUS_WIFI_MULTISSID_REJECTED;
            added = event == CERBERUS_WIFI_MULTISSID_ADDED;
        } else {
            cerberus_wifi_pinescan_event_t event = cerberus_wifi_pinescan_observe(
                &store->evidence.pine.table, frame, length, observation->channel,
                observation->rssi_dbm, observation->observed_us, &index);
            valid = event != CERBERUS_WIFI_PINESCAN_INVALID;
            added = event == CERBERUS_WIFI_PINESCAN_ADDED;
        }
        if (valid) {
            if (observation->channel <= 14U) store->received_24ghz = count_one(store->received_24ghz);
            else store->received_5ghz = count_one(store->received_5ghz);
        }
        if (index < stored_count(store)) {
            if (added) store->first_received_ms[index] = received;
            store->last_received_ms[index] = received;
        }
    }
    xSemaphoreGive(analysis_mutex);
}

esp_err_t cerberus_beacon_session_init(void)
{
    if (!analysis_mutex) analysis_mutex = xSemaphoreCreateMutex();
    return analysis_mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t cerberus_beacon_session_start(cerberus_beacon_session_mode_t mode, bool save_pcap)
{
    if (!valid_mode(mode)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    cerberus_s3_capture_status_t current;
    cerberus_s3_capture_status(&current);
    if (current.busy) {
        xSemaphoreGive(analysis_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    analysis_store_t *pending = heap_caps_calloc(1U, sizeof(*pending),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pending) {
        xSemaphoreGive(analysis_mutex);
        return ESP_ERR_NO_MEM;
    }
    pending->mode = mode;
    if (mode == CERBERUS_BEACON_SESSION_MULTISSID)
        (void)cerberus_wifi_multissid_init(&pending->evidence.multi.table,
            pending->evidence.multi.records, CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS);
    else
        (void)cerberus_wifi_pinescan_init(&pending->evidence.pine.table,
            pending->evidence.pine.records, CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS);
    cerberus_s3_capture_options_t options = {
        .channel = 0U, .mode_mask = CERBERUS_WIFI_CAPTURE_MODE_BEACON,
        .hopping = true, .save_pcap = save_pcap,
        .observer = receive_beacon, .observer_context = pending,
    };
    uint32_t started_generation = 0U;
    error = cerberus_s3_capture_start_options_generation(&options, &started_generation);
    if (error == ESP_OK) {
        pending->generation = started_generation;
        adopt_started_capture(pending, started_generation, &options);
        analysis_store_t *previous = analysis_store;
        analysis_store = pending;
        /* capture.busy was false before a successful start, so the previous
         * worker has finished every observer call before its store is freed. */
        heap_caps_free(previous);
    } else heap_caps_free(pending);
    xSemaphoreGive(analysis_mutex);
    return error;
}

void cerberus_beacon_session_stop(cerberus_beacon_session_mode_t mode)
{
    if (!valid_mode(mode)) return;
    if (take_analysis() != ESP_OK) return;
    if (owns_mode(mode)) {
        refresh_owned_capture(analysis_store);
        if (analysis_store->last_capture.busy)
            (void)cerberus_s3_capture_stop_generation(analysis_store->capture_generation);
    }
    xSemaphoreGive(analysis_mutex);
}

esp_err_t cerberus_beacon_session_status(cerberus_beacon_session_mode_t mode,
    cerberus_beacon_session_status_t *out)
{
    if (!out || !valid_mode(mode)) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    if (owns_mode(mode)) {
        analysis_store_t *store = analysis_store;
        refresh_owned_capture(store);
        out->capture = store->last_capture;
        out->generation = store->generation;
        out->following = store->following;
        if (mode == CERBERUS_BEACON_SESSION_MULTISSID) {
            const cerberus_wifi_multissid_t *table = &store->evidence.multi.table;
            out->transmitters = table->count;
            out->matches = table->confirmed_count;
            out->capacity = table->capacity;
            out->valid_beacons = table->valid_beacons;
            out->rejected_frames = table->rejected_frames;
            out->capacity_drops = table->capacity_drops;
            out->stale_frames = table->stale_frames;
        } else {
            const cerberus_wifi_pinescan_t *table = &store->evidence.pine.table;
            out->transmitters = out->matches = table->count;
            out->capacity = table->capacity;
            out->valid_beacons = table->valid_beacons;
            out->matching_beacons = table->matching_beacons;
            out->rejected_frames = table->rejected_frames;
            out->capacity_drops = table->capacity_drops;
            out->stale_frames = table->stale_frames;
        }
        out->received_24ghz = store->received_24ghz;
        out->received_5ghz = store->received_5ghz;
    } else out->capacity = mode_capacity(mode);
    xSemaphoreGive(analysis_mutex);
    return ESP_OK;
}

esp_err_t cerberus_beacon_session_page(cerberus_beacon_session_mode_t mode,
    uint16_t offset, void *rows, size_t capacity, cerberus_s3_beacon_page_t *page)
{
    if (!valid_mode(mode) || !page || (capacity && !rows) || capacity > 4U) return ESP_ERR_INVALID_ARG;
    memset(page, 0, sizeof(*page));
    size_t row_size = mode == CERBERUS_BEACON_SESSION_MULTISSID ?
        sizeof(cerberus_s3_multissid_row_t) : sizeof(cerberus_s3_pinescan_row_t);
    if (capacity) memset(rows, 0, capacity * row_size);
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    if (owns_mode(mode)) {
        analysis_store_t *store = analysis_store;
        page->generation = store->generation;
        page->total = mode == CERBERUS_BEACON_SESSION_MULTISSID ?
            store->evidence.multi.table.confirmed_count : store->evidence.pine.table.count;
        uint16_t matched = 0U;
        uint64_t now = local_ms();
        for (uint16_t i = 0U; i < stored_count(store) && page->copied < capacity; ++i) {
            if (!matching_row(store, i)) continue;
            if (matched++ < offset) continue;
            if (mode == CERBERUS_BEACON_SESSION_MULTISSID) {
                cerberus_s3_multissid_row_t *row = &((cerberus_s3_multissid_row_t *)rows)[page->copied++];
                row->generation = store->generation;
                row->evidence = store->evidence.multi.records[i];
                row->first_received_age_ms = delivery_age(now, store->first_received_ms[i]);
                row->last_received_age_ms = delivery_age(now, store->last_received_ms[i]);
            } else {
                cerberus_s3_pinescan_row_t *row = &((cerberus_s3_pinescan_row_t *)rows)[page->copied++];
                row->generation = store->generation;
                row->evidence = store->evidence.pine.records[i];
                row->first_received_age_ms = delivery_age(now, store->first_received_ms[i]);
                row->last_received_age_ms = delivery_age(now, store->last_received_ms[i]);
            }
        }
    }
    xSemaphoreGive(analysis_mutex);
    return ESP_OK;
}

esp_err_t cerberus_beacon_session_follow(cerberus_beacon_session_mode_t mode,
    uint32_t generation, const cerberus_wifi_beacon_observation_t *selected, bool save_pcap)
{
    if (!valid_mode(mode) || !selected || !selected->rx_channel) return ESP_ERR_INVALID_ARG;
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    analysis_store_t *store = analysis_store;
    cerberus_s3_capture_status_t current;
    cerberus_s3_capture_status(&current);
    if (!owns_mode(mode) || generation != store->generation || current.busy) {
        xSemaphoreGive(analysis_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    bool known = false;
    for (uint16_t i = 0U; i < stored_count(store); ++i)
        if (matching_row(store, i) && !memcmp(row_latest(store, i)->transmitter,
                selected->transmitter, 6U)) { known = true; break; }
    if (!known) {
        xSemaphoreGive(analysis_mutex);
        return ESP_ERR_INVALID_ARG;
    }
    cerberus_s3_capture_options_t options = {
        .channel = selected->rx_channel,
        .mode_mask = CERBERUS_WIFI_CAPTURE_MODE_BEACON,
        .save_pcap = save_pcap,
        .observer = receive_beacon, .observer_context = store,
    };
    /* Capture has no BSSID filter: the selected identity is addr2, while the
     * existing wire filter is addr3. Only exact addr2 matches update follow. */
    uint32_t started_generation = 0U;
    error = cerberus_s3_capture_start_options_generation(&options, &started_generation);
    if (error == ESP_OK) {
        adopt_started_capture(store, started_generation, &options);
        store->last_sequence = 0U;
        store->following = true;
        memcpy(store->target, selected->transmitter, 6U);
        store->target_channel = selected->rx_channel;
        memset(&store->follow, 0, sizeof(store->follow));
        store->follow.generation = store->capture_generation;
        store->follow_received_ms = 0U;
    }
    xSemaphoreGive(analysis_mutex);
    return error;
}

esp_err_t cerberus_beacon_session_follow_status(cerberus_beacon_session_mode_t mode,
    cerberus_s3_beacon_follow_t *out)
{
    if (!out || !valid_mode(mode)) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    if (owns_mode(mode) && analysis_store->following) {
        *out = analysis_store->follow;
        if (out->valid) out->last_received_age_ms = delivery_age(local_ms(), analysis_store->follow_received_ms);
    }
    xSemaphoreGive(analysis_mutex);
    return ESP_OK;
}

esp_err_t cerberus_beacon_session_release(cerberus_beacon_session_mode_t mode)
{
    if (!valid_mode(mode)) return ESP_ERR_INVALID_ARG;
    esp_err_t error = take_analysis();
    if (error != ESP_OK) return error;
    if (owns_mode(mode)) {
        refresh_owned_capture(analysis_store);
        if (analysis_store->last_capture.busy) error = ESP_ERR_INVALID_STATE;
        else {
            heap_caps_free(analysis_store);
            analysis_store = NULL;
        }
    }
    xSemaphoreGive(analysis_mutex);
    return error;
}
