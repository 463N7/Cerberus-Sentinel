/* Private passive-analysis UI, included by ui.c. Rows are copied before paint;
 * a tap freezes that painted evidence, never a mutable service row index. */
#include "wifi_pinescan_session.h"

typedef struct {
    cerberus_s3_pinescan_row_t rows[4], staging[4], selected;
} pinescan_ui_store_t;
static pinescan_ui_store_t *pinescan_ui_store;
static cerberus_s3_pinescan_status_t pinescan_ui_status;
static cerberus_s3_pinescan_follow_t pinescan_ui_signal;
static cerberus_s3_pinescan_page_t pinescan_ui_page;
static uint16_t pinescan_ui_offset;
static uint8_t pinescan_ui_variant;
static bool pinescan_ui_open, pinescan_ui_detail, pinescan_ui_save;
static bool pinescan_ui_follow_pending, pinescan_ui_list_pending, pinescan_ui_exit_pending;
static uint32_t pinescan_ui_last_poll, pinescan_ui_selected_ms, pinescan_ui_rows_ms;
static char pinescan_ui_message[50];

static bool pinescan_busy(void)
{
    if (!pinescan_ui_open) return false;
    cerberus_s3_pinescan_status_t state;
    return cerberus_s3_pinescan_status(&state) != ESP_OK || state.capture.busy;
}

/* Every original byte has one unambiguous printable representation. */
static size_t pinescan_escape(const uint8_t *bytes, size_t length, char *out, size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    if (!out || !capacity || (!bytes && length) || length > CERBERUS_WIFI_BEACON_SSID_BYTES) return 0U;
    size_t used = 0U;
    for (size_t i = 0U; i < length; ++i) {
        unsigned char byte = bytes[i];
        char token[4]; size_t count;
        if (byte == '\\') { token[0] = token[1] = '\\'; count = 2U; }
        else if (byte >= 32U && byte <= 126U) { token[0] = (char)byte; count = 1U; }
        else { token[0] = '\\'; token[1] = 'x'; token[2] = hex[byte >> 4U]; token[3] = hex[byte & 15U]; count = 4U; }
        if (used + count >= capacity) { out[0] = '\0'; return 0U; }
        memcpy(out + used, token, count); used += count;
    }
    out[used] = '\0';
    return used;
}

static bool pinescan_supported(void)
{
    return c5_online && c5_status.firmware_version >= CERBERUS_WIFI_CAPTURE_HOP_MIN_FIRMWARE;
}

static void pinescan_ui_error(const char *operation, esp_err_t error)
{
    snprintf(pinescan_ui_message, sizeof(pinescan_ui_message), "%.12s: %.32s", operation, esp_err_to_name(error));
}

static void pinescan_ui_recovered(const char *operation)
{
    size_t length = strlen(operation);
    if (length < sizeof(pinescan_ui_message) &&
        strncmp(pinescan_ui_message, operation, length) == 0 && pinescan_ui_message[length] == ':')
        pinescan_ui_message[0] = '\0';
}

static void pinescan_enter(void)
{
    pinescan_ui_open = true;
    pinescan_ui_detail = pinescan_ui_follow_pending = pinescan_ui_list_pending = pinescan_ui_exit_pending = false;
    pinescan_ui_save = cerberus_storage_is_mounted();
    pinescan_ui_offset = pinescan_ui_variant = 0U;
    pinescan_ui_last_poll = 0U;
    pinescan_ui_message[0] = '\0';
    memset(&pinescan_ui_page, 0, sizeof(pinescan_ui_page));
    memset(&pinescan_ui_signal, 0, sizeof(pinescan_ui_signal));
    memset(&pinescan_ui_status, 0, sizeof(pinescan_ui_status));
    (void)cerberus_s3_pinescan_status(&pinescan_ui_status);
}

