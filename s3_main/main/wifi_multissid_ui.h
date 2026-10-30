/* Private passive-analysis UI, included by ui.c. Rows are copied before paint;
 * a tap freezes that painted evidence, never a mutable service row index. */
#include "wifi_multissid_session.h"

typedef struct {
    cerberus_s3_multissid_row_t rows[4], staging[4], selected;
} multissid_ui_store_t;
static multissid_ui_store_t *multissid_ui_store;
static cerberus_s3_multissid_status_t multissid_ui_status;
static cerberus_s3_multissid_follow_t multissid_ui_signal;
static cerberus_s3_multissid_page_t multissid_ui_page;
static uint16_t multissid_ui_offset;
static uint8_t multissid_ui_variant;
static bool multissid_ui_open, multissid_ui_detail, multissid_ui_save;
static bool multissid_ui_follow_pending, multissid_ui_list_pending, multissid_ui_exit_pending;
static uint32_t multissid_ui_last_poll, multissid_ui_selected_ms, multissid_ui_rows_ms;
static char multissid_ui_message[50];

static bool multissid_busy(void)
{
    if (!multissid_ui_open) return false;
    cerberus_s3_multissid_status_t state;
    return cerberus_s3_multissid_status(&state) != ESP_OK || state.capture.busy;
}

/* Every original byte has one unambiguous printable representation. */
static size_t multissid_escape(const uint8_t *bytes, size_t length, char *out, size_t capacity)
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

static bool multissid_supported(void)
{
    return c5_online && c5_status.firmware_version >= CERBERUS_WIFI_CAPTURE_HOP_MIN_FIRMWARE;
}

static void multissid_ui_error(const char *operation, esp_err_t error)
{
    snprintf(multissid_ui_message, sizeof(multissid_ui_message), "%.12s: %.32s", operation, esp_err_to_name(error));
}

static void multissid_ui_recovered(const char *operation)
{
    size_t length = strlen(operation);
    if (length < sizeof(multissid_ui_message) &&
        strncmp(multissid_ui_message, operation, length) == 0 && multissid_ui_message[length] == ':')
        multissid_ui_message[0] = '\0';
}

static void multissid_enter(void)
{
    multissid_ui_open = true;
    multissid_ui_detail = multissid_ui_follow_pending = multissid_ui_list_pending = multissid_ui_exit_pending = false;
    multissid_ui_save = cerberus_storage_is_mounted();
    multissid_ui_offset = multissid_ui_variant = 0U;
    multissid_ui_last_poll = 0U;
    multissid_ui_message[0] = '\0';
    memset(&multissid_ui_page, 0, sizeof(multissid_ui_page));
    memset(&multissid_ui_signal, 0, sizeof(multissid_ui_signal));
    memset(&multissid_ui_status, 0, sizeof(multissid_ui_status));
    (void)cerberus_s3_multissid_status(&multissid_ui_status);
}

static void multissid_refresh_rows(void)
{
    if (!multissid_ui_store) {
        if (!multissid_ui_status.confirmed) { memset(&multissid_ui_page, 0, sizeof(multissid_ui_page)); return; }
        multissid_ui_store = heap_caps_calloc(1U, sizeof(*multissid_ui_store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!multissid_ui_store) { multissid_ui_error("DISPLAY", ESP_ERR_NO_MEM); return; }
    }
    cerberus_s3_multissid_page_t page = {0};
    esp_err_t err = cerberus_s3_multissid_page(multissid_ui_offset, multissid_ui_store->staging, 4U, &page);
    if (err == ESP_OK && page.copied > 4U) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK && multissid_ui_offset >= page.total && multissid_ui_offset) {
        multissid_ui_offset = 0U;
        err = cerberus_s3_multissid_page(0U, multissid_ui_store->staging, 4U, &page);
    }
    if (err == ESP_OK && (page.copied > 4U || page.copied > page.total)) err = ESP_ERR_INVALID_RESPONSE;
    for (size_t i = 0; err == ESP_OK && i < page.copied; ++i) {
        const cerberus_s3_multissid_row_t *row = &multissid_ui_store->staging[i];
        if (row->generation != page.generation ||
            row->evidence.variant_count != CERBERUS_WIFI_MULTISSID_THRESHOLD ||
            row->evidence.latest.ssid_length > CERBERUS_WIFI_BEACON_SSID_BYTES) err = ESP_ERR_INVALID_RESPONSE;
        for (size_t v = 0; err == ESP_OK && v < CERBERUS_WIFI_MULTISSID_THRESHOLD; ++v)
            if (row->evidence.variants[v].length > CERBERUS_WIFI_BEACON_SSID_BYTES) err = ESP_ERR_INVALID_RESPONSE;
    }
    if (err != ESP_OK) { multissid_ui_error("RESULTS", err); return; }
    memcpy(multissid_ui_store->rows, multissid_ui_store->staging, page.copied * sizeof(multissid_ui_store->rows[0]));
    multissid_ui_page = page;
    multissid_ui_rows_ms = (uint32_t)(esp_timer_get_time() / 1000U);
    multissid_ui_recovered("RESULTS");
    multissid_ui_recovered("DISPLAY");
}

