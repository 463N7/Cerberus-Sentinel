/* Included by ui.c before wifi_target_ui.h. The AP identity comes from the
 * selected scan card; selecting a row never starts capture automatically. */
static cerberus_c5_wifi_result_t capture_target;
static uint8_t capture_mode_index;
static uint32_t capture_last_poll;
static char capture_ssid[33], capture_message[65];
static cerberus_s3_capture_status_t capture_last_status;
static bool capture_has_last_status;

static const uint8_t capture_modes[] = {
    CERBERUS_WIFI_CAPTURE_MODE_RAW,
    CERBERUS_WIFI_CAPTURE_MODE_PROBE,
    CERBERUS_WIFI_CAPTURE_MODE_DEAUTH,
    CERBERUS_WIFI_CAPTURE_MODE_EAPOL,
    CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT,
};
static const char *const capture_mode_names[] = {
    "AP FRAMES", "CHANNEL PROBES", "DEAUTH / DISASSOC", "EAPOL FRAMES",
    "SAE COMMIT FRAMES",
};

static uint32_t capture_required_firmware(void)
{
    return capture_modes[capture_mode_index] == CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT ?
        CERBERUS_WIFI_CAPTURE_SAE_MIN_FIRMWARE : CERBERUS_WIFI_CAPTURE_MIN_FIRMWARE;
}

static void capture_enter(void)
{
    capture_target = wifi_selected_result;
    snprintf(capture_ssid, sizeof(capture_ssid), "%s",
        capture_target.ssid[0] ? capture_target.ssid : "<HIDDEN SSID>");
    for (char *p = capture_ssid; *p; ++p)
        if ((unsigned char)*p < 32U || (unsigned char)*p > 126U) *p = '?';
    capture_mode_index = 0U;
    capture_message[0] = '\0';
    capture_last_poll = 0U;
    capture_has_last_status = false;
}

static bool capture_busy(void)
{
    cerberus_s3_capture_status_t state;
    cerberus_s3_capture_status(&state);
    return state.state == CERBERUS_S3_CAPTURE_STARTING ||
        state.state == CERBERUS_S3_CAPTURE_RUNNING ||
        state.state == CERBERUS_S3_CAPTURE_STOPPING;
}

static void capture_start(void)
{
    capture_message[0] = '\0';
    if (!c5_online || c5_status.firmware_version < capture_required_firmware()) {
        snprintf(capture_message, sizeof(capture_message), "C5 CAPTURE REQUIRES REVISION %lu",
            (unsigned long)capture_required_firmware());
        return;
    }
    /* Wildcard probe requests have no selected AP BSSID. Probe mode therefore
     * captures the whole selected channel and says so on screen. */
    bool filter_bssid = capture_modes[capture_mode_index] != CERBERUS_WIFI_CAPTURE_MODE_PROBE;
    esp_err_t error = cerberus_s3_capture_start(capture_target.channel,
        capture_modes[capture_mode_index], capture_target.bssid, filter_bssid);
    if (error != ESP_OK)
        snprintf(capture_message, sizeof(capture_message), "CAPTURE START: %s", esp_err_to_name(error));
}

