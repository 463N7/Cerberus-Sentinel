/* Private integration on the existing UI task. C5 Wi-Fi and S3 BLE each own
 * one bounded passive sweep and can run at the same time. */
typedef enum { WATCH_LIVE, WATCH_HISTORY, WATCH_DETAILS, WATCH_DEMO, WATCH_FILTERS, WATCH_WHY,
    WATCH_OPTIONS, WATCH_ACTIONS, WATCH_SIGNAL } watch_page_t;
static sentinel_watch_t watch_state;
static sentinel_watch_view_t watch_view;
static sentinel_watch_entry_t watch_detail;
static char watch_detail_vendor[RECON_DB_VENDOR_SIZE];
static recon_db_result_t watch_detail_vendor_result = RECON_DB_UNAVAILABLE;
static cerberus_c5_ble_advertisement_t watch_advertisements[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_watch_observation_t watch_extra[CERBERUS_WATCH_MAX_OBSERVATIONS];
static cerberus_watch_scan_status_payload_t watch_extra_status;
static cerberus_ble_activity_payload_t watch_activity_sample;
static sentinel_watch_activity_t watch_activity_state;
static bool watch_extra_unavailable[2], watch_activity_available;
static watch_page_t watch_page;
static bool watch_initialized, watch_paused, watch_capture_failed[2];
static uint8_t watch_demo_scene, watch_history_page, watch_filters_page;
static uint32_t watch_seen_capture[2], watch_last_draw, watch_last_read[2];
static uint16_t watch_last_count[2];
static bool watch_limited_capture[2];
static bool watch_capture_waiting[2];
static uint32_t watch_capture_wait_since[2];
static char watch_phase_text[52], watch_detail_text[52];
static char recon_message[49];
static unsigned recon_confirm;
static void watch_notify(bool queued, const sentinel_watch_match_t *match, uint32_t now);

static const sentinel_watch_category_t watch_categories[] = {
    SENTINEL_WATCH_AXON, SENTINEL_WATCH_FLOCK, SENTINEL_WATCH_WELLNESS,
    SENTINEL_WATCH_FINDMY, SENTINEL_WATCH_TRACKER, SENTINEL_WATCH_CAMERA,
    SENTINEL_WATCH_FLIPPER, SENTINEL_WATCH_MESHTASTIC, SENTINEL_WATCH_SAMSUNG,
    SENTINEL_WATCH_BEACON, SENTINEL_WATCH_PWNAGOTCHI, SENTINEL_WATCH_DRONE,
    SENTINEL_WATCH_BLE_ACTIVITY,
};
#define WATCH_FILTER_COUNT (sizeof(watch_categories) / sizeof(watch_categories[0]))

static const char *watch_radio_name(sentinel_watch_radio_t radio)
{
    return radio == SENTINEL_WATCH_RADIO_ENVIRONMENT ? "AREA" :
        radio == SENTINEL_WATCH_RADIO_BLE ? "BLE" : "WI-FI";
}

static bool watch_raw_supported(void)
{
    return live_records[SENTINEL_SCAN_BLE].valid;
}

static bool watch_extra_supported(sentinel_scan_tool_t tool)
{
    if (tool == SENTINEL_SCAN_BLE) return live_records[SENTINEL_SCAN_BLE].valid;
    return c5_online && c5_status.firmware_version >= CERBERUS_WATCH_MIN_FIRMWARE &&
        (c5_status.capabilities & CERBERUS_C5_CAP_WATCH_OBSERVATIONS) != 0;
}

static bool watch_activity_supported(void)
{
    return live_records[SENTINEL_SCAN_BLE].valid;
}

static sentinel_scan_tool_t watch_requested_tool(void)
{
    return watch_paused || watch_page == WATCH_DEMO ||
        (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ) == 0 ?
        SENTINEL_SCAN_NONE : SENTINEL_SCAN_WIFI;
}

static bool watch_ble_requested(void)
{
    return !watch_paused && watch_page != WATCH_DEMO;
}

static bool watch_keeps_awake(void)
{
    return current_page == UI_PAGE_WATCH && (!watch_paused || watch_page == WATCH_DEMO);
}

static sentinel_watch_scene_t watch_scene(sentinel_watch_category_t category)
{
    switch (category) {
    case SENTINEL_WATCH_AXON: return SENTINEL_WATCH_SCENE_AXON;
    case SENTINEL_WATCH_FLOCK: return SENTINEL_WATCH_SCENE_FLOCK;
    case SENTINEL_WATCH_WELLNESS: return SENTINEL_WATCH_SCENE_WELLNESS;
    case SENTINEL_WATCH_FINDMY: return SENTINEL_WATCH_SCENE_FINDMY;
    case SENTINEL_WATCH_TRACKER: return SENTINEL_WATCH_SCENE_TRACKER;
    case SENTINEL_WATCH_CAMERA: return SENTINEL_WATCH_SCENE_CAMERA;
    case SENTINEL_WATCH_SMART_GLASSES: return SENTINEL_WATCH_SCENE_SMART_GLASSES;
    case SENTINEL_WATCH_FLIPPER: return SENTINEL_WATCH_SCENE_FLIPPER;
    case SENTINEL_WATCH_MESHTASTIC: return SENTINEL_WATCH_SCENE_MESHTASTIC;
    case SENTINEL_WATCH_SAMSUNG: return SENTINEL_WATCH_SCENE_SAMSUNG;
    case SENTINEL_WATCH_BEACON: return SENTINEL_WATCH_SCENE_BEACON;
    case SENTINEL_WATCH_PWNAGOTCHI: return SENTINEL_WATCH_SCENE_PWNAGOTCHI;
    case SENTINEL_WATCH_DRONE: return SENTINEL_WATCH_SCENE_DRONE;
    case SENTINEL_WATCH_BLE_ACTIVITY: return SENTINEL_WATCH_SCENE_BLE_ACTIVITY;
    default: return SENTINEL_WATCH_SCENE_GUARDIAN;
    }
}

static esp_err_t watch_stage_extra(sentinel_scan_tool_t tool, uint8_t generation)
{
    unsigned slot = tool == SENTINEL_SCAN_WIFI ? 0U : 1U;
    uint8_t radio = tool == SENTINEL_SCAN_BLE ? CERBERUS_WATCH_RADIO_BLE : CERBERUS_WATCH_RADIO_WIFI;
    memset(&watch_extra_status, 0, sizeof(watch_extra_status));
    memset(&watch_activity_sample, 0, sizeof(watch_activity_sample));
    watch_extra_unavailable[slot] = !watch_extra_supported(tool);
    watch_activity_available = false;
    if (watch_extra_supported(tool)) {
        esp_err_t err = tool == SENTINEL_SCAN_BLE ?
            cerberus_s3_ble_scanner_get_watch_status(generation, &watch_extra_status) :
            c5_spi_link_get_watch_status(radio, generation, &watch_extra_status);
        if (err != ESP_OK) return err;
        if (watch_extra_status.radio != radio || watch_extra_status.generation != generation ||
            watch_extra_status.state != CERBERUS_WATCH_COMPLETE ||
            watch_extra_status.stored_count > CERBERUS_WATCH_MAX_OBSERVATIONS) return ESP_ERR_INVALID_RESPONSE;
        watch_extra_unavailable[slot] = (watch_extra_status.flags & CERBERUS_WATCH_CAPTURE_UNAVAILABLE) != 0;
        if (watch_extra_unavailable[slot] && watch_extra_status.stored_count) return ESP_ERR_INVALID_RESPONSE;
        for (uint8_t i = 0; i < watch_extra_status.stored_count; ++i) {
            err = tool == SENTINEL_SCAN_BLE ?
                cerberus_s3_ble_scanner_get_watch_result(generation, i, &watch_extra[i]) :
                c5_spi_link_get_watch_result(radio, generation, i, &watch_extra[i]);
            if (err != ESP_OK) return err;
        }
        cerberus_watch_scan_status_payload_t end = {0};
        err = tool == SENTINEL_SCAN_BLE ?
            cerberus_s3_ble_scanner_get_watch_status(generation, &end) :
            c5_spi_link_get_watch_status(radio, generation, &end);
        if (err != ESP_OK) return err;
        if (memcmp(&end, &watch_extra_status, sizeof(end)) != 0) return ESP_ERR_INVALID_STATE;
    }
    if (tool == SENTINEL_SCAN_BLE && watch_activity_supported()) {
        esp_err_t err = cerberus_s3_ble_scanner_get_activity(generation, &watch_activity_sample);
        if (err != ESP_OK) return err;
        if (watch_activity_sample.generation != generation || watch_activity_sample.state != CERBERUS_WATCH_COMPLETE)
            return ESP_ERR_INVALID_RESPONSE;
        watch_activity_available = (watch_activity_sample.flags & CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED) == 0;
    }
    return ESP_OK;
}

static bool watch_extra_has_drone(const uint8_t address[6], uint8_t address_type)
{
    for (uint8_t i = 0; i < watch_extra_status.stored_count; ++i)
        if (watch_extra[i].kind == SENTINEL_WATCH_PROTOCOL_DRONE &&
            watch_extra[i].address_type == address_type && !memcmp(watch_extra[i].address, address, 6)) return true;
    return false;
}

static void watch_consume_extra(sentinel_scan_tool_t tool, uint32_t now)
{
    for (uint8_t i = 0; i < watch_extra_status.stored_count; ++i) {
        const cerberus_watch_observation_t *row = &watch_extra[i];
        sentinel_watch_match_t match;
        if (row->kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI) {
            match = (sentinel_watch_match_t){SENTINEL_WATCH_PWNAGOTCHI, SENTINEL_WATCH_CONFIDENCE_HIGH,
                "wifi.pwnagotchi-peer", "Pwnagotchi peer hint", "Validated custom Wi-Fi peer advertisement"};
        } else if (row->kind == SENTINEL_WATCH_PROTOCOL_DRONE) {
            match = (sentinel_watch_match_t){SENTINEL_WATCH_DRONE, SENTINEL_WATCH_CONFIDENCE_HIGH,
                row->transport == SENTINEL_WATCH_TRANSPORT_BLE ? "drone.remoteid-ble" :
                row->transport == SENTINEL_WATCH_TRANSPORT_WIFI_NAN ? "drone.remoteid-nan" : "drone.remoteid-beacon",
                "Drone Remote ID", "Validated Remote ID broadcast / self-reported"};
        } else continue;
        sentinel_watch_details_t details = {0};
        details.protocol.kind = row->kind;
        details.protocol.transport = row->transport;
        details.protocol.flags = row->flags;
        details.protocol.altitude_kind = row->altitude_kind;
        details.protocol.latitude_e7 = row->latitude_e7;
        details.protocol.longitude_e7 = row->longitude_e7;
        details.protocol.altitude_dm = row->altitude_dm;
        memcpy(details.protocol.id, row->id, sizeof(details.protocol.id));
        bool queued = sentinel_watch_observe_details(&watch_state,
            tool == SENTINEL_SCAN_BLE ? SENTINEL_WATCH_RADIO_BLE : SENTINEL_WATCH_RADIO_WIFI,
            row->address, row->address_type, row->id, strnlen(row->id, sizeof(row->id)), row->rssi,
            &match, &details, now);
        watch_notify(queued, &match, now);
    }
    if (watch_activity_available) {
        sentinel_watch_details_t details = {.has_activity = true, .activity = {
            watch_activity_sample.duration_ms, watch_activity_sample.reports, watch_activity_sample.valid_reports,
            watch_activity_sample.identities, watch_activity_sample.peak_reports_per_second}};
        sentinel_watch_match_t match;
        if (sentinel_watch_activity_observe(&watch_activity_state, &details.activity, now, &match)) {
            /* A separate environmental identity never impersonates a device. */
            static const uint8_t environment[6] = {0};
            bool queued = sentinel_watch_observe_details(&watch_state, SENTINEL_WATCH_RADIO_ENVIRONMENT,
                environment, 0, "Bluetooth environment", 21, 0, &match, &details, now);
            watch_notify(queued, &match, now);
        }
    }
}

static void watch_notify(bool queued, const sentinel_watch_match_t *match, uint32_t now)
{
    if (!queued) return;
    char notification[CERBERUS_NOTIFICATION_TEXT_MAX];
    snprintf(notification, sizeof(notification), "RECON: %.31s", match->label);
    cerberus_ui_notification_add(now, notification);
}

static bool watch_consume_radio(sentinel_scan_tool_t tool, uint32_t now)
{
    unsigned slot = tool == SENTINEL_SCAN_WIFI ? 0U : 1U;
    sentinel_scan_session_t *session = live_session_for(tool);
    if (watch_paused || watch_page == WATCH_DEMO ||
        (tool == SENTINEL_SCAN_WIFI && !c5_online) ||
        !session->completed || session->desired != tool || session->active != SENTINEL_SCAN_NONE) {
        watch_capture_waiting[slot] = false;
        return false;
    }
    live_scan_record_t *record = &live_records[tool];
    if (!record->captured || record->captured_ms == watch_seen_capture[slot] ||
        !record->valid || record->state != SENTINEL_SCAN_COMPLETE || record->saved_generation != record->generation) {
        /* The radio can finish before its ordinary records can be copied.
         * live_observe retries that copy; give it a bounded recovery window
         * without interpreting an old or partially copied inventory. */
        watch_capture_failed[slot] = true;
        if (!watch_capture_waiting[slot]) {
            watch_capture_waiting[slot] = true;
            watch_capture_wait_since[slot] = now;
        } else if ((uint32_t)(now - watch_capture_wait_since[slot]) >= 15000U) {
            sentinel_scan_session_pause(session, true);
        }
        return false;
    }
    watch_capture_waiting[slot] = false;
    if ((uint32_t)(now - watch_last_read[slot]) < 1000U) return false;
    watch_last_read[slot] = now;

    /* Stage all raw packets, then confirm the generation before accepting any
     * matches. A transport failure cannot yield a mixed-generation alert. */
    if (tool == SENTINEL_SCAN_BLE && watch_raw_supported()) {
        esp_err_t err = ESP_OK;
        for (uint16_t i = 0; i < record->count && err == ESP_OK; ++i) {
            err = cerberus_s3_ble_scanner_get_advertisement(record->saved_generation, i,
                &watch_advertisements[i]);
            if (err == ESP_OK && (watch_advertisements[i].address_type != live_store->ble[i].address_type ||
                memcmp(watch_advertisements[i].address, live_store->ble[i].address, 6) != 0)) err = ESP_ERR_INVALID_RESPONSE;
        }
        cerberus_ble_scan_status_payload_t end = {0};
        if (err == ESP_OK) err = cerberus_s3_ble_scanner_get_status(&end);
        if (err == ESP_OK && (end.state != CERBERUS_BLE_SCAN_COMPLETE ||
            end.generation != record->saved_generation || end.stored_count != record->count)) err = ESP_ERR_INVALID_STATE;
        if (err != ESP_OK) {
            watch_capture_failed[slot] = true;
            /* Bounded retry: preserve evidence instead of silently skipping a
             * failed packet transfer and claiming full monitoring coverage. */
            if ((uint32_t)(now - record->captured_ms) > 15000U)
                sentinel_scan_session_pause(session, true);
            return false;
        }
    }
    if (watch_stage_extra(tool, record->saved_generation) != ESP_OK) {
        watch_capture_failed[slot] = true;
        if ((uint32_t)(now - record->captured_ms) > 15000U)
            sentinel_scan_session_pause(session, true);
        return false;
    }
    watch_capture_failed[slot] = false;
    watch_last_count[slot] = record->count;
    watch_limited_capture[slot] = tool == SENTINEL_SCAN_BLE ?
        (!watch_raw_supported() || ble_scan_status.dropped_reports != 0) :
        wifi_scan_status.total_count > wifi_scan_status.stored_count;
    watch_limited_capture[slot] = watch_limited_capture[slot] ||
        (watch_extra_status.flags & CERBERUS_WATCH_CAPTURE_OVERFLOW) != 0;
    for (uint16_t i = 0; i < record->count; ++i) {
        sentinel_watch_match_t match = {0};
        bool detected, queued;
        if (tool == SENTINEL_SCAN_WIFI) {
            const cerberus_c5_wifi_result_t *row = &live_store->wifi[i];
            sentinel_watch_wifi_observation_t observation = {
                .ssid = (const uint8_t *)row->ssid, .ssid_length = strlen(row->ssid)};
            memcpy(observation.bssid, row->bssid, 6);
            detected = sentinel_watch_classify_wifi(&observation, &match);
            queued = sentinel_watch_observe(&watch_state, SENTINEL_WATCH_RADIO_WIFI,
                row->bssid, 0, row->ssid, observation.ssid_length, row->rssi, detected ? &match : NULL, now);
        } else {
            const cerberus_c5_ble_result_t *row = &live_store->ble[i];
            sentinel_watch_ble_observation_t observation = {
                .name = (const uint8_t *)row->name, .name_length = strlen(row->name),
                .address_type = row->address_type};
            memcpy(observation.address, row->address, 6);
            if (watch_raw_supported()) {
                observation.advertisement = watch_advertisements[i].data;
                observation.advertisement_length = watch_advertisements[i].length;
                observation.advertisement_flags = watch_advertisements[i].flags;
            }
            detected = sentinel_watch_classify_ble(&observation, &match);
            if (detected && match.category == SENTINEL_WATCH_DRONE && watch_extra_has_drone(row->address, row->address_type))
                continue; /* The richer same-sweep record below owns this observation. */
            queued = sentinel_watch_observe(&watch_state, SENTINEL_WATCH_RADIO_BLE,
                row->address, row->address_type, row->name, observation.name_length, row->rssi, detected ? &match : NULL, now);
        }
        watch_notify(queued, &match, now);
    }
    watch_consume_extra(tool, now);
    if (watch_state.sweeps < UINT32_MAX) ++watch_state.sweeps;
    watch_seen_capture[slot] = record->captured_ms;
    /* Each radio rearms after its own complete generation has been consumed.
     * A delayed C5 sweep cannot hold the S3 BLE observer idle, or vice versa. */
    sentinel_scan_session_pause(session, false);
    return true;
}

static bool watch_consume(uint32_t now)
{
    bool changed = watch_consume_radio(SENTINEL_SCAN_WIFI, now);
    return watch_consume_radio(SENTINEL_SCAN_BLE, now) || changed;
}

static void watch_select_detail(const sentinel_watch_entry_t *entry)
{
    watch_detail = *entry;
    watch_detail_vendor[0] = '\0';
    if (entry->radio == SENTINEL_WATCH_RADIO_BLE)
        watch_detail_vendor_result = recon_store_ble_vendor(entry->address,
            entry->address_type, watch_detail_vendor);
    else if (entry->radio == SENTINEL_WATCH_RADIO_WIFI)
        watch_detail_vendor_result = recon_store_vendor(entry->address, watch_detail_vendor);
    else watch_detail_vendor_result = RECON_DB_PRIVATE_ADDRESS;
}

static esp_err_t watch_draw_history(uint32_t now)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "watch history clear");
    ESP_RETURN_ON_ERROR(draw_header("RECON LOG"), TAG, "watch history header");
    char line[52];
    snprintf(line, sizeof(line), "%u SESSION HINTS / %lu SWEEPS", watch_state.history_count, (unsigned long)watch_state.sweeps);
    ESP_RETURN_ON_ERROR(draw_centered(60, line, UI_GOOD, 1), TAG, "watch history count");
    ESP_RETURN_ON_ERROR(draw_centered(79, "BROADCAST HINTS / NOT VERIFIED IDENTITIES", UI_MUTED, 1), TAG, "watch history note");
    for (size_t row = 0; row < 4; ++row) {
        const sentinel_watch_entry_t *entry = sentinel_watch_history(&watch_state, watch_history_page * 4U + row);
        if (!entry) break;
        unsigned y = 104U + row * 68U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304, 60, UI_PANEL), TAG, "watch row");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 8, entry->match.label, UI_TEXT, UI_PANEL, 1), TAG, "watch label");
        if (entry->radio == SENTINEL_WATCH_RADIO_ENVIRONMENT)
            snprintf(line, sizeof(line), "AREA ACTIVITY / EXPERIMENTAL");
        else snprintf(line, sizeof(line), "%s / %s / %d DBM", watch_radio_name(entry->radio),
            sentinel_watch_confidence_name(entry->match.confidence), entry->rssi);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 25, line, UI_GOOD, UI_PANEL, 1), TAG, "watch confidence");
        snprintf(line, sizeof(line), "%s%u SWEEPS / %lus AGO",
            sentinel_watch_is_ignored(&watch_state, entry) ? "IGNORED / " : "SEEN ",
            entry->sightings, (unsigned long)((uint32_t)(now - entry->last_ms) / 1000U));
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 42, line, UI_MUTED, UI_PANEL, 1), TAG, "watch age");
    }
    if (!watch_state.history_count) ESP_RETURN_ON_ERROR(draw_centered(192, "NO SIGNATURE MATCHES YET", UI_TEXT, 1), TAG, "watch empty");
    ESP_RETURN_ON_ERROR(draw_button(60, 380, 200, 34, "CATEGORIES"), TAG, "watch categories");
    ESP_RETURN_ON_ERROR(draw_button(8, 426, 94, 44, "BACK"), TAG, "watch history back");
    ESP_RETURN_ON_ERROR(draw_button(112, 426, 94, 44, "PREV"), TAG, "watch history prev");
    return draw_button(216, 426, 96, 44, "NEXT");
}

