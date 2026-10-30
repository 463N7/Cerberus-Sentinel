/* Private Recon controls on the existing UI task. No second radio owner. */
static esp_err_t recon_draw_options(void)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "recon options clear");
    ESP_RETURN_ON_ERROR(draw_header("RECON OPTIONS"), TAG, "recon options header");
    char confidence[40];
    snprintf(confidence, sizeof(confidence), "ALERT FILTER: %s", watch_state.minimum_confidence == SENTINEL_WATCH_CONFIDENCE_HIGH ?
        "HIGH ONLY" : watch_state.minimum_confidence == SENTINEL_WATCH_CONFIDENCE_MEDIUM ? "MEDIUM +" : "ALL");
    const char *labels[] = {confidence, watch_state.muted ? "ALERTS: MUTED" : "ALERTS: ON", "TYPE FILTER",
        recon_confirm == 4 ? "TAP AGAIN: RESET IGNORE / SNOOZE" : "RESET IGNORE / SNOOZE",
        recon_confirm == 5 ? "TAP AGAIN: CLEAR SESSION LOG" : "CLEAR SESSION LOG", "SCENE GALLERY"};
    for (unsigned i = 0; i < 6; ++i)
        ESP_RETURN_ON_ERROR(draw_button(8, 72 + i * 52, 304, 44, labels[i]), TAG, "recon option");
    ESP_RETURN_ON_ERROR(draw_centered(393, "SETTINGS AND LOG LAST UNTIL POWER OFF", UI_MUTED, 1), TAG, "recon lifetime");
    return draw_button(64, 426, 192, 44, "BACK TO RECON");
}
static esp_err_t recon_draw_actions(void)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "recon actions clear");
    ESP_RETURN_ON_ERROR(draw_header("DEVICE ACTIONS"), TAG, "recon actions header");
    ESP_RETURN_ON_ERROR(draw_centered(74, watch_detail.match.label, UI_GOOD, 1), TAG, "recon selected");
    if (watch_detail.radio == SENTINEL_WATCH_RADIO_ENVIRONMENT) {
        ESP_RETURN_ON_ERROR(draw_centered(170, "AREA OBSERVATION / NO DEVICE TARGET", UI_MUTED, 1), TAG, "recon area");
    } else {
        ESP_RETURN_ON_ERROR(draw_button(8, 106, 304, 44, "SIGNAL TRACKING"), TAG, "recon signal");
        ESP_RETURN_ON_ERROR(draw_button(8, 160, 304, 44,
            sentinel_watch_is_ignored(&watch_state, &watch_detail) ? "UN-IGNORE DEVICE" : "IGNORE DEVICE ALERTS"), TAG, "recon ignore");
        ESP_RETURN_ON_ERROR(draw_centered(218, "SNOOZE ALERTS FOR THIS ADDRESS", UI_MUTED, 1), TAG, "recon snooze");
        const char *labels[] = {"10 MIN", "30 MIN", "1 HOUR", "4 HOURS"};
        for (unsigned i = 0; i < 4; ++i)
            ESP_RETURN_ON_ERROR(draw_button(8 + (i % 2) * 156, 242 + (i / 2) * 54, 148, 44, labels[i]), TAG, "recon duration");
    }
    ESP_RETURN_ON_ERROR(draw_centered(358, recon_message, UI_GOOD, 1), TAG, "recon action status");
    ESP_RETURN_ON_ERROR(draw_centered(387, "OBSERVATIONS CONTINUE IN SESSION LOG", UI_MUTED, 1), TAG, "recon action effect");
    return draw_button(64, 426, 192, 44, "BACK TO EVIDENCE");
}
static esp_err_t recon_draw_signal(uint32_t now)
{
    bool found = false;
    for (unsigned i = 0; i < watch_state.history_count; ++i) {
        const sentinel_watch_entry_t *entry = sentinel_watch_history(&watch_state, i);
        if (entry && entry->radio == watch_detail.radio && entry->address_type == watch_detail.address_type &&
            entry->match.category == watch_detail.match.category && !memcmp(entry->address, watch_detail.address, 6)) {
            watch_detail = *entry; found = true; break;
        }
    }
    uint32_t age = (uint32_t)(now - watch_detail.last_ms) / 1000U;
    bool fresh = found && age <= 30 && !watch_paused && c5_online;
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "recon signal clear");
    ESP_RETURN_ON_ERROR(draw_header("SIGNAL TRACKING"), TAG, "recon signal header");
    ESP_RETURN_ON_ERROR(draw_centered(80, watch_detail.match.label, UI_TEXT, 1), TAG, "recon signal device");
    ESP_RETURN_ON_ERROR(draw_centered(111, "EXACT BROADCAST ADDRESS / PASSIVE", UI_MUTED, 1), TAG, "recon signal identity");
    char line[48]; snprintf(line, sizeof(line), "%d dBm", watch_detail.rssi);
    ESP_RETURN_ON_ERROR(draw_centered(166, line, fresh ? UI_GOOD : UI_MUTED, 3), TAG, "recon rssi");
    int level = watch_detail.rssi + 100; if (level < 0) level = 0; if (level > 70) level = 70;
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(20, 218, 280, 26, UI_PANEL), TAG, "recon gauge background");
    if (level && fresh) ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(20, 218, level * 4, 26, UI_GOOD), TAG, "recon gauge");
    snprintf(line, sizeof(line), "%s / LAST SEEN %lus AGO", fresh ? "RECENT SAMPLE" : "STALE SAMPLE", (unsigned long)age);
    ESP_RETURN_ON_ERROR(draw_centered(270, line, fresh ? UI_GOOD : UI_MUTED, 1), TAG, "recon signal age");
    ESP_RETURN_ON_ERROR(draw_centered(307, "UPDATED AFTER EACH COMPLETED SWEEP", UI_MUTED, 1), TAG, "recon signal rate");
    ESP_RETURN_ON_ERROR(draw_centered(337, "SIGNAL STRENGTH IS NOT DISTANCE", UI_MUTED, 1), TAG, "recon signal scope");
    return draw_button(64, 426, 192, 44, "STOP TRACKING");
}
static bool recon_handle_control_tap(uint16_t x, uint16_t y, uint32_t now)
{
    if (x >= 64 && x < 256 && y >= 426 && y < 470) {
        watch_page = watch_page == WATCH_OPTIONS ? WATCH_LIVE : watch_page == WATCH_SIGNAL ? WATCH_ACTIONS : WATCH_DETAILS;
        recon_confirm = 0; return true;
    }
    if (watch_page == WATCH_OPTIONS && x >= 8 && x < 312 && y >= 72 && y < 376 && (y - 72) % 52 < 44) {
        unsigned action = (y - 72) / 52;
        unsigned confirmation = recon_confirm; recon_confirm = 0;
        if (action == 0) sentinel_watch_set_alert_filter(&watch_state,
            (sentinel_watch_confidence_t)(watch_state.minimum_confidence % 3 + 1), watch_state.muted, now);
        else if (action == 1) sentinel_watch_set_alert_filter(&watch_state, watch_state.minimum_confidence, !watch_state.muted, now);
        else if (action == 2) { watch_page = WATCH_FILTERS; watch_filters_page = 0; }
        else if (action == 3) {
            if (confirmation == 4) sentinel_watch_clear_device_policies(&watch_state); else recon_confirm = 4;
        } else if (action == 4) {
            if (confirmation == 5) { sentinel_watch_clear_history(&watch_state); watch_history_page = 0; }
            else recon_confirm = 5;
        } else { watch_page = WATCH_DEMO; watch_demo_scene = 0; }
        return true;
    }
    if (watch_page != WATCH_ACTIONS || watch_detail.radio == SENTINEL_WATCH_RADIO_ENVIRONMENT) return false;
    if (x >= 8 && x < 312 && y >= 106 && y < 150) { watch_page = WATCH_SIGNAL; return true; }
    bool ok;
    if (x >= 8 && x < 312 && y >= 160 && y < 204) {
        bool ignored = !sentinel_watch_is_ignored(&watch_state, &watch_detail);
        ok = sentinel_watch_set_ignored(&watch_state, &watch_detail, ignored, now);
        snprintf(recon_message, sizeof(recon_message), "%s", ok ? ignored ? "DEVICE ALERTS IGNORED" : "DEVICE ALERTS RESTORED" : "DEVICE POLICY LIST FULL (16)");
    } else if (x >= 8 && x < 312 && (x - 8) % 156 < 148 && y >= 242 && y < 340 && (y - 242) % 54 < 44) {
        const unsigned minutes[] = {10, 30, 60, 240};
        unsigned duration = minutes[((y - 242) / 54) * 2 + (x - 8) / 156];
        ok = sentinel_watch_snooze(&watch_state, &watch_detail, duration * 60000U, now);
        if (ok) snprintf(recon_message, sizeof(recon_message), "ALERTS SNOOZED FOR %u MINUTES", duration);
        else snprintf(recon_message, sizeof(recon_message), "DEVICE POLICY LIST FULL (16)");
    } else return false;
    return true;
}
