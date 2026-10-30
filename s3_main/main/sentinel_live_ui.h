/* Private UI integration. Included once by ui.c after the base tool functions.
 * The C5 and S3 have independent scan schedulers on the UI task. */
#include "s3_ble_scanner.h"
#include "scan_display_store.h"
#include "s3_ble_list.h"
#include "s3_ble_recent.h"
static bool watch_ble_requested(void);

typedef struct {
    bool valid, captured;
    uint8_t state, generation, saved_generation, completed;
    uint16_t count;
    uint32_t captured_ms;
    esp_err_t error;
} live_scan_record_t;

static sentinel_scan_session_t live_session;
static sentinel_scan_session_t live_ble_session;
static bool live_ble_session_initialized;
static live_scan_record_t live_records[SENTINEL_SCAN_TOOL_COUNT];
/* A witnessed new cycle can reuse an eight-bit generation. Keep its pending
 * publication separate from the last frozen bank and its identity/time. */
static uint32_t live_capture_pending;
_Static_assert(SENTINEL_SCAN_TOOL_COUNT <= 32, "Capture pending mask capacity");
static cerberus_scan_display_store_t *live_store;
static cerberus_findmy_monitor_record_t *live_ble_evidence; /* Frozen sweep in PSRAM. */
static uint8_t live_ble_focus_bits[CERBERUS_BLE_SCAN_MAX_RESULTS];
static uint8_t live_ble_indexes[CERBERUS_BLE_SCAN_MAX_RESULTS];
static size_t live_ble_filtered_count;
static char live_ble_filter_label[19];
static cerberus_s3_ble_sort_t live_ble_sort = CERBERUS_S3_BLE_SORT_SIGNAL_DESC;
static bool live_ble_order_dirty = true;
static bool live_ble_order_discovery;
static uint16_t live_ble_order_count;
static uint64_t live_ble_order_epoch, live_ble_order_revision;
static cerberus_s3_ble_filter_t live_ble_order_filter;
static cerberus_s3_ble_sort_t live_ble_order_sort;
static bool live_ble_recent_only;
static uint32_t live_ble_visible_mask = UINT32_MAX;
static int64_t live_ble_published_ms; /* Expiry ceiling, never a source observation time. */
static bool live_wifi_recent_only;
static uint8_t live_wifi_indexes[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static size_t live_wifi_filtered_count;
static uint8_t live_activity[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static int8_t live_energy[16];
static uint16_t live_page[SENTINEL_SCAN_TOOL_COUNT];
static uint32_t live_last_poll, live_frame;
static bool live_radio_busy;
static bool live_ble_observation_valid;
static bool live_status_changed;
static sentinel_scan_view_t live_view;
static char live_names[4][40], live_details[4][52], live_badges[4][34];
static char live_subtitle[52], live_hint[52];

#include "s3_ble_discovery_live_ui.h"

static live_scan_record_t *live_display_record(sentinel_scan_tool_t tool)
{
    return tool == SENTINEL_SCAN_BLE && ble_discovery_presenting() ?
        &ble_discovery_live->presentation : &live_records[tool];
}

static const cerberus_c5_ble_result_t *live_ble_display_peer(uint8_t index)
{
    return ble_discovery_presenting() ?
        &ble_discovery_live->inventory.entries[index].record.peer : &live_store->ble[index];
}

static const cerberus_s3_ble_focus_t *live_ble_display_focus(uint8_t index)
{
    return ble_discovery_presenting() ?
        &ble_discovery_live->inventory.entries[index].record.focus :
        live_ble_evidence ? &live_ble_evidence[index].focus : NULL;
}

static esp_err_t live_paint(bool full)
{
    esp_err_t error = full ? sentinel_scan_view_draw(&live_view, live_frame) :
        sentinel_scan_view_update(&live_view, live_frame);
    if (error == ESP_OK && ble_discovery_presenting()) {
        ble_discovery_live->painted_count = 0U;
        for (uint8_t row = 0U; row < live_view.row_count && row < 4U; ++row) {
            size_t position = (size_t)live_view.page * 4U + row;
            if (position >= live_ble_filtered_count) break;
            if (!cerberus_s3_ble_discovery_inventory_copy(&ble_discovery_live->inventory,
                    live_ble_indexes[position], &ble_discovery_live->painted[row])) break;
            ++ble_discovery_live->painted_count;
        }
    }
    return error;
}

static bool live_has_snapshot(sentinel_scan_tool_t tool)
{
    return tool == SENTINEL_SCAN_BLE && ble_discovery_live &&
        ble_discovery_live->presentation.captured ? true : live_records[tool].captured;
}
static uint16_t live_snapshot_count(sentinel_scan_tool_t tool)
{
    return tool == SENTINEL_SCAN_BLE && ble_discovery_live &&
        ble_discovery_live->presentation.captured ? ble_discovery_live->inventory.count : live_records[tool].count;
}
static sentinel_scan_session_t *live_session_for(sentinel_scan_tool_t tool)
{
    return tool == SENTINEL_SCAN_BLE ? &live_ble_session : &live_session;
}

static bool live_supported(sentinel_scan_tool_t tool)
{
    if (tool == SENTINEL_SCAN_BLE) return true; /* Local S3 NimBLE observer. */
    uint32_t cap = tool == SENTINEL_SCAN_WIFI ? CERBERUS_C5_CAP_WIFI_2_4_GHZ :
        tool == SENTINEL_SCAN_DEFENSE ? CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY : CERBERUS_C5_CAP_IEEE802154;
    return (c5_status.capabilities & cap) != 0;
}

static sentinel_scan_tool_t live_page_tool(void)
{
    if (ui_capability_blocked) return SENTINEL_SCAN_NONE;
    if (current_page == UI_PAGE_WATCH) return watch_requested_tool();
    if (current_page == UI_PAGE_WIFI) {
        if (wifi_mode == UI_WIFI_MODE_LIST || wifi_mode == UI_WIFI_MODE_STATS ||
            wifi_mode == UI_WIFI_MODE_BASELINE)
            return SENTINEL_SCAN_WIFI;
        if (wifi_mode == UI_WIFI_MODE_DEFENSE) {
            if (!live_supported(SENTINEL_SCAN_DEFENSE)) return SENTINEL_SCAN_NONE;
            /* An internal phase change must not undo an explicit Stop (or a
             * failed prerequisite). Scan again may advance using that inventory. */
            if (live_session.desired == SENTINEL_SCAN_WIFI &&
                (live_session.paused || live_session.faulted)) return SENTINEL_SCAN_WIFI;
            /* The review needs an AP inventory before its first listening pass. */
            return (live_session.active == SENTINEL_SCAN_DEFENSE ||
                (live_records[SENTINEL_SCAN_WIFI].valid && wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE))
                ? SENTINEL_SCAN_DEFENSE : SENTINEL_SCAN_WIFI;
        }
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST) {
        return SENTINEL_SCAN_BLE;
    } else if (current_page == UI_PAGE_IEEE802154) {
        if (ieee802154_mode == UI_IEEE802154_MODE_ENERGY) return SENTINEL_SCAN_ENERGY;
        if (ieee802154_mode == UI_IEEE802154_MODE_BEACONS) return SENTINEL_SCAN_BEACON;
        if (ieee802154_mode == UI_IEEE802154_MODE_ACTIVITY) return SENTINEL_SCAN_ACTIVITY;
    }
    return SENTINEL_SCAN_NONE;
}

static bool live_is_view(void)
{
    return (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_LIST) ||
        (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST) ||
        (current_page == UI_PAGE_IEEE802154 &&
            (ieee802154_mode == UI_IEEE802154_MODE_ENERGY ||
             ieee802154_mode == UI_IEEE802154_MODE_BEACONS ||
             ieee802154_mode == UI_IEEE802154_MODE_ACTIVITY));
}

static void live_status(sentinel_scan_tool_t tool, esp_err_t err, uint8_t state,
                        uint8_t generation, uint8_t completed, int32_t radio_error)
{
    live_scan_record_t *record = &live_records[tool];
    const bool supported = live_supported(tool);
    if (!supported) { err = ESP_OK; state = SENTINEL_SCAN_IDLE; generation = completed = 0; radio_error = 0; }
    if (supported && err == ESP_OK && (state == SENTINEL_SCAN_RUNNING ||
        (record->captured && generation != record->saved_generation)))
        live_capture_pending |= UINT32_C(1) << tool;
    live_status_changed |= record->valid != (err == ESP_OK) || record->state != state ||
        record->generation != generation ||
        record->error != (err != ESP_OK ? err : state == SENTINEL_SCAN_ERROR ? radio_error : ESP_OK);
    record->valid = err == ESP_OK;
    record->state = state;
    record->generation = generation;
    record->completed = completed;
    record->error = err != ESP_OK ? err : state == SENTINEL_SCAN_ERROR ? radio_error : ESP_OK;
}

static bool live_store_acquire(void)
{
    if (!live_store) live_store = heap_caps_calloc(1U, sizeof(*live_store),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return live_store != NULL;
}

static bool live_capture(sentinel_scan_tool_t tool, uint32_t now)
{
    if (tool == SENTINEL_SCAN_BLE && ble_discovery_live && ble_discovery_live->session.epoch)
        return false;
    live_scan_record_t *record = &live_records[tool];
    if (!record->valid || record->state != SENTINEL_SCAN_COMPLETE ||
        (record->captured && record->saved_generation == record->generation &&
         !(live_capture_pending & (UINT32_C(1) << tool)))) return false;
    esp_err_t err = ESP_OK;
    uint16_t count = 0;
    uint8_t gen = record->generation;
    cerberus_c5_wifi_relation_bank_t relation_binding = {0};
    esp_err_t relation_binding_error = ESP_ERR_NOT_SUPPORTED;
    if (tool == SENTINEL_SCAN_WIFI) {
        count = wifi_scan_status.stored_count;
        if (count > CERBERUS_WIFI_SCAN_MAX_RESULTS) { record->error = ESP_ERR_INVALID_RESPONSE; return false; }
        if (count && !live_store_acquire()) {
            record->error = ESP_ERR_NO_MEM;
            return false;
        }
        relation_binding_error = wifi_relation_capture_before(gen, &relation_binding);
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i)
            err = c5_spi_link_get_wifi_scan_observed_result(gen, i,
                &live_store->staging.wifi[i], &live_store->wifi_staging_observations[i]);
        cerberus_wifi_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_wifi_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_WIFI_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK && relation_binding_error == ESP_OK)
            relation_binding_error = wifi_relation_capture_after(&relation_binding);
        if (err == ESP_OK) {
            if (count) {
                memcpy(live_store->wifi, live_store->staging.wifi, count * sizeof(live_store->wifi[0]));
                memcpy(live_store->wifi_observations, live_store->wifi_staging_observations,
                    count * sizeof(live_store->wifi_observations[0]));
            }
            (void)refresh_wifi_scan_status();
            if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_BASELINE)
                (void)refresh_wifi_baseline();
        }
    } else if (tool == SENTINEL_SCAN_BLE) {
        count = ble_scan_status.stored_count;
        if (count > CERBERUS_BLE_SCAN_MAX_RESULTS) { record->error = ESP_ERR_INVALID_RESPONSE; return false; }
        if (count && !live_store_acquire()) {
            record->error = ESP_ERR_NO_MEM;
            return false;
        }
        cerberus_findmy_monitor_record_t *evidence = count ?
            heap_caps_calloc(count, sizeof(*evidence), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
        if (count && !evidence) { record->error = ESP_ERR_NO_MEM; return false; }
        uint8_t focus_bits[CERBERUS_BLE_SCAN_MAX_RESULTS] = {0};
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i) {
            err = cerberus_s3_ble_scanner_get_result(gen, i, &live_store->staging.ble[i]);
            cerberus_s3_ble_focus_t focus;
            if (err == ESP_OK) err = cerberus_s3_ble_scanner_get_focus(gen, i, &focus);
            if (err == ESP_OK) focus_bits[i] = focus.category_bits;
            if (err == ESP_OK) err = cerberus_s3_ble_scanner_get_advertisement(gen, i, &evidence[i].advertisement);
            if (err == ESP_OK) err = cerberus_s3_ble_scanner_get_advertisement_time(gen, i, &evidence[i].observed_ms);
            if (err == ESP_OK) {
                evidence[i].peer = live_store->staging.ble[i]; evidence[i].focus = focus;
                evidence[i].generation = gen; evidence[i].index = (uint8_t)i;
            }
        }
        cerberus_ble_scan_status_payload_t end;
        if (err == ESP_OK) err = cerberus_s3_ble_scanner_get_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_BLE_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            if (count) memcpy(live_store->ble, live_store->staging.ble, count * sizeof(live_store->ble[0]));
            memcpy(live_ble_focus_bits, focus_bits, sizeof(focus_bits));
            heap_caps_free(live_ble_evidence);
            live_ble_evidence = evidence;
            evidence = NULL;
            live_ble_published_ms = esp_timer_get_time() / 1000LL;
            live_ble_order_dirty = true;
        }
        heap_caps_free(evidence);
    } else if (tool == SENTINEL_SCAN_ENERGY) {
        cerberus_ieee802154_energy_scan_result_payload_t result;
        err = c5_spi_link_get_ieee802154_energy_scan_result(gen, &result);
        if (err == ESP_OK && (result.generation != gen || result.first_channel != 11 || result.channel_count != 16)) err = ESP_ERR_INVALID_RESPONSE;
        if (err == ESP_OK) {
            memcpy(live_energy, result.power_dbm, sizeof(live_energy));
            ieee802154_scan_result = result;
            count = 16;
        }
    } else if (tool == SENTINEL_SCAN_BEACON) {
        count = ieee802154_beacon_status.stored_count;
        if (count > CERBERUS_IEEE802154_BEACON_MAX_RESULTS) { record->error = ESP_ERR_INVALID_RESPONSE; return false; }
        if (count && !live_store_acquire()) {
            record->error = ESP_ERR_NO_MEM;
            return false;
        }
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i)
            err = c5_spi_link_get_ieee802154_beacon_scan_result(gen, i, &live_store->staging.beacon[i]);
        cerberus_ieee802154_beacon_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_ieee802154_beacon_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK && count) memcpy(live_store->beacons, live_store->staging.beacon, count * sizeof(live_store->beacons[0]));
    } else if (tool == SENTINEL_SCAN_ACTIVITY) {
        uint8_t staging[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT] = {0};
        for (uint8_t i = 0; i < CERBERUS_IEEE802154_ACTIVITY_RESULT_COUNT && err == ESP_OK; ++i) {
            cerberus_ieee802154_activity_scan_result_payload_t result;
            err = c5_spi_link_get_ieee802154_activity_scan_result(gen, i, &result);
            if (err == ESP_OK && (result.generation != gen || result.first_channel != 11 + i * 4)) err = ESP_ERR_INVALID_RESPONSE;
            if (err == ESP_OK) memcpy(&staging[i * 4], result.counts, sizeof(result.counts));
        }
        cerberus_ieee802154_activity_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_ieee802154_activity_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE || end.generation != gen)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            memcpy(live_activity, staging, sizeof(live_activity));
            memcpy(ieee802154_activity_counts, staging, sizeof(live_activity));
            count = 16;
        }
    } else if (tool == SENTINEL_SCAN_DEFENSE) {
        wifi_defense_loaded_count = 0;
        err = refresh_wifi_defense_survey();
        count = wifi_defense_loaded_count;
    }
    if (err != ESP_OK) { record->error = err; return false; }
    record->count = count;
    record->captured = true;
    record->saved_generation = gen;
    record->captured_ms = now;
    if (tool == SENTINEL_SCAN_WIFI) wifi_relation_publish(gen, &relation_binding, relation_binding_error);
    live_capture_pending &= ~(UINT32_C(1) << tool);
    if (live_page[tool] * 4 >= count) live_page[tool] = count ? (count - 1) / 4 : 0;
    return true;
}