static esp_err_t watch_draw_details(uint32_t now)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "watch detail clear");
    ESP_RETURN_ON_ERROR(draw_header("BROADCAST EVIDENCE"), TAG, "watch detail header");
    char line[56], identifier[24];
    if (watch_detail.radio == SENTINEL_WATCH_RADIO_ENVIRONMENT)
        snprintf(identifier, sizeof(identifier), "AREA MEASUREMENT");
    else if (watch_detail.radio == SENTINEL_WATCH_RADIO_BLE)
        cerberus_privacy_format_ble_identifier(watch_detail.address, privacy_addresses_masked(), identifier, sizeof(identifier));
    else cerberus_privacy_format_wifi_identifier(watch_detail.address, privacy_addresses_masked(), identifier, sizeof(identifier));
    ESP_RETURN_ON_ERROR(draw_centered(75, watch_detail.match.label, UI_GOOD, 1), TAG, "watch detail label");
    ESP_RETURN_ON_ERROR(draw_centered(102, watch_detail.match.evidence, UI_TEXT, 1), TAG, "watch detail evidence");
    snprintf(line, sizeof(line), "RULE: %.42s", watch_detail.match.rule_id);
    ESP_RETURN_ON_ERROR(draw_centered(130, line, UI_MUTED, 1), TAG, "watch detail rule");
    snprintf(line, sizeof(line), "%s EVIDENCE / %s", sentinel_watch_confidence_name(watch_detail.match.confidence),
        watch_detail.radio == SENTINEL_WATCH_RADIO_ENVIRONMENT ? "AREA ACTIVITY" :
        watch_detail.radio == SENTINEL_WATCH_RADIO_BLE ? "PASSIVE BLE" : "PASSIVE WI-FI");
    ESP_RETURN_ON_ERROR(draw_centered(160, line, UI_GOOD, 1), TAG, "watch detail confidence");
    ESP_RETURN_ON_ERROR(draw_centered(191, identifier, UI_TEXT, 1), TAG, "watch detail identifier");
    ESP_RETURN_ON_ERROR(draw_centered(216, watch_detail.name[0] ? watch_detail.name : "NO ADVERTISED NAME", UI_TEXT, 1), TAG, "watch detail name");
    snprintf(line, sizeof(line), "%u SWEEPS / LAST %lus AGO", watch_detail.sightings,
        (unsigned long)((uint32_t)(now - watch_detail.last_ms) / 1000U));
    ESP_RETURN_ON_ERROR(draw_centered(244, line, UI_MUTED, 1), TAG, "watch detail stats");
    if (watch_detail_vendor_result == RECON_DB_FOUND)
        snprintf(line, sizeof(line), "VENDOR %.38s", watch_detail_vendor);
    else if (watch_detail_vendor_result == RECON_DB_PRIVATE_ADDRESS)
        snprintf(line, sizeof(line), "NO OUI / PRIVATE OR AREA ADDRESS");
    else if (watch_detail_vendor_result == RECON_DB_UNKNOWN)
        snprintf(line, sizeof(line), "VENDOR NOT IN SD REGISTRY");
    else snprintf(line, sizeof(line), "SD VENDOR REGISTRY UNAVAILABLE");
    ESP_RETURN_ON_ERROR(draw_centered(260, line, UI_MUTED, 1), TAG, "watch detail vendor");
    const sentinel_watch_protocol_record_t *p = &watch_detail.details.protocol;
    if (p->kind == SENTINEL_WATCH_PROTOCOL_DRONE) {
        if (p->flags & SENTINEL_WATCH_PROTOCOL_HAS_POSITION) {
            /* Decimal strings preserve negative coordinates below one degree. */
            int64_t lat = p->latitude_e7, lon = p->longitude_e7;
            uint32_t alat = (uint32_t)(lat < 0 ? -lat : lat), alon = (uint32_t)(lon < 0 ? -lon : lon);
            snprintf(line, sizeof(line), "LAT %s%lu.%07lu", lat < 0 ? "-" : "", (unsigned long)(alat / 10000000U), (unsigned long)(alat % 10000000U));
            ESP_RETURN_ON_ERROR(draw_centered(272, line, UI_TEXT, 1), TAG, "watch latitude");
            snprintf(line, sizeof(line), "LON %s%lu.%07lu", lon < 0 ? "-" : "", (unsigned long)(alon / 10000000U), (unsigned long)(alon % 10000000U));
        } else snprintf(line, sizeof(line), "POSITION NOT RECEIVED THIS SWEEP");
        ESP_RETURN_ON_ERROR(draw_centered(293, line, UI_TEXT, 1), TAG, "watch longitude");
        if (p->flags & SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE) {
            int64_t alt = p->altitude_dm;
            uint32_t magnitude = (uint32_t)(alt < 0 ? -alt : alt);
            snprintf(line, sizeof(line), "ALT %s%lu.%lu M / %s", alt < 0 ? "-" : "",
                (unsigned long)(magnitude / 10U), (unsigned long)(magnitude % 10U),
                p->altitude_kind == SENTINEL_WATCH_ALTITUDE_GEODETIC ? "WGS84 HAE" : "PRESSURE");
        } else snprintf(line, sizeof(line), "ALTITUDE NOT RECEIVED THIS SWEEP");
        ESP_RETURN_ON_ERROR(draw_centered(314, line, UI_TEXT, 1), TAG, "watch altitude");
        ESP_RETURN_ON_ERROR(draw_centered(339, "REPORTED BY TRANSMITTER / UNVERIFIED", UI_MUTED, 1), TAG, "watch drone claim");
    } else if (watch_detail.details.has_activity) {
        const sentinel_watch_activity_sample_t *s = &watch_detail.details.activity;
        snprintf(line, sizeof(line), "%lu REPORTS / %lu MS WINDOW", (unsigned long)s->reports, (unsigned long)s->duration_ms);
        ESP_RETURN_ON_ERROR(draw_centered(278, line, UI_TEXT, 1), TAG, "watch activity reports");
        snprintf(line, sizeof(line), "%u+ ADDRESSES / PEAK %u PER SECOND", s->identities, s->peak_reports_per_second);
        ESP_RETURN_ON_ERROR(draw_centered(305, line, UI_TEXT, 1), TAG, "watch activity peak");
        ESP_RETURN_ON_ERROR(draw_centered(333, "CROWDED PLACES CAN ALSO TRIGGER THIS", UI_MUTED, 1), TAG, "watch activity caution");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(287, "BROADCASTS CAN BE SHARED OR SPOOFED.", UI_TEXT, 1), TAG, "watch caution");
        ESP_RETURN_ON_ERROR(draw_centered(310, "A MATCH DOES NOT PROVE RECORDING,", UI_MUTED, 1), TAG, "watch caution 2");
        ESP_RETURN_ON_ERROR(draw_centered(330, "OWNERSHIP, OR THAT YOU ARE FOLLOWED.", UI_MUTED, 1), TAG, "watch caution 3");
    }
    ESP_RETURN_ON_ERROR(draw_button(8, 378, 148, 34, "WHY THIS MATCH?"), TAG, "watch why");
    ESP_RETURN_ON_ERROR(draw_button(164, 378, 148, 34, "ACTIONS"), TAG, "recon actions");
    return draw_button(64, 426, 192, 44, "BACK TO RECON");
}