static bool multissid_finish_exit(void)
{
    esp_err_t err = cerberus_s3_multissid_release();
    if (err != ESP_OK) { multissid_ui_error("CLEANUP", err); return false; }
    heap_caps_free(multissid_ui_store); multissid_ui_store = NULL;
    memset(&multissid_ui_page, 0, sizeof(multissid_ui_page));
    multissid_ui_open = multissid_ui_exit_pending = false;
    return true;
}

static void multissid_request_stop(void)
{
    multissid_ui_follow_pending = false;
    cerberus_s3_multissid_stop();
    (void)cerberus_s3_multissid_status(&multissid_ui_status);
}

static bool multissid_back(void)
{
    if (multissid_ui_detail) {
        if (multissid_ui_status.following || multissid_ui_follow_pending) {
            multissid_request_stop();
            if (multissid_busy()) { multissid_ui_list_pending = true; return false; }
        }
        multissid_ui_detail = false;
        multissid_refresh_rows();
        return false;
    }
    multissid_ui_exit_pending = true;
    multissid_request_stop();
    if (multissid_busy()) return false;
    return multissid_finish_exit();
}

static void multissid_start(void)
{
    if (!multissid_supported()) { snprintf(multissid_ui_message, sizeof(multissid_ui_message), "C5 BEACON SCAN UNAVAILABLE"); return; }
    esp_err_t err = cerberus_s3_multissid_start(multissid_ui_save);
    if (err != ESP_OK) { multissid_ui_error("START", err); return; }
    multissid_ui_message[0] = '\0';
    multissid_ui_offset = 0U;
    memset(&multissid_ui_page, 0, sizeof(multissid_ui_page));
    memset(&multissid_ui_signal, 0, sizeof(multissid_ui_signal));
    (void)cerberus_s3_multissid_status(&multissid_ui_status);
}

static void multissid_start_follow(void)
{
    if (!multissid_ui_store || !multissid_ui_detail) return;
    multissid_ui_follow_pending = false;
    esp_err_t err = cerberus_s3_multissid_follow(&multissid_ui_store->selected, multissid_ui_save);
    if (err != ESP_OK) { multissid_ui_error("FOLLOW", err); return; }
    multissid_ui_message[0] = '\0';
    memset(&multissid_ui_signal, 0, sizeof(multissid_ui_signal));
    (void)cerberus_s3_multissid_status(&multissid_ui_status);
}