static bool live_observe(uint32_t now)
{
    /* Keep the C5 and S3 observations independent. S3 BLE must not make the
     * C5 Wi-Fi scheduler think its own radio is occupied. */
    live_status_changed = false;
    const bool local_discovery = ble_discovery_page();
    wifi_scan_checked = ble_scan_checked = ieee802154_scan_checked = true;
    ieee802154_beacon_checked = ieee802154_activity_checked = wifi_defense_checked = true;
    if (!local_discovery || live_session.active == SENTINEL_SCAN_WIFI) {
        wifi_scan_error = c5_online && live_supported(SENTINEL_SCAN_WIFI) ? c5_spi_link_get_wifi_scan_status(&wifi_scan_status) : ESP_ERR_NOT_SUPPORTED;
        live_status(SENTINEL_SCAN_WIFI, wifi_scan_error, wifi_scan_status.state, wifi_scan_status.generation, 0, wifi_scan_status.error_code);
    }
    ble_scan_error = cerberus_s3_ble_scanner_get_status(&ble_scan_status);
    live_status(SENTINEL_SCAN_BLE, ble_scan_error, ble_scan_status.state, ble_scan_status.generation, 0, ble_scan_status.error_code);
    if (!local_discovery || live_session.active == SENTINEL_SCAN_ENERGY) {
        ieee802154_scan_error = c5_online && live_supported(SENTINEL_SCAN_ENERGY) ? c5_spi_link_get_ieee802154_energy_scan_status(&ieee802154_scan_status) : ESP_ERR_NOT_SUPPORTED;
        live_status(SENTINEL_SCAN_ENERGY, ieee802154_scan_error, ieee802154_scan_status.state, ieee802154_scan_status.generation, ieee802154_scan_status.completed_count, ieee802154_scan_status.error_code);
    }
    if (!local_discovery || live_session.active == SENTINEL_SCAN_BEACON) {
        ieee802154_beacon_error = c5_online && live_supported(SENTINEL_SCAN_BEACON) ? c5_spi_link_get_ieee802154_beacon_scan_status(&ieee802154_beacon_status) : ESP_ERR_NOT_SUPPORTED;
        live_status(SENTINEL_SCAN_BEACON, ieee802154_beacon_error, ieee802154_beacon_status.state, ieee802154_beacon_status.generation, ieee802154_beacon_status.completed_count, ieee802154_beacon_status.error_code);
    }
    if (!local_discovery || live_session.active == SENTINEL_SCAN_ACTIVITY) {
        ieee802154_activity_error = c5_online && live_supported(SENTINEL_SCAN_ACTIVITY) ? c5_spi_link_get_ieee802154_activity_scan_status(&ieee802154_activity_status) : ESP_ERR_NOT_SUPPORTED;
        live_status(SENTINEL_SCAN_ACTIVITY, ieee802154_activity_error, ieee802154_activity_status.state, ieee802154_activity_status.generation, ieee802154_activity_status.completed_count, ieee802154_activity_status.error_code);
    }
    if (!local_discovery || live_session.active == SENTINEL_SCAN_DEFENSE) {
        uint8_t previous_defense_generation = wifi_defense_status.generation;
        wifi_defense_error = c5_online && live_supported(SENTINEL_SCAN_DEFENSE) ? c5_spi_link_get_wifi_defense_status(&wifi_defense_status) : ESP_ERR_NOT_SUPPORTED;
        if (previous_defense_generation != wifi_defense_status.generation) wifi_defense_loaded_count = 0;
        live_status(SENTINEL_SCAN_DEFENSE, wifi_defense_error, wifi_defense_status.state, wifi_defense_status.generation, wifi_defense_status.completed_count, wifi_defense_status.error_code);
    }
    cerberus_wifi_connection_status_payload_t wifi = {0};
    bool valid = c5_online && (local_discovery || !(c5_status.capabilities & CERBERUS_C5_CAP_WIFI_STATION) ||
        c5_spi_link_get_wifi_connection_status(&wifi) == ESP_OK);
    /* S3 owns both passive BLE observation and GATT. C5 Wi-Fi Recon has no
     * dependency on the S3 connection or its NimBLE host status. */
    live_radio_busy = !valid || (c5_status.radio_state & CERBERUS_C5_RADIO_BLE_SCAN) != 0U ||
        wifi.state == CERBERUS_WIFI_CONNECTION_STAGING ||
        wifi.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING || wifi.state == CERBERUS_WIFI_CONNECTION_CONNECTING ||
        wifi.state == CERBERUS_WIFI_CONNECTION_CONNECTED;
    bool changed = false;
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        if (tool != SENTINEL_SCAN_BLE) {
            valid = valid && live_records[tool].valid;
            if (tool != live_session.active && live_records[tool].state == SENTINEL_SCAN_RUNNING)
                live_radio_busy = true;
        }
        if (!local_discovery || tool == live_session.active ||
            (tool == SENTINEL_SCAN_BLE && live_ble_session.active == SENTINEL_SCAN_BLE))
            changed = live_capture(tool, now) || changed;
    }
    /* Unknown OTHER work blocks a start; it must never prevent stopping our
     * known active scan. A failed active query still blocks both actions. */
    live_radio_busy |= !valid;
    live_scan_observation_valid = live_session.active == SENTINEL_SCAN_NONE ? valid : live_records[live_session.active].valid;
    live_ble_observation_valid = live_records[SENTINEL_SCAN_BLE].valid;
    return changed || live_status_changed;
}

