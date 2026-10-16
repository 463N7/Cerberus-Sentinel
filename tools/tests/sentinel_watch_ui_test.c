/* Appended to the existing live adapter fixture by test_sentinel_watch_ui.py. */
#define CERBERUS_NOTIFICATION_TEXT_MAX 64
#define UI_BACKGROUND 0
#define UI_PANEL 1
#define UI_TEXT 2
#define UI_GOOD 3
#define UI_MUTED 4
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); esp_err_t e_ = (expr); if(e_ != ESP_OK) return e_; } while(0)
static const char *TAG = "watch-test", *ui_capability_blocked_reason;
static unsigned raw_reads, notifications, classifier_calls, watch_draws, raw_fault;
static bool classify_match = true;
static sentinel_watch_category_t ble_category = SENTINEL_WATCH_TRACKER;
static unsigned extra_status_reads, extra_result_reads, activity_reads, extra_fault;
static uint8_t extra_count;
static uint16_t extra_flags;
static cerberus_watch_observation_t extra_rows[CERBERUS_WATCH_MAX_OBSERVATIONS];
static char drawn_text[8192];
static void capture_text(const char *text)
{
    size_t used = strlen(drawn_text), length = strlen(text);
    if (used + length + 2U < sizeof(drawn_text)) {
        memcpy(drawn_text + used, text, length); drawn_text[used + length] = '\n';
        drawn_text[used + length + 1U] = 0;
    }
}

esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{ (void)color; assert(x + w <= 320 && y + h <= 480); return ESP_OK; }
esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t background, uint8_t scale)
{ (void)color; (void)background; assert(x < 320 && y < 480 && text && scale); capture_text(text); return ESP_OK; }
static esp_err_t draw_header(const char *text) { assert(text); return ESP_OK; }
static esp_err_t draw_centered(uint16_t y, const char *text, uint16_t color, uint8_t scale)
{ (void)color; assert(y < 480 && text && scale); capture_text(text); return ESP_OK; }
static esp_err_t draw_button(uint16_t x, uint16_t y, uint16_t w, uint16_t h, const char *text)
{ assert(x + w <= 320 && y + h <= 480 && text); return ESP_OK; }
static bool privacy_addresses_masked(void) { return true; }
static void cerberus_privacy_format_ble_identifier(const uint8_t *address, bool masked, char *out, size_t capacity)
{ (void)address; assert(masked); snprintf(out, capacity, "BLE-hidden"); }
static void cerberus_privacy_format_wifi_identifier(const uint8_t *address, bool masked, char *out, size_t capacity)
{ (void)address; assert(masked); snprintf(out, capacity, "WiFi-hidden"); }
static void cerberus_ui_notification_add(uint32_t now, const char *text)
{ (void)now; assert(text && strstr(text, "WATCH:")); ++notifications; }
static void open_main_menu(void) { current_page = UI_PAGE_HOME; fixture_selected_tool = SENTINEL_SCAN_NONE; }

esp_err_t sentinel_watch_view_draw(const sentinel_watch_view_t *view, uint32_t now)
{ (void)now; assert(view); ++watch_draws; return ESP_OK; }
esp_err_t sentinel_watch_view_update(const sentinel_watch_view_t *view, uint32_t now)
{ return sentinel_watch_view_draw(view, now); }
void sentinel_watch_view_invalidate(void) {}
sentinel_watch_action_t sentinel_watch_view_hit_test(const sentinel_watch_view_t *view, uint16_t x, uint16_t y)
{
    if (y >= 348 && y < 412 && x >= 8 && x < 312 && view->has_details) return SENTINEL_WATCH_ACTION_DETAILS;
    if (y < 426 || y >= 470) return SENTINEL_WATCH_ACTION_NONE;
    if (x >= 8 && x < 76) return SENTINEL_WATCH_ACTION_BACK;
    if (x >= 84 && x < 152 && !view->demo) return SENTINEL_WATCH_ACTION_PAUSE;
    if (x >= 160 && x < 228 && !view->demo) return SENTINEL_WATCH_ACTION_HISTORY;
    if (x >= 236 && x < 312) return SENTINEL_WATCH_ACTION_DEMO;
    return SENTINEL_WATCH_ACTION_NONE;
}
static bool classified(sentinel_watch_match_t *match, sentinel_watch_category_t category)
{
    ++classifier_calls; memset(match, 0, sizeof(*match));
    if (!classify_match) return false;
    *match = (sentinel_watch_match_t){category, SENTINEL_WATCH_CONFIDENCE_LOW,
        "fixture-rule", "Fixture hint", "Fixture broadcast hint"};
    return true;
}
bool sentinel_watch_classify_ble(const sentinel_watch_ble_observation_t *observation, sentinel_watch_match_t *match)
{ assert(observation && observation->name_length <= 32 && observation->advertisement_length <= 31);
  return classified(match, ble_category); }