static bool multissid_tick(uint32_t now, bool visible)
{
    if (!multissid_ui_open) return false;
    if (!visible) {
        multissid_ui_exit_pending = true;
        multissid_request_stop();
    }
    if ((uint32_t)(now - multissid_ui_last_poll) < 500U) return false;
    multissid_ui_last_poll = now;
    esp_err_t err = cerberus_s3_multissid_status(&multissid_ui_status);
    if (err != ESP_OK) { multissid_ui_error("STATUS", err); return visible; }
    multissid_ui_recovered("STATUS");
    if (!multissid_ui_status.capture.busy) {
        if (multissid_ui_exit_pending) {
            if (multissid_finish_exit() && current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MULTISSID)
                wifi_mode = UI_WIFI_MODE_MENU;
            return visible;
        }
        if (multissid_ui_list_pending) {
            multissid_ui_list_pending = false; multissid_ui_detail = false;
        } else if (multissid_ui_follow_pending) multissid_start_follow();
    }
    if (multissid_ui_detail) {
        cerberus_s3_multissid_follow_t signal = {0};
        err = cerberus_s3_multissid_follow_status(&signal);
        if (err == ESP_OK && signal.valid && multissid_ui_store && multissid_ui_status.following &&
            signal.generation == multissid_ui_status.capture.generation &&
            memcmp(signal.latest.transmitter, multissid_ui_store->selected.evidence.latest.transmitter, 6U) == 0)
            multissid_ui_signal = signal;
        else memset(&multissid_ui_signal, 0, sizeof(multissid_ui_signal));
    } else multissid_refresh_rows();
    return visible;
}

static esp_err_t multissid_line(uint16_t y, const char *line, uint16_t color)
{
    char bounded[50]; snprintf(bounded, sizeof(bounded), "%.49s", line);
    return draw_centered(y, bounded, color, 1);
}

static const char *multissid_phase(void)
{
    if (multissid_ui_exit_pending || multissid_ui_list_pending) return "STOPPING / WAIT FOR CLEANUP";
    if (multissid_ui_follow_pending) return "STOPPING SCAN / FOLLOW NEXT";
    switch (multissid_ui_status.capture.state) {
    case CERBERUS_S3_CAPTURE_STARTING: return "STARTING";
    case CERBERUS_S3_CAPTURE_RUNNING: return multissid_ui_status.following ? "FOLLOWING / RECEIVE ONLY" : "LISTENING / 2.4 + 5 GHZ HOPPING";
    case CERBERUS_S3_CAPTURE_STOPPING: return "STOPPING / DRAINING";
    case CERBERUS_S3_CAPTURE_SAVED: return "STOPPED / PCAP SAVED";
    case CERBERUS_S3_CAPTURE_COMPLETE: return "STOPPED / ANALYSIS RETAINED";
    case CERBERUS_S3_CAPTURE_ERROR: return "STOPPED WITH ERROR";
    default: return "READY / RECEIVE ONLY";
    }
}