static esp_err_t watch_draw_filters(void)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "watch filters clear");
    ESP_RETURN_ON_ERROR(draw_header("RECON CATEGORIES"), TAG, "watch filters header");
    ESP_RETURN_ON_ERROR(draw_centered(64, "TAP: ALERT > OFF > HISTORY > ALERT", UI_MUTED, 1), TAG, "watch filters help");
    static const char *const modes[] = {"OFF", "HISTORY ONLY", "ANIMATED ALERT"};
    for (size_t row = 0; row < 4; ++row) {
        size_t index = watch_filters_page * 4U + row;
        if (index >= WATCH_FILTER_COUNT) break;
        sentinel_watch_category_t category = watch_categories[index];
        uint16_t y = 100U + row * 68U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304, 60, UI_PANEL), TAG, "watch filter row");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 10, sentinel_watch_category_name(category), UI_TEXT, UI_PANEL, 1), TAG, "watch filter name");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 35, modes[sentinel_watch_get_mode(&watch_state, category)], UI_GOOD, UI_PANEL, 1), TAG, "watch filter mode");
    }
    ESP_RETURN_ON_ERROR(draw_centered(385, "PREFERENCES LAST UNTIL POWER OFF", UI_MUTED, 1), TAG, "watch session preferences");
    ESP_RETURN_ON_ERROR(draw_button(8, 426, 94, 44, "BACK"), TAG, "watch filters back");
    ESP_RETURN_ON_ERROR(draw_button(112, 426, 94, 44, "PREV"), TAG, "watch filters prev");
    return draw_button(216, 426, 96, 44, "NEXT");
}