static esp_err_t capture_draw(void)
{
    cerberus_s3_capture_status_t state;
    cerberus_s3_capture_status(&state);
    char line[80], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(64, "CAPTURE THIS AP", UI_TEXT, 2), TAG, "capture title");
    ESP_RETURN_ON_ERROR(draw_centered(94, capture_ssid, UI_TEXT, 1), TAG, "capture SSID");
    cerberus_privacy_format_wifi_identifier(capture_target.bssid,
        privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "%s / CHANNEL %u", identifier, capture_target.channel);
    ESP_RETURN_ON_ERROR(draw_centered(116, line, UI_MUTED, 1), TAG, "capture identity");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,150,296,40},
        capture_mode_names[capture_mode_index], 1, !capture_busy(), UI_WIDGET_THEME), TAG, "capture mode");
    const char *phase = state.state == CERBERUS_S3_CAPTURE_STARTING ? "STARTING" :
        state.state == CERBERUS_S3_CAPTURE_RUNNING ? "CAPTURING" :
        state.state == CERBERUS_S3_CAPTURE_STOPPING ? "STOPPING / SAVING" :
        state.state == CERBERUS_S3_CAPTURE_SAVED ? "SAVED" :
        state.state == CERBERUS_S3_CAPTURE_ERROR ? "STOPPED WITH ERROR" : "READY";
    snprintf(line, sizeof(line), "%s / %lu S", phase, (unsigned long)(state.elapsed_ms / 1000U));
    ESP_RETURN_ON_ERROR(draw_centered(213, line,
        state.state == CERBERUS_S3_CAPTURE_ERROR ? UI_BAD : UI_GOOD, 1), TAG, "capture phase");
    snprintf(line, sizeof(line), "C5 HEARD %lu / SD WRITTEN %lu",
        (unsigned long)state.received, (unsigned long)state.written);
    ESP_RETURN_ON_ERROR(draw_centered(238, line, UI_TEXT, 1), TAG, "capture counts");
    snprintf(line, sizeof(line), "C5 DROPPED %lu + %lu / SD %llu KB",
        (unsigned long)state.dropped_full, (unsigned long)state.dropped_other,
        (unsigned long long)(state.bytes_written / 1024U));
    ESP_RETURN_ON_ERROR(draw_centered(260, line, UI_MUTED, 1), TAG, "capture drops");
    ESP_RETURN_ON_ERROR(draw_centered(282,
        capture_modes[capture_mode_index] == CERBERUS_WIFI_CAPTURE_MODE_PROBE ?
            "PROBES FROM ALL APS ON THIS CHANNEL" : "FILTERED TO THIS AP BSSID",
        UI_MUTED, 1), TAG, "capture filter scope");
    if (state.error_code != ESP_OK) snprintf(line, sizeof(line), "ERROR %s", esp_err_to_name(state.error_code));
    else if (capture_message[0]) snprintf(line, sizeof(line), "%s", capture_message);
    else if (state.file_name[0]) snprintf(line, sizeof(line), "FILE %s", state.file_name);
    else snprintf(line, sizeof(line), "C5 RADIO + S3 SD / NO GPS REQUIRED");
    ESP_RETURN_ON_ERROR(draw_centered(302, line,
        state.error_code != ESP_OK ? UI_BAD : UI_MUTED, 1), TAG, "capture note");
    if (state.pending)
        snprintf(line, sizeof(line), "C5 HAS %u PENDING FRAMES / RECOVER BEFORE NEXT", state.pending);
    else snprintf(line, sizeof(line), "PCAPNG / NO UTC OR FRAME TIMES");
    ESP_RETURN_ON_ERROR(draw_centered(326, line,
        state.pending ? UI_BAD : UI_MUTED, 1), TAG, "capture file limit");
    ESP_RETURN_ON_ERROR(draw_centered(346, "64 MB FILE LIMIT / MANUAL STOP",
        UI_MUTED, 1), TAG, "capture stop note");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12,366,296,46},
        capture_busy() ? "STOP + SAVE" : "START CAPTURE", 1,
        capture_busy() || (c5_online && c5_status.firmware_version >= capture_required_firmware()),
        UI_WIDGET_THEME);
}

static void capture_tap(uint16_t x, uint16_t y)
{
    if (x < 12U || x >= 308U) return;
    if (y >= 150U && y < 190U && !capture_busy()) {
        capture_mode_index = (uint8_t)((capture_mode_index + 1U) %
            (sizeof(capture_modes) / sizeof(capture_modes[0])));
    } else if (y >= 366U && y < 412U) {
        if (capture_busy()) cerberus_s3_capture_stop();
        else capture_start();
    }
}

static bool capture_back(void)
{
    if (capture_busy()) {
        cerberus_s3_capture_stop();
        snprintf(capture_message, sizeof(capture_message), "STOPPING / WAIT FOR FILE CLOSE");
        return false;
    }
    return true;
}

static bool capture_tick(uint32_t now)
{
    if ((uint32_t)(now - capture_last_poll) < 500U) return false;
    capture_last_poll = now;
    cerberus_s3_capture_status_t state;
    cerberus_s3_capture_status(&state);
    if (capture_has_last_status &&
        (capture_last_status.state == CERBERUS_S3_CAPTURE_STARTING ||
         capture_last_status.state == CERBERUS_S3_CAPTURE_RUNNING ||
         capture_last_status.state == CERBERUS_S3_CAPTURE_STOPPING) &&
        state.state != CERBERUS_S3_CAPTURE_STARTING &&
        state.state != CERBERUS_S3_CAPTURE_RUNNING &&
        state.state != CERBERUS_S3_CAPTURE_STOPPING)
        capture_message[0] = '\0';
    bool changed = !capture_has_last_status ||
        state.state != capture_last_status.state ||
        state.received != capture_last_status.received ||
        state.written != capture_last_status.written ||
        state.dropped_full != capture_last_status.dropped_full ||
        state.dropped_other != capture_last_status.dropped_other ||
        state.elapsed_ms / 1000U != capture_last_status.elapsed_ms / 1000U ||
        state.error_code != capture_last_status.error_code ||
        strcmp(state.file_name, capture_last_status.file_name) != 0;
    capture_last_status = state;
    capture_has_last_status = true;
    return changed;
}