static void pinescan_refresh_rows(void)
{
    if (!pinescan_ui_store) {
        if (!pinescan_ui_status.candidates) { memset(&pinescan_ui_page, 0, sizeof(pinescan_ui_page)); return; }
        pinescan_ui_store = heap_caps_calloc(1U, sizeof(*pinescan_ui_store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!pinescan_ui_store) { pinescan_ui_error("DISPLAY", ESP_ERR_NO_MEM); return; }
    }
    cerberus_s3_pinescan_page_t page = {0};
    esp_err_t err = cerberus_s3_pinescan_page(pinescan_ui_offset, pinescan_ui_store->staging, 4U, &page);
    if (err == ESP_OK && page.copied > 4U) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && pinescan_ui_offset >= page.total && pinescan_ui_offset) {
        pinescan_ui_offset = 0U;
        err = cerberus_s3_pinescan_page(0U, pinescan_ui_store->staging, 4U, &page);
    }
    if (err == ESP_OK && (page.copied > 4U || page.copied > page.total)) err = ESP_ERR_INVALID_RESPONSE;
    for (size_t i = 0; err == ESP_OK && i < page.copied; ++i) {
        const cerberus_s3_pinescan_row_t *row = &pinescan_ui_store->staging[i];
        const cerberus_wifi_pinescan_evidence_t *match = &row->evidence.last_match;
        const uint8_t allowed = CERBERUS_WIFI_PINESCAN_REASON_OUI | CERBERUS_WIFI_PINESCAN_REASON_MINIMAL;
        if (row->generation != page.generation || !match->reasons ||
            (match->reasons & ~allowed) || match->beacon.ssid_length > CERBERUS_WIFI_BEACON_SSID_BYTES ||
            row->evidence.latest.ssid_length > CERBERUS_WIFI_BEACON_SSID_BYTES ||
            memcmp(match->beacon.transmitter, row->evidence.latest.transmitter, 6U) != 0)
            err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) { pinescan_ui_error("RESULTS", err); return; }
    memcpy(pinescan_ui_store->rows, pinescan_ui_store->staging, page.copied * sizeof(pinescan_ui_store->rows[0]));
    pinescan_ui_page = page;
    pinescan_ui_rows_ms = (uint32_t)(esp_timer_get_time() / 1000U);
    pinescan_ui_recovered("RESULTS");
    pinescan_ui_recovered("DISPLAY");
}

static bool pinescan_finish_exit(void)
{
    esp_err_t err = cerberus_s3_pinescan_release();
    if (err != ESP_OK) { pinescan_ui_error("CLEANUP", err); return false; }
    heap_caps_free(pinescan_ui_store); pinescan_ui_store = NULL;
    memset(&pinescan_ui_page, 0, sizeof(pinescan_ui_page));
    pinescan_ui_open = pinescan_ui_exit_pending = false;
    return true;
}

static void pinescan_request_stop(void)
{
    pinescan_ui_follow_pending = false;
    cerberus_s3_pinescan_stop();
    (void)cerberus_s3_pinescan_status(&pinescan_ui_status);
}

static bool pinescan_back(void)
{
    if (pinescan_ui_detail) {
        if (pinescan_ui_status.following || pinescan_ui_follow_pending) {
            pinescan_request_stop();
            if (pinescan_busy()) { pinescan_ui_list_pending = true; return false; }
        }
        pinescan_ui_detail = false;
        pinescan_refresh_rows();
        return false;
    }
    pinescan_ui_exit_pending = true;
    pinescan_request_stop();
    if (pinescan_busy()) return false;
    return pinescan_finish_exit();
}

static void pinescan_start(void)
{
    if (!pinescan_supported()) { snprintf(pinescan_ui_message, sizeof(pinescan_ui_message), "C5 BEACON SCAN UNAVAILABLE"); return; }
    esp_err_t err = cerberus_s3_pinescan_start(pinescan_ui_save);
    if (err != ESP_OK) { pinescan_ui_error("START", err); return; }
    pinescan_ui_message[0] = '\0';
    pinescan_ui_offset = 0U;
    memset(&pinescan_ui_page, 0, sizeof(pinescan_ui_page));
    memset(&pinescan_ui_signal, 0, sizeof(pinescan_ui_signal));
    (void)cerberus_s3_pinescan_status(&pinescan_ui_status);
}