bool sentinel_watch_classify_wifi(const sentinel_watch_wifi_observation_t *observation, sentinel_watch_match_t *match)
{ assert(observation && observation->ssid_length <= 32); return classified(match, SENTINEL_WATCH_CAMERA); }
const char *sentinel_watch_confidence_name(sentinel_watch_confidence_t confidence)
{ (void)confidence; return "LOW"; }
const char *sentinel_watch_category_name(sentinel_watch_category_t category)
{ assert(category > SENTINEL_WATCH_NONE && category < SENTINEL_WATCH_CATEGORY_COUNT); return "Fixture category"; }

esp_err_t c5_spi_link_get_watch_status(uint8_t radio, uint8_t generation, cerberus_watch_scan_status_payload_t *output)
{
    ++extra_status_reads;
    assert(radio == CERBERUS_WATCH_RADIO_BLE || radio == CERBERUS_WATCH_RADIO_WIFI);
    assert(generation == (radio == CERBERUS_WATCH_RADIO_BLE ? hardware_ble.generation : hardware_wifi.generation));
    *output = (cerberus_watch_scan_status_payload_t){.radio=radio, .generation=generation,
        .state=CERBERUS_WATCH_COMPLETE, .stored_count=extra_count, .flags=extra_flags, .duration_ms=4000};
    if (extra_fault == 1 || (extra_fault == 8 && extra_status_reads % 2 == 0)) return ESP_FAIL;
    if (extra_fault == 2) output->radio = 99;
    if (extra_fault == 3) ++output->generation;
    if (extra_fault == 4) output->state = CERBERUS_WATCH_RUNNING;
    if (extra_fault == 5) output->stored_count = CERBERUS_WATCH_MAX_OBSERVATIONS + 1U;
    if (extra_fault == 6) { output->flags |= CERBERUS_WATCH_CAPTURE_UNAVAILABLE; output->stored_count = 1; }
    if (extra_fault == 7 && extra_status_reads % 2 == 0) ++output->reports;
    return ESP_OK;
}
esp_err_t c5_spi_link_get_watch_result(uint8_t radio, uint8_t generation, uint8_t index, cerberus_watch_observation_t *output)
{
    ++extra_result_reads;
    assert(radio == CERBERUS_WATCH_RADIO_BLE || radio == CERBERUS_WATCH_RADIO_WIFI);
    assert(generation == (radio == CERBERUS_WATCH_RADIO_BLE ? hardware_ble.generation : hardware_wifi.generation));
    assert(index < extra_count && index < CERBERUS_WATCH_MAX_OBSERVATIONS);
    *output = extra_rows[index];
    return extra_fault == 9 && index == 1 ? ESP_FAIL : ESP_OK;
}
esp_err_t c5_spi_link_get_ble_activity(uint8_t generation, cerberus_ble_activity_payload_t *output)
{
    ++activity_reads; assert(generation == hardware_ble.generation);
    *output = (cerberus_ble_activity_payload_t){.generation=generation, .state=CERBERUS_WATCH_COMPLETE,
        .duration_ms=4000, .reports=40, .valid_reports=40, .identities=3, .peak_reports_per_second=10};
    if (extra_fault == 10) return ESP_FAIL;
    if (extra_fault == 11) ++output->generation;
    if (extra_fault == 12) output->state = CERBERUS_WATCH_RUNNING;
    if (extra_fault == 13) output->flags = CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED;
    return ESP_OK;
}

