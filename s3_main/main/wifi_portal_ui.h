/* Included by ui.c after the selected-AP state and display helpers. UI task
 * only. The S3 service owns HTTP/DNS/AP; the C5 supplies a frozen AP proof and
 * one bounded passive scan of the original radio after Start. */
static cerberus_c5_wifi_result_t portal_target;
static char portal_ssid_label[33];
static uint8_t portal_selected_index, portal_generation, portal_local_channel;
static bool portal_target_valid, portal_c5_scan_pending, portal_c5_scan_done;
static cerberus_wifi_scan_status_payload_t portal_c5_scan_status;
static esp_err_t portal_error, portal_c5_error;
static uint32_t portal_last_poll;
static char portal_message[65], portal_report_name[32];

static void portal_enter(void)
{
    portal_target_valid = wifi_selection_valid;
    if (portal_target_valid) {
        portal_target = wifi_selected_result;
        snprintf(portal_ssid_label, sizeof(portal_ssid_label), "%s",
            portal_target.ssid[0] ? portal_target.ssid : "<HIDDEN SSID>");
        for (char *cursor = portal_ssid_label; *cursor; ++cursor)
            if ((uint8_t)*cursor < 32U || (uint8_t)*cursor > 126U) *cursor = '?';
        portal_selected_index = wifi_selected_index;
        portal_generation = wifi_scan_status.generation;
        portal_local_channel = portal_target.channel <= 11U ? portal_target.channel : 6U;
    } else {
        memset(&portal_target, 0, sizeof(portal_target));
        snprintf(portal_ssid_label, sizeof(portal_ssid_label), "NO AP SELECTED");
    }
    portal_error = portal_c5_error = ESP_OK;
    portal_c5_scan_pending = portal_c5_scan_done = false;
    portal_report_name[0] = portal_message[0] = '\0';
    portal_last_poll = 0U;
}

static bool portal_preflight(cerberus_c5_wifi_result_t *fresh, uint8_t *index,
    uint8_t *generation)
{
    if (!portal_target_valid || !c5_online ||
        !(c5_status.capabilities & (CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ))) {
        snprintf(portal_message, sizeof(portal_message), "SELECT AP / CONNECT C5 FIRST");
        return false;
    }
    if (!(c5_status.capabilities & (portal_target.channel > 14U ?
        CERBERUS_C5_CAP_WIFI_5_GHZ : CERBERUS_C5_CAP_WIFI_2_4_GHZ))) {
        snprintf(portal_message, sizeof(portal_message), "C5 SOURCE BAND NOT AVAILABLE");
        return false;
    }
    if (!portal_target.ssid[0]) {
        snprintf(portal_message, sizeof(portal_message), "HIDDEN SSID NEEDS A KNOWN NAME");
        return false;
    }
    cerberus_wifi_connection_status_payload_t connection;
    portal_error = c5_spi_link_get_wifi_connection_status(&connection);
    if (portal_error != ESP_OK) return false;
    if (connection.state != CERBERUS_WIFI_CONNECTION_IDLE &&
        connection.state != CERBERUS_WIFI_CONNECTION_ERROR) {
        snprintf(portal_message, sizeof(portal_message), "DISCONNECT C5 WI-FI BEFORE PORTAL");
        return false;
    }
    cerberus_radio_test_status_t job;
    portal_error = c5_spi_link_test_status(0, &job);
    if (portal_error != ESP_OK) return false;
    if (job.state == CERBERUS_TEST_RUNNING || job.state == CERBERUS_TEST_STOPPING) {
        snprintf(portal_message, sizeof(portal_message), "C5 RADIO JOB IS BUSY");
        return false;
    }
    cerberus_wifi_scan_status_payload_t scan;
    portal_error = c5_spi_link_get_wifi_scan_status(&scan);
    if (portal_error != ESP_OK) return false;
    if (scan.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        scan.generation != portal_generation || !scan.generation) {
        snprintf(portal_message, sizeof(portal_message), "SCAN CHANGED / RESELECT AP");
        return false;
    }
    unsigned limit = scan.stored_count < CERBERUS_WIFI_SCAN_MAX_RESULTS ?
        scan.stored_count : CERBERUS_WIFI_SCAN_MAX_RESULTS;
    bool found = false;
    unsigned candidate_index = portal_selected_index;
    if (candidate_index < limit) {
        portal_error = c5_spi_link_get_wifi_scan_result(scan.generation,
            (uint8_t)candidate_index, fresh);
        if (portal_error != ESP_OK) return false;
        found = !memcmp(fresh->bssid, portal_target.bssid, 6U) &&
            fresh->channel == portal_target.channel;
    }
    if (!found) for (candidate_index = 0U; candidate_index < limit; ++candidate_index) {
        if (candidate_index == portal_selected_index) continue;
        portal_error = c5_spi_link_get_wifi_scan_result(scan.generation,
            (uint8_t)candidate_index, fresh);
        if (portal_error != ESP_OK) return false;
        if (!memcmp(fresh->bssid, portal_target.bssid, 6U) &&
            fresh->channel == portal_target.channel) { found = true; break; }
    }
    if (!found || strcmp(fresh->ssid, portal_target.ssid) ||
        fresh->security != portal_target.security) {
        snprintf(portal_message, sizeof(portal_message), "AP IDENTITY CHANGED / RESELECT");
        return false;
    }
    *index = (uint8_t)candidate_index;
    *generation = scan.generation;
    return true;
}