static esp_err_t live_execute(sentinel_scan_command_t command)
{
    bool start = command.kind == SENTINEL_SCAN_COMMAND_START;
    switch (command.tool) {
    case SENTINEL_SCAN_WIFI: return start ? c5_spi_link_start_wifi_scan(&wifi_scan_status) : c5_spi_link_stop_wifi_scan(&wifi_scan_status);
    case SENTINEL_SCAN_BLE:
        if (!start) return cerberus_s3_ble_scanner_stop(&ble_scan_status);
        return current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST ?
            cerberus_s3_ble_scanner_start_active(&ble_scan_status) :
            cerberus_s3_ble_scanner_start(&ble_scan_status);
    case SENTINEL_SCAN_ENERGY: return start ? c5_spi_link_start_ieee802154_energy_scan(&ieee802154_scan_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_BEACON: return start ? c5_spi_link_start_ieee802154_beacon_scan(&ieee802154_beacon_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_ACTIVITY: return start ? c5_spi_link_start_ieee802154_activity_scan(&ieee802154_activity_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_DEFENSE: return start ? c5_spi_link_start_wifi_defense_survey(&wifi_defense_status) : c5_spi_link_stop_wifi_defense_survey(&wifi_defense_status);
    default: return ESP_ERR_INVALID_ARG;
    }
}

static uint32_t live_ble_recent_mask(int64_t now_ms)
{
    if (ble_discovery_presenting())
        return now_ms >= 0 ? ble_discovery_visible_mask((uint64_t)now_ms) : 0U;
    if (!live_ble_recent_only || !cerberus_s3_ble_recent_supported(ble_filter) ||
        current_page != UI_PAGE_BLE || ble_mode != UI_BLE_MODE_LIST) return UINT32_MAX;
    const live_scan_record_t *record = &live_records[SENTINEL_SCAN_BLE];
    if (!record->captured || !live_ble_evidence || now_ms < live_ble_published_ms ||
        now_ms - live_ble_published_ms > CERBERUS_S3_BLE_RECENT_WINDOW_MS) return 0U;
    uint32_t mask = 0U;
    for (uint16_t i = 0U; i < record->count && i < CERBERUS_BLE_SCAN_MAX_RESULTS; ++i)
        if (cerberus_s3_ble_focus_recent(&live_ble_evidence[i].focus, ble_filter, (uint32_t)now_ms))
            mask |= UINT32_C(1) << i;
    return mask;
}

static void live_make_view(void)
{
    sentinel_scan_tool_t tool = live_page_tool();
    live_scan_record_t *record = live_display_record(tool);
    sentinel_scan_session_t *session = live_session_for(tool);
    bool radio_busy = tool == SENTINEL_SCAN_BLE ? false : live_radio_busy;
    memset(&live_view, 0, sizeof(live_view));
    live_view.kind = tool == SENTINEL_SCAN_WIFI ? SENTINEL_VIEW_WIFI :
        tool == SENTINEL_SCAN_BLE ? SENTINEL_VIEW_BLE :
        tool == SENTINEL_SCAN_ENERGY ? SENTINEL_VIEW_ENERGY : SENTINEL_VIEW_RADIO_LIST;
    live_view.state = record->state == SENTINEL_SCAN_RUNNING ? SENTINEL_VIEW_LIVE : SENTINEL_VIEW_IDLE;
    if ((tool == SENTINEL_SCAN_BLE && !record->valid) ||
        (tool != SENTINEL_SCAN_BLE && !c5_online)) live_view.state = SENTINEL_VIEW_OFFLINE;
    else if (session->faulted || record->error != ESP_OK) live_view.state = SENTINEL_VIEW_ERROR;
    else if (session->paused && session->desired == tool) live_view.state = SENTINEL_VIEW_PAUSED;
    else if (session->completed && session->desired == tool) live_view.state = SENTINEL_VIEW_COMPLETE;
    if (tool == SENTINEL_SCAN_BLE && ble_discovery_presenting()) {
        const cerberus_ble_discovery_session_t *discovery = &ble_discovery_live->session;
        live_view.state = !cerberus_ble_discovery_session_released(discovery) || discovery->enabled ?
            SENTINEL_VIEW_LIVE : discovery->faulted || discovery->error ?
            SENTINEL_VIEW_ERROR : SENTINEL_VIEW_PAUSED;
    }
    live_view.total_count = record->count;
    if (tool == SENTINEL_SCAN_BLE) {
        if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST &&
            !cerberus_s3_ble_recent_supported(ble_filter)) live_ble_recent_only = false;
        const uint32_t visible_mask = live_ble_recent_mask(esp_timer_get_time() / 1000LL);
        const bool discovery = ble_discovery_presenting();
        const uint64_t epoch = discovery ? ble_discovery_live->inventory.epoch : 0U;
        const uint64_t revision = discovery ? ble_discovery_live->inventory.revision : 0U;
        if (live_ble_order_dirty || live_ble_order_filter != ble_filter ||
            live_ble_order_sort != live_ble_sort || live_ble_visible_mask != visible_mask ||
            live_ble_order_discovery != discovery || live_ble_order_count != record->count ||
            live_ble_order_epoch != epoch || live_ble_order_revision != revision) {
            if (discovery)
                live_ble_filtered_count = cerberus_s3_ble_discovery_inventory_page_sorted(
                    &ble_discovery_live->inventory, live_ble_sort, visible_mask, 0U,
                    live_ble_indexes, sizeof(live_ble_indexes));
            else
                live_ble_filtered_count = cerberus_s3_ble_focus_page_sorted_masked(
                    live_store ? live_store->ble : NULL, live_ble_focus_bits,
                    record->count, ble_filter, live_ble_sort, visible_mask, 0U,
                    live_ble_indexes, sizeof(live_ble_indexes));
            live_ble_order_filter = ble_filter;
            live_ble_order_sort = live_ble_sort;
            live_ble_visible_mask = visible_mask;
            live_ble_order_discovery = discovery;
            live_ble_order_count = record->count;
            live_ble_order_epoch = epoch;
            live_ble_order_revision = revision;
            live_ble_order_dirty = false;
        }
        live_view.total_count = (uint16_t)live_ble_filtered_count;
        if ((size_t)live_page[tool] * 4U >= live_ble_filtered_count) live_page[tool] = 0U;
        snprintf(live_ble_filter_label, sizeof(live_ble_filter_label), "%.15s >", ble_filter_name(ble_filter));
        live_view.tools_label = ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY && !ble_attack_selecting ? "MONITOR" : "TOOLS";
        live_view.sort_label = live_ble_sort == CERBERUS_S3_BLE_SORT_SIGNAL_DESC ? "SIGNAL >" :
            live_ble_sort == CERBERUS_S3_BLE_SORT_NAME_ASC ? "NAME A-Z >" : "SCAN ORDER >";
        if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST &&
            cerberus_s3_ble_recent_supported(ble_filter))
            live_view.age_label = live_ble_recent_only ? "WITHIN 30S >" : "ALL AGES >";
    }
    if(tool==SENTINEL_SCAN_WIFI){
        live_view.tools_label=wifi_attack_selecting?"ATTACKS":"TOOLS";
        live_view.tools_disabled=wifi_attack_selecting&&!wifi_target_count;
        live_view.selection_mode=wifi_attack_selecting;live_view.selection_count=wifi_target_count;
    }
    if (tool == SENTINEL_SCAN_WIFI) live_view.count_label = "NETWORKS STORED";
    if(tool==SENTINEL_SCAN_WIFI&&wifi_attack_selecting)live_view.count_label="NETWORKS SELECTED";
    if (tool == SENTINEL_SCAN_BLE) live_view.count_label = live_ble_filter_label;
    if (tool == SENTINEL_SCAN_BEACON) live_view.count_label = "BEACONS OBSERVED";
    if (tool == SENTINEL_SCAN_ACTIVITY) live_view.count_label = "CHANNELS OBSERVED";
    if (tool == SENTINEL_SCAN_WIFI) {
        bool normal_list = current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_LIST &&
            !wifi_attack_selecting;
        bool age_supported = c5_status.firmware_version >= CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE;
        if (normal_list && !age_supported) live_wifi_recent_only = false;
        bool recent = normal_list && age_supported && live_wifi_recent_only;
        live_wifi_filtered_count = cerberus_s3_wifi_observation_indexes(
            live_store ? live_store->wifi_observations : NULL, record->count,
            recent, (uint64_t)esp_timer_get_time(), live_wifi_indexes,
            sizeof(live_wifi_indexes));
        live_view.total_count = (uint16_t)live_wifi_filtered_count;
        if ((size_t)live_page[tool] * 4U >= live_wifi_filtered_count) live_page[tool] = 0U;
        if (normal_list && age_supported)
            live_view.age_label = recent ? "WITHIN 30S >" : "ALL AGES >";
        if (recent) live_view.count_label = "RECENT NETWORKS";
    }
    live_view.page = live_page[tool];
    live_view.page_count = (live_view.total_count + 3) / 4;
    live_view.has_previous = live_view.page > 0;
    live_view.has_next = live_view.page + 1 < live_view.page_count;
    if (tool >= SENTINEL_SCAN_ENERGY && tool <= SENTINEL_SCAN_ACTIVITY && record->valid) {
        live_view.progress_known = true;
        live_view.progress_percent = record->completed * 100 / 16;
    }
    if (live_view.state == SENTINEL_VIEW_OFFLINE) snprintf(live_subtitle, sizeof(live_subtitle),
        "%s OFFLINE / RETRYING", tool == SENTINEL_SCAN_BLE ? "S3 BLE" : "C5 RADIO");
    else if (session->active != SENTINEL_SCAN_NONE &&
             (session->active != tool || session->paused))
        snprintf(live_subtitle, sizeof(live_subtitle), "FINISHING CURRENT SWEEP / PLEASE WAIT");
    else if (session->faulted) snprintf(live_subtitle, sizeof(live_subtitle), "SCAN NEEDS ATTENTION / TAP SCAN");
    else if (radio_busy) snprintf(live_subtitle, sizeof(live_subtitle), "WAITING FOR ACTIVE RADIO SESSION");
    else if (record->error != ESP_OK) snprintf(live_subtitle, sizeof(live_subtitle), "RADIO ERROR / %.30s", esp_err_to_name(record->error));
    else if (session->paused) snprintf(live_subtitle, sizeof(live_subtitle), "STOPPED / TAP SCAN FOR A NEW SWEEP");
    else if (live_view.state == SENTINEL_VIEW_COMPLETE) snprintf(live_subtitle, sizeof(live_subtitle), "SWEEP COMPLETE / TAP AGAIN TO RESCAN");
    else snprintf(live_subtitle, sizeof(live_subtitle), "%s / %s",
        tool == SENTINEL_SCAN_WIFI ? "2.4 + 5 GHZ" : tool == SENTINEL_SCAN_BLE ? "BLE ADVERTISEMENTS" :
        tool == SENTINEL_SCAN_BEACON ? "ZIGBEE BEACONS" : tool == SENTINEL_SCAN_ACTIVITY ? "FRAME COUNTS" : "ENERGY CH 11-26",
        record->state == SENTINEL_SCAN_RUNNING ? "LISTENING" : "PREPARING ONE SWEEP");
    if (record->captured) snprintf(live_hint, sizeof(live_hint), "LATEST SWEEP %lus AGO / %s",
        (unsigned long)((uint32_t)(esp_timer_get_time() / 1000 - record->captured_ms) / 1000),
        tool == SENTINEL_SCAN_WIFI || tool == SENTINEL_SCAN_BLE ? "TAP TO EXPLORE" : "PASSIVE OBSERVATION");
    else snprintf(live_hint, sizeof(live_hint), "FIRST RESULTS APPEAR AFTER A FULL SWEEP");
    if(tool==SENTINEL_SCAN_WIFI&&wifi_attack_selecting&&record->captured){
        snprintf(live_subtitle,sizeof(live_subtitle),"TAP NETWORKS TO TOGGLE / THEN ATTACKS");
        snprintf(live_hint,sizeof(live_hint),"%u SELECTED / %u IN SWEEP / CLEAR ABOVE",wifi_target_count,record->count);
    }
    if (tool == SENTINEL_SCAN_BLE && record->captured)
        snprintf(live_hint, sizeof(live_hint), live_view.age_label ?
            "FILTER LEFT / SORT & AGE RIGHT / %u IN SWEEP" :
            "FILTER LEFT / SORT RIGHT / %u IN SWEEP", record->count);
    if (tool == SENTINEL_SCAN_BLE && ble_discovery_presenting()) {
        const cerberus_ble_discovery_session_t *discovery = &ble_discovery_live->session;
        if (discovery->drain_requested || (!discovery->enabled && discovery->epoch))
            snprintf(live_subtitle, sizeof(live_subtitle), "STOPPING / WAITING FOR RADIO RELEASE");
        else if (discovery->error)
            snprintf(live_subtitle, sizeof(live_subtitle), "SCAN ERROR / %.30s", esp_err_to_name(discovery->error));
        else if (discovery->enabled)
            snprintf(live_subtitle, sizeof(live_subtitle), "LIVE BLE / RESULTS UPDATE WHILE LISTENING");
        else
            snprintf(live_subtitle, sizeof(live_subtitle), "STOPPED / TAP SCAN TO CONTINUE");
        snprintf(live_hint, sizeof(live_hint), "KEPT %u / REPLACED %lu / CACHE FULL %u",
            ble_discovery_live->inventory.count,
            (unsigned long)ble_discovery_live->inventory.replacements,
            ble_discovery_live->binding_overflows);
    }
    if (tool == SENTINEL_SCAN_WIFI && live_view.age_label && record->captured)
        snprintf(live_hint, sizeof(live_hint), live_wifi_recent_only ?
            "RECENT MATCHING FRAMES / %u IN SWEEP" :
            "AGE FILTER ABOVE / %u IN SWEEP", record->count);
    live_view.subtitle = live_subtitle;
    live_view.hint = live_hint;
    live_view.message = live_view.state == SENTINEL_VIEW_LIVE ?
        (record->captured ? "LAST SWEEP WAS EMPTY" : "LISTENING") :
        live_view.state == SENTINEL_VIEW_COMPLETE ? "NO SIGNALS FOUND" : NULL;
    if (tool == SENTINEL_SCAN_BLE && ble_discovery_presenting())
        live_view.message = ble_discovery_live->session.enabled ?
            "WAITING FOR MATCHING REPORTS" : "NO MATCHING OBSERVATIONS";
    if (tool == SENTINEL_SCAN_BLE && live_ble_recent_only && live_view.age_label && record->captured)
        live_view.message = "NO HINTS WITHIN 30S";
    if (tool == SENTINEL_SCAN_WIFI && live_wifi_recent_only && live_view.age_label && record->captured)
        live_view.message = "NO NETWORKS WITHIN 30S";
    if (tool == SENTINEL_SCAN_ENERGY) {
        if (record->captured) {
            memcpy(live_view.energy_dbm, live_energy, sizeof(live_energy));
            live_view.energy_valid_mask = 0xffff;
        }
        return;
    }
    for (uint16_t row = 0; row < 4; ++row) {
        uint16_t index = live_page[tool] * 4 + row;
        if (index >= live_view.total_count) break;
        if (tool == SENTINEL_SCAN_BLE) index = live_ble_indexes[index];
        else if (tool == SENTINEL_SCAN_WIFI) index = live_wifi_indexes[index];
        sentinel_scan_row_t *out = &live_view.rows[row];
        live_names[row][0] = live_details[row][0] = live_badges[row][0] = '\0';
        if (tool == SENTINEL_SCAN_WIFI) {
            const cerberus_c5_wifi_result_t *result = &live_store->wifi[index];
            cerberus_wifi_trust_state_t trust = cerberus_settings_wifi_trust_state(result->ssid, result->bssid, result->security);
            snprintf(live_names[row], sizeof(live_names[row]), "%.32s", result->ssid[0] ? result->ssid : "Hidden network");
            char identifier[24];
            cerberus_privacy_format_wifi_identifier(result->bssid,privacy_addresses_masked(),identifier,sizeof(identifier));
            snprintf(live_details[row], sizeof(live_details[row]), "%s / CH %u",identifier,result->channel);
            out->selected=wifi_attack_selecting?wifi_targets_find(result)>=0:
                wifi_selection_valid&&!memcmp(wifi_selected_result.bssid,result->bssid,6)&&wifi_selected_result.channel==result->channel;
            snprintf(live_badges[row], sizeof(live_badges[row]), "%s / %s / %s",result->channel>14?"5G":"2.4G",wifi_security_text(result->security),
                out->selected?"SELECTED":trust==CERBERUS_WIFI_TRUST_EXACT?"TRUSTED":trust==CERBERUS_WIFI_TRUST_MISMATCH?"REVIEW":"TAP TO SELECT");
            out->rssi = result->rssi; out->signal_valid = true;
            out->marked = trust == CERBERUS_WIFI_TRUST_EXACT; out->review = trust == CERBERUS_WIFI_TRUST_MISMATCH;
        } else if (tool == SENTINEL_SCAN_BLE) {
            const cerberus_c5_ble_result_t *result = live_ble_display_peer((uint8_t)index);
            const cerberus_s3_ble_focus_t *row_focus = live_ble_display_focus((uint8_t)index);
            char identifier[24];
            cerberus_privacy_format_ble_identifier(result->address, privacy_addresses_masked(), identifier, sizeof(identifier));
            if (result->name[0])
                (void)cerberus_s3_ble_list_label(result, live_names[row], sizeof(live_names[row]));
            else snprintf(live_names[row], sizeof(live_names[row]), "%s",
                privacy_addresses_masked() ? "Unnamed device" : identifier);
            snprintf(live_details[row], sizeof(live_details[row]), "%s", identifier);
            if (ble_discovery_presenting() &&
                (!cerberus_s3_ble_discovery_inventory_current(&ble_discovery_live->inventory, (uint8_t)index) ||
                 ble_discovery_live->inventory.entries[index].epoch != ble_discovery_live->session.epoch))
                snprintf(live_details[row], sizeof(live_details[row]), "%s / EARLIER", identifier);
            out->marked = cerberus_settings_ble_is_favorite(result->address, result->address_type);
            snprintf(live_badges[row], sizeof(live_badges[row]), "%s", out->marked ? "FAVORITE" :
                (result->flags & CERBERUS_BLE_RESULT_CONNECTABLE) ? "CONNECTABLE" : "BROADCAST");
            if (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY && row_focus) {
                const cerberus_s3_ble_discovery_evidence_t *hints = &row_focus->discovery;
                cerberus_findmy_hint_sources_line(&hints->findmy_advertisement,
                    &hints->findmy_scan_response, live_badges[row], sizeof(live_badges[row]));
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_FLIPPER && row_focus) {
                const cerberus_s3_ble_discovery_evidence_t *hints = &row_focus->discovery;
                cerberus_flipper_hint_sources_line(&hints->flipper_advertisement,
                    &hints->flipper_scan_response, live_badges[row], sizeof(live_badges[row]));
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_META_HINT && row_focus) {
                const cerberus_s3_ble_discovery_evidence_t *hints = &row_focus->discovery;
                cerberus_meta_hint_sources_line(&hints->meta_advertisement,
                    &hints->meta_scan_response, live_badges[row], sizeof(live_badges[row]));
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_IBEACON && row_focus) {
                const cerberus_s3_ble_focus_t *focus = row_focus;
                cerberus_ibeacon_source_line(focus->ibeacon_scan_response,
                    cerberus_s3_ble_ibeacon_is_current(focus),
                    (uint32_t)(esp_timer_get_time() / 1000LL) - focus->ibeacon_observed_ms,
                    true, live_badges[row], sizeof(live_badges[row]));
                if (ble_discovery_presenting())
                    ble_hint_age64_line(live_badges[row], sizeof(live_badges[row]),
                        focus->ibeacon_observed_ms,
                        ble_discovery_live->inventory.entries[index].source_now_ms,
                        esp_timer_get_time() / 1000LL);
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_FLOCK && row_focus) {
                const cerberus_s3_ble_focus_t *focus = row_focus;
                cerberus_flock_sources_line(&focus->discovery.flock_advertisement,
                    &focus->discovery.flock_scan_response, focus->flock_oui_clue,
                    live_badges[row], sizeof(live_badges[row]));
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_HC_NAME_HINT && row_focus) {
                const cerberus_s3_ble_discovery_evidence_t *evidence = &row_focus->discovery;
                cerberus_hc_sources_line(evidence->hc_advertisement, evidence->hc_scan_response,
                    live_badges[row], sizeof(live_badges[row]));
            }
            out->rssi = result->rssi;
            out->signal_valid = cerberus_ble_rssi_valid(result->rssi);
        } else if (tool == SENTINEL_SCAN_BEACON) {
            const cerberus_ieee802154_beacon_scan_result_payload_t *result = &live_store->beacons[index];
            snprintf(live_names[row], sizeof(live_names[row]), "Zigbee / channel %u", result->channel);
            if (privacy_addresses_masked()) snprintf(live_details[row], sizeof(live_details[row]), "PAN masked / LQI %u", result->lqi);
            else snprintf(live_details[row], sizeof(live_details[row]), "PAN %04X / LQI %u", result->pan_id, result->lqi);
            snprintf(live_badges[row], sizeof(live_badges[row]), "BEACON");
            out->rssi = result->rssi; out->signal_valid = true;
        } else if (tool == SENTINEL_SCAN_ACTIVITY) {
            const uint8_t *counts = live_activity[index];
            unsigned total = 0;
            for (unsigned i = 0; i < CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT; ++i) total += counts[i];
            snprintf(live_names[row], sizeof(live_names[row]), "Channel %u / %u+", index + 11, total);
            snprintf(live_details[row], sizeof(live_details[row]), "BEACON %u / DATA %u / ACK %u", counts[0], counts[1], counts[2]);
            snprintf(live_badges[row], sizeof(live_badges[row]), "FRAMES");
        }
        out->name = live_names[row]; out->detail = live_details[row]; out->badge = live_badges[row];
        ++live_view.row_count;
    }
}

static esp_err_t draw_live_scan(void)
{
    if (ble_discovery_page()) (void)ble_discovery_acquire();
    live_make_view();
    return live_paint(true);
}

static void live_scan_tick(uint32_t now, bool asleep)
{
    if (!live_ble_session_initialized) {
        sentinel_scan_session_init(&live_ble_session);
        live_ble_session_initialized = true;
    }
    ble_discovery_tick((uint64_t)esp_timer_get_time() / 1000U, asleep);
    sentinel_scan_tool_t page_tool = live_page_tool();
    sentinel_scan_tool_t desired = page_tool == SENTINEL_SCAN_BLE ? SENTINEL_SCAN_NONE : page_tool;
    sentinel_scan_tool_t ble_desired = (page_tool == SENTINEL_SCAN_BLE && !ble_discovery_page()) ||
        (current_page == UI_PAGE_WATCH && watch_ble_requested()) ?
        SENTINEL_SCAN_BLE : SENTINEL_SCAN_NONE;
    if (ble_discovery_live && !cerberus_ble_discovery_session_released(&ble_discovery_live->session))
        ble_desired = SENTINEL_SCAN_NONE;
    /* Sleeping may cancel an active sweep, but must not rearm a completed or
     * explicitly stopped page when the user wakes the same screen. */
    if (asleep) {
        desired = live_session.completed || live_session.paused ?
            live_session.desired : SENTINEL_SCAN_NONE;
        ble_desired = live_ble_session.completed || live_ble_session.paused ?
            live_ble_session.desired : SENTINEL_SCAN_NONE;
    }
    bool changed = live_session.desired != desired;
    changed |= live_ble_session.desired != ble_desired;
    /* Derived pages reuse a real completed inventory. Apply this only on entry:
     * subsequent ticks must preserve explicit Scan Again and Stop requests. */
    bool cached_wifi_view = !asleep && desired == SENTINEL_SCAN_WIFI &&
        current_page == UI_PAGE_WIFI && live_records[SENTINEL_SCAN_WIFI].captured &&
        ((wifi_mode == UI_WIFI_MODE_LIST && live_records[SENTINEL_SCAN_WIFI].valid &&
          wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE &&
          wifi_scan_status.generation == live_records[SENTINEL_SCAN_WIFI].saved_generation) ||
         wifi_mode == UI_WIFI_MODE_STATS);
    if (changed && cached_wifi_view) sentinel_scan_session_select_snapshot(&live_session, desired);
    else sentinel_scan_session_select(&live_session, desired);
    sentinel_scan_session_select(&live_ble_session, ble_desired);
    /* Inspection drains Wi-Fi instead of cancelling: its C5 cancellation
     * deliberately discards the inventory needed for an explicit connection. */
    bool inspecting_wifi = (current_page == UI_PAGE_FIELD_MENU && field_menu.attack && wifi_selection_valid) ||
        (current_page == UI_PAGE_WIFI && (wifi_mode == UI_WIFI_MODE_DETAIL || wifi_mode == UI_WIFI_MODE_TARGET ||
        wifi_mode == UI_WIFI_MODE_PASSWORD || wifi_mode == UI_WIFI_MODE_CONNECTION || wifi_mode == UI_WIFI_MODE_STATIONS ||
        wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS || wifi_mode == UI_WIFI_MODE_AP_JOB));
    sentinel_scan_session_set_stop_supported(&live_session, SENTINEL_SCAN_WIFI, !inspecting_wifi);
    uint32_t poll_ms = live_session.active == SENTINEL_SCAN_NONE &&
        live_ble_session.active == SENTINEL_SCAN_NONE &&
        live_session.completed && live_ble_session.completed ? 1000U : 500U;
    if (!changed && (uint32_t)(now - live_last_poll) < poll_ms) return;
    if (desired == SENTINEL_SCAN_NONE && live_session.active == SENTINEL_SCAN_NONE &&
        ble_desired == SENTINEL_SCAN_NONE && live_ble_session.active == SENTINEL_SCAN_NONE) return;
    live_last_poll = now;
    changed = live_observe(now) || changed;
    if (c5_online) {
        if (live_session.active == SENTINEL_SCAN_DEFENSE && wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING)
            (void)refresh_wifi_defense_survey();
        sentinel_scan_tool_t active = live_session.active;
        bool was_complete = live_session.completed;
        sentinel_scan_command_t command = sentinel_scan_session_step(&live_session, now,
            (sentinel_scan_observation_t){.valid = live_scan_observation_valid,
                .radio_busy = live_radio_busy, .tool = active, .state = live_records[active].state});
        changed |= was_complete != live_session.completed;
        if (command.kind != SENTINEL_SCAN_COMMAND_NONE) {
            esp_err_t err = live_execute(command);
            sentinel_scan_session_ack(&live_session, command, err == ESP_OK ? SENTINEL_SCAN_ACCEPTED :
                err == ESP_ERR_INVALID_STATE ? SENTINEL_SCAN_BUSY : SENTINEL_SCAN_FAILED, now);
            live_records[command.tool].error = err;
            if (err == ESP_OK && command.kind == SENTINEL_SCAN_COMMAND_START) {
                live_records[command.tool].state = SENTINEL_SCAN_RUNNING;
                live_capture_pending |= UINT32_C(1) << command.tool;
            }
            changed = true;
        }
    }
    sentinel_scan_tool_t ble_active = live_ble_session.active;
    bool ble_was_complete = live_ble_session.completed;
    sentinel_scan_command_t ble_command = sentinel_scan_session_step(&live_ble_session, now,
        (sentinel_scan_observation_t){.valid = live_ble_observation_valid,
            .radio_busy = false, .tool = ble_active, .state = live_records[SENTINEL_SCAN_BLE].state});
    changed |= ble_was_complete != live_ble_session.completed;
    if (ble_command.kind != SENTINEL_SCAN_COMMAND_NONE) {
        esp_err_t err = live_execute(ble_command);
        sentinel_scan_session_ack(&live_ble_session, ble_command,
            err == ESP_OK ? SENTINEL_SCAN_ACCEPTED :
            err == ESP_ERR_INVALID_STATE ? SENTINEL_SCAN_BUSY : SENTINEL_SCAN_FAILED, now);
        live_records[SENTINEL_SCAN_BLE].error = err;
        if (err == ESP_OK && ble_command.kind == SENTINEL_SCAN_COMMAND_START) {
            live_records[SENTINEL_SCAN_BLE].state = SENTINEL_SCAN_RUNNING;
            live_capture_pending |= UINT32_C(1) << SENTINEL_SCAN_BLE;
        }
        changed = true;
    }
    if (!asleep && !ui_capability_blocked && live_is_view()) {
        ++live_frame;
        live_make_view();
        ESP_ERROR_CHECK(live_paint(false));
    } else if (!asleep && (page_tool != SENTINEL_SCAN_NONE || inspecting_wifi ||
        (current_page == UI_PAGE_IEEE802154 && ieee802154_mode == UI_IEEE802154_MODE_ASSESSMENT)) && changed) {
        ESP_ERROR_CHECK(draw_current_page());
    }
}

static void live_reset_observations(void)
{
    /* C5 restarts do not invalidate a completed S3 BLE generation. */
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        if (tool == SENTINEL_SCAN_BLE) continue;
        memset(&live_records[tool], 0, sizeof(live_records[tool]));
        live_capture_pending &= ~(UINT32_C(1) << tool);
        live_page[tool] = 0;
    }
    live_last_poll = 0;
}