static esp_err_t watch_draw_why(void)
{
    const char *lines[5] = {"Cerberus recognized a documented", "pattern in a public broadcast.",
        "The evidence strength describes", "that pattern, not a verified device", "identity, owner, or activity."};
    switch (watch_detail.match.category) {
    case SENTINEL_WATCH_FINDMY:
        lines[0] = "An Apple Find My broadcast was seen."; lines[1] = "AirTags and other Find My accessories";
        lines[2] = "can share this packet format."; lines[3] = "The exact model and owner are unknown."; lines[4] = "Presence does not prove tracking."; break;
    case SENTINEL_WATCH_TRACKER:
        if (watch_detail.match.rule_id && !strcmp(watch_detail.match.rule_id, "google.findhub-160")) {
            lines[0] = "A Google Find Hub frame was seen."; lines[1] = "Compatible tags and accessories can";
            lines[2] = "broadcast this finding-network format."; lines[3] = "The model and owner are unknown."; lines[4] = "This uses Google's finding network.";
        } else if (watch_detail.match.rule_id && !strncmp(watch_detail.match.rule_id, "chipolo.", 8U)) {
            lines[0] = "A Chipolo Bluetooth service was seen."; lines[1] = "Chipolo makes key and item finders.";
            lines[2] = "This service identifies an ecosystem."; lines[3] = "The exact model and owner are unknown."; lines[4] = "Presence does not prove tracking.";
        } else if (watch_detail.match.rule_id && !strncmp(watch_detail.match.rule_id, "tile.", 5U)) {
            lines[0] = "A Tile Bluetooth service was seen."; lines[1] = "Tile services are used by item finders";
            lines[2] = "and compatible accessories."; lines[3] = "The exact model and owner are unknown."; lines[4] = "Presence does not prove tracking.";
        }
        break;
    case SENTINEL_WATCH_CAMERA:
        if (watch_detail.match.rule_id && !strcmp(watch_detail.match.rule_id, "gopro.schema")) {
            lines[0] = "A GoPro service and matching maker"; lines[1] = "data appeared in the same packet.";
        } else {
            lines[0] = "A GoPro Bluetooth service was seen."; lines[1] = "Its assigned service identifies GoPro.";
        }
        lines[2] = "The exact camera model is unknown."; lines[3] = "This cannot reveal recording status."; lines[4] = "Other camera brands need their own rule."; break;
    case SENTINEL_WATCH_SMART_GLASSES:
        lines[0] = "Smart glasses is an art preview."; lines[1] = "There is no live detection rule yet.";
        lines[2] = "Shared vendor codes also appear on"; lines[3] = "other products, so they cannot confirm"; lines[4] = "that smart glasses are nearby."; break;
    case SENTINEL_WATCH_FLIPPER:
        lines[0] = "Flipper's Bluetooth service and name"; lines[1] = "appeared together in an advertisement.";
        lines[2] = "Custom firmware can change these."; lines[3] = "Bluetooth must be advertising."; lines[4] = "Presence does not establish misuse."; break;
    case SENTINEL_WATCH_MESHTASTIC:
        lines[0] = "A Meshtastic Bluetooth service was"; lines[1] = "advertised by a nearby device.";
        lines[2] = "This detects its phone interface."; lines[3] = "Cerberus cannot receive LoRa traffic"; lines[4] = "with its onboard radios."; break;
    case SENTINEL_WATCH_SAMSUNG:
        lines[0] = "A Samsung finding-network frame"; lines[1] = "matched the documented tag structure.";
        lines[2] = "Addresses and identifiers can rotate."; lines[3] = "This does not establish that a tag"; lines[4] = "is following you or identify its owner."; break;
    case SENTINEL_WATCH_BEACON:
        lines[0] = "A documented beacon packet was seen."; lines[1] = "Beacons may serve shops, navigation,";
        lines[2] = "sensors, or hobby projects."; lines[3] = "A beacon is not inherently a threat."; lines[4] = "History only is the default setting."; break;
    case SENTINEL_WATCH_PWNAGOTCHI:
        lines[0] = "A custom Pwnagotchi peer broadcast"; lines[1] = "contained the expected structure.";
        lines[2] = "Peer advertising can be disabled."; lines[3] = "Broadcast contents can be imitated."; lines[4] = "No attack on your network is proven."; break;
    case SENTINEL_WATCH_DRONE:
        lines[0] = "A supported Remote ID broadcast"; lines[1] = "matched the expected packet format.";
        lines[2] = "Position is reported by the sender."; lines[3] = "No receiver GPS or range is inferred."; lines[4] = "Silent or unsupported drones are missed."; break;
    case SENTINEL_WATCH_BLE_ACTIVITY:
        lines[0] = "After three baseline sweeps, two"; lines[1] = "sweeps had a sustained traffic rise.";
        lines[2] = "This is an experimental area alert."; lines[3] = "Crowds and rotating addresses matter."; lines[4] = "It identifies no attacker or gadget."; break;
    default: break;
    }
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, 320, 480, UI_BACKGROUND), TAG, "watch why clear");
    ESP_RETURN_ON_ERROR(draw_header("WHY CERBERUS NOTICED"), TAG, "watch why header");
    ESP_RETURN_ON_ERROR(draw_centered(78, watch_detail.match.label, UI_GOOD, 1), TAG, "watch why title");
    for (size_t i = 0; i < 5; ++i)
        ESP_RETURN_ON_ERROR(draw_centered(125U + i * 34U, lines[i], UI_TEXT, 1), TAG, "watch why text");
    ESP_RETURN_ON_ERROR(draw_centered(345, "HISTORY STAYS IN THIS POWER SESSION", UI_MUTED, 1), TAG, "watch why privacy");
    return draw_button(64, 426, 192, 44, "BACK TO EVIDENCE");
}