static void portal_start(void)
{
    portal_error = portal_c5_error = ESP_OK;
    portal_message[0] = portal_report_name[0] = '\0';
    cerberus_s3_portal_status_t state;
    cerberus_s3_portal_get_status(&state);
    if (state.running || state.resources_held || state.report_pending) {
        snprintf(portal_message, sizeof(portal_message), "STOP / SAVE OR DISCARD PRIOR SESSION");
        return;
    }
    cerberus_c5_wifi_result_t fresh = {0};
    uint8_t index = 0U, generation = 0U;
    if (!portal_preflight(&fresh, &index, &generation)) return;
    cerberus_web_ui_status_t web;
    cerberus_web_ui_get_status(&web);
    if (web.running || web.radio_active) {
        portal_error = cerberus_web_ui_stop();
        if (portal_error != ESP_OK) {
            snprintf(portal_message, sizeof(portal_message), "LOCAL WEB STOP FAILED / RETRY");
            return;
        }
    }
    if (cerberus_s3_wifi_owner() != CERBERUS_S3_WIFI_NONE) {
        snprintf(portal_message, sizeof(portal_message), "S3 WI-FI IS IN USE / STOP RECEIVER");
        return;
    }
    portal_selected_index = index;
    cerberus_s3_portal_request_t request = {
        .source_ssid_length = (uint8_t)strnlen(fresh.ssid, 32U),
        .source_channel = fresh.channel,
        .local_channel = portal_local_channel,
        .c5_scan_generation = generation,
        .c5_preflight_passed = true,
        .duration_seconds = 120U,
    };
    memcpy(request.source_bssid, fresh.bssid, 6U);
    memcpy(request.source_ssid, fresh.ssid, request.source_ssid_length);
    portal_error = cerberus_s3_portal_start(&request);
    if (portal_error != ESP_OK) {
        snprintf(portal_message, sizeof(portal_message), "S3 PORTAL START FAILED");
        return;
    }
    /* C5 scan uses WIFI_SCAN_TYPE_PASSIVE in wifi_scanner.c. It is independent
     * of the S3 AP and never transmits the selected AP attack modes. */
    portal_c5_error = c5_spi_link_start_wifi_scan(&portal_c5_scan_status);
    portal_c5_scan_pending = portal_c5_error == ESP_OK;
    portal_c5_scan_done = portal_c5_error != ESP_OK;
    snprintf(portal_message, sizeof(portal_message), "%s", portal_c5_scan_pending ?
        "S3 PORTAL ACTIVE / C5 PASSIVE CHECK" : "S3 PORTAL ACTIVE / C5 CHECK UNAVAILABLE");
}

static void portal_stop_c5_scan(void)
{
    if (!portal_c5_scan_pending) return;
    portal_c5_error = c5_spi_link_get_wifi_scan_status(&portal_c5_scan_status);
    if (portal_c5_error == ESP_OK &&
        portal_c5_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING)
        portal_c5_error = c5_spi_link_stop_wifi_scan(&portal_c5_scan_status);
    portal_c5_scan_pending = false;
    portal_c5_scan_done = true;
}