static void pinescan_start_follow(void)
{
    if (!pinescan_ui_store || !pinescan_ui_detail) return;
    pinescan_ui_follow_pending = false;
    esp_err_t err = cerberus_s3_pinescan_follow(&pinescan_ui_store->selected, pinescan_ui_save);
    if (err != ESP_OK) { pinescan_ui_error("FOLLOW", err); return; }
    pinescan_ui_message[0] = '\0';
    memset(&pinescan_ui_signal, 0, sizeof(pinescan_ui_signal));
    (void)cerberus_s3_pinescan_status(&pinescan_ui_status);
}

static bool pinescan_tick(uint32_t now, bool visible)
{
    if (!pinescan_ui_open) return false;
    if (!visible) {
        pinescan_ui_exit_pending = true;
        pinescan_request_stop();
    }
    if ((uint32_t)(now - pinescan_ui_last_poll) < 500U) return false;
    pinescan_ui_last_poll = now;
    esp_err_t err = cerberus_s3_pinescan_status(&pinescan_ui_status);
    if (err != ESP_OK) { pinescan_ui_error("STATUS", err); return visible; }
    pinescan_ui_recovered("STATUS");
    if (!pinescan_ui_status.capture.busy) {
        if (pinescan_ui_exit_pending) {
            if (pinescan_finish_exit() && current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PINESCAN)
                wifi_mode = UI_WIFI_MODE_MENU;
            return visible;
        }
        if (pinescan_ui_list_pending) {
            pinescan_ui_list_pending = false; pinescan_ui_detail = false;
        } else if (pinescan_ui_follow_pending) pinescan_start_follow();
    }
    if (pinescan_ui_detail) {
        cerberus_s3_pinescan_follow_t signal = {0};
        err = cerberus_s3_pinescan_follow_status(&signal);
        if (err == ESP_OK && signal.valid && pinescan_ui_store && pinescan_ui_status.following &&
            signal.generation == pinescan_ui_status.capture.generation &&
            memcmp(signal.latest.transmitter, pinescan_ui_store->selected.evidence.latest.transmitter, 6U) == 0)
            pinescan_ui_signal = signal;
        else memset(&pinescan_ui_signal, 0, sizeof(pinescan_ui_signal));
    } else pinescan_refresh_rows();
    return visible;
}

static esp_err_t pinescan_line(uint16_t y, const char *line, uint16_t color)
{
    char bounded[50]; snprintf(bounded, sizeof(bounded), "%.49s", line);
    return draw_centered(y, bounded, color, 1);
}

static const char *pinescan_phase(void)
{
    if (pinescan_ui_exit_pending || pinescan_ui_list_pending) return "STOPPING / WAIT FOR CLEANUP";
    if (pinescan_ui_follow_pending) return "STOPPING SCAN / FOLLOW NEXT";
    switch (pinescan_ui_status.capture.state) {
    case CERBERUS_S3_CAPTURE_STARTING: return "STARTING";
    case CERBERUS_S3_CAPTURE_RUNNING: return pinescan_ui_status.following ? "FOLLOWING / RECEIVE ONLY" : "LISTENING / 2.4 + 5 GHZ HOPPING";
    case CERBERUS_S3_CAPTURE_STOPPING: return "STOPPING / DRAINING";
    case CERBERUS_S3_CAPTURE_SAVED: return "STOPPED / PCAP SAVED";
    case CERBERUS_S3_CAPTURE_COMPLETE: return "STOPPED / ANALYSIS RETAINED";
    case CERBERUS_S3_CAPTURE_ERROR: return "STOPPED WITH ERROR";
    default: return "READY / RECEIVE ONLY";
    }
}

static const char *pinescan_reasons(uint8_t reasons)
{
    if ((reasons & 3U) == 3U) return "OUI + MINIMAL LAYOUT";
    return reasons & CERBERUS_WIFI_PINESCAN_REASON_OUI ? "OUI RULE" : "MINIMAL LAYOUT";
}

