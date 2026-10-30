/* Included by ui.c after selected Find My helpers. The UI owns this session;
 * each completed S3 sweep is copied before another scan can replace it. */
static uint8_t ble_findmy_row_address[UI_WIFI_VISIBLE_ROWS][6];
static uint8_t ble_findmy_row_type[UI_WIFI_VISIBLE_ROWS], ble_findmy_rows;
static bool ble_findmy_release_pending;
static bool ble_findmy_selection_is_monitor;

static esp_err_t draw_ble_findmy_monitor(void)
{
    char line[64];
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000LL);
    snprintf(line, sizeof(line), "FIND MY MONITOR / %s / %lu SWEEPS",
        ble_findmy_stopping ? "STOPPING" : (ble_findmy_running || ble_findmy_scan_owned) ? "RUNNING" : "STOPPED",
        (unsigned long)ble_findmy_sweeps);
    ESP_RETURN_ON_ERROR(draw_centered(57, line, UI_GOOD, 1), TAG, "Find My monitor header");
    ESP_RETURN_ON_ERROR(draw_centered(72, "ACTIVE SCAN / RSP=SCAN RESPONSE / HINTS ONLY",
        UI_MUTED, 1), TAG, "Find My monitor scope");
    ble_findmy_rows = 0U;
    if (ble_findmy_monitor) {
        if (ble_findmy_page >= ble_findmy_monitor->count) ble_findmy_page = 0U;
        for (size_t row = 0U; row < UI_WIFI_VISIBLE_ROWS &&
            ble_findmy_page + row < ble_findmy_monitor->count; ++row) {
            const cerberus_findmy_monitor_record_t *record =
                &ble_findmy_monitor->records[ble_findmy_page + row];
            memcpy(ble_findmy_row_address[row], record->peer.address, 6U);
            ble_findmy_row_type[row] = record->peer.address_type;
            ++ble_findmy_rows;
            uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
            char identifier[24];
            cerberus_privacy_format_ble_identifier(record->peer.address,
                privacy_addresses_masked(), identifier, sizeof(identifier));
            bool fresh = cerberus_findmy_monitor_hint_age_ms(record, now) <= 30000U;
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304,
                UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL), TAG, "Find My monitor row");
            snprintf(line, sizeof(line), "%s PEAK %dDB / %s", identifier,
                record->peer.rssi, (record->peer.address_type & 1U) ? "RND" : "PUB");
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 3U, line,
                fresh ? UI_TEXT : UI_MUTED, UI_PANEL, 1), TAG, "Find My identity");
            cerberus_findmy_hint_line(&record->focus.discovery.findmy_advertisement, false,
                record->focus.discovery.advertisement_observed_ms, now, line, sizeof(line));
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 16U, line,
                UI_MUTED, UI_PANEL, 1), TAG, "Find My ADV hints");
            cerberus_findmy_hint_line(&record->focus.discovery.findmy_scan_response, true,
                record->focus.discovery.scan_response_observed_ms, now, line, sizeof(line));
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 29U, line,
                UI_MUTED, UI_PANEL, 1), TAG, "Find My response hints");
        }
    }
    if (!ble_findmy_rows)
        ESP_RETURN_ON_ERROR(draw_centered(180, "WAITING FOR FIND MY FORMAT HINTS",
            UI_MUTED, 1), TAG, "Find My monitor empty");
    if (ble_findmy_error != ESP_OK) {
        snprintf(line, sizeof(line), "S3 %s", esp_err_to_name(ble_findmy_error));
        ESP_RETURN_ON_ERROR(draw_centered(402, line, UI_BAD, 1), TAG, "Find My monitor error");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 84, 42, "PREV"), TAG, "Find My prev");
    ESP_RETURN_ON_ERROR(draw_small_button(98, UI_WIFI_CONTROLS_Y, 84, 42, "NEXT"), TAG, "Find My next");
    return draw_small_button(188, UI_WIFI_CONTROLS_Y, 124, 42,
        ble_findmy_stopping ? "STOPPING" : (ble_findmy_running || ble_findmy_scan_owned) ? "STOP" : "START");
}

static void ble_findmy_request_stop(bool exit, bool select)
{
    ble_findmy_running = false;
    ble_findmy_exit_pending |= exit;
    if (exit) ble_findmy_select_pending = false;
    else if (!ble_findmy_exit_pending) ble_findmy_select_pending |= select;
    if (!ble_findmy_stopping)
        ble_findmy_wait_since = (uint32_t)(esp_timer_get_time() / 1000LL);
    ble_findmy_stopping = true;
    ble_findmy_last_tick = 0U;
}