static void portal_tick(uint32_t now)
{
    if (now - portal_last_poll < 1000U) return;
    portal_last_poll = now;
    if (!portal_c5_scan_pending) return;
    cerberus_s3_portal_status_t state;
    cerberus_s3_portal_get_status(&state);
    if (!state.running) {
        portal_stop_c5_scan();
        return;
    }
    portal_c5_error = c5_spi_link_get_wifi_scan_status(&portal_c5_scan_status);
    if (portal_c5_error != ESP_OK || portal_c5_scan_status.state == CERBERUS_WIFI_SCAN_ERROR) {
        if (portal_c5_error == ESP_OK) portal_c5_error = portal_c5_scan_status.error_code;
        portal_c5_scan_pending = false;
        portal_c5_scan_done = true;
        return;
    }
    if (portal_c5_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE) return;
    unsigned limit = portal_c5_scan_status.stored_count < CERBERUS_WIFI_SCAN_MAX_RESULTS ?
        portal_c5_scan_status.stored_count : CERBERUS_WIFI_SCAN_MAX_RESULTS;
    for (unsigned index = 0U; index < limit; ++index) {
        cerberus_c5_wifi_result_t observed;
        portal_c5_error = c5_spi_link_get_wifi_scan_result(portal_c5_scan_status.generation,
            (uint8_t)index, &observed);
        if (portal_c5_error != ESP_OK) break;
        if (!memcmp(observed.bssid, portal_target.bssid, 6U) &&
            observed.channel == portal_target.channel &&
            !strcmp(observed.ssid, portal_target.ssid)) {
            portal_c5_error = cerberus_s3_portal_note_c5_observation(portal_generation,
                portal_target.bssid, 1U);
            break;
        }
    }
    portal_c5_scan_pending = false;
    portal_c5_scan_done = true;
}

static esp_err_t portal_draw(void)
{
    cerberus_s3_portal_status_t state;
    cerberus_s3_portal_get_status(&state);
    char line[80], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(64, "EVIL PORTAL / AP CLONE", UI_TEXT, 2), TAG, "portal title");
    ESP_RETURN_ON_ERROR(draw_centered(92, portal_ssid_label, UI_TEXT, 1), TAG, "portal SSID");
    cerberus_privacy_format_wifi_identifier(portal_target.bssid,
        privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "SOURCE %s / CH %u", identifier, portal_target.channel);
    ESP_RETURN_ON_ERROR(draw_centered(110, line, UI_MUTED, 1), TAG, "portal source");
    snprintf(line, sizeof(line), "S3 LOCAL AP / 2.4 GHZ CH %u", portal_local_channel);
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,140,296,36},
        line, 1, !state.running && !state.report_pending && !state.resources_held, UI_WIDGET_THEME),
        TAG, "portal local channel");
    if (portal_target.channel > 11U)
        ESP_RETURN_ON_ERROR(draw_centered(184, "5 GHZ SOURCE / S3 LOCAL AP IS 2.4 GHZ", UI_MUTED, 1), TAG, "portal band");
    else ESP_RETURN_ON_ERROR(draw_centered(184, "LOCAL BSSID DIFFERS FROM SOURCE AP", UI_MUTED, 1), TAG, "portal identity note");
    const char *phase = state.running ? "RUNNING" : state.resources_held ? "STOP ERROR / RETRY" :
        state.report_pending ? "STOPPED / REPORT IN RAM" : "READY / 2 MINUTE SESSION LIMIT";
    snprintf(line, sizeof(line), "%s / %lu S", phase, (unsigned long)(state.elapsed_ms / 1000U));
    ESP_RETURN_ON_ERROR(draw_centered(214, line, state.resources_held ? UI_BAD : UI_TEXT, 1), TAG, "portal state");
    if (state.running || state.report_pending) {
        cerberus_privacy_format_wifi_identifier(state.local_bssid,
            privacy_addresses_masked(), identifier, sizeof(identifier));
        snprintf(line, sizeof(line), "LOCAL %s / %s", identifier, "172.0.0.1");
        ESP_RETURN_ON_ERROR(draw_centered(236, line, UI_GOOD, 1), TAG, "portal address");
    }
    snprintf(line, sizeof(line), "CLIENTS %lu / DNS %lu / HTTP %lu",
        (unsigned long)state.client_joins, (unsigned long)state.dns_queries,
        (unsigned long)state.http_requests);
    ESP_RETURN_ON_ERROR(draw_centered(258, line, UI_MUTED, 1), TAG, "portal counters");
    snprintf(line, sizeof(line), "FORMS %lu / C5 ORIGINAL AP %s",
        (unsigned long)state.form_submissions,
        state.c5_observation_available ? "SEEN" : portal_c5_scan_pending ? "SCANNING" :
        portal_c5_scan_done ? "NOT SEEN" : "PENDING");
    ESP_RETURN_ON_ERROR(draw_centered(278, line, UI_MUTED, 1), TAG, "portal observation");
    const char *note = portal_report_name[0] ? portal_report_name :
        portal_message[0] ? portal_message : portal_error != ESP_OK ? esp_err_to_name(portal_error) :
        state.error_code != ESP_OK ? esp_err_to_name(state.error_code) :
        portal_c5_error != ESP_OK ? esp_err_to_name(portal_c5_error) :
        "START USES FRESH C5 AP PREFLIGHT";
    ESP_RETURN_ON_ERROR(draw_centered(310, note,
        portal_error != ESP_OK || state.error_code != ESP_OK ? UI_BAD : UI_MUTED, 1), TAG, "portal note");
    esp_err_t visible_error = portal_error != ESP_OK ? portal_error :
        state.error_code != ESP_OK ? state.error_code : portal_c5_error;
    if (visible_error != ESP_OK)
        ESP_RETURN_ON_ERROR(draw_centered(326, esp_err_to_name(visible_error), UI_BAD, 1), TAG, "portal error code");
    cerberus_web_ui_status_t web;
    cerberus_web_ui_get_status(&web);
    const char *start_label = state.running || state.resources_held ? "STOP PORTAL" :
        web.running || web.radio_active ? "STOP LOCAL WEB + START" : "START PORTAL";
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,340,296,36},
        start_label, 1, state.running || state.resources_held ||
        (portal_target_valid && !state.report_pending && c5_online), UI_WIDGET_THEME), TAG, "portal start/stop");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,382,144,32},
        "SAVE REPORT", 1, state.report_pending && !state.resources_held, UI_WIDGET_THEME), TAG, "portal save");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){164,382,144,32},
        "DISCARD", 1, state.report_pending && !state.resources_held, UI_WIDGET_THEME);
}