static esp_err_t pinescan_draw_detail(void)
{
    if (!pinescan_ui_store) return ESP_ERR_INVALID_STATE;
    const cerberus_s3_pinescan_row_t *selected = &pinescan_ui_store->selected;
    const cerberus_wifi_pinescan_record_t *record = &selected->evidence;
    const cerberus_wifi_pinescan_evidence_t *match = &record->last_match;
    const cerberus_wifi_beacon_observation_t *beacon = pinescan_ui_variant ? &record->latest : &match->beacon;
    char line[96], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(54, "PINESCAN EVIDENCE", UI_TEXT, 2), TAG, "pine title");
    cerberus_privacy_format_wifi_identifier(beacon->transmitter, privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "TX CLAIM %s", identifier);
    ESP_RETURN_ON_ERROR(pinescan_line(78, line, UI_TEXT), TAG, "pine transmitter");
    cerberus_privacy_format_wifi_identifier(beacon->bssid, privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "BSSID %s%s", identifier, record->multiple_bssids ? " / CHANGED" : "");
    ESP_RETURN_ON_ERROR(pinescan_line(92, line, UI_MUTED), TAG, "pine bssid");
    ESP_RETURN_ON_ERROR(pinescan_line(107, pinescan_ui_variant ? "LATEST BEACON / NOT A NEW CLASSIFICATION" :
        "LAST MATCH / RETAINED QUALIFYING BEACON", UI_TEXT), TAG, "pine evidence scope");
    if (!pinescan_ui_variant) {
        snprintf(line, sizeof(line), "HINT: %s", pinescan_reasons(match->reasons));
        ESP_RETURN_ON_ERROR(pinescan_line(123, line, UI_TEXT), TAG, "pine exact reason");
        if (match->reasons & CERBERUS_WIFI_PINESCAN_REASON_OUI)
            snprintf(line, sizeof(line), "PREFIX RULE: %.34s", cerberus_wifi_pinescan_profile_name(match->profile));
        else snprintf(line, sizeof(line), "NO MATCHED PREFIX RULE");
        ESP_RETURN_ON_ERROR(pinescan_line(139, line, UI_MUTED), TAG, "pine profile");
        const char *condition = match->condition == CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET ? "SET" :
            match->condition == CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR ? "CLEAR" : "EITHER";
        snprintf(line, sizeof(line), "PREFIX RULE REQUIRES PRIVACY BIT %s",
            match->reasons & CERBERUS_WIFI_PINESCAN_REASON_OUI ? condition : "N/A");
        ESP_RETURN_ON_ERROR(pinescan_line(153, line, UI_MUTED), TAG, "pine condition");
        snprintf(line, sizeof(line), "CAP %04X / PRIVACY %s / IES %u", beacon->capabilities,
            match->privacy_bit ? "SET" : "CLEAR", match->ie_count);
        ESP_RETURN_ON_ERROR(pinescan_line(167, line, UI_TEXT), TAG, "pine raw capability");
        ESP_RETURN_ON_ERROR(pinescan_line(181,
            match->reasons & CERBERUS_WIFI_PINESCAN_REASON_MINIMAL ? "LAYOUT: ONLY SSID THEN DS / CAP 0001" : "MINIMAL LAYOUT RULE NOT MATCHED",
            UI_MUTED), TAG, "pine layout");
    } else {
        snprintf(line, sizeof(line), "LATEST %d DBM / PEAK %d DBM", record->latest.rssi, record->peak_rssi);
        ESP_RETURN_ON_ERROR(pinescan_line(123, line, UI_TEXT), TAG, "pine latest RSSI");
        uint64_t span = record->latest.observed_us >= record->first_observed_us ? record->latest.observed_us - record->first_observed_us : 0U;
        snprintf(line, sizeof(line), "SOURCE SPAN %llu MS", (unsigned long long)(span / 1000U));
        ESP_RETURN_ON_ERROR(pinescan_line(139, line, UI_MUTED), TAG, "pine source span");
        uint32_t elapsed = (uint32_t)((uint32_t)(esp_timer_get_time() / 1000U) - pinescan_ui_selected_ms);
        snprintf(line, sizeof(line), "S3 RXD AGE FIRST %lluS / LAST %lluS",
            (unsigned long long)(((uint64_t)selected->first_received_age_ms + elapsed) / 1000U),
            (unsigned long long)(((uint64_t)selected->last_received_age_ms + elapsed) / 1000U));
        ESP_RETURN_ON_ERROR(pinescan_line(153, line, UI_MUTED), TAG, "pine delivery ages");
        snprintf(line, sizeof(line), "FRAMES %lu / MATCHING %lu", (unsigned long)record->frames, (unsigned long)record->matching_frames);
        ESP_RETURN_ON_ERROR(pinescan_line(167, line, UI_TEXT), TAG, "pine observation counts");
        snprintf(line, sizeof(line), "HISTORICAL HINTS: %s", pinescan_reasons(record->reasons_seen));
        ESP_RETURN_ON_ERROR(pinescan_line(181, line, UI_MUTED), TAG, "pine historical reason");
    }
    snprintf(line, sizeof(line), "SSID %uB / RX %u / ADV %u / %d DBM", beacon->ssid_length, beacon->rx_channel, beacon->advertised_channel, beacon->rssi);
    ESP_RETURN_ON_ERROR(pinescan_line(197, line, UI_TEXT), TAG, "pine SSID scope");
    char escaped[129]; (void)pinescan_escape(beacon->ssid, beacon->ssid_length, escaped, sizeof(escaped));
    if (!beacon->ssid_length) snprintf(escaped, sizeof(escaped), "<EMPTY SSID>");
    for (unsigned row = 0; row < 3U; ++row) {
        size_t offset = row * 44U;
        snprintf(line, sizeof(line), "%.44s", offset < strlen(escaped) ? escaped + offset : "");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(28, (uint16_t)(211U + row * 14U), line, UI_TEXT, UI_BACKGROUND, 1), TAG, "pine exact SSID");
    }
    ESP_RETURN_ON_ERROR(pinescan_line(263, pinescan_phase(),
        pinescan_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_GOOD), TAG, "pine follow phase");
    if (pinescan_ui_signal.valid) {
        snprintf(line, sizeof(line), "LIVE %d DBM / PEAK %d / BEACONS %lu", pinescan_ui_signal.latest.rssi,
            pinescan_ui_signal.peak_rssi, (unsigned long)pinescan_ui_signal.matching_beacons);
        ESP_RETURN_ON_ERROR(pinescan_line(281, line, UI_TEXT), TAG, "pine follow signal");
        snprintf(line, sizeof(line), "RX %u / ADV %u / UPDATED %luS AGO", pinescan_ui_signal.latest.rx_channel,
            pinescan_ui_signal.latest.advertised_channel, (unsigned long)(pinescan_ui_signal.last_received_age_ms / 1000U));
        ESP_RETURN_ON_ERROR(pinescan_line(297, line, UI_MUTED), TAG, "pine follow age");
    } else {
        if (pinescan_ui_status.following && pinescan_ui_status.capture.busy)
            snprintf(line, sizeof(line), "LISTENING RX CH %u / WAITING FOR THIS TX", pinescan_ui_status.capture.channel);
        else snprintf(line, sizeof(line), "FOLLOW USES LATEST RX CH %u", record->latest.rx_channel);
        ESP_RETURN_ON_ERROR(pinescan_line(286, line, UI_MUTED), TAG, "pine follow hint");
    }
    snprintf(line, sizeof(line), "C5 DROP %lu+%lu / TABLE DROP %lu", (unsigned long)pinescan_ui_status.capture.dropped_full,
        (unsigned long)pinescan_ui_status.capture.dropped_other, (unsigned long)pinescan_ui_status.capacity_drops);
    ESP_RETURN_ON_ERROR(pinescan_line(318, line, UI_MUTED), TAG, "pine drops");
    ESP_RETURN_ON_ERROR(pinescan_line(336, "HINTS DO NOT IDENTIFY A PHYSICAL DEVICE", UI_MUTED), TAG, "pine identity limit");
    const char *message = pinescan_ui_message[0] ? pinescan_ui_message :
        pinescan_ui_status.capture.error_code != ESP_OK ? esp_err_to_name(pinescan_ui_status.capture.error_code) : "PRIVACY BIT IS NOT VERIFIED WPA / RSN";
    ESP_RETURN_ON_ERROR(pinescan_line(350, message,
        pinescan_ui_message[0] || pinescan_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_MUTED), TAG, "pine note");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,366,96,42},
        pinescan_ui_variant ? "MATCH >" : "LATEST >", 1, true, UI_WIDGET_THEME), TAG, "pine evidence tab");
    bool pending = pinescan_ui_follow_pending || pinescan_ui_list_pending || pinescan_ui_exit_pending;
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){112,366,96,42}, "FOLLOW", 1,
        !pending && !(pinescan_ui_status.following && pinescan_ui_status.capture.busy) && pinescan_supported(), UI_WIDGET_THEME), TAG, "pine follow");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){216,366,96,42}, "STOP", 1,
        pinescan_ui_status.capture.busy || pinescan_ui_follow_pending, UI_WIDGET_THEME);
}

