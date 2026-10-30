/* Ordinary scan evidence only. This store never enters wifi_targets or the
 * selected-AP station service. The UI performs one bounded link step per tick. */
#include "s3_wifi_relation.h"

typedef struct {
    cerberus_s3_wifi_relation_bank_t bank;
    cerberus_s3_wifi_relation_selection_t selected;
    cerberus_wifi_relation_row_t staging[CERBERUS_WIFI_RELATION_LIMIT];
    cerberus_c5_wifi_relation_reader_t reader;
    cerberus_c5_wifi_result_t ap;
    uint64_t reading_publication, selected_publication;
    uint32_t last_draw_ms;
    uint8_t page, detail_index;
    bool reading, detail;
    char message[49];
} wifi_relation_ui_store_t;

static wifi_relation_ui_store_t *wifi_relation_ui;
static uint64_t wifi_relation_publication, wifi_relation_selected_publication;
static uint8_t wifi_relation_selected_bssid[6], wifi_relation_selected_channel;
static uint8_t wifi_relation_generation;
static bool wifi_relation_queued, wifi_relation_allocation_failed, wifi_relation_exhausted;
static cerberus_c5_wifi_relation_bank_t wifi_relation_binding;
static esp_err_t wifi_relation_binding_error = ESP_ERR_NOT_SUPPORTED;

static bool wifi_relation_supported(void)
{ return c5_status.firmware_version >= CERBERUS_WIFI_RELATION_MIN_FIRMWARE; }

/* These status-only fences bracket AP row collection. Failure does not discard
 * valid AP rows, but it forbids an unbound client-evidence fetch afterwards. */
static esp_err_t wifi_relation_capture_before(uint8_t generation, cerberus_c5_wifi_relation_bank_t *proof)
{
    if (!wifi_relation_supported()) return ESP_ERR_NOT_SUPPORTED;
    return c5_spi_link_get_wifi_relation_bank(generation,
        (uint32_t)(esp_timer_get_time() / 1000LL) + 100U, proof);
}
static esp_err_t wifi_relation_capture_after(const cerberus_c5_wifi_relation_bank_t *proof)
{
    return c5_spi_link_check_wifi_relation_bank(proof,
        (uint32_t)(esp_timer_get_time() / 1000LL) + 100U);
}

static bool wifi_relation_acquire(void)
{
    if (!wifi_relation_ui && !wifi_relation_allocation_failed) {
        wifi_relation_ui = heap_caps_calloc(1, sizeof(*wifi_relation_ui),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        wifi_relation_allocation_failed = wifi_relation_ui == NULL;
    }
    return wifi_relation_ui != NULL;
}

static void wifi_relation_refresh_selection(void)
{
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!s || !s->selected_publication ||
        s->selected.bank.outcome == CERBERUS_S3_WIFI_RELATION_READY) return;
    (void)cerberus_s3_wifi_relation_select(&s->bank, s->selected_publication,
        s->ap.bssid, s->ap.channel, &s->selected);
}

static void wifi_relation_cancel(void)
{
    wifi_relation_queued = false;
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!s) return;
    if (s->reading || s->bank.outcome == CERBERUS_S3_WIFI_RELATION_LOADING) {
        c5_spi_link_wifi_relation_cancel(&s->reader);
        s->reading = false;
        (void)cerberus_s3_wifi_relation_set_outcome(&s->bank,
            wifi_relation_publication, wifi_relation_generation,
            CERBERUS_S3_WIFI_RELATION_CANCELLED, 0);
        wifi_relation_refresh_selection();
    }
}

/* Called only after a complete AP snapshot is committed. Unlike the wire's
 * eight-bit generation, this local publication identity is never reused. */