static esp_err_t multissid_draw_detail(void)
{
    if (!multissid_ui_store) return ESP_ERR_INVALID_STATE;
    const cerberus_s3_multissid_row_t *selected = &multissid_ui_store->selected;
    const cerberus_wifi_multissid_record_t *record = &selected->evidence;
    char line[96], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(54, "SSID VARIATION", UI_TEXT, 2), TAG, "multi title");
    cerberus_privacy_format_wifi_identifier(record->latest.transmitter, privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "TX CLAIM %s", identifier);
    ESP_RETURN_ON_ERROR(multissid_line(78, line, UI_TEXT), TAG, "multi transmitter");
    cerberus_privacy_format_wifi_identifier(record->latest.bssid, privacy_addresses_masked(), identifier, sizeof(identifier));
    snprintf(line, sizeof(line), "LATEST BSSID %s%s", identifier, record->multiple_bssids ? " / CHANGED" : "");
    ESP_RETURN_ON_ERROR(multissid_line(92, line, UI_MUTED), TAG, "multi bssid");
    snprintf(line, sizeof(line), ">=3 VARIANTS / %lu FRAMES / GENERATION %lu", (unsigned long)record->frames, (unsigned long)selected->generation);
    ESP_RETURN_ON_ERROR(multissid_line(108, line, UI_TEXT), TAG, "multi evidence count");
    snprintf(line, sizeof(line), "FROZEN LAST %d DBM / PEAK %d DBM", record->latest.rssi, record->peak_rssi);
    ESP_RETURN_ON_ERROR(multissid_line(124, line, UI_TEXT), TAG, "multi signal");
    snprintf(line, sizeof(line), "RX CH %u / ADVERTISED %u (0=ABSENT)", record->latest.rx_channel, record->latest.advertised_channel);
    ESP_RETURN_ON_ERROR(multissid_line(140, line, UI_MUTED), TAG, "multi channel");
    uint64_t span = record->latest.observed_us >= record->first_observed_us ? record->latest.observed_us - record->first_observed_us : 0U;
    snprintf(line, sizeof(line), "SOURCE SPAN %llu MS / FROZEN EVIDENCE", (unsigned long long)(span / 1000U));
    ESP_RETURN_ON_ERROR(multissid_line(156, line, UI_MUTED), TAG, "multi span");
    uint32_t elapsed = (uint32_t)((uint32_t)(esp_timer_get_time() / 1000U) - multissid_ui_selected_ms);
    snprintf(line, sizeof(line), "S3 RXD AGE FIRST %lluS / LAST %lluS",
        (unsigned long long)(((uint64_t)selected->first_received_age_ms + elapsed) / 1000U),
        (unsigned long long)(((uint64_t)selected->last_received_age_ms + elapsed) / 1000U));
    ESP_RETURN_ON_ERROR(multissid_line(172, line, UI_MUTED), TAG, "multi delivery ages");
    bool latest = multissid_ui_variant == CERBERUS_WIFI_MULTISSID_THRESHOLD;
    const uint8_t *bytes = latest ? record->latest.ssid : record->variants[multissid_ui_variant].bytes;
    size_t length = latest ? record->latest.ssid_length : record->variants[multissid_ui_variant].length;
    if (latest) snprintf(line, sizeof(line), "LATEST RAW SSID / %u BYTES", (unsigned)length);
    else snprintf(line, sizeof(line), "RETAINED SSID %u/3 / %u BYTES", (unsigned)multissid_ui_variant + 1U, (unsigned)length);
    ESP_RETURN_ON_ERROR(multissid_line(196, line, UI_TEXT), TAG, "multi variant label");
    char escaped[129]; (void)multissid_escape(bytes, length, escaped, sizeof(escaped));
    if (!length) snprintf(escaped, sizeof(escaped), "%s", latest ? "<EMPTY>" : "<HIDDEN-LIKE: EMPTY / ALL ZERO GROUP>");
    for (unsigned row = 0; row < 3U; ++row) {
        size_t offset = row * 44U;
        snprintf(line, sizeof(line), "%.44s", offset < strlen(escaped) ? escaped + offset : "");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(28, (uint16_t)(214U + row * 14U), line, UI_TEXT, UI_BACKGROUND, 1), TAG, "multi exact bytes");
    }
    ESP_RETURN_ON_ERROR(multissid_line(264, multissid_phase(),
        multissid_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_GOOD), TAG, "multi follow phase");
    if (multissid_ui_signal.valid) {
        snprintf(line, sizeof(line), "LIVE %d DBM / PEAK %d / MATCHES %lu", multissid_ui_signal.latest.rssi,
            multissid_ui_signal.peak_rssi, (unsigned long)multissid_ui_signal.matching_beacons);
        ESP_RETURN_ON_ERROR(multissid_line(281, line, UI_TEXT), TAG, "multi follow signal");
        snprintf(line, sizeof(line), "RX %u / ADV %u / UPDATED %luS AGO", multissid_ui_signal.latest.rx_channel,
            multissid_ui_signal.latest.advertised_channel, (unsigned long)(multissid_ui_signal.last_received_age_ms / 1000U));
        ESP_RETURN_ON_ERROR(multissid_line(297, line, UI_MUTED), TAG, "multi follow age");
    } else ESP_RETURN_ON_ERROR(multissid_line(286, "FOLLOW LISTENS FOR THIS TX ADDRESS", UI_MUTED), TAG, "multi follow hint");
    snprintf(line, sizeof(line), "C5 DROP %lu+%lu / TABLE DROP %lu", (unsigned long)multissid_ui_status.capture.dropped_full,
        (unsigned long)multissid_ui_status.capture.dropped_other, (unsigned long)multissid_ui_status.capacity_drops);
    ESP_RETURN_ON_ERROR(multissid_line(318, line, UI_MUTED), TAG, "multi drops");
    ESP_RETURN_ON_ERROR(multissid_line(336, "SHARED ADDRESS DOES NOT PROVE DEVICE ID", UI_MUTED), TAG, "multi identity limit");
    const char *message = multissid_ui_message[0] ? multissid_ui_message :
        multissid_ui_status.capture.error_code != ESP_OK ? esp_err_to_name(multissid_ui_status.capture.error_code) : "S3 RECEIVED AGE IS NOT RF AGE";
    ESP_RETURN_ON_ERROR(multissid_line(350, message,
        multissid_ui_message[0] || multissid_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_MUTED), TAG, "multi note");
    snprintf(line, sizeof(line), "SSID %u/4 >", multissid_ui_variant + 1U);
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,366,96,42}, line, 1, true, UI_WIDGET_THEME), TAG, "multi variants");
    bool pending = multissid_ui_follow_pending || multissid_ui_list_pending || multissid_ui_exit_pending;
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){112,366,96,42}, "FOLLOW", 1,
        !pending && !(multissid_ui_status.following && multissid_ui_status.capture.busy) && multissid_supported(), UI_WIDGET_THEME), TAG, "multi follow");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){216,366,96,42}, "STOP", 1,
        multissid_ui_status.capture.busy || multissid_ui_follow_pending, UI_WIDGET_THEME);
}