static void portal_tap(uint16_t x, uint16_t y)
{
    cerberus_s3_portal_status_t state;
    cerberus_s3_portal_get_status(&state);
    if (y >= 140U && y < 176U && x >= 12U && x < 308U &&
        !state.running && !state.report_pending && !state.resources_held) {
        portal_local_channel = portal_local_channel == 1U ? 6U :
            portal_local_channel == 6U ? 11U : 1U;
    } else if (y >= 340U && y < 376U && x >= 12U && x < 308U) {
        if (state.running || state.resources_held) {
            portal_stop_c5_scan();
            portal_error = cerberus_s3_portal_stop();
            snprintf(portal_message, sizeof(portal_message), portal_error == ESP_OK ?
                "STOPPED / SAVE OR DISCARD REPORT" : "STOP FAILED / RETRY");
        } else portal_start();
    } else if (y >= 382U && y < 414U && state.report_pending && !state.resources_held) {
        if (x >= 12U && x < 156U) {
            portal_error = cerberus_s3_portal_save(portal_report_name, sizeof(portal_report_name));
            if (portal_error != ESP_OK)
                snprintf(portal_message, sizeof(portal_message), "SAVE FAILED / CHECK SD OR USB");
        } else if (x >= 164U && x < 308U) {
            portal_error = cerberus_s3_portal_discard();
            if (portal_error == ESP_OK) snprintf(portal_message, sizeof(portal_message), "SESSION DISCARDED");
        }
    }
}

static bool portal_back(void)
{
    cerberus_s3_portal_status_t state;
    cerberus_s3_portal_get_status(&state);
    if (state.running || state.resources_held) {
        portal_stop_c5_scan();
        portal_error = cerberus_s3_portal_stop();
        snprintf(portal_message, sizeof(portal_message), portal_error == ESP_OK ?
            "STOPPED / SAVE OR DISCARD REPORT" : "STOP FAILED / RETRY");
        return false;
    }
    if (state.report_pending) {
        snprintf(portal_message, sizeof(portal_message), "SAVE OR DISCARD SESSION FIRST");
        return false;
    }
    return true;
}