static bool live_scan_paused(void)
{
    if (ble_discovery_presenting())
        return !ble_discovery_live->session.enabled;
    sentinel_scan_session_t *session = live_session_for(live_page_tool());
    return session->paused || session->faulted;
}

static const char *live_scan_action_label(void)
{
    if (ble_discovery_presenting())
        return ble_discovery_live->session.enabled ||
            !cerberus_ble_discovery_session_released(&ble_discovery_live->session) ? "STOP" : "SCAN";
    sentinel_scan_session_t *session = live_session_for(live_page_tool());
    if (session->completed) return "SCAN AGAIN";
    return live_scan_paused() || live_records[live_page_tool()].error != ESP_OK ? "SCAN" : "STOP";
}

static void live_toggle_pause(void)
{
    if (ble_discovery_page()) {
        ble_discovery_toggle();
        return;
    }
    sentinel_scan_session_t *session = live_session_for(live_page_tool());
    sentinel_scan_session_select(session, live_page_tool());
    sentinel_scan_session_pause(session, !(live_scan_paused() || session->completed ||
        live_records[live_page_tool()].error != ESP_OK));
    live_last_poll = 0;
}

static bool live_connection_ready(void)
{
    if (!c5_online) return false;
    (void)live_observe((uint32_t)(esp_timer_get_time() / 1000));
    if (!live_scan_observation_valid || live_radio_busy) return false;
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        if (tool == SENTINEL_SCAN_BLE) continue;
        if (!live_records[tool].valid || live_records[tool].state == SENTINEL_SCAN_RUNNING) return false;
    }
    return true;
}

