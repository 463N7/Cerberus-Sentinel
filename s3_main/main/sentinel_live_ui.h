/* Private UI integration. Included once by ui.c after the base tool functions.
 * All radio commands run on the UI task through the scan session scheduler. */

typedef struct {
    bool valid, captured;
    uint8_t state, generation, saved_generation, completed;
    uint16_t count;
    uint32_t captured_ms;
    esp_err_t error;
} live_scan_record_t;

static sentinel_scan_session_t live_session;
static live_scan_record_t live_records[SENTINEL_SCAN_TOOL_COUNT];
static cerberus_c5_wifi_result_t live_wifi[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_c5_ble_result_t live_ble[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_ieee802154_beacon_scan_result_payload_t live_beacons[CERBERUS_IEEE802154_BEACON_MAX_RESULTS];
static uint8_t live_activity[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static int8_t live_energy[16];
/* Staging lives in static RAM, not on the display task's small stack. */
static union {
    cerberus_c5_wifi_result_t wifi[CERBERUS_WIFI_SCAN_MAX_RESULTS];
    cerberus_c5_ble_result_t ble[CERBERUS_BLE_SCAN_MAX_RESULTS];
    cerberus_ieee802154_beacon_scan_result_payload_t beacon[CERBERUS_IEEE802154_BEACON_MAX_RESULTS];
} live_staging;
static uint16_t live_page[SENTINEL_SCAN_TOOL_COUNT];
static uint32_t live_last_poll, live_frame;
static bool live_radio_busy;
static bool live_status_changed;
static sentinel_scan_view_t live_view;
static char live_names[4][40], live_details[4][52], live_badges[4][16];
static char live_subtitle[52], live_hint[52];

static bool live_has_snapshot(sentinel_scan_tool_t tool) { return live_records[tool].captured; }
static uint16_t live_snapshot_count(sentinel_scan_tool_t tool) { return live_records[tool].count; }

static bool live_supported(sentinel_scan_tool_t tool)
{
    uint32_t cap = tool == SENTINEL_SCAN_WIFI ? CERBERUS_C5_CAP_WIFI_2_4_GHZ :
        tool == SENTINEL_SCAN_BLE ? CERBERUS_C5_CAP_BLE :
        tool == SENTINEL_SCAN_DEFENSE ? CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY : CERBERUS_C5_CAP_IEEE802154;
    return (c5_status.capabilities & cap) != 0;
}

static sentinel_scan_tool_t live_page_tool(void)
{
    if (ui_capability_blocked) return SENTINEL_SCAN_NONE;
    if (current_page == UI_PAGE_WATCH) return watch_requested_tool();
    if (current_page == UI_PAGE_WIFI) {
        if (wifi_mode == UI_WIFI_MODE_LIST || wifi_mode == UI_WIFI_MODE_STATS ||
            wifi_mode == UI_WIFI_MODE_CHANNELS_24 || wifi_mode == UI_WIFI_MODE_CHANNELS_5 ||
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
    if (!live_supported(tool)) { err = ESP_OK; state = SENTINEL_SCAN_IDLE; generation = completed = 0; radio_error = 0; }
    live_status_changed |= record->valid != (err == ESP_OK) || record->state != state ||
        record->generation != generation ||
        record->error != (err != ESP_OK ? err : state == SENTINEL_SCAN_ERROR ? radio_error : ESP_OK);
    record->valid = err == ESP_OK;
    record->state = state;
    record->generation = generation;
    record->completed = completed;
    record->error = err != ESP_OK ? err : state == SENTINEL_SCAN_ERROR ? radio_error : ESP_OK;
}

static bool live_capture(sentinel_scan_tool_t tool, uint32_t now)
{
    live_scan_record_t *record = &live_records[tool];
    if (!record->valid || record->state != SENTINEL_SCAN_COMPLETE ||
        (record->captured && record->saved_generation == record->generation)) return false;
    esp_err_t err = ESP_OK;
    uint16_t count = 0;
    uint8_t gen = record->generation;
    if (tool == SENTINEL_SCAN_WIFI) {
        count = wifi_scan_status.stored_count;
        if (count > CERBERUS_WIFI_SCAN_MAX_RESULTS) return false;
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i)
            err = c5_spi_link_get_wifi_scan_result(gen, i, &live_staging.wifi[i]);
        cerberus_wifi_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_wifi_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_WIFI_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            memcpy(live_wifi, live_staging.wifi, count * sizeof(live_wifi[0]));
            (void)refresh_wifi_scan_status();
            if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_BASELINE)
                (void)refresh_wifi_baseline();
        }
    } else if (tool == SENTINEL_SCAN_BLE) {
        count = ble_scan_status.stored_count;
        if (count > CERBERUS_BLE_SCAN_MAX_RESULTS) return false;
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i)
            err = c5_spi_link_get_ble_scan_result(gen, i, &live_staging.ble[i]);
        cerberus_ble_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_ble_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_BLE_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) {
            memcpy(live_ble, live_staging.ble, count * sizeof(live_ble[0]));
            (void)refresh_ble_scan_status();
        }
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
        if (count > CERBERUS_IEEE802154_BEACON_MAX_RESULTS) return false;
        for (uint16_t i = 0; i < count && err == ESP_OK; ++i)
            err = c5_spi_link_get_ieee802154_beacon_scan_result(gen, i, &live_staging.beacon[i]);
        cerberus_ieee802154_beacon_scan_status_payload_t end;
        if (err == ESP_OK) err = c5_spi_link_get_ieee802154_beacon_scan_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE || end.generation != gen || end.stored_count != count)) err = ESP_ERR_INVALID_STATE;
        if (err == ESP_OK) memcpy(live_beacons, live_staging.beacon, count * sizeof(live_beacons[0]));
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
    if (live_page[tool] * 4 >= count) live_page[tool] = count ? (count - 1) / 4 : 0;
    return true;
}