static esp_err_t multissid_draw(void)
{
    if (multissid_ui_detail) return multissid_draw_detail();
    char line[96], identifier[24];
    ESP_RETURN_ON_ERROR(draw_centered(54, "MULTISSID SCAN", UI_TEXT, 2), TAG, "multi title");
    ESP_RETURN_ON_ERROR(multissid_line(79, multissid_phase(),
        multissid_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_GOOD), TAG, "multi phase");
    snprintf(line, sizeof(line), "%u GROUPS (>=3 SSIDS) / %lu BEACONS", multissid_ui_page.total, (unsigned long)multissid_ui_status.valid_beacons);
    ESP_RETURN_ON_ERROR(multissid_line(98, line, UI_TEXT), TAG, "multi counts");
    snprintf(line, sizeof(line), "C5 DROP %lu+%lu / TABLE DROP %lu", (unsigned long)multissid_ui_status.capture.dropped_full,
        (unsigned long)multissid_ui_status.capture.dropped_other, (unsigned long)multissid_ui_status.capacity_drops);
    ESP_RETURN_ON_ERROR(multissid_line(115, line, UI_MUTED), TAG, "multi drops");
    if (!multissid_ui_page.copied) {
        ESP_RETURN_ON_ERROR(multissid_line(166, "C5 LISTENS / S3 ANALYZES IN PSRAM", UI_TEXT), TAG, "multi split");
        ESP_RETURN_ON_ERROR(multissid_line(188, "NO AP SELECTION REQUIRED", UI_MUTED), TAG, "multi no target");
        ESP_RETURN_ON_ERROR(multissid_line(210, "ROWS APPEAR AFTER 3 SSID VARIANTS", UI_MUTED), TAG, "multi threshold");
        ESP_RETURN_ON_ERROR(multissid_line(232, "PER OBSERVED TRANSMITTER ADDRESS", UI_MUTED), TAG, "multi key");
        ESP_RETURN_ON_ERROR(multissid_line(270, multissid_ui_save ? "SAVE ON / SD PCAP RECORDING" : "SAVE OFF / NO SD CARD REQUIRED", UI_TEXT), TAG, "multi SD");
    }
    for (size_t i = 0; i < multissid_ui_page.copied; ++i) {
        const cerberus_s3_multissid_row_t *row = &multissid_ui_store->rows[i];
        const cerberus_wifi_multissid_record_t *record = &row->evidence;
        uint16_t y = (uint16_t)(138U + i * 48U);
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8,y,304,44,UI_PANEL), TAG, "multi row");
        cerberus_privacy_format_wifi_identifier(record->latest.transmitter, privacy_addresses_masked(), identifier, sizeof(identifier));
        snprintf(line, sizeof(line), "TX %s / >=3 SSIDS", identifier);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+4,line,UI_TEXT,UI_PANEL,1), TAG, "multi row id");
        snprintf(line, sizeof(line), "RX %u / ADV %u / LAST %d / PEAK %d", record->latest.rx_channel, record->latest.advertised_channel, record->latest.rssi, record->peak_rssi);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+17,line,UI_MUTED,UI_PANEL,1), TAG, "multi row signal");
        snprintf(line, sizeof(line), "S3 UPDATED %luS AGO / %lu FRAMES", (unsigned long)(row->last_received_age_ms/1000U), (unsigned long)record->frames);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(16,y+30,line,UI_MUTED,UI_PANEL,1), TAG, "multi row time");
    }
    if (multissid_ui_status.capture.file_name[0])
        snprintf(line, sizeof(line), "SD PCAP: %.31s", multissid_ui_status.capture.file_name);
    else snprintf(line, sizeof(line), "3 IS A LOWER BOUND / TAP FOR EXACT BYTES");
    ESP_RETURN_ON_ERROR(multissid_line(337, line, UI_MUTED), TAG, "multi list hint");
    const char *message = multissid_ui_message[0] ? multissid_ui_message :
        multissid_ui_status.capture.error_code != ESP_OK ? esp_err_to_name(multissid_ui_status.capture.error_code) :
        multissid_supported() ? "RECEIVE ONLY / MANUAL STOP" : "C5 BEACON SCAN UNAVAILABLE";
    ESP_RETURN_ON_ERROR(multissid_line(351, message,
        multissid_ui_message[0] || multissid_ui_status.capture.error_code != ESP_OK ? UI_BAD : UI_MUTED), TAG, "multi list note");
    bool busy = multissid_ui_status.capture.busy;
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8,366,68,42}, "PREV", 1, multissid_ui_offset > 0U, UI_WIDGET_THEME), TAG, "multi previous");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){84,366,68,42}, busy ? "STOP" : "START", 1,
        busy || (!multissid_ui_exit_pending && multissid_supported()), UI_WIDGET_THEME), TAG, "multi start stop");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){160,366,68,42}, "NEXT", 1,
        multissid_ui_offset + multissid_ui_page.copied < multissid_ui_page.total, UI_WIDGET_THEME), TAG, "multi next");
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){236,366,76,42}, multissid_ui_save ? "SAVE ON" : "SAVE OFF", 1,
        !busy && !multissid_ui_exit_pending && (cerberus_storage_is_mounted() || multissid_ui_save), UI_WIDGET_THEME);
}