esp_err_t c5_spi_link_get_ble_advertisement(uint8_t generation, uint8_t index, cerberus_c5_ble_advertisement_t *output)
{
    ++raw_reads; assert(generation == hardware_ble.generation && index < hardware_ble.stored_count);
    memset(output, 0, sizeof(*output));
    output->address_type = 1; output->address[5] = index;
    output->data[0] = 2; output->data[1] = 1; output->data[2] = 6; output->length = 3;
    if (raw_fault == 1 && index == 1) return ESP_FAIL;
    if (raw_fault == 2 && index == 1) output->address[0]++;
    if (raw_fault == 3 && index + 1 == hardware_ble.stored_count) ++hardware_ble.generation;
    if (raw_fault == 4 && index + 1 == hardware_ble.stored_count) --hardware_ble.stored_count;
    if (raw_fault == 5 && index + 1 == hardware_ble.stored_count) hardware_ble.state = CERBERUS_BLE_SCAN_RUNNING;
    if (raw_fault == 6 && index + 1 == hardware_ble.stored_count) status_errors[SENTINEL_SCAN_BLE] = ESP_FAIL;
    return ESP_OK;
}

#include "sentinel_watch_ui.h"

static void watch_reset(void)
{
    reset();
    memset(&watch_state, 0, sizeof(watch_state)); memset(&watch_view, 0, sizeof(watch_view));
    memset(&watch_detail, 0, sizeof(watch_detail)); memset(watch_advertisements, 0, sizeof(watch_advertisements));
    watch_initialized = watch_paused = watch_capture_failed = false;
    watch_phase = SENTINEL_SCAN_BLE; watch_page = WATCH_LIVE;
    watch_demo_scene = watch_history_page = 0;
    watch_seen_capture[0] = watch_seen_capture[1] = watch_last_draw = watch_last_read = 0;
    watch_last_count = 0; watch_limited_capture = false;
    watch_capture_waiting = false; watch_capture_wait_since = 0;
    memset(watch_extra, 0, sizeof(watch_extra)); memset(&watch_extra_status, 0, sizeof(watch_extra_status));
    memset(&watch_activity_sample, 0, sizeof(watch_activity_sample)); memset(&watch_activity_state, 0, sizeof(watch_activity_state));
    watch_extra_unavailable = watch_activity_available = false; watch_filters_page = 0;
    c5_status.firmware_version = CERBERUS_C5_FIRMWARE_VERSION;
    raw_reads = notifications = classifier_calls = watch_draws = raw_fault = 0;
    extra_status_reads = extra_result_reads = activity_reads = extra_fault = 0;
    extra_count = 0; extra_flags = 0; memset(extra_rows, 0, sizeof(extra_rows)); drawn_text[0] = 0;
    ble_category = SENTINEL_WATCH_TRACKER;
    classify_match = true; fixture_now = 1000; fixture_live_view = false;
    watch_open(fixture_now);
    assert(current_page == UI_PAGE_WATCH && watch_requested_tool() == SENTINEL_SCAN_BLE);
}
static void tick(uint32_t now)
{ fixture_now = now; live_scan_tick(now, false); watch_ui_tick(now); }
static void finish(sentinel_scan_tool_t tool, uint8_t count)
{
    assert(live_session.active == tool);
    if (tool == SENTINEL_SCAN_BLE) { hardware_ble.state = CERBERUS_BLE_SCAN_COMPLETE; hardware_ble.stored_count = count; }
    else { assert(tool == SENTINEL_SCAN_WIFI); hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE; hardware_wifi.stored_count = count; }
}
static void ready_to_consume(void)
{
    watch_reset(); live_scan_tick(1000, false); finish(SENTINEL_SCAN_BLE, 3);
    live_scan_tick(1500, false); assert(live_session.completed && live_session.active == SENTINEL_SCAN_NONE);
}
static void test_alternating_and_idempotent(void)
{
    watch_reset(); assert(watch_keeps_awake()); tick(1000);
    assert(starts_count == 1 && starts[0] == SENTINEL_SCAN_BLE);
    for (unsigned cycle = 0; cycle < 8; ++cycle) {
        sentinel_scan_tool_t tool = cycle % 2 ? SENTINEL_SCAN_WIFI : SENTINEL_SCAN_BLE;
        uint32_t completed = 2000 + cycle * 2000;
        finish(tool, 2); tick(completed);
        assert(watch_state.sweeps == cycle + 1 && watch_state.observations == (cycle + 1) * 2);
        uint32_t observations = watch_state.observations; unsigned reads = raw_reads;
        assert(!watch_consume(completed + 1)); assert(!watch_consume(completed + 100));
        assert(watch_state.observations == observations && raw_reads == reads);
        tick(completed + 500);
        assert(starts_count == cycle + 2 && starts[cycle + 1] != tool);
    }
    assert(connect_calls == 0);
}
static void test_failure_atomicity_and_limit(void)
{
    for (unsigned injection = 1; injection <= 6; ++injection) {
        ready_to_consume(); raw_fault = injection;
        sentinel_watch_t original = watch_state;
        assert(!watch_consume(2000));
        assert(!memcmp(&original, &watch_state, sizeof(original)) && !notifications && !classifier_calls);
        assert(watch_capture_failed && watch_phase == SENTINEL_SCAN_BLE);
    }
    ready_to_consume(); raw_fault = 1;
    for (uint32_t now = 2000; now <= 17000; now += 1000) {
        unsigned before = raw_reads; assert(!watch_consume(now));
        assert(raw_reads <= before + 2); assert(!watch_consume(now + 10));
    }
    assert(watch_paused && watch_capture_failed && watch_requested_tool() == SENTINEL_SCAN_NONE);
    assert(!watch_state.sweeps && !watch_state.observations && !notifications);
    unsigned reads = raw_reads; assert(!watch_consume(30000) && raw_reads == reads);
    tick(18000); assert(live_session.desired == SENTINEL_SCAN_NONE);
    raw_fault = 0; fixture_now = 19000; watch_handle_tap(100, 440); tick(19000);
    assert(!watch_paused && !watch_capture_failed && starts_count == 2 && hardware_ble.state == CERBERUS_BLE_SCAN_RUNNING);
    finish(SENTINEL_SCAN_BLE, 1); tick(20000); assert(watch_state.sweeps == 1);
}
static void test_pause_demo_exit_and_draining(void)
{
    for (unsigned action = 0; action < 3; ++action) {
        watch_reset(); tick(1000); defer_stop = true;
        fixture_now = 1500;
        watch_handle_tap(action == 0 ? 100 : action == 1 ? 260 : 20, 440);
        assert(watch_requested_tool() == SENTINEL_SCAN_NONE || current_page == UI_PAGE_HOME);
        tick(1500); assert(scan_stop_calls == 1 && starts_count == 1);
        tick(2000); assert(starts_count == 1 && live_session.active == SENTINEL_SCAN_BLE);
        hardware_ble.state = CERBERUS_BLE_SCAN_IDLE; tick(2500);
        assert(live_session.active == SENTINEL_SCAN_NONE && starts_count == 1);
        assert(!watch_state.sweeps && !watch_state.observations);
        if (action == 1) {
            sentinel_watch_t original = watch_state;
            for (unsigned i = 0; i < 20; ++i) { fixture_now = 3000 + i * 100; watch_handle_tap(260, 440); tick(fixture_now); }
            assert(!memcmp(&original, &watch_state, sizeof(original)) && !raw_reads && starts_count == 1);
            assert(watch_page == WATCH_DEMO && watch_keeps_awake());
            fixture_now = 5500; watch_handle_tap(20, 440); tick(5500);
        } else if (action == 0) {
            assert(!watch_keeps_awake()); fixture_now = 5500; watch_handle_tap(100, 440); tick(5500);
        } else { watch_open(5500); tick(5500); }
        assert(starts_count == 2 && hardware_ble.state == CERBERUS_BLE_SCAN_RUNNING);
    }
    /* Entering Watch waits for an already running bounded energy sweep. */
    watch_reset(); current_page = UI_PAGE_IEEE802154; fixture_selected_tool = SENTINEL_SCAN_ENERGY;
    live_scan_tick(1000, false); assert(starts[0] == SENTINEL_SCAN_ENERGY);
    watch_open(1500); tick(1500); assert(starts_count == 1 && scan_stop_calls == 0);
    hardware_energy.state = CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE;
    hardware_energy_result.generation = hardware_energy.generation;
    hardware_energy_result.first_channel = 11; hardware_energy_result.channel_count = 16;
    tick(2000); assert(starts_count == 2 && starts[1] == SENTINEL_SCAN_BLE);
}
static void test_busy_offline_and_history(void)
{
    watch_reset(); c5_online = false; tick(1000); tick(5000); assert(starts_count == 0);
    c5_online = true; hardware_wifi_connection.state = CERBERUS_WIFI_CONNECTION_STAGING;
    tick(6000); assert(starts_count == 0);
    hardware_wifi_connection.state = CERBERUS_WIFI_CONNECTION_IDLE;
    hardware_ble_connection.state = CERBERUS_BLE_CONNECTION_CONNECTED;
    tick(7000); assert(starts_count == 0);
    hardware_ble_connection.state = CERBERUS_BLE_CONNECTION_IDLE;
    tick(8000); assert(starts_count == 1);
    finish(SENTINEL_SCAN_BLE, 2); tick(9000);
    assert(watch_state.history_count == 2 && watch_state.alert_active);
    fixture_now = 9100; watch_handle_tap(180, 440); assert(watch_page == WATCH_HISTORY);
    watch_handle_tap(20, 115); assert(watch_page == WATCH_DETAILS);
    watch_handle_tap(100, 440); assert(watch_page == WATCH_LIVE);
    watch_handle_tap(20, 365); assert(watch_page == WATCH_DETAILS);
    watch_handle_tap(100, 440); assert(watch_page == WATCH_LIVE);
    sentinel_watch_t saved = watch_state; watch_open(10000);
    assert(!memcmp(&saved, &watch_state, sizeof(saved)) && !watch_consume(10000));
    tick(10000); assert(starts_count == 2); /* Fresh sweep, retained history. */
    ready_to_consume(); c5_status.capabilities &= ~CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT;
    assert(watch_consume(2000) && !raw_reads && watch_limited_capture);
    assert(watch_state.sweeps == 1 && watch_state.observations == 3);
}
static void test_resume_before_scheduler_observes_pause(void)
{
    /* A read timeout pauses in watch_ui_tick, after live_scan_tick. A Resume
       tap can arrive in that same UI iteration before desired becomes NONE. */
    ready_to_consume(); raw_fault = 1;
    assert(!watch_consume(17000) && watch_paused && live_session.completed);
    raw_fault = 0; fixture_now = 17100;
    watch_handle_tap(100, 440); tick(18000);
    assert(starts_count == 2 && hardware_ble.state == CERBERUS_BLE_SCAN_RUNNING);
    assert(!watch_state.sweeps && !watch_state.observations);
}
static void test_normal_capture_failure_and_recovery(void)
{
    watch_reset(); tick(1000); result_failure_index = 1;
    finish(SENTINEL_SCAN_BLE, 3); tick(2000);
    assert(live_session.completed && !live_records[SENTINEL_SCAN_BLE].captured);
    assert(watch_capture_failed && !watch_paused && !classifier_calls && !raw_reads);
    for (uint32_t now = 3000; now <= 17000; now += 1000) tick(now);
    assert(watch_paused && watch_capture_failed && watch_requested_tool() == SENTINEL_SCAN_NONE);
    assert(!watch_state.observations && !watch_state.history_count && !notifications && starts_count == 1);
    /* Resume can immediately follow the timeout, before desired becomes NONE. */
    result_failure_index = -1; fixture_now = 17100; watch_handle_tap(100, 440); tick(18000);
    assert(starts_count == 2 && hardware_ble.state == CERBERUS_BLE_SCAN_RUNNING);
    finish(SENTINEL_SCAN_BLE, 1); tick(19000);
    assert(watch_state.sweeps == 1 && watch_state.observations == 1 && !watch_capture_failed);

    /* A transient ordinary read failure recovers without skipping the sweep. */
    watch_reset(); tick(1000); result_failure_index = 1;
    finish(SENTINEL_SCAN_BLE, 3); tick(2000);
    result_failure_index = -1; tick(3000);
    assert(!watch_paused && !watch_capture_failed && watch_state.observations == 3 && watch_state.sweeps == 1);

    /* A stale retained inventory must not hide failure of the new generation. */
    watch_reset(); tick(1000); finish(SENTINEL_SCAN_BLE, 2); tick(2000);
    assert(watch_state.sweeps == 1); watch_open(2500); tick(2500);
    result_failure_index = 1; finish(SENTINEL_SCAN_BLE, 3); tick(3500);
    assert(watch_capture_failed && live_records[SENTINEL_SCAN_BLE].captured);
    assert(live_records[SENTINEL_SCAN_BLE].saved_generation != live_records[SENTINEL_SCAN_BLE].generation);
    tick(18500); assert(watch_paused && watch_state.sweeps == 1 && watch_state.observations == 2);
}
static void test_missing_required_radios_and_empty_sweeps(void)
{
    const uint32_t missing[] = {CERBERUS_C5_CAP_BLE, CERBERUS_C5_CAP_WIFI_2_4_GHZ};
    for (unsigned i = 0; i < 2; ++i) {
        watch_reset(); c5_status.capabilities &= ~missing[i];
        assert(watch_requested_tool() == SENTINEL_SCAN_NONE);
        tick(1000); tick(3000); tick(20000);
        assert(starts_count == 0 && !watch_state.observations && !raw_reads);
    }
    watch_reset(); tick(1000); finish(SENTINEL_SCAN_BLE, 0); tick(2000);
    assert(watch_state.sweeps == 1 && !watch_state.observations && !notifications && !raw_reads);
    tick(2500); finish(SENTINEL_SCAN_WIFI, 0); tick(3500);
    assert(watch_state.sweeps == 2 && !watch_state.observations && !notifications);
    tick(4000); assert(starts_count == 3 && starts[2] == SENTINEL_SCAN_BLE);
}