static void wifi_relation_publish(uint8_t generation,
    const cerberus_c5_wifi_relation_bank_t *proof, esp_err_t binding_error)
{
    wifi_relation_cancel();
    if (wifi_relation_exhausted || wifi_relation_publication == UINT64_MAX) {
        wifi_relation_exhausted = true;
        wifi_relation_publication = 0;
        return;
    }
    ++wifi_relation_publication;
    wifi_relation_generation = generation;
    memset(&wifi_relation_binding, 0, sizeof(wifi_relation_binding));
    if (binding_error == ESP_OK && (!proof || proof->status.generation != generation ||
        !cerberus_s3_wifi_relation_meta_valid(proof))) binding_error = ESP_ERR_INVALID_RESPONSE;
    wifi_relation_binding_error = binding_error;
    if (binding_error == ESP_OK) wifi_relation_binding = *proof;
    wifi_relation_allocation_failed = false;
    wifi_relation_queued = binding_error == ESP_OK;
    if (wifi_relation_ui)
        (void)cerberus_s3_wifi_relation_set_outcome(&wifi_relation_ui->bank,
            wifi_relation_publication, generation, wifi_relation_queued ?
            CERBERUS_S3_WIFI_RELATION_LOADING : binding_error == ESP_ERR_NOT_SUPPORTED ?
            CERBERUS_S3_WIFI_RELATION_UNSUPPORTED : CERBERUS_S3_WIFI_RELATION_FAILED, binding_error);
}

static void wifi_relation_bind_selection(bool ordinary_publication)
{
    wifi_relation_selected_publication = ordinary_publication ? wifi_relation_publication : 0;
    memcpy(wifi_relation_selected_bssid, wifi_selected_result.bssid, 6U);
    wifi_relation_selected_channel = wifi_selected_result.channel;
}

static bool wifi_relation_selection_bound(void)
{
    return wifi_relation_selected_publication &&
        wifi_relation_selected_channel == wifi_selected_result.channel &&
        !memcmp(wifi_relation_selected_bssid, wifi_selected_result.bssid, 6U);
}

static void wifi_relation_open(void)
{
    wifi_mode = UI_WIFI_MODE_OBSERVED_CLIENTS;
    if (!wifi_relation_acquire()) return;
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!s->bank.ap_publication && wifi_relation_publication)
        (void)cerberus_s3_wifi_relation_set_outcome(&s->bank, wifi_relation_publication,
            wifi_relation_generation, wifi_relation_binding_error == ESP_OK ?
            (wifi_relation_queued ? CERBERUS_S3_WIFI_RELATION_LOADING : CERBERUS_S3_WIFI_RELATION_CANCELLED) :
            wifi_relation_binding_error == ESP_ERR_NOT_SUPPORTED ? CERBERUS_S3_WIFI_RELATION_UNSUPPORTED :
            CERBERUS_S3_WIFI_RELATION_FAILED, wifi_relation_binding_error);
    s->ap = wifi_selected_result;
    for (char *p = s->ap.ssid; *p; ++p) if ((uint8_t)*p < 32U || (uint8_t)*p > 126U) *p = '?';
    s->selected_publication = wifi_relation_selection_bound() ? wifi_relation_selected_publication : 0;
    s->page = s->detail_index = 0;
    s->detail = false;
    s->message[0] = 0;
    (void)cerberus_s3_wifi_relation_select(&s->bank, s->selected_publication,
        s->ap.bssid, s->ap.channel, &s->selected);
}

static void wifi_relation_retry(void)
{
    if (wifi_relation_exhausted || wifi_relation_binding_error != ESP_OK || !wifi_relation_selection_bound() ||
        wifi_relation_selected_publication != wifi_relation_publication) return;
    wifi_relation_allocation_failed = false;
    if (!wifi_relation_acquire()) return;
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (s->reading || s->bank.outcome == CERBERUS_S3_WIFI_RELATION_READY) return;
    wifi_relation_queued = true;
    (void)cerberus_s3_wifi_relation_set_outcome(&s->bank,
        wifi_relation_publication, wifi_relation_generation, CERBERUS_S3_WIFI_RELATION_LOADING, 0);
    /* Allocation may have failed on Open, so freeze the AP here as well. */
    wifi_relation_open();
}