#include "recon_controls_ui.h"
static esp_err_t watch_draw(uint32_t now, bool full)
{
    if (watch_page == WATCH_HISTORY) return watch_draw_history(now);
    if (watch_page == WATCH_DETAILS) return watch_draw_details(now);
    if (watch_page == WATCH_FILTERS) return watch_draw_filters();
    if (watch_page == WATCH_WHY) return watch_draw_why();
    if (watch_page == WATCH_OPTIONS) return recon_draw_options();
    if (watch_page == WATCH_ACTIONS) return recon_draw_actions();
    if (watch_page == WATCH_SIGNAL) return recon_draw_signal(now);
    memset(&watch_view, 0, sizeof(watch_view));
    watch_view.demo = watch_page == WATCH_DEMO;
    watch_view.paused = watch_paused;
    watch_view.radio_online = c5_online || live_records[SENTINEL_SCAN_BLE].valid;
    watch_view.observation_count = watch_last_count[0] + watch_last_count[1];
    watch_view.match_count = watch_state.matches > UINT16_MAX ? UINT16_MAX : watch_state.matches;
    if (watch_page == WATCH_DEMO) {
        watch_view.scene = (sentinel_watch_scene_t)watch_demo_scene;
        watch_view.paused = false;
        watch_view.phase = live_session.active != SENTINEL_SCAN_NONE ||
            live_ble_session.active != SENTINEL_SCAN_NONE ?
            "DEMO GALLERY / STOPPING RADIO SCAN" : "DEMO GALLERY / RADIO SCANS STOPPED";
        watch_view.evidence = "ART PREVIEW / NO DEVICE WAS DETECTED";
        watch_view.detail = "NEXT: SEE ANOTHER CERBERUS SCENE";
        watch_view.observation_count = watch_view.match_count = 0;
    } else {
        bool wifi_ready = c5_online &&
            (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ) != 0;
        bool ble_ready = live_records[SENTINEL_SCAN_BLE].valid;
        if (watch_paused) snprintf(watch_phase_text, sizeof(watch_phase_text), "C5 WI-FI + S3 BLE PAUSED / TAP RESUME");
        else snprintf(watch_phase_text, sizeof(watch_phase_text), "C5 WI-FI %s / S3 BLE %s",
            !wifi_ready ? "OFF" : live_session.faulted || watch_capture_failed[0] ? "ERROR" :
                live_session.active == SENTINEL_SCAN_WIFI ? "SCANNING" : live_radio_busy ? "WAITING" : "READY",
            !ble_ready ? "OFF" : live_ble_session.faulted || watch_capture_failed[1] ? "ERROR" :
                live_ble_session.active == SENTINEL_SCAN_BLE ? "SCANNING" : "READY");
        watch_view.phase = watch_phase_text;
        if (watch_state.alert_active) {
            const sentinel_watch_entry_t *entry = &watch_state.alert;
            watch_view.scene = watch_scene(entry->match.category);
            watch_view.confidence = entry->match.confidence;
            watch_view.title = entry->match.label;
            watch_view.evidence = entry->match.evidence;
            if (entry->radio == SENTINEL_WATCH_RADIO_ENVIRONMENT)
                snprintf(watch_detail_text, sizeof(watch_detail_text), "EXPERIMENTAL / TAP FOR MEASUREMENTS");
            else snprintf(watch_detail_text, sizeof(watch_detail_text), "%s / %d DBM / TAP FOR EVIDENCE",
                watch_radio_name(entry->radio), entry->rssi);
            watch_view.detail = watch_detail_text;
            watch_view.has_details = true;
        } else {
            watch_view.scene = SENTINEL_WATCH_SCENE_GUARDIAN;
            watch_view.evidence = "LISTENING FOR DOCUMENTED DEVICE HINTS";
            watch_view.detail = !watch_raw_supported() || !watch_extra_supported(SENTINEL_SCAN_WIFI) ||
                !watch_extra_supported(SENTINEL_SCAN_BLE) ? "LIMITED COVERAGE / RADIO UPDATE REQUIRED" :
                watch_extra_unavailable[0] || watch_extra_unavailable[1] ? "LIMITED COVERAGE / PACKET CAPTURE UNAVAILABLE" :
                watch_limited_capture[0] || watch_limited_capture[1] ?
                "BUSY AIRSPACE / CAPTURE LIMIT REACHED" :
                watch_activity_state.learned < 3U ? "LEARNING LOCAL BLUETOOTH ACTIVITY BASELINE" :
                watch_state.muted ? "ALERTS MUTED / OBSERVATIONS STILL LOGGED" :
                "C5 WI-FI + S3 BLE / PASSIVE SCANS";
        }
    }
    return full ? sentinel_watch_view_draw(&watch_view, now) : sentinel_watch_view_update(&watch_view, now);
}