static void fill_extra_drone(uint8_t index, uint8_t address)
{
    assert(index < CERBERUS_WATCH_MAX_OBSERVATIONS);
    extra_rows[index] = (cerberus_watch_observation_t){.address_type=1, .rssi=-61,
        .kind=SENTINEL_WATCH_PROTOCOL_DRONE, .transport=SENTINEL_WATCH_TRANSPORT_BLE,
        .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID | SENTINEL_WATCH_PROTOCOL_HAS_POSITION | SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE,
        .altitude_kind=SENTINEL_WATCH_ALTITUDE_GEODETIC, .latitude_e7=-1234567,
        .longitude_e7=-7654321, .altitude_dm=-5};
    extra_rows[index].address[5] = address;
    snprintf(extra_rows[index].id, sizeof(extra_rows[index].id), "DRONE-%u", index);
}
static void test_extra_atomicity_and_capability_gates(void)
{
    for (unsigned injection=1; injection<=12; ++injection) {
        ready_to_consume(); extra_count=2; fill_extra_drone(0, 100); fill_extra_drone(1, 101); extra_fault=injection;
        sentinel_watch_t saved=watch_state; sentinel_watch_activity_t activity=watch_activity_state;
        assert(!watch_consume(2000));
        assert(watch_capture_failed && !notifications && !classifier_calls);
        assert(!memcmp(&saved, &watch_state, sizeof(saved)));
        assert(!memcmp(&activity, &watch_activity_state, sizeof(activity)));
        assert(watch_phase==SENTINEL_SCAN_BLE && !watch_seen_capture[1]);
    }
    /* A failed second row or end fence never publishes the first staged row. */
    ready_to_consume(); extra_count=2; fill_extra_drone(0, 100); fill_extra_drone(1, 101); extra_fault=9;
    assert(!watch_consume(2000) && extra_result_reads==2);
    extra_fault=0; assert(watch_consume(3000));
    assert(watch_state.history_count==5 && watch_state.observations==5 && watch_state.sweeps==1);
    assert(watch_activity_state.learned==1 && notifications==2);
    unsigned reads=extra_status_reads; assert(!watch_consume(4000) && extra_status_reads==reads);

    ready_to_consume(); extra_fault=1; assert(!watch_consume(2000));
    assert(!watch_consume(17000) && watch_paused && !watch_state.observations);

    for (unsigned gate=0; gate<3; ++gate) {
        ready_to_consume(); extra_fault=1;
        if (gate==0) c5_status.firmware_version=CERBERUS_WATCH_MIN_FIRMWARE-1U;
        else if (gate==1) c5_status.capabilities &= ~CERBERUS_C5_CAP_WATCH_OBSERVATIONS;
        else c5_status.capabilities &= ~(CERBERUS_C5_CAP_WATCH_OBSERVATIONS | CERBERUS_C5_CAP_BLE_ACTIVITY);
        assert(watch_consume(2000) && !extra_status_reads && !extra_result_reads && watch_extra_unavailable);
        assert(activity_reads==(gate==1?1U:0U));
    }
    ready_to_consume(); c5_status.capabilities &= ~CERBERUS_C5_CAP_BLE_ACTIVITY;
    assert(watch_consume(2000) && extra_status_reads==2 && !activity_reads);
    ready_to_consume(); extra_fault=13; assert(watch_consume(2000));
    assert(!watch_activity_available && watch_activity_state.learned==0);
    ready_to_consume(); extra_flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;
    assert(watch_consume(2000) && watch_extra_unavailable && !watch_capture_failed);
    ready_to_consume(); extra_flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;
    assert(watch_consume(2000) && watch_limited_capture);
}
static void test_extra_drone_dedup_and_immutable_details(void)
{
    watch_reset(); tick(1000); finish(SENTINEL_SCAN_BLE, 1); live_scan_tick(1500, false);
    extra_count=1; fill_extra_drone(0, 0); ble_category=SENTINEL_WATCH_DRONE;
    assert(watch_consume(2000));
    assert(watch_state.observations==1 && watch_state.history_count==1 && notifications==1);
    assert(sentinel_watch_tick(&watch_state, 2000));
    assert(watch_draw(2000, true)==ESP_OK);
    fixture_now=2100; drawn_text[0]=0; watch_handle_tap(20, 365);
    assert(watch_page==WATCH_DETAILS && watch_detail.details.protocol.latitude_e7==-1234567);
    assert(strstr(drawn_text,"LAT -0.1234567") && strstr(drawn_text,"LON -0.7654321"));
    assert(strstr(drawn_text,"ALT -0.5 M / WGS84 HAE") && strstr(drawn_text,"UNVERIFIED"));
    sentinel_watch_entry_t saved=watch_detail;
    sentinel_watch_details_t changed=watch_detail.details;
    changed.protocol.latitude_e7=90000000; strcpy(changed.protocol.id,"NEW-REPORT");
    assert(!sentinel_watch_observe_details(&watch_state, saved.radio, saved.address, saved.address_type,
        "NEW NAME",8,-20,&saved.match,&changed,2500));
    memset(extra_rows,0,sizeof(extra_rows)); memset(watch_extra,0,sizeof(watch_extra));
    assert(!memcmp(&saved,&watch_detail,sizeof(saved)));
    drawn_text[0]=0; fixture_now=3000; watch_handle_tap(100,390);
    assert(watch_page==WATCH_WHY && strstr(drawn_text,"Position is reported by the sender."));
    assert(strstr(drawn_text,"No receiver GPS or range is inferred."));
    assert(!memcmp(&saved,&watch_detail,sizeof(saved)));
    watch_handle_tap(100,440); assert(watch_page==WATCH_DETAILS);
    for(size_t i=0;i<WATCH_FILTER_COUNT;++i){
        watch_detail.match.category=watch_categories[i]; drawn_text[0]=0;
        assert(watch_draw_why()==ESP_OK && drawn_text[0]);
    }
    /* A distinct address type must not suppress an ordinary BLE observation. */
    watch_reset(); tick(1000); finish(SENTINEL_SCAN_BLE,1); live_scan_tick(1500,false);
    extra_count=1; fill_extra_drone(0,0); extra_rows[0].address_type=0; ble_category=SENTINEL_WATCH_DRONE;
    assert(watch_consume(2000) && watch_state.observations==2 && watch_state.history_count==2);
}
static void test_category_controls_and_observation_modes(void)
{
    watch_reset(); fixture_now=1100; watch_handle_tap(180,440); assert(watch_page==WATCH_HISTORY);
    watch_handle_tap(100,390); assert(watch_page==WATCH_FILTERS && watch_filters_page==0);
    unsigned initial_starts=starts_count;
    for(size_t i=0;i<WATCH_FILTER_COUNT;++i){
        if(i && i%4U==0U)watch_handle_tap(260,440);
        assert(watch_filters_page==i/4U);
        sentinel_watch_category_t category=watch_categories[i];
        sentinel_watch_mode_t original=sentinel_watch_get_mode(&watch_state,category);
        for(unsigned step=1;step<=3;++step){
            watch_handle_tap(20,110+(i%4U)*68U);
            assert(sentinel_watch_get_mode(&watch_state,category)==(original+step)%3U);
        }
    }
    assert(starts_count==initial_starts && !notifications);
    uint8_t page=watch_filters_page; sentinel_watch_t saved=watch_state;
    watch_handle_tap(260,440); assert(watch_filters_page==page); /* Final page bounded. */
    watch_handle_tap(20,175); assert(!memcmp(&saved,&watch_state,sizeof(saved))); /* Empty row. */
    while(watch_filters_page)watch_handle_tap(140,440);
    watch_handle_tap(140,440); assert(watch_filters_page==0);
    watch_handle_tap(20,440); assert(watch_page==WATCH_HISTORY);
    watch_handle_tap(20,440); assert(watch_page==WATCH_LIVE);

    const sentinel_watch_mode_t modes[]={SENTINEL_WATCH_MODE_OFF,SENTINEL_WATCH_MODE_HISTORY,SENTINEL_WATCH_MODE_ALERT};
    for(unsigned i=0;i<3;++i){
        ready_to_consume(); ble_category=SENTINEL_WATCH_FLIPPER;
        sentinel_watch_set_mode(&watch_state,SENTINEL_WATCH_FLIPPER,modes[i]);
        assert(watch_consume(2000) && watch_state.observations==3);
        assert(watch_state.history_count==(i?3U:0U));
        assert(watch_state.queue_count==(i==2?1U:0U) && notifications==(i==2?1U:0U));
    }
    ready_to_consume(); ble_category=SENTINEL_WATCH_BEACON;
    assert(watch_consume(2000) && watch_state.history_count==3 && !notifications && !watch_state.queue_count);
}
static void test_family_explanations(void)
{
    watch_reset();
    const struct {
        sentinel_watch_category_t category;
        const char *rule, *label, *expected;
    } cases[] = {
        {SENTINEL_WATCH_FINDMY, "apple.offline-finding", "AirTag / Find My hint", "AirTags and other Find My accessories"},
        {SENTINEL_WATCH_TRACKER, "google.findhub-160", "Find Hub broadcast", "A Google Find Hub frame was seen."},
        {SENTINEL_WATCH_TRACKER, "tile.feed", "Tile tracker hint", "A Tile Bluetooth service was seen."},
        {SENTINEL_WATCH_TRACKER, "chipolo.fe65", "Chipolo tracker hint", "A Chipolo Bluetooth service was seen."},
        {SENTINEL_WATCH_CAMERA, "gopro.fea6", "GoPro camera hint", "A GoPro Bluetooth service was seen."},
        {SENTINEL_WATCH_CAMERA, "gopro.schema", "GoPro camera hint", "A GoPro service and matching maker"},
        {SENTINEL_WATCH_SMART_GLASSES, "demo", "Smart glasses demo", "There is no live detection rule yet."},
    };
    for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
        watch_detail.match.category=cases[i].category;
        watch_detail.match.rule_id=cases[i].rule;
        watch_detail.match.label=cases[i].label;
        drawn_text[0]=0; assert(watch_draw_why()==ESP_OK);
        assert(strstr(drawn_text,cases[i].expected));
        if(cases[i].category!=SENTINEL_WATCH_FINDMY)assert(!strstr(drawn_text,"AirTag"));
    }
    assert(watch_scene(SENTINEL_WATCH_FINDMY)==SENTINEL_WATCH_SCENE_FINDMY);
    assert(watch_scene(SENTINEL_WATCH_TRACKER)==SENTINEL_WATCH_SCENE_TRACKER);
}
int main(void)
{
    assert(legacy_adapter_tests() == 0);
    test_alternating_and_idempotent(); test_failure_atomicity_and_limit();
    test_pause_demo_exit_and_draining(); test_busy_offline_and_history();
    test_resume_before_scheduler_observes_pause();
    test_normal_capture_failure_and_recovery(); test_missing_required_radios_and_empty_sweeps();
    test_extra_atomicity_and_capability_gates(); test_extra_drone_dedup_and_immutable_details();
    test_category_controls_and_observation_modes(); test_family_explanations();
    puts("PASS: actual Watch UI atomic ordinary/extra snapshots, capability gates, filters, immutable evidence, one-radio alternation, bounded retry, pause/resume/demo/exit, and legacy one-shot scans.");
    return 0;
}