static void handle_live_scan_tap(uint16_t x, uint16_t y)
{
    /* Hit-test the last painted snapshot, never a newly fetched row ordering. */
    sentinel_scan_tool_t tool = live_page_tool();
    if (tool == SENTINEL_SCAN_BLE && x >= 8U && x < 216U && y >= 48U && y < 98U) {
        ble_filter = (cerberus_s3_ble_filter_t)(((unsigned)ble_filter + 1U) % CERBERUS_S3_BLE_FILTER_COUNT);
        if (ble_discovery_presenting()) {
            ble_discovery_live->selection_pending = false;
            ble_discovery_live->destination = 0U;
            (void)cerberus_ble_discovery_session_select(&ble_discovery_live->session, (uint8_t)ble_filter);
        }
        live_page[tool] = 0U;
        live_make_view();
        ESP_ERROR_CHECK(live_paint(false));
        return;
    }
    sentinel_view_action_t action = sentinel_scan_view_hit_test(&live_view, x, y);
    if (action == SENTINEL_VIEW_SORT && tool == SENTINEL_SCAN_BLE) {
        live_ble_sort = (cerberus_s3_ble_sort_t)(((unsigned)live_ble_sort + 1U) % CERBERUS_S3_BLE_SORT_COUNT);
        live_page[tool] = 0U;
    }
    else if (action == SENTINEL_VIEW_AGE && tool == SENTINEL_SCAN_BLE &&
        current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST &&
        cerberus_s3_ble_recent_supported(ble_filter)) {
        live_ble_recent_only = !live_ble_recent_only;
        live_page[tool] = 0U;
    }
    else if (action == SENTINEL_VIEW_AGE && tool == SENTINEL_SCAN_WIFI &&
        current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_LIST &&
        !wifi_attack_selecting && c5_status.firmware_version >= CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE) {
        live_wifi_recent_only = !live_wifi_recent_only;
        live_page[tool] = 0U;
    }
    else if(action==SENTINEL_VIEW_CLEAR&&tool==SENTINEL_SCAN_WIFI&&wifi_attack_selecting){
        wifi_targets_clear();wifi_selection_valid=false;
    }
    else if (action == SENTINEL_VIEW_PREV && live_page[tool]) --live_page[tool];
    else if (action == SENTINEL_VIEW_NEXT) ++live_page[tool];
    else if (action == SENTINEL_VIEW_PAUSE) {
        if (ble_discovery_page()) ble_discovery_toggle();
        else {
            sentinel_scan_session_t *session = live_session_for(tool);
            sentinel_scan_session_select(session, tool);
            sentinel_scan_session_pause(session, live_view.state == SENTINEL_VIEW_LIVE);
        }
        live_last_poll = 0;
    } else if (action == SENTINEL_VIEW_TOOLS) {
        if (current_page == UI_PAGE_WIFI) {
            if(wifi_attack_selecting&&wifi_target_count)wifi_target_open_attacks();
            else wifi_mode=UI_WIFI_MODE_MENU;
        }
        else if (current_page == UI_PAGE_BLE) {
            if (ble_discovery_presenting()) {
                ble_discovery_live->selection_pending = false;
                ble_discovery_live->destination = ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY &&
                    !ble_connection_is_active() ? 2U : 1U;
                cerberus_ble_discovery_session_stop(&ble_discovery_live->session);
            } else if (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY && !ble_attack_selecting &&
                !ble_connection_is_active()) ble_findmy_monitor_open();
            else ble_mode = UI_BLE_MODE_MENU;
        }
        else ieee802154_mode = UI_IEEE802154_MODE_MENU;
    } else if (action >= SENTINEL_VIEW_ROW0 && action <= SENTINEL_VIEW_ROW3) {
        if (tool == SENTINEL_SCAN_BLE && ble_discovery_presenting()) {
            ble_discovery_select_painted((unsigned)(action - SENTINEL_VIEW_ROW0));
            live_make_view();
            ESP_ERROR_CHECK(live_paint(false));
            return;
        }
        uint16_t index = live_page[tool] * 4 + action - SENTINEL_VIEW_ROW0;
        if (tool == SENTINEL_SCAN_BLE) {
            if (index >= live_ble_filtered_count) return;
            index = live_ble_indexes[index];
        } else if (tool == SENTINEL_SCAN_WIFI && !wifi_attack_selecting) {
            if (index >= live_wifi_filtered_count) return;
            index = live_wifi_indexes[index];
        } else if (index >= live_records[tool].count) return;
        if (tool == SENTINEL_SCAN_WIFI) {
            if(wifi_attack_selecting){
                if(!wifi_target_toggle_selection(&live_store->wifi[index],(uint8_t)index)){
                    snprintf(live_hint,sizeof(live_hint),"NO MEMORY / SELECTION LIMIT REACHED");
                    live_view.hint=live_hint;ESP_ERROR_CHECK(sentinel_scan_view_update(&live_view,live_frame));return;
                }
                live_make_view();ESP_ERROR_CHECK(sentinel_scan_view_update(&live_view,live_frame));return;
            }
            wifi_selected_result = live_store->wifi[index];
            wifi_relation_bind_selection(true);
            wifi_selected_index = index;
            wifi_selection_valid = true;
            wifi_analysis_generation = 0;
            wifi_analysis_error = ESP_ERR_INVALID_STATE;
            wifi_trust_error = ESP_OK;
            wifi_target_finish_selection();
            wifi_connection_error = c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
        } else if (tool == SENTINEL_SCAN_BLE) {
            if (!live_ble_evidence ||
                live_ble_evidence[index].generation != live_records[tool].saved_generation ||
                live_ble_evidence[index].peer.address_type != live_store->ble[index].address_type ||
                memcmp(live_ble_evidence[index].peer.address, live_store->ble[index].address, 6U) != 0) {
                snprintf(live_hint, sizeof(live_hint), "WAIT FOR SCAN COMPLETE / THEN SELECT AGAIN");
                live_view.hint = live_hint;
                ESP_ERROR_CHECK(sentinel_scan_view_update(&live_view, live_frame));
                return;
            }
            ble_select_frozen_live(&live_ble_evidence[index]);
        } else return;
    } else return;
    if (live_is_view()) {
        live_make_view();
        ESP_ERROR_CHECK(live_paint(false));
    } else {
        ESP_ERROR_CHECK(draw_current_page());
    }
}