static void watch_open(uint32_t now)
{
    if (!watch_initialized) {
        sentinel_watch_init(&watch_state);
        sentinel_watch_activity_init(&watch_activity_state);
        watch_initialized = true;
    }
    watch_page = WATCH_LIVE;
    watch_paused = false;
    memset(watch_capture_failed, 0, sizeof(watch_capture_failed));
    memset(watch_capture_waiting, 0, sizeof(watch_capture_waiting));
    memset(watch_last_count, 0, sizeof(watch_last_count));
    watch_seen_capture[0] = live_records[SENTINEL_SCAN_WIFI].captured_ms;
    watch_seen_capture[1] = live_records[SENTINEL_SCAN_BLE].captured_ms;
    sentinel_scan_session_select(&live_session, SENTINEL_SCAN_NONE);
    if (!live_ble_session_initialized) {
        sentinel_scan_session_init(&live_ble_session);
        live_ble_session_initialized = true;
    }
    sentinel_scan_session_select(&live_ble_session, SENTINEL_SCAN_NONE);
    current_page = UI_PAGE_WATCH;
    ui_capability_blocked = false;
    ui_capability_blocked_reason = NULL;
    watch_last_draw = now;
    sentinel_watch_view_invalidate();
    ESP_ERROR_CHECK(watch_draw(now, true));
}