static bool live_observe(uint32_t now)
{
    /* Fresh status from every radio avoids Wi-Fi/BLE overlap and respects
     * sessions opened explicitly through a connection page or USB console. */
    live_status_changed = false;
    wifi_scan_checked = ble_scan_checked = ieee802154_scan_checked = true;
    ieee802154_beacon_checked = ieee802154_activity_checked = wifi_defense_checked = true;
    wifi_scan_error = live_supported(SENTINEL_SCAN_WIFI) ? c5_spi_link_get_wifi_scan_status(&wifi_scan_status) : ESP_ERR_NOT_SUPPORTED;
    live_status(SENTINEL_SCAN_WIFI, wifi_scan_error, wifi_scan_status.state, wifi_scan_status.generation, 0, wifi_scan_status.error_code);
    ble_scan_error = live_supported(SENTINEL_SCAN_BLE) ? c5_spi_link_get_ble_scan_status(&ble_scan_status) : ESP_ERR_NOT_SUPPORTED;
    live_status(SENTINEL_SCAN_BLE, ble_scan_error, ble_scan_status.state, ble_scan_status.generation, 0, ble_scan_status.error_code);
    ieee802154_scan_error = live_supported(SENTINEL_SCAN_ENERGY) ? c5_spi_link_get_ieee802154_energy_scan_status(&ieee802154_scan_status) : ESP_ERR_NOT_SUPPORTED;
    live_status(SENTINEL_SCAN_ENERGY, ieee802154_scan_error, ieee802154_scan_status.state, ieee802154_scan_status.generation, ieee802154_scan_status.completed_count, ieee802154_scan_status.error_code);
    ieee802154_beacon_error = live_supported(SENTINEL_SCAN_BEACON) ? c5_spi_link_get_ieee802154_beacon_scan_status(&ieee802154_beacon_status) : ESP_ERR_NOT_SUPPORTED;
    live_status(SENTINEL_SCAN_BEACON, ieee802154_beacon_error, ieee802154_beacon_status.state, ieee802154_beacon_status.generation, ieee802154_beacon_status.completed_count, ieee802154_beacon_status.error_code);
    ieee802154_activity_error = live_supported(SENTINEL_SCAN_ACTIVITY) ? c5_spi_link_get_ieee802154_activity_scan_status(&ieee802154_activity_status) : ESP_ERR_NOT_SUPPORTED;
    live_status(SENTINEL_SCAN_ACTIVITY, ieee802154_activity_error, ieee802154_activity_status.state, ieee802154_activity_status.generation, ieee802154_activity_status.completed_count, ieee802154_activity_status.error_code);
    uint8_t previous_defense_generation = wifi_defense_status.generation;
    wifi_defense_error = live_supported(SENTINEL_SCAN_DEFENSE) ? c5_spi_link_get_wifi_defense_status(&wifi_defense_status) : ESP_ERR_NOT_SUPPORTED;
    if (previous_defense_generation != wifi_defense_status.generation) wifi_defense_loaded_count = 0;
    live_status(SENTINEL_SCAN_DEFENSE, wifi_defense_error, wifi_defense_status.state, wifi_defense_status.generation, wifi_defense_status.completed_count, wifi_defense_status.error_code);
    cerberus_wifi_connection_status_payload_t wifi = {0};
    cerberus_ble_connection_status_payload_t ble = {0};
    bool valid = !(c5_status.capabilities & CERBERUS_C5_CAP_WIFI_STATION) || c5_spi_link_get_wifi_connection_status(&wifi) == ESP_OK;
    valid = (!(c5_status.capabilities & CERBERUS_C5_CAP_BLE_GATT_CLIENT) || c5_spi_link_get_ble_connection_status(&ble) == ESP_OK) && valid;
    live_radio_busy = !valid || wifi.state == CERBERUS_WIFI_CONNECTION_STAGING ||
        wifi.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING || wifi.state == CERBERUS_WIFI_CONNECTION_CONNECTING ||
        wifi.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        ble.state == CERBERUS_BLE_CONNECTION_CONNECTING || ble.state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        ble.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
    bool changed = false;
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        valid = valid && live_records[tool].valid;
        if (tool != live_session.active && live_records[tool].state == SENTINEL_SCAN_RUNNING) live_radio_busy = true;
        changed = live_capture(tool, now) || changed;
    }
    /* Unknown OTHER work blocks a start; it must never prevent stopping our
     * known active scan. A failed active query still blocks both actions. */
    live_radio_busy |= !valid;
    live_scan_observation_valid = live_session.active == SENTINEL_SCAN_NONE ? valid : live_records[live_session.active].valid;
    return changed || live_status_changed;
}

