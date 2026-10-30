/* UI-task-only lifetime: one session/view/cache allocation in PSRAM. Opening
 * freezes the selected AP but does not reserve or start the C5 radio. */
typedef struct {
    cerberus_csi_session_t session;
    cerberus_csi_session_view_t view;
    cerberus_csi_ui_cache_t cache;
    cerberus_csi_ops_t ops;
    uint64_t last_draw_us;
    bool exit_pending;
} csi_live_store_t;
static csi_live_store_t *csi_live;

static esp_err_t csi_open_selected(void)
{
    if (!wifi_selection_valid) return ESP_ERR_INVALID_STATE;
    if (csi_live && !cerberus_csi_session_released(&csi_live->session)) return ESP_ERR_INVALID_STATE;
    cerberus_csi_target_t target = {.channel = wifi_selected_result.channel};
    memcpy(target.bssid, wifi_selected_result.bssid, 6U);
    target.ssid_length = (uint8_t)strnlen(wifi_selected_result.ssid, 32U);
    memcpy(target.ssid, wifi_selected_result.ssid, target.ssid_length);
    if (!cerberus_csi_target_valid(&target)) return ESP_ERR_INVALID_ARG;
    if (!csi_live) csi_live = heap_caps_calloc(1, sizeof(*csi_live), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!csi_live) return ESP_ERR_NO_MEM;
    memset(csi_live, 0, sizeof(*csi_live));
    if (!cerberus_csi_session_init(&csi_live->session, &target)) {
        free(csi_live); csi_live = NULL; return ESP_ERR_INVALID_ARG;
    }
    cerberus_csi_link_ops(&csi_live->ops);
    wifi_mode = UI_WIFI_MODE_CSI;
    return ESP_OK;
}

static esp_err_t csi_draw(bool force)
{
    if (!csi_live) return ESP_ERR_INVALID_STATE;
    uint64_t now = (uint64_t)esp_timer_get_time();
    cerberus_csi_session_view(&csi_live->session, now, &csi_live->view);
    char source[24];
    cerberus_privacy_format_wifi_identifier(csi_live->session.target.bssid,
        privacy_addresses_masked(), source, sizeof(source));
    cerberus_csi_ui_view_t view = {.session=&csi_live->view,.source_label=source};
    esp_err_t error = cerberus_csi_ui_update(&csi_live->cache, &view, force);
    if (error == ESP_OK) csi_live->last_draw_us = now;
    return error;
}

static void csi_close_released(void)
{
    if (!csi_live || !cerberus_csi_session_released(&csi_live->session)) return;
    bool here = current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI;
    free(csi_live); csi_live = NULL;
    if (here) { wifi_mode = UI_WIFI_MODE_TARGET; ESP_ERROR_CHECK(draw_detail_page()); }
}

static void csi_handle_tap(uint16_t x, uint16_t y)
{
    if (!csi_live) return;
    switch (cerberus_csi_ui_tap(&csi_live->cache, x, y)) {
    case CERBERUS_CSI_UI_START:
        (void)cerberus_csi_session_start(&csi_live->session); break;
    case CERBERUS_CSI_UI_STOP:
        cerberus_csi_session_stop(&csi_live->session); break;
    case CERBERUS_CSI_UI_RECALIBRATE:
        (void)cerberus_csi_session_recalibrate(&csi_live->session); break;
    case CERBERUS_CSI_UI_BACK:
        csi_live->exit_pending = true;
        cerberus_csi_session_stop(&csi_live->session);
        if (cerberus_csi_session_released(&csi_live->session)) { csi_close_released(); return; }
        break;
    default: return;
    }
    ESP_ERROR_CHECK(csi_draw(false));
}

static void csi_live_tick(bool visible)
{
    if (!csi_live) return;
    if (!visible && !cerberus_csi_session_released(&csi_live->session) &&
        !csi_live->session.stop_requested) cerberus_csi_session_stop(&csi_live->session);
    cerberus_csi_session_tick(&csi_live->session, &csi_live->ops);
    bool here = current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI;
    if ((csi_live->exit_pending || !here) && cerberus_csi_session_released(&csi_live->session)) {
        csi_close_released(); return;
    }
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (visible && (!csi_live->last_draw_us || now-csi_live->last_draw_us >= 200000U))
        ESP_ERROR_CHECK(csi_draw(false));
}