static void watch_ui_tick(uint32_t now)
{
    if (current_page != UI_PAGE_WATCH) return;
    if (live_session.faulted) watch_capture_failed[0] = true;
    if (live_ble_session.faulted) watch_capture_failed[1] = true;
    bool changed = watch_consume(now);
    if (watch_page == WATCH_LIVE && !watch_paused) changed = sentinel_watch_tick(&watch_state, now) || changed;
    if ((changed && (watch_page == WATCH_LIVE || watch_page == WATCH_HISTORY)) ||
        ((watch_page == WATCH_LIVE || watch_page == WATCH_DEMO) && (uint32_t)(now - watch_last_draw) >= 100U) ||
        (watch_page == WATCH_SIGNAL && (uint32_t)(now - watch_last_draw) >= 1000U)) {
        ESP_ERROR_CHECK(watch_draw(now, false));
        watch_last_draw = now;
    }
}

static void watch_handle_tap(uint16_t x, uint16_t y)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (watch_page == WATCH_OPTIONS || watch_page == WATCH_ACTIONS || watch_page == WATCH_SIGNAL) {
        if (!recon_handle_control_tap(x, y, now)) return;
    } else if (watch_page == WATCH_HISTORY) {
        if (y >= 104 && y < 376 && x >= 8 && x < 312 && (y - 104) % 68 < 60) {
            const sentinel_watch_entry_t *entry = sentinel_watch_history(&watch_state, watch_history_page * 4U + (y - 104) / 68);
            if (entry) { watch_select_detail(entry); watch_page = WATCH_DETAILS; }
        } else if (x >= 60 && x < 260 && y >= 380 && y < 414) {
            watch_page = WATCH_FILTERS; watch_filters_page = 0;
        } else if (y >= 426 && y < 470) {
            if (x >= 8 && x < 102) watch_page = WATCH_LIVE;
            else if (x >= 112 && x < 206 && watch_history_page) --watch_history_page;
            else if (x >= 216 && x < 312 && (watch_history_page + 1U) * 4U < watch_state.history_count) ++watch_history_page;
        }
    } else if (watch_page == WATCH_DETAILS) {
        if (x >= 64 && x < 256 && y >= 426 && y < 470) watch_page = WATCH_LIVE;
        else if (y >= 378 && y < 412) {
            if (x >= 8 && x < 156) watch_page = WATCH_WHY;
            else if (x >= 164 && x < 312) { watch_page = WATCH_ACTIONS; recon_message[0] = 0; }
        }
    } else if (watch_page == WATCH_WHY) {
        if (x >= 64 && x < 256 && y >= 426 && y < 470) watch_page = WATCH_DETAILS;
    } else if (watch_page == WATCH_FILTERS) {
        if (x >= 8 && x < 312 && y >= 100 && y < 372 && (y - 100) % 68 < 60) {
            size_t index = watch_filters_page * 4U + (y - 100) / 68U;
            if (index < WATCH_FILTER_COUNT) {
                sentinel_watch_category_t category = watch_categories[index];
                sentinel_watch_set_mode(&watch_state, category,
                    (sentinel_watch_mode_t)((sentinel_watch_get_mode(&watch_state, category) + 1U) % 3U));
            }
        } else if (y >= 426 && y < 470) {
            if (x >= 8 && x < 102) watch_page = WATCH_OPTIONS;
            else if (x >= 112 && x < 206 && watch_filters_page) --watch_filters_page;
            else if (x >= 216 && x < 312 && (watch_filters_page + 1U) * 4U < WATCH_FILTER_COUNT) ++watch_filters_page;
        }
    } else {
        sentinel_watch_action_t action = sentinel_watch_view_hit_test(&watch_view, x, y);
        if (action == SENTINEL_WATCH_ACTION_BACK) {
            if (watch_page == WATCH_DEMO) watch_page = WATCH_LIVE;
            else {
                sentinel_scan_session_select(&live_session, SENTINEL_SCAN_NONE);
                sentinel_scan_session_select(&live_ble_session, SENTINEL_SCAN_NONE);
                open_main_menu(); return;
            }
        } else if (action == SENTINEL_WATCH_ACTION_PAUSE) {
            watch_paused = !watch_paused;
            if (!watch_paused) {
                memset(watch_capture_failed, 0, sizeof(watch_capture_failed));
                /* Resume is an explicit fresh request even if the user taps
                 * before the scheduler has observed the preceding Pause. */
                sentinel_scan_session_select(&live_session, SENTINEL_SCAN_WIFI);
                sentinel_scan_session_pause(&live_session, false);
                sentinel_scan_session_select(&live_ble_session, SENTINEL_SCAN_BLE);
                sentinel_scan_session_pause(&live_ble_session, false);
            } else {
                sentinel_scan_session_select(&live_session, SENTINEL_SCAN_NONE);
                sentinel_scan_session_select(&live_ble_session, SENTINEL_SCAN_NONE);
            }
            live_last_poll = 0;
        } else if (action == SENTINEL_WATCH_ACTION_HISTORY) {
            watch_page = WATCH_HISTORY;
            watch_history_page = 0;
        } else if (action == SENTINEL_WATCH_ACTION_DEMO) {
            if (watch_page == WATCH_DEMO) watch_demo_scene = (watch_demo_scene + 1U) % SENTINEL_WATCH_SCENE_COUNT;
            else { watch_page = WATCH_OPTIONS; recon_confirm = 0; }
        } else if (action == SENTINEL_WATCH_ACTION_DETAILS && watch_state.alert_active) {
            watch_select_detail(&watch_state.alert);
            watch_page = WATCH_DETAILS;
        } else return;
    }
    sentinel_watch_view_invalidate();
    ESP_ERROR_CHECK(watch_draw(now, true));
}