static esp_err_t live_execute(sentinel_scan_command_t command)
{
    bool start = command.kind == SENTINEL_SCAN_COMMAND_START;
    switch (command.tool) {
    case SENTINEL_SCAN_WIFI: return start ? c5_spi_link_start_wifi_scan(&wifi_scan_status) : c5_spi_link_stop_wifi_scan(&wifi_scan_status);
    case SENTINEL_SCAN_BLE: return start ? c5_spi_link_start_ble_scan(&ble_scan_status) : c5_spi_link_stop_ble_scan(&ble_scan_status);
    case SENTINEL_SCAN_ENERGY: return start ? c5_spi_link_start_ieee802154_energy_scan(&ieee802154_scan_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_BEACON: return start ? c5_spi_link_start_ieee802154_beacon_scan(&ieee802154_beacon_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_ACTIVITY: return start ? c5_spi_link_start_ieee802154_activity_scan(&ieee802154_activity_status) : ESP_ERR_NOT_SUPPORTED;
    case SENTINEL_SCAN_DEFENSE: return start ? c5_spi_link_start_wifi_defense_survey(&wifi_defense_status) : c5_spi_link_stop_wifi_defense_survey(&wifi_defense_status);
    default: return ESP_ERR_INVALID_ARG;
    }
}

static void live_make_view(void)
{
    sentinel_scan_tool_t tool = live_page_tool();
    live_scan_record_t *record = &live_records[tool];
    memset(&live_view, 0, sizeof(live_view));
    live_view.kind = tool == SENTINEL_SCAN_WIFI ? SENTINEL_VIEW_WIFI :
        tool == SENTINEL_SCAN_BLE ? SENTINEL_VIEW_BLE :
        tool == SENTINEL_SCAN_ENERGY ? SENTINEL_VIEW_ENERGY : SENTINEL_VIEW_RADIO_LIST;
    live_view.state = record->state == SENTINEL_SCAN_RUNNING ? SENTINEL_VIEW_LIVE : SENTINEL_VIEW_IDLE;
    if (!c5_online) live_view.state = SENTINEL_VIEW_OFFLINE;
    else if (live_session.faulted || record->error != ESP_OK) live_view.state = SENTINEL_VIEW_ERROR;
    else if (live_session.paused && live_session.desired == tool) live_view.state = SENTINEL_VIEW_PAUSED;
    else if (live_session.completed && live_session.desired == tool) live_view.state = SENTINEL_VIEW_COMPLETE;
    live_view.total_count = record->count;
    if (tool == SENTINEL_SCAN_WIFI) live_view.count_label = "NETWORKS STORED";
    if (tool == SENTINEL_SCAN_BLE) live_view.count_label = "DEVICES STORED";
    if (tool == SENTINEL_SCAN_BEACON) live_view.count_label = "BEACONS OBSERVED";
    if (tool == SENTINEL_SCAN_ACTIVITY) live_view.count_label = "CHANNELS OBSERVED";
    live_view.page = live_page[tool];
    live_view.page_count = (record->count + 3) / 4;
    live_view.has_previous = live_view.page > 0;
    live_view.has_next = live_view.page + 1 < live_view.page_count;
    if (tool >= SENTINEL_SCAN_ENERGY && tool <= SENTINEL_SCAN_ACTIVITY && record->valid) {
        live_view.progress_known = true;
        live_view.progress_percent = record->completed * 100 / 16;
    }
    if (!c5_online) snprintf(live_subtitle, sizeof(live_subtitle), "RADIO OFFLINE / RETRYING THE LINK");
    else if (live_session.active != SENTINEL_SCAN_NONE &&
             (live_session.active != tool || live_session.paused))
        snprintf(live_subtitle, sizeof(live_subtitle), "FINISHING CURRENT SWEEP / PLEASE WAIT");
    else if (live_session.faulted) snprintf(live_subtitle, sizeof(live_subtitle), "SCAN NEEDS ATTENTION / TAP SCAN");
    else if (live_radio_busy) snprintf(live_subtitle, sizeof(live_subtitle), "WAITING FOR ACTIVE RADIO SESSION");
    else if (record->error != ESP_OK) snprintf(live_subtitle, sizeof(live_subtitle), "RADIO ERROR / %.30s", esp_err_to_name(record->error));
    else if (live_session.paused) snprintf(live_subtitle, sizeof(live_subtitle), "STOPPED / TAP SCAN FOR A NEW SWEEP");
    else if (live_view.state == SENTINEL_VIEW_COMPLETE) snprintf(live_subtitle, sizeof(live_subtitle), "SWEEP COMPLETE / TAP AGAIN TO RESCAN");
    else snprintf(live_subtitle, sizeof(live_subtitle), "%s / %s",
        tool == SENTINEL_SCAN_WIFI ? "2.4 + 5 GHZ" : tool == SENTINEL_SCAN_BLE ? "BLE ADVERTISEMENTS" :
        tool == SENTINEL_SCAN_BEACON ? "ZIGBEE BEACONS" : tool == SENTINEL_SCAN_ACTIVITY ? "FRAME COUNTS" : "ENERGY CH 11-26",
        record->state == SENTINEL_SCAN_RUNNING ? "LISTENING" : "PREPARING ONE SWEEP");
    if (record->captured) snprintf(live_hint, sizeof(live_hint), "LATEST SWEEP %lus AGO / %s",
        (unsigned long)((uint32_t)(esp_timer_get_time() / 1000 - record->captured_ms) / 1000),
        tool == SENTINEL_SCAN_WIFI || tool == SENTINEL_SCAN_BLE ? "TAP TO EXPLORE" : "PASSIVE OBSERVATION");
    else snprintf(live_hint, sizeof(live_hint), "FIRST RESULTS APPEAR AFTER A FULL SWEEP");
    live_view.subtitle = live_subtitle;
    live_view.hint = live_hint;
    live_view.message = live_view.state == SENTINEL_VIEW_LIVE ?
        (record->captured ? "LAST SWEEP WAS EMPTY" : "LISTENING") :
        live_view.state == SENTINEL_VIEW_COMPLETE ? "NO SIGNALS FOUND" : NULL;
    if (tool == SENTINEL_SCAN_ENERGY) {
        if (record->captured) {
            memcpy(live_view.energy_dbm, live_energy, sizeof(live_energy));
            live_view.energy_valid_mask = 0xffff;
        }
        return;
    }
    for (uint16_t row = 0; row < 4; ++row) {
        uint16_t index = live_page[tool] * 4 + row;
        if (index >= record->count) break;
        sentinel_scan_row_t *out = &live_view.rows[row];
        live_names[row][0] = live_details[row][0] = live_badges[row][0] = '\0';
        if (tool == SENTINEL_SCAN_WIFI) {
            const cerberus_c5_wifi_result_t *result = &live_wifi[index];
            cerberus_wifi_trust_state_t trust = cerberus_settings_wifi_trust_state(result->ssid, result->bssid, result->security);
            snprintf(live_names[row], sizeof(live_names[row]), "%.32s", result->ssid[0] ? result->ssid : "Hidden network");
            snprintf(live_details[row], sizeof(live_details[row]), "CH %u / %s / %s", result->channel,
                result->channel > 14 ? "5 GHz" : "2.4 GHz", wifi_security_text(result->security));
            snprintf(live_badges[row], sizeof(live_badges[row]), "%s", trust == CERBERUS_WIFI_TRUST_EXACT ? "TRUSTED" :
                trust == CERBERUS_WIFI_TRUST_MISMATCH ? "REVIEW" : wifi_security_text(result->security));
            out->rssi = result->rssi; out->signal_valid = true;
            out->marked = trust == CERBERUS_WIFI_TRUST_EXACT; out->review = trust == CERBERUS_WIFI_TRUST_MISMATCH;
        } else if (tool == SENTINEL_SCAN_BLE) {
            const cerberus_c5_ble_result_t *result = &live_ble[index];
            snprintf(live_names[row], sizeof(live_names[row]), "%.32s", result->name[0] ? result->name : "Unnamed device");
            char identifier[24];
            cerberus_privacy_format_ble_identifier(result->address, privacy_addresses_masked(), identifier, sizeof(identifier));
            snprintf(live_details[row], sizeof(live_details[row]), "%s", identifier);
            out->marked = cerberus_settings_ble_is_favorite(result->address, result->address_type);
            snprintf(live_badges[row], sizeof(live_badges[row]), "%s", out->marked ? "FAVORITE" :
                (result->flags & CERBERUS_BLE_RESULT_CONNECTABLE) ? "CONNECTABLE" : "BROADCAST");
            out->rssi = result->rssi; out->signal_valid = true;
        } else if (tool == SENTINEL_SCAN_BEACON) {
            const cerberus_ieee802154_beacon_scan_result_payload_t *result = &live_beacons[index];
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
    live_make_view();
    return sentinel_scan_view_draw(&live_view, live_frame);
}

static void live_scan_tick(uint32_t now, bool asleep)
{
    sentinel_scan_tool_t desired = live_page_tool();
    /* Sleeping may cancel an active sweep, but must not rearm a completed or
     * explicitly stopped page when the user wakes the same screen. */
    if (asleep) desired = live_session.completed || live_session.paused ?
        live_session.desired : SENTINEL_SCAN_NONE;
    bool changed = live_session.desired != desired;
    /* Derived pages reuse a real completed inventory. Apply this only on entry:
     * subsequent ticks must preserve explicit Scan Again and Stop requests. */
    bool cached_wifi_view = !asleep && desired == SENTINEL_SCAN_WIFI &&
        current_page == UI_PAGE_WIFI && live_records[SENTINEL_SCAN_WIFI].captured &&
        (wifi_mode == UI_WIFI_MODE_STATS || wifi_mode == UI_WIFI_MODE_CHANNELS_24 ||
         wifi_mode == UI_WIFI_MODE_CHANNELS_5);
    if (changed && cached_wifi_view) sentinel_scan_session_select_snapshot(&live_session, desired);
    else sentinel_scan_session_select(&live_session, desired);
    /* Inspection drains Wi-Fi instead of cancelling: its C5 cancellation
     * deliberately discards the inventory needed for an explicit connection. */
    bool inspecting_wifi = current_page == UI_PAGE_WIFI && (wifi_mode == UI_WIFI_MODE_DETAIL ||
        wifi_mode == UI_WIFI_MODE_PASSWORD || wifi_mode == UI_WIFI_MODE_CONNECTION);
    sentinel_scan_session_set_stop_supported(&live_session, SENTINEL_SCAN_WIFI, !inspecting_wifi);
    uint32_t poll_ms = live_session.completed && live_session.active == SENTINEL_SCAN_NONE ? 1000U : 500U;
    if (!changed && (uint32_t)(now - live_last_poll) < poll_ms) return;
    if (desired == SENTINEL_SCAN_NONE && live_session.active == SENTINEL_SCAN_NONE) return;
    live_last_poll = now;
    if (c5_online) {
        changed = live_observe(now) || changed;
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
            if (err == ESP_OK && command.kind == SENTINEL_SCAN_COMMAND_START)
                live_records[command.tool].state = SENTINEL_SCAN_RUNNING;
            changed = true;
        }
    }
    if (!asleep && !ui_capability_blocked && live_is_view()) {
        ++live_frame;
        live_make_view();
        ESP_ERROR_CHECK(sentinel_scan_view_update(&live_view, live_frame));
    } else if (!asleep && (desired != SENTINEL_SCAN_NONE || inspecting_wifi ||
        (current_page == UI_PAGE_IEEE802154 && ieee802154_mode == UI_IEEE802154_MODE_ASSESSMENT)) && changed) {
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void live_reset_observations(void)
{
    memset(live_records, 0, sizeof(live_records));
    memset(live_page, 0, sizeof(live_page));
    live_last_poll = 0;
}

static bool live_scan_paused(void) { return live_session.paused || live_session.faulted; }

static const char *live_scan_action_label(void)
{
    if (live_session.completed) return "SCAN AGAIN";
    return live_scan_paused() || live_records[live_page_tool()].error != ESP_OK ? "SCAN" : "STOP";
}

static void live_toggle_pause(void)
{
    sentinel_scan_session_select(&live_session, live_page_tool());
    sentinel_scan_session_pause(&live_session, !(live_scan_paused() || live_session.completed ||
        live_records[live_page_tool()].error != ESP_OK));
    live_last_poll = 0;
}

static bool live_connection_ready(void)
{
    if (!c5_online) return false;
    (void)live_observe((uint32_t)(esp_timer_get_time() / 1000));
    if (!live_scan_observation_valid || live_radio_busy) return false;
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool)
        if (!live_records[tool].valid || live_records[tool].state == SENTINEL_SCAN_RUNNING) return false;
    return true;
}

static void handle_live_scan_tap(uint16_t x, uint16_t y)
{
    /* Hit-test the last painted snapshot, never a newly fetched row ordering. */
    sentinel_scan_tool_t tool = live_page_tool();
    sentinel_view_action_t action = sentinel_scan_view_hit_test(&live_view, x, y);
    if (action == SENTINEL_VIEW_PREV && live_page[tool]) --live_page[tool];
    else if (action == SENTINEL_VIEW_NEXT) ++live_page[tool];
    else if (action == SENTINEL_VIEW_PAUSE) {
        sentinel_scan_session_select(&live_session, tool);
        sentinel_scan_session_pause(&live_session, live_view.state == SENTINEL_VIEW_LIVE);
        live_last_poll = 0;
    } else if (action == SENTINEL_VIEW_TOOLS) {
        if (current_page == UI_PAGE_WIFI) wifi_mode = UI_WIFI_MODE_MENU;
        else if (current_page == UI_PAGE_BLE) ble_mode = UI_BLE_MODE_MENU;
        else ieee802154_mode = UI_IEEE802154_MODE_MENU;
    } else if (action >= SENTINEL_VIEW_ROW0 && action <= SENTINEL_VIEW_ROW3) {
        uint16_t index = live_page[tool] * 4 + action - SENTINEL_VIEW_ROW0;
        if (index >= live_records[tool].count) return;
        if (tool == SENTINEL_SCAN_WIFI) {
            wifi_selected_result = live_wifi[index];
            wifi_selected_index = index;
            wifi_selection_valid = true;
            wifi_analysis_generation = 0;
            wifi_analysis_error = ESP_ERR_INVALID_STATE;
            wifi_trust_error = ESP_OK;
            wifi_mode = UI_WIFI_MODE_DETAIL;
        } else if (tool == SENTINEL_SCAN_BLE) {
            ble_selected_result = live_ble[index];
            ble_selection_valid = true;
            ble_mode = UI_BLE_MODE_DETAIL;
            ble_connection_checked = ble_service_checked = ble_characteristic_checked = ble_read_checked = false;
            set_ble_action_message("SCAN STOPS WHILE YOU EXPLORE", UI_MUTED);
        } else return;
    } else return;
    if (live_is_view()) {
        live_make_view();
        ESP_ERROR_CHECK(sentinel_scan_view_update(&live_view, live_frame));
    } else {
        ESP_ERROR_CHECK(draw_detail_page());
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
            candidate.security == wifi_selected_result.security && strcmp(candidate.ssid, wifi_selected_result.ssid) == 0)
            return c5_spi_link_connect_wifi(status.generation, i, password, &wifi_connection_status);
    }
    return ESP_ERR_NOT_FOUND;
}