static esp_err_t pinescan_draw(void)
{
    if (pinescan_ui_detail) return pinescan_draw_detail();
    char line[96], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(54, "PINESCAN", UI_TEXT, 2), TAG, "pine title");
    ESP_RETURN_ON_ERROR(pinescan_line(79, pinescan_phase(),
        pinescan_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_GOOD), TAG, "pine phase");
    snprintf(line, sizeof(line), "%u HINT ROWS / %lu BEACONS", pinescan_ui_page.total, (unsigned long)pinescan_ui_status.valid_beacons);
    ESP_RETURN_ON_ERROR(pinescan_line(97, line, UI_TEXT), TAG, "pine counts");
    snprintf(line, sizeof(line), "2.4G %lu / 5G %lu / HINT FRAMES %lu", (unsigned long)pinescan_ui_status.received_24ghz,
        (unsigned long)pinescan_ui_status.received_5ghz, (unsigned long)pinescan_ui_status.matching_beacons);
    ESP_RETURN_ON_ERROR(pinescan_line(111, line, UI_MUTED), TAG, "pine band counts");
    snprintf(line, sizeof(line), "C5 DROP %lu+%lu / TABLE DROP %lu", (unsigned long)pinescan_ui_status.capture.dropped_full,
        (unsigned long)pinescan_ui_status.capture.dropped_other, (unsigned long)pinescan_ui_status.capacity_drops);
    ESP_RETURN_ON_ERROR(pinescan_line(125, line, UI_MUTED), TAG, "pine drops");
    if (!pinescan_ui_page.copied) {
        ESP_RETURN_ON_ERROR(pinescan_line(169, "C5 LISTENS / S3 ANALYZES IN PSRAM", UI_TEXT), TAG, "pine split");
        ESP_RETURN_ON_ERROR(pinescan_line(191, "NO AP SELECTION REQUIRED", UI_MUTED), TAG, "pine no target");
        ESP_RETURN_ON_ERROR(pinescan_line(213, "PREFIX / BEACON LAYOUT HINTS ONLY", UI_MUTED), TAG, "pine predicate");
        ESP_RETURN_ON_ERROR(pinescan_line(235, "ORDINARY ADAPTERS CAN MATCH THESE RULES", UI_MUTED), TAG, "pine scope");
        ESP_RETURN_ON_ERROR(pinescan_line(270, pinescan_ui_save ? "SAVE ON / SD PCAP RECORDING" : "SAVE OFF / NO SD CARD REQUIRED", UI_TEXT), TAG, "pine SD");
    }
    for (size_t i = 0; i < pinescan_ui_page.copied; ++i) {
        const cerberus_s3_pinescan_row_t *row = &pinescan_ui_store->rows[i];
        const cerberus_wifi_pinescan_record_t *record = &row->evidence;
        uint16_t y = (uint16_t)(140U + i * 47U);
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8,y,304,43,UI_PANEL), TAG, "pine row");
        cerberus_privacy_format_wifi_identifier(record->latest.transmitter, privacy_addresses_masked(), identifier, sizeof(identifier));
        snprintf(line, sizeof(line), "TX %s / %d DBM", identifier, record->latest.rssi);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+4,line,UI_TEXT,UI_PANEL,1), TAG, "pine row id");
        snprintf(line, sizeof(line), "LAST MATCH: %s", pinescan_reasons(record->last_match.reasons));
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+17,line,UI_MUTED,UI_PANEL,1), TAG, "pine row reasons");
        snprintf(line, sizeof(line), "RX %u / ADV %u / S3 UPDATED %luS", record->latest.rx_channel,
            record->latest.advertised_channel, (unsigned long)(row->last_received_age_ms/1000U));
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+30,line,UI_MUTED,UI_PANEL,1), TAG, "pine row time");
    }
    if (pinescan_ui_status.capture.file_name[0]) snprintf(line, sizeof(line), "SD PCAP: %.31s", pinescan_ui_status.capture.file_name);
    else snprintf(line, sizeof(line), "HINTS ONLY / TAP FOR EXACT EVIDENCE");
    ESP_RETURN_ON_ERROR(pinescan_line(337, line, UI_MUTED), TAG, "pine list hint");
    const char *message = pinescan_ui_message[0] ? pinescan_ui_message :
        pinescan_ui_status.capture.error_code != ESP_OK ? esp_err_to_name(pinescan_ui_status.capture.error_code) :
        pinescan_supported() ? "RECEIVE ONLY / MANUAL STOP" : "C5 BEACON SCAN UNAVAILABLE";
    ESP_RETURN_ON_ERROR(pinescan_line(351, message,
        pinescan_ui_message[0] || pinescan_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_MUTED), TAG, "pine list note");
    bool busy = pinescan_ui_status.capture.busy;
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,366,68,42}, "PREV", 1, pinescan_ui_offset > 0U, UI_WIDGET_THEME), TAG, "pine previous");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){84,366,68,42}, busy ? "STOP" : "START", 1,
        busy || (!pinescan_ui_exit_pending && pinescan_supported()), UI_WIDGET_THEME), TAG, "pine start stop");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){160,366,68,42}, "NEXT", 1,
        pinescan_ui_offset + pinescan_ui_page.copied < pinescan_ui_page.total, UI_WIDGET_THEME), TAG, "pine next");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){236,366,76,42}, pinescan_ui_save ? "SAVE ON" : "SAVE OFF", 1,
        !busy && !pinescan_ui_exit_pending && (cerberus_storage_is_mounted() || pinescan_ui_save), UI_WIDGET_THEME);
}