static void multissid_tap(uint16_t x, uint16_t y)
{
    if (multissid_ui_exit_pending || multissid_ui_list_pending) return;
    if (multissid_ui_detail) {
        if (y < 366U || y >= 408U) return;
        if (x >= 8U && x < 104U) multissid_ui_variant = (uint8_t)((multissid_ui_variant + 1U) % 4U);
        else if (x >= 112U && x < 208U && !multissid_ui_follow_pending &&
            !(multissid_ui_status.following && multissid_ui_status.capture.busy) && multissid_supported()) {
            if (multissid_busy()) { cerberus_s3_multissid_stop(); multissid_ui_follow_pending = true; }
            else multissid_start_follow();
        } else if (x >= 216U && x < 312U) multissid_request_stop();
        return;
    }
    if (x >= 8U && x < 312U && y >= 138U && y < 326U && (y - 138U) % 48U < 44U) {
        unsigned row = (y - 138U) / 48U;
        if (multissid_ui_store && row < multissid_ui_page.copied) {
            multissid_ui_store->selected = multissid_ui_store->rows[row];
            multissid_ui_selected_ms = multissid_ui_rows_ms;
            multissid_ui_variant = 0U; multissid_ui_detail = true;
            memset(&multissid_ui_signal, 0, sizeof(multissid_ui_signal));
        }
        return;
    }
    if (y < 366U || y >= 408U) return;
    if (x >= 8U && x < 76U && multissid_ui_offset >= 4U) { multissid_ui_offset -= 4U; multissid_refresh_rows(); }
    else if (x >= 84U && x < 152U) {
        if (multissid_busy()) multissid_request_stop(); else multissid_start();
    } else if (x >= 160U && x < 228U && multissid_ui_offset + multissid_ui_page.copied < multissid_ui_page.total) {
        multissid_ui_offset += 4U; multissid_refresh_rows();
    } else if (x >= 236U && x < 312U && !multissid_busy() && (cerberus_storage_is_mounted() || multissid_ui_save))
        multissid_ui_save = !multissid_ui_save;
}