static void wifi_relation_tick(uint32_t now, bool useful, bool page_visible)
{
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!useful) { wifi_relation_cancel(); return; }
    if (wifi_relation_queued && !wifi_relation_acquire()) {
        wifi_relation_queued = false;
        if (page_visible) ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    s = wifi_relation_ui;
    if (!s) return;
    esp_err_t error = ESP_OK;
    bool changed = false;
    if (wifi_relation_queued) {
        wifi_relation_queued = false;
        s->reading_publication = wifi_relation_publication;
        (void)cerberus_s3_wifi_relation_set_outcome(&s->bank,
            wifi_relation_publication, wifi_relation_generation, CERBERUS_S3_WIFI_RELATION_LOADING, 0);
        error = c5_spi_link_wifi_relation_begin_expected(&s->reader, &wifi_relation_binding,
            (uint32_t)(esp_timer_get_time() / 1000LL) + 15000U, s->staging, CERBERUS_WIFI_RELATION_LIMIT);
        s->reading = error == ESP_OK;
        changed = true;
    }
    if (s->reading && error == ESP_OK) {
        cerberus_c5_wifi_relation_bank_t proof;
        bool complete = false;
        /* One step, never a whole-table loop on the UI task. */
        error = c5_spi_link_wifi_relation_step(&s->reader, &proof, &complete, NULL, NULL);
        if (error == ESP_OK && complete) {
            s->reading = false;
            if (s->reading_publication != wifi_relation_publication ||
                cerberus_s3_wifi_relation_apply(&s->bank, s->reading_publication,
                    &proof, s->staging, proof.status.count, (uint64_t)esp_timer_get_time()) ==
                    CERBERUS_S3_WIFI_RELATION_REJECTED) error = ESP_ERR_INVALID_RESPONSE;
            changed = true;
        }
    }
    if (error != ESP_OK) {
        c5_spi_link_wifi_relation_cancel(&s->reader);
        s->reading = false;
        (void)cerberus_s3_wifi_relation_set_outcome(&s->bank,
            wifi_relation_publication, wifi_relation_generation,
            error == ESP_ERR_NOT_SUPPORTED ? CERBERUS_S3_WIFI_RELATION_UNSUPPORTED :
                CERBERUS_S3_WIFI_RELATION_FAILED, error);
        changed = true;
    }
    if (changed) wifi_relation_refresh_selection();
    if (page_visible && (changed || (uint32_t)(now - s->last_draw_ms) >= 1000U)) {
        s->last_draw_ms = now;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

/* Only numeric/hex evidence is exported: no untrusted SSID/formula fields.
 * Callback times are C5 uptime microseconds; request time is S3 uptime. */
static size_t wifi_relation_csv(char *out, size_t capacity,
    const cerberus_s3_wifi_relation_selection_t *selected)
{
    if (!out || !capacity || !selected || !selected->valid ||
        selected->bank.outcome != CERBERUS_S3_WIFI_RELATION_READY ||
        selected->count > CERBERUS_WIFI_RELATION_LIMIT) return 0;
    const cerberus_s3_wifi_relation_meta_t *m = &selected->bank.meta;
    int n = snprintf(out, capacity,
        "# Cerberus observed DS relationships; association unverified; C5 RX != UTC\n"
        "# epoch=%llu; c5_now_us=%llu; s3_request_us=%llu; bank_frames=%lu; bank_dropped=%lu\n"
        "bssid,station,channel,to_ap_frames,from_ap_frames,station_rssi,ap_rssi,to_ap_rx_us,from_ap_rx_us,latest_ds\n",
        (unsigned long long)m->status.epoch, (unsigned long long)m->source_now_us,
        (unsigned long long)m->request_started_us, (unsigned long)m->status.matched_frames,
        (unsigned long)m->status.dropped_frames);
    if (n < 0 || (size_t)n >= capacity) return 0;
    size_t used = (size_t)n;
    for (unsigned i = 0; i < selected->count; ++i) {
        const cerberus_wifi_relation_row_t *r = &selected->bank.rows[i];
        char station_rssi[8] = "", ap_rssi[8] = "";
        if (r->rssi_valid & CERBERUS_WIFI_RELATION_TO_AP)
            snprintf(station_rssi, sizeof(station_rssi), "%d", r->to_ap_rssi);
        if (r->rssi_valid & CERBERUS_WIFI_RELATION_FROM_AP)
            snprintf(ap_rssi, sizeof(ap_rssi), "%d", r->from_ap_rssi);
        n = snprintf(out + used, capacity - used,
            "%02X:%02X:%02X:%02X:%02X:%02X,%02X:%02X:%02X:%02X:%02X:%02X,%u,%lu,%lu,%s,%s,%llu,%llu,%u\n",
            r->bssid[0], r->bssid[1], r->bssid[2], r->bssid[3], r->bssid[4], r->bssid[5],
            r->station[0], r->station[1], r->station[2], r->station[3], r->station[4], r->station[5],
            r->channel, (unsigned long)r->to_ap_frames, (unsigned long)r->from_ap_frames,
            station_rssi, ap_rssi, (unsigned long long)r->to_ap_callback_us,
            (unsigned long long)r->from_ap_callback_us, r->latest_direction);
        if (n < 0 || (size_t)n >= capacity - used) return 0;
        used += (size_t)n;
    }
    return used;
}

static void wifi_relation_save(void)
{
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!s || s->selected.bank.outcome != CERBERUS_S3_WIFI_RELATION_READY) return;
    char *csv = heap_caps_calloc(8192U, 1U, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    esp_err_t error = csv ? ESP_OK : ESP_ERR_NO_MEM;
    size_t length = csv ? wifi_relation_csv(csv, 8192U, &s->selected) : 0;
    char name[32] = "";
    if (csv && !length) error = ESP_ERR_INVALID_SIZE;
    if (error == ESP_OK) error = cerberus_storage_write_wifi_report(csv, length, name, sizeof(name));
    heap_caps_free(csv);
    if (error == ESP_OK) snprintf(s->message, sizeof(s->message), "SAVED %.31s", name);
    else snprintf(s->message, sizeof(s->message), "SAVE: %.40s", esp_err_to_name(error));
}

static esp_err_t wifi_relation_direction_draw(uint16_t y, uint8_t direction)
{
    const wifi_relation_ui_store_t *s = wifi_relation_ui;
    const cerberus_wifi_relation_row_t *r = &s->selected.bank.rows[s->detail_index];
    bool uplink = direction == CERBERUS_WIFI_RELATION_TO_AP;
    char line[49];
    snprintf(line, sizeof(line), "%s / %lu FRAMES", uplink ? "STA > AP" : "AP > STA",
        (unsigned long)(uplink ? r->to_ap_frames : r->from_ap_frames));
    ESP_RETURN_ON_ERROR(draw_centered(y, line, UI_TEXT, 1), TAG, "relation direction");
    if (!(r->directions & direction)) snprintf(line, sizeof(line), "NOT OBSERVED");
    else if (!(r->rssi_valid & direction)) snprintf(line, sizeof(line), "%s SIGNAL UNAVAILABLE", uplink ? "STATION" : "AP");
    else snprintf(line, sizeof(line), "%s SIGNAL: %d DBM", uplink ? "STATION" : "AP",
        uplink ? r->to_ap_rssi : r->from_ap_rssi);
    ESP_RETURN_ON_ERROR(draw_centered(y + 18U, line, UI_MUTED, 1), TAG, "relation signal");
    uint64_t age;
    if (cerberus_s3_wifi_relation_age_ms(&s->selected, s->detail_index, direction,
        (uint64_t)esp_timer_get_time(), &age))
        snprintf(line, sizeof(line), "RX AGE <= %llu S", (unsigned long long)(age / 1000U + (age % 1000U != 0U)));
    else snprintf(line, sizeof(line), "RX AGE UNAVAILABLE");
    return draw_centered(y + 36U, line, UI_MUTED, 1);
}

static esp_err_t wifi_relation_draw(void)
{
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    char line[49];
    ESP_RETURN_ON_ERROR(draw_centered(58, "OBSERVED CLIENTS", UI_TEXT, 2), TAG, "relations title");
    if (!s) {
        ESP_RETURN_ON_ERROR(draw_centered(140, "NO MEMORY FOR OBSERVATIONS", UI_MUTED, 1), TAG, "relations memory");
        return draw_small_button(8, 378, 304, 36, "RETRY");
    }
    ESP_RETURN_ON_ERROR(draw_centered(84, s->ap.ssid[0] ? s->ap.ssid : "<HIDDEN SSID>", UI_MUTED, 1), TAG, "relations AP");
    cerberus_s3_wifi_relation_outcome_t outcome = s->selected.bank.outcome;
    const char *state = outcome == CERBERUS_S3_WIFI_RELATION_READY ? "SCAN OBSERVATIONS" :
        outcome == CERBERUS_S3_WIFI_RELATION_LOADING ? "LOADING OBSERVATIONS" :
        outcome == CERBERUS_S3_WIFI_RELATION_FAILED ? "OBSERVATIONS READ FAILED" :
        outcome == CERBERUS_S3_WIFI_RELATION_UNSUPPORTED ? "C5 UPDATE REQUIRED" :
        outcome == CERBERUS_S3_WIFI_RELATION_UNAVAILABLE ? "CAPTURE UNAVAILABLE" :
        outcome == CERBERUS_S3_WIFI_RELATION_CANCELLED ? "READ CANCELLED / RETRY" : "SCAN WI-FI AGAIN FOR EVIDENCE";
    ESP_RETURN_ON_ERROR(draw_centered(106, state, UI_MUTED, 1), TAG, "relations state");
    if (s->detail && s->detail_index < s->selected.count) {
        const cerberus_wifi_relation_row_t *r = &s->selected.bank.rows[s->detail_index];
        cerberus_privacy_format_wifi_identifier(r->station, privacy_addresses_masked(), line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(130, line, UI_TEXT, 1), TAG, "relation station");
        refresh_selected_vendor(false, r->station, 0U, esp_timer_get_time() / 1000LL);
        snprintf(line, sizeof(line), "VENDOR: %.39s", vendor_cache.result == RECON_DB_FOUND ? vendor_cache.name :
            vendor_cache.result == RECON_DB_PRIVATE_ADDRESS ? "LOCAL ADDRESS" : "UNKNOWN / UNAVAILABLE");
        ESP_RETURN_ON_ERROR(draw_centered(150, line, UI_MUTED, 1), TAG, "relation vendor");
        ESP_RETURN_ON_ERROR(wifi_relation_direction_draw(186, CERBERUS_WIFI_RELATION_TO_AP), TAG, "relation uplink");
        ESP_RETURN_ON_ERROR(wifi_relation_direction_draw(252, CERBERUS_WIFI_RELATION_FROM_AP), TAG, "relation downlink");
    } else {
        if (outcome == CERBERUS_S3_WIFI_RELATION_READY)
            snprintf(line, sizeof(line), "%u ROWS / CH %u / PAGE %u", s->selected.count, s->ap.channel, s->page + 1U);
        else if (s->selected.bank.error) snprintf(line, sizeof(line), "%.48s", esp_err_to_name(s->selected.bank.error));
        else line[0] = 0;
        ESP_RETURN_ON_ERROR(draw_centered(126, line, UI_TEXT, 1), TAG, "relations count");
        for (unsigned row = 0; row < 4U; ++row) {
            unsigned i = s->page * 4U + row;
            if (i >= s->selected.count) break;
            const cerberus_wifi_relation_row_t *r = &s->selected.bank.rows[i];
            char mac[24];
            cerberus_privacy_format_wifi_identifier(r->station, privacy_addresses_masked(), mac, sizeof(mac));
            snprintf(line, sizeof(line), "%s / %s", mac, r->directions == 3U ? "BOTH" :
                r->directions == CERBERUS_WIFI_RELATION_TO_AP ? "STA>AP" : "AP>STA");
            ESP_RETURN_ON_ERROR(draw_small_button(8, 148U + row * 42U, 304, 36, line), TAG, "relation row");
        }
        if (outcome == CERBERUS_S3_WIFI_RELATION_READY && !s->selected.count)
            ESP_RETURN_ON_ERROR(draw_centered(204, "NO MATCHING ROWS RETAINED", UI_MUTED, 1), TAG, "relations empty");
    }
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8, 322, 304, 28},
        "FOCUSED SCAN OPTIONS", 1, c5_online && (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_STATIONS), UI_WIDGET_THEME), TAG, "relation focus");
    if (s->message[0]) snprintf(line, sizeof(line), "%.48s", s->message);
    else if (s->selected.bank.meta.status.dropped_frames)
        snprintf(line, sizeof(line), "BANK FULL / %lu FRAMES NOT RETAINED", (unsigned long)s->selected.bank.meta.status.dropped_frames);
    else snprintf(line, sizeof(line), "OBSERVED / ASSOCIATION NOT VERIFIED");
    ESP_RETURN_ON_ERROR(draw_centered(356, line, UI_MUTED, 1), TAG, "relation note");
    if (s->detail) return ESP_OK; /* Parent footer owns Back on every state. */
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8, 378, 72, 36}, "PREV", 1, s->page > 0U, UI_WIDGET_THEME), TAG, "relation prev");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){88, 378, 72, 36}, "NEXT", 1,
        (s->page + 1U) * 4U < s->selected.count, UI_WIDGET_THEME), TAG, "relation next");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){168, 378, 144, 36},
        outcome == CERBERUS_S3_WIFI_RELATION_READY ? "SAVE SD" : wifi_relation_binding_error == ESP_OK ? "RETRY" : "RESCAN REQUIRED", 1,
        outcome == CERBERUS_S3_WIFI_RELATION_READY || (!s->reading && s->selected_publication &&
            s->selected_publication == wifi_relation_publication && wifi_relation_binding_error == ESP_OK), UI_WIDGET_THEME);
}