static void pinescan_tap(uint16_t x, uint16_t y)
{
    if (pinescan_ui_exit_pending || pinescan_ui_list_pending) return;
    if (pinescan_ui_detail) {
        if (y < 366U || y >= 408U) return;
        if (x >= 8U && x < 104U) pinescan_ui_variant = (uint8_t)((pinescan_ui_variant + 1U) % 2U);
        else if (x >= 112U && x < 208U && !pinescan_ui_follow_pending &&
            !(pinescan_ui_status.following && pinescan_ui_status.capture.busy) && pinescan_supported()) {
            if (pinescan_busy()) { cerberus_s3_pinescan_stop(); pinescan_ui_follow_pending = true; }
            else pinescan_start_follow();
        } else if (x >= 216U && x < 312U) pinescan_request_stop();
        return;
    }
    if (x >= 8U && x < 312U && y >= 140U && y < 324U && (y - 140U) % 47U < 43U) {
        unsigned row = (y - 140U) / 47U;
        if (pinescan_ui_store && row < pinescan_ui_page.copied) {
            pinescan_ui_store->selected = pinescan_ui_store->rows[row];
            pinescan_ui_selected_ms = pinescan_ui_rows_ms;
            pinescan_ui_variant = 0U; pinescan_ui_detail = true;
            memset(&pinescan_ui_signal, 0, sizeof(pinescan_ui_signal));
        }
        return;
    }
    if (y < 366U || y >= 408U) return;
    if (x >= 8U && x < 76U && pinescan_ui_offset >= 4U) { pinescan_ui_offset -= 4U; pinescan_refresh_rows(); }
    else if (x >= 84U && x < 152U) {
        if (pinescan_busy()) pinescan_request_stop(); else pinescan_start();
    } else if (x >= 160U && x < 228U && pinescan_ui_offset + pinescan_ui_page.copied < pinescan_ui_page.total) {
        pinescan_ui_offset += 4U; pinescan_refresh_rows();
    } else if (x >= 236U && x < 312U && !pinescan_busy() && (cerberus_storage_is_mounted() || pinescan_ui_save))
        pinescan_ui_save = !pinescan_ui_save;
}