static void ble_findmy_monitor_open(void)
{
    if (!ble_findmy_monitor)
        ble_findmy_monitor = heap_caps_calloc(1U, sizeof(*ble_findmy_monitor),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    ble_findmy_error = ble_findmy_monitor ? ESP_OK : ESP_ERR_NO_MEM;
    if (ble_findmy_monitor) cerberus_findmy_monitor_init(ble_findmy_monitor);
    ble_findmy_page = 0U;
    ble_findmy_sweeps = 0U;
    ble_findmy_scan_owned = ble_findmy_stopping = false;
    ble_findmy_release_pending = false;
    ble_findmy_exit_pending = ble_findmy_select_pending = false;
    ble_findmy_running = ble_findmy_monitor != NULL;
    ble_findmy_wait_since = (uint32_t)(esp_timer_get_time() / 1000LL);
    ble_findmy_last_tick = ble_findmy_last_draw = 0U;
    ble_mode = UI_BLE_MODE_FINDMY_MONITOR;
}

static void ble_findmy_monitor_ingest(const cerberus_ble_scan_status_payload_t *status,
    uint32_t now)
{
    for (uint8_t i = 0U; i < status->stored_count; ++i) {
        cerberus_c5_ble_result_t peer;
        cerberus_c5_ble_advertisement_t raw;
        cerberus_s3_ble_focus_t focus;
        uint32_t observed;
        if (cerberus_s3_ble_scanner_get_result(status->generation, i, &peer) != ESP_OK ||
            cerberus_s3_ble_scanner_get_advertisement(status->generation, i, &raw) != ESP_OK ||
            cerberus_s3_ble_scanner_get_advertisement_time(status->generation, i, &observed) != ESP_OK ||
            cerberus_s3_ble_scanner_get_focus(status->generation, i, &focus) != ESP_OK) {
            ble_findmy_error = ESP_ERR_INVALID_STATE;
            ble_findmy_running = false;
            return;
        }
        (void)cerberus_findmy_monitor_ingest(ble_findmy_monitor, &peer, &raw,
            &focus, status->generation, i, observed, now);
    }
    ++ble_findmy_sweeps;
}

static void ble_findmy_finish_stop(bool asleep)
{
    ble_findmy_stopping = false;
    ble_findmy_scan_owned = false;
    ble_findmy_release_pending = false;
    if (ble_findmy_select_pending) {
        ble_findmy_select_pending = false;
        ble_selected_from_monitor = ble_findmy_selection_is_monitor;
        ble_selected_raw_frozen = true;
        ble_selected_result = ble_findmy_selected.peer;
        make_ble_name_safe(ble_selected_result.name);
        ble_selected_focus = ble_findmy_selected.focus;
        ble_selected_scan_index = ble_findmy_selected.index;
        ble_selected_sound_profile = selected_findmy_profile(
            ble_selected_scan_index, &ble_selected_result, &ble_selected_scan_ms);
        ble_selection_valid = true;
        ble_attack_capture_valid = false;
        ble_mode = UI_BLE_MODE_DETAIL;
        ble_connection_checked = ble_service_checked = false;
        ble_characteristic_checked = ble_read_checked = false;
        set_ble_action_message(ble_findmy_selection_is_monitor ?
            "MONITOR PAUSED / ORIGINAL ADV SELECTED" :
            "SCAN STOPPED / ORIGINAL ADV SELECTED", UI_MUTED);
        if (!ble_findmy_selection_is_monitor) {
            heap_caps_free(ble_findmy_monitor);
            ble_findmy_monitor = NULL;
            if (ble_attack_selecting) {
                ble_attack_selecting = false;
                (void)ble_attack_capture_selected_ad(ble_selected_scan_index);
                open_tool_page(UI_PAGE_BLE_ATTACK);
            }
        }
    } else if (ble_findmy_exit_pending) {
        ble_findmy_exit_pending = false;
        heap_caps_free(ble_findmy_monitor);
        ble_findmy_monitor = NULL;
        ble_mode = UI_BLE_MODE_LIST;
        ble_filter = CERBERUS_S3_BLE_FILTER_FIND_MY;
        ble_page_offset = 0U;
        ble_scan_checked = false;
        (void)refresh_ble_scan_status();
    }
    if (!asleep && current_page == UI_PAGE_BLE) ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_findmy_monitor_tick(uint32_t now, bool asleep)
{
    if (current_page != UI_PAGE_BLE || ble_mode != UI_BLE_MODE_FINDMY_MONITOR) return;
    if (!ble_findmy_monitor) {
        if (ble_findmy_stopping && !ble_findmy_scan_owned &&
            (!ble_findmy_release_pending || cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_SCANNER))
            ble_findmy_finish_stop(asleep);
        return;
    }
    if (ble_findmy_last_tick && (uint32_t)(now - ble_findmy_last_tick) < 250U) return;
    ble_findmy_last_tick = now;
    cerberus_findmy_monitor_expire(ble_findmy_monitor, now);
    cerberus_ble_scan_status_payload_t status = {0};
    if (ble_findmy_scan_owned) {
        esp_err_t error = cerberus_s3_ble_scanner_get_status(&status);
        if (error != ESP_OK) {
            ble_findmy_error = error;
            ble_findmy_request_stop(false, false);
        } else if (status.generation != ble_findmy_generation) {
            ble_findmy_scan_owned = false;
            ble_findmy_release_pending = false;
            ble_findmy_running = false;
            ble_findmy_error = ESP_ERR_INVALID_STATE;
        } else if (status.state == CERBERUS_BLE_SCAN_COMPLETE) {
            if (!ble_findmy_select_pending)
                ble_findmy_monitor_ingest(&status, now);
            ble_findmy_scan_owned = false;
        } else if (status.state != CERBERUS_BLE_SCAN_RUNNING) {
            ble_findmy_scan_owned = false;
            if (status.state == CERBERUS_BLE_SCAN_ERROR) {
                ble_findmy_error = status.error_code ? status.error_code : ESP_FAIL;
                ble_findmy_running = false;
            }
        } else if (ble_findmy_stopping) {
            error = cerberus_s3_ble_scanner_stop(&status);
            if (error != ESP_OK) ble_findmy_error = error;
        }
    }
    if (ble_findmy_stopping) {
        if (!ble_findmy_release_pending ||
            cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_SCANNER) {
            ble_findmy_finish_stop(asleep);
            return;
        }
        if ((uint32_t)(now - ble_findmy_wait_since) >= 12000U)
            ble_findmy_error = ESP_ERR_TIMEOUT;
    } else if (ble_findmy_running && !ble_findmy_scan_owned) {
        esp_err_t error = ESP_ERR_INVALID_STATE;
        if (cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_NONE)
            error = cerberus_s3_ble_scanner_start_active(&status);
        if (error == ESP_OK) {
            ble_findmy_generation = status.generation;
            ble_findmy_scan_owned = true;
            ble_findmy_release_pending = true;
            ble_findmy_wait_since = now;
        } else if (error != ESP_ERR_INVALID_STATE ||
            cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER ||
            (uint32_t)(now - ble_findmy_wait_since) >= 12000U) {
            ble_findmy_error = error;
            ble_findmy_running = false;
        }
    }
    if (!asleep && (!ble_findmy_last_draw ||
        (uint32_t)(now - ble_findmy_last_draw) >= UI_BLE_REFRESH_MS)) {
        ble_findmy_last_draw = now;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void ble_findmy_monitor_tap(uint16_t x, uint16_t y)
{
    if (ble_findmy_stopping || !ble_findmy_monitor) return;
    if (y >= UI_WIFI_LIST_Y && y < UI_WIFI_CONTROLS_Y) {
        size_t row = (y - UI_WIFI_LIST_Y) / UI_WIFI_ROW_HEIGHT;
        if (row < ble_findmy_rows) {
            for (uint8_t i = 0U; i < ble_findmy_monitor->count; ++i) {
                const cerberus_findmy_monitor_record_t *record = &ble_findmy_monitor->records[i];
                if (record->peer.address_type == ble_findmy_row_type[row] &&
                    memcmp(record->peer.address, ble_findmy_row_address[row], 6U) == 0 &&
                    cerberus_findmy_monitor_copy(ble_findmy_monitor, i, &ble_findmy_selected)) {
                    ble_selected_original_time64_valid = false;
                    ble_findmy_selection_is_monitor = true;
                    ble_findmy_request_stop(false, true);
                    break;
                }
            }
        }
    } else if (y >= UI_WIFI_CONTROLS_Y && y < UI_WIFI_CONTROLS_Y + 42U) {
        if (x < 92U) ble_findmy_page = ble_findmy_page >= UI_WIFI_VISIBLE_ROWS ?
            ble_findmy_page - UI_WIFI_VISIBLE_ROWS : 0U;
        else if (x >= 98U && x < 182U &&
            ble_findmy_page + UI_WIFI_VISIBLE_ROWS < ble_findmy_monitor->count)
            ble_findmy_page += UI_WIFI_VISIBLE_ROWS;
        else if (x >= 188U) {
            if (ble_findmy_running || ble_findmy_scan_owned) ble_findmy_request_stop(false, false);
            else {
                ble_findmy_error = ESP_OK;
                ble_findmy_running = true;
                ble_findmy_wait_since = (uint32_t)(esp_timer_get_time() / 1000LL);
                ble_findmy_last_tick = 0U;
            }
        }
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

/* The live list freezes evidence when it captures a completed sweep. Selection
 * uses that immutable row while its scheduler releases any newer S3 sweep. */
static void ble_select_frozen_live(const cerberus_findmy_monitor_record_t *record)
{
    ble_selected_original_time64_valid = false;
    ble_findmy_selected = *record;
    ble_findmy_selection_is_monitor = false;
    ble_findmy_scan_owned = false;
    ble_findmy_release_pending = cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_SCANNER;
    ble_findmy_exit_pending = ble_findmy_stopping = false;
    ble_findmy_select_pending = false;
    ble_mode = UI_BLE_MODE_FINDMY_MONITOR;
    ble_findmy_request_stop(false, true);
}