static void wifi_relation_tap(uint16_t x, uint16_t y)
{
    wifi_relation_ui_store_t *s = wifi_relation_ui;
    if (!s) { if (x >= 8U && x < 312U && y >= 378U && y < 414U) wifi_relation_retry(); return; }
    if (!s->detail && x >= 8U && x < 312U && y >= 148U && y < 316U && (y - 148U) % 42U < 36U) {
        unsigned i = s->page * 4U + (y - 148U) / 42U;
        if (i < s->selected.count) { s->detail_index = (uint8_t)i; s->detail = true; }
    } else if (x >= 8U && x < 312U && y >= 322U && y < 350U &&
        c5_online && (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_STATIONS)) {
        wifi_relation_cancel(); station_enter(); wifi_mode = UI_WIFI_MODE_STATIONS;
    } else if (!s->detail && y >= 378U && y < 414U) {
        if (x >= 8U && x < 80U && s->page) --s->page;
        else if (x >= 88U && x < 160U && (s->page + 1U) * 4U < s->selected.count) ++s->page;
        else if (x >= 168U && x < 312U) {
            if (s->selected.bank.outcome == CERBERUS_S3_WIFI_RELATION_READY) wifi_relation_save();
            else wifi_relation_retry();
        }
    }
}

static void wifi_relation_back(void)
{
    if (wifi_relation_ui && wifi_relation_ui->detail) wifi_relation_ui->detail = false;
    else { wifi_relation_cancel(); wifi_mode = UI_WIFI_MODE_TARGET; }
}