static esp_err_t live_connect_selected_wifi(const char *password)
{
    /* Resolve the immutable chosen identity against a fresh COMPLETE snapshot.
     * Never pair an old row index with a new generation. */
    if (!live_connection_ready()) return ESP_ERR_INVALID_STATE;
    cerberus_wifi_scan_status_payload_t status;
    esp_err_t err = c5_spi_link_get_wifi_scan_status(&status);
    if (err != ESP_OK) return err;
    if (!wifi_selection_valid || status.state != CERBERUS_WIFI_SCAN_COMPLETE) return ESP_ERR_INVALID_STATE;
    for (uint8_t i = 0; i < status.stored_count && i < CERBERUS_WIFI_SCAN_MAX_RESULTS; ++i) {
        cerberus_c5_wifi_result_t candidate;
        err = c5_spi_link_get_wifi_scan_result(status.generation, i, &candidate);
        if (err != ESP_OK) return err;
        if (memcmp(candidate.bssid, wifi_selected_result.bssid, 6) == 0 &&
            candidate.security == wifi_selected_result.security && strcmp(candidate.ssid, wifi_selected_result.ssid) == 0) {
            wifi_connection_target=candidate;wifi_connection_target_valid=true;
            err=c5_spi_link_connect_wifi(status.generation, i, password, &wifi_connection_status);
            return err;
        }
    }
    return ESP_ERR_NOT_FOUND;
}
