/* PRELUDE/DECLARATIONS/IMPLEMENTATION placeholders are populated from the actual
 * firmware by test_sentinel_live_ui.py. Radio transport is the only fake. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "c5_spi_link.h"
#include "sentinel_scan_session.h"
#include "sentinel_scan_view.h"

#define ESP_ERROR_CHECK(expression) assert((expression) == ESP_OK)

/* ACTUAL_DECLARATIONS */

static cerberus_c5_status_t c5_status;
static bool c5_online;
static cerberus_wifi_scan_status_payload_t wifi_scan_status, hardware_wifi;
static cerberus_ble_scan_status_payload_t ble_scan_status, hardware_ble;
static cerberus_ieee802154_energy_scan_status_payload_t ieee802154_scan_status, hardware_energy;
static cerberus_ieee802154_beacon_scan_status_payload_t ieee802154_beacon_status, hardware_beacon;
static cerberus_ieee802154_activity_scan_status_payload_t ieee802154_activity_status, hardware_activity;
static cerberus_wifi_defense_status_payload_t wifi_defense_status, hardware_defense;
static cerberus_wifi_connection_status_payload_t hardware_wifi_connection;
static cerberus_wifi_connection_status_payload_t wifi_connection_status;
static cerberus_c5_wifi_result_t wifi_selected_result;
static bool wifi_selection_valid;
static cerberus_ble_connection_status_payload_t hardware_ble_connection;
static cerberus_ieee802154_energy_scan_result_payload_t ieee802154_scan_result, hardware_energy_result;
static uint8_t ieee802154_activity_counts[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static cerberus_wifi_defense_result_payload_t wifi_defense_results[64];
static size_t wifi_defense_loaded_count, wifi_defense_page_offset;
static bool wifi_scan_checked, ble_scan_checked, ieee802154_scan_checked;
static bool ieee802154_beacon_checked, ieee802154_activity_checked, wifi_defense_checked;
static bool live_scan_observation_valid, live_radio_busy;
static bool live_status_changed;
enum {
    UI_PAGE_WIFI = 1, UI_PAGE_IEEE802154 = 2,
    UI_WIFI_MODE_BASELINE = 10, UI_WIFI_MODE_DETAIL = 11,
    UI_WIFI_MODE_PASSWORD = 12, UI_WIFI_MODE_CONNECTION = 13,
    UI_WIFI_MODE_STATS = 14, UI_WIFI_MODE_CHANNELS_24 = 15,
    UI_WIFI_MODE_CHANNELS_5 = 16, UI_IEEE802154_MODE_ASSESSMENT = 20,
    UI_IEEE802154_MODE_ENERGY = 21,
};
static int current_page, wifi_mode, ieee802154_mode;
static bool ui_capability_blocked, fixture_live_view;
static sentinel_scan_tool_t fixture_selected_tool;
static sentinel_scan_view_t live_view;
static uint32_t live_last_poll, live_frame;
static unsigned scan_start_calls, scan_stop_calls, view_updates, detail_draws;
static esp_err_t wifi_scan_error, ble_scan_error, ieee802154_scan_error;
static esp_err_t ieee802154_beacon_error, ieee802154_activity_error, wifi_defense_error;
static esp_err_t status_errors[SENTINEL_SCAN_TOOL_COUNT];
static int result_failure_index = -1;
static unsigned status_calls, wifi_result_calls, defense_result_calls;
static bool result_generation_race, bad_activity_chunk;
static bool reordered_rows, connect_race;
static unsigned connect_calls;
static uint8_t connected_index, connected_generation;
static const char *wifi_prefix = "Example";

int64_t esp_timer_get_time(void) { return 123456000LL; }

#define STATUS_FN(name, type, source, tool) \
esp_err_t name(type *output) { ++status_calls; *output = source; return status_errors[tool]; }
STATUS_FN(c5_spi_link_get_wifi_scan_status, cerberus_wifi_scan_status_payload_t, hardware_wifi, SENTINEL_SCAN_WIFI)
STATUS_FN(c5_spi_link_get_ble_scan_status, cerberus_ble_scan_status_payload_t, hardware_ble, SENTINEL_SCAN_BLE)
STATUS_FN(c5_spi_link_get_ieee802154_energy_scan_status, cerberus_ieee802154_energy_scan_status_payload_t, hardware_energy, SENTINEL_SCAN_ENERGY)
STATUS_FN(c5_spi_link_get_ieee802154_beacon_scan_status, cerberus_ieee802154_beacon_scan_status_payload_t, hardware_beacon, SENTINEL_SCAN_BEACON)
STATUS_FN(c5_spi_link_get_ieee802154_activity_scan_status, cerberus_ieee802154_activity_scan_status_payload_t, hardware_activity, SENTINEL_SCAN_ACTIVITY)
STATUS_FN(c5_spi_link_get_wifi_defense_status, cerberus_wifi_defense_status_payload_t, hardware_defense, SENTINEL_SCAN_DEFENSE)

esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *output)
{ *output = hardware_wifi_connection; return ESP_OK; }
esp_err_t c5_spi_link_get_ble_connection_status(cerberus_ble_connection_status_payload_t *output)
{ *output = hardware_ble_connection; return ESP_OK; }

esp_err_t c5_spi_link_get_wifi_scan_result(uint8_t generation, uint8_t index, cerberus_c5_wifi_result_t *result)
{
    ++wifi_result_calls;
    if (index == result_failure_index) { return ESP_FAIL; }
    assert(generation == hardware_wifi.generation);
    const uint8_t identity = reordered_rows ? (index + 2U) % 3U : index;
    memset(result, 0, sizeof(*result));
    snprintf(result->ssid, sizeof(result->ssid), "%s-%u", wifi_prefix, identity);
    result->bssid[5] = identity;
    result->security = 1U;
    result->channel = 1U + index;
    if (result_generation_race && index + 1U == hardware_wifi.stored_count) { ++hardware_wifi.generation; }
    return ESP_OK;
}

esp_err_t c5_spi_link_connect_wifi(uint8_t generation, uint8_t index, const char *password, cerberus_wifi_connection_status_payload_t *status)
{
    assert(strcmp(password, "ExamplePassword") == 0);
    (void)status;
    ++connect_calls;
    connected_index = index;
    connected_generation = generation;
    if (connect_race) { ++hardware_wifi.generation; }
    return generation == hardware_wifi.generation ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t c5_spi_link_get_ble_scan_result(uint8_t generation, uint8_t index, cerberus_c5_ble_result_t *result)
{
    assert(generation == hardware_ble.generation);
    if (index == result_failure_index) { return ESP_FAIL; }
    memset(result, 0, sizeof(*result));
    snprintf(result->name, sizeof(result->name), "Sensor-%u", index);
    return ESP_OK;
}

esp_err_t c5_spi_link_get_ieee802154_energy_scan_result(uint8_t generation, cerberus_ieee802154_energy_scan_result_payload_t *result)
{ (void)generation; *result = hardware_energy_result; return ESP_OK; }

esp_err_t c5_spi_link_get_ieee802154_beacon_scan_result(uint8_t generation, uint8_t index, cerberus_ieee802154_beacon_scan_result_payload_t *result)
{
    if (index == result_failure_index) { return ESP_FAIL; }
    memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->channel = 11U + index;
    return ESP_OK;
}

esp_err_t c5_spi_link_get_ieee802154_activity_scan_result(uint8_t generation, uint8_t index, cerberus_ieee802154_activity_scan_result_payload_t *result)
{
    if (index == result_failure_index) { return ESP_FAIL; }
    memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->first_channel = 11U + index * 4U + (bad_activity_chunk && index == 2U ? 1U : 0U);
    memset(result->counts, generation, sizeof(result->counts));
    return ESP_OK;
}

esp_err_t c5_spi_link_get_wifi_defense_result(uint8_t generation, uint8_t index, cerberus_wifi_defense_result_payload_t *result)
{
    ++defense_result_calls;
    if (index == result_failure_index) { return ESP_FAIL; }
    memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->channel = generation;
    return ESP_OK;
}

static esp_err_t refresh_wifi_scan_status(void) { return ESP_OK; }
static esp_err_t refresh_ble_scan_status(void) { return ESP_OK; }
static esp_err_t refresh_wifi_baseline(void) { return ESP_OK; }

static sentinel_scan_tool_t live_page_tool(void) { return fixture_selected_tool; }
static bool live_is_view(void) { return fixture_live_view; }
static void live_make_view(void) { memset(&live_view, 0, sizeof(live_view)); }
esp_err_t sentinel_scan_view_update(const sentinel_scan_view_t *view, uint32_t frame)
{ (void)view; (void)frame; ++view_updates; return ESP_OK; }
static esp_err_t draw_detail_page(void) { ++detail_draws; return ESP_OK; }

static esp_err_t live_execute(sentinel_scan_command_t command)
{
    if (command.kind == SENTINEL_SCAN_COMMAND_START) { ++scan_start_calls; }
    else { assert(command.kind == SENTINEL_SCAN_COMMAND_STOP); ++scan_stop_calls; }
    uint8_t *state = NULL, *generation = NULL;
    switch (command.tool) {
    case SENTINEL_SCAN_WIFI: state = &hardware_wifi.state; generation = &hardware_wifi.generation; break;
    case SENTINEL_SCAN_BLE: state = &hardware_ble.state; generation = &hardware_ble.generation; break;
    case SENTINEL_SCAN_ENERGY: state = &hardware_energy.state; generation = &hardware_energy.generation; break;
    case SENTINEL_SCAN_BEACON: state = &hardware_beacon.state; generation = &hardware_beacon.generation; break;
    case SENTINEL_SCAN_ACTIVITY: state = &hardware_activity.state; generation = &hardware_activity.generation; break;
    case SENTINEL_SCAN_DEFENSE: state = &hardware_defense.state; generation = &hardware_defense.generation; break;
    default: assert(false);
    }
    if (command.kind == SENTINEL_SCAN_COMMAND_START) {
        ++*generation;
        *state = SENTINEL_SCAN_RUNNING;
    } else {
        *state = SENTINEL_SCAN_IDLE;
    }
    return ESP_OK;
}

/* ACTUAL_DEFENSE_REFRESH */
/* ACTUAL_IMPLEMENTATION */

static void reset(void)
{
    memset(live_records, 0, sizeof(live_records));
    memset(live_page, 0, sizeof(live_page));
    memset(live_wifi, 0, sizeof(live_wifi));
    memset(live_ble, 0, sizeof(live_ble));
    memset(live_beacons, 0, sizeof(live_beacons));
    memset(live_energy, 0, sizeof(live_energy));
    memset(live_activity, 0, sizeof(live_activity));
    memset(status_errors, 0, sizeof(status_errors));
    memset(&hardware_wifi, 0, sizeof(hardware_wifi));
    memset(&hardware_ble, 0, sizeof(hardware_ble));
    memset(&hardware_energy, 0, sizeof(hardware_energy));
    memset(&hardware_beacon, 0, sizeof(hardware_beacon));
    memset(&hardware_activity, 0, sizeof(hardware_activity));
    memset(&hardware_defense, 0, sizeof(hardware_defense));
    memset(&wifi_defense_status, 0, sizeof(wifi_defense_status));
    memset(&hardware_wifi_connection, 0, sizeof(hardware_wifi_connection));
    memset(&hardware_ble_connection, 0, sizeof(hardware_ble_connection));
    sentinel_scan_session_init(&live_session);
    c5_status.capabilities = UINT32_MAX;
    c5_online = true;
    wifi_defense_loaded_count = 0U;
    wifi_defense_page_offset = 0U;
    result_failure_index = -1;
    result_generation_race = bad_activity_chunk = false;
    reordered_rows = connect_race = false;
    connect_calls = 0U;
    wifi_selection_valid = false;
    current_page = UI_PAGE_WIFI;
    wifi_mode = ieee802154_mode = 0;
    ui_capability_blocked = false;
    fixture_live_view = true;
    fixture_selected_tool = SENTINEL_SCAN_NONE;
    live_last_poll = live_frame = 0U;
    scan_start_calls = scan_stop_calls = view_updates = detail_draws = 0U;
    status_calls = wifi_result_calls = defense_result_calls = 0U;
    wifi_prefix = "Example";
}

static void test_wifi_snapshot(void)
{
    reset();
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.generation = 1U;
    hardware_wifi.stored_count = 2U;
    assert(live_observe(100U));
    assert(live_records[SENTINEL_SCAN_WIFI].captured);
    assert(live_records[SENTINEL_SCAN_WIFI].count == 2U);
    assert(strcmp(live_wifi[0].ssid, "Example-0") == 0);
    const unsigned old_reads = wifi_result_calls;
    assert(!live_observe(200U));
    assert(wifi_result_calls == old_reads); /* no reread of committed snapshot */

    hardware_wifi.generation = 2U;
    wifi_prefix = "Changed";
    result_failure_index = 1;
    (void)live_observe(300U);
    assert(strcmp(live_wifi[0].ssid, "Example-0") == 0); /* no partial publication */
    assert(live_records[SENTINEL_SCAN_WIFI].saved_generation == 1U);
    assert(live_records[SENTINEL_SCAN_WIFI].captured_ms == 100U);
    result_failure_index = -1;
    result_generation_race = true;
    (void)live_observe(400U);
    assert(strcmp(live_wifi[0].ssid, "Example-0") == 0);
    result_generation_race = false;
    assert(live_observe(500U));
    assert(strcmp(live_wifi[0].ssid, "Changed-0") == 0);
    assert(live_records[SENTINEL_SCAN_WIFI].captured_ms == 500U);

    ++hardware_wifi.generation;
    hardware_wifi.stored_count = 0U;
    assert(live_observe(600U));
    assert(live_records[SENTINEL_SCAN_WIFI].captured && live_records[SENTINEL_SCAN_WIFI].count == 0U);
}

static void test_activity_atomicity(void)
{
    reset();
    hardware_activity.state = CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE;
    hardware_activity.generation = 1U;
    assert(live_observe(100U));
    assert(live_activity[0][0] == 1U && live_activity[15][4] == 1U);
    hardware_activity.generation = 2U;
    bad_activity_chunk = true;
    (void)live_observe(200U);
    assert(live_activity[0][0] == 1U && live_activity[15][4] == 1U);
    assert(live_records[SENTINEL_SCAN_ACTIVITY].saved_generation == 1U);
    bad_activity_chunk = false;
    assert(live_observe(300U));
    assert(live_activity[0][0] == 2U && live_activity[15][4] == 2U);
}

static void test_defense_generations(void)
{
    reset();
    hardware_defense.state = CERBERUS_WIFI_DEFENSE_COMPLETE;
    hardware_defense.generation = 1U;
    hardware_defense.completed_count = 2U;
    assert(live_observe(100U));
    assert(wifi_defense_loaded_count == 2U);
    assert(wifi_defense_results[0].generation == 1U);
    hardware_defense.generation = 2U;
    assert(live_observe(200U));
    assert(wifi_defense_loaded_count == 2U);
    assert(wifi_defense_results[0].generation == 2U);
    assert(wifi_defense_results[1].generation == 2U);
    assert(defense_result_calls == 4U);
}

static void test_connection_exclusion(void)
{
    reset();
    const uint8_t wifi_states[] = {
        CERBERUS_WIFI_CONNECTION_STAGING, CERBERUS_WIFI_CONNECTION_CONNECTING,
        CERBERUS_WIFI_CONNECTION_CONNECTED, CERBERUS_WIFI_CONNECTION_DISCONNECTING,
    };
    for (size_t i = 0U; i < sizeof(wifi_states); ++i) {
        hardware_wifi_connection.state = wifi_states[i];
        (void)live_observe(100U);
        assert(live_radio_busy);
    }
    hardware_wifi_connection.state = CERBERUS_WIFI_CONNECTION_IDLE;
    const uint8_t ble_states[] = {
        CERBERUS_BLE_CONNECTION_CONNECTING, CERBERUS_BLE_CONNECTION_CONNECTED,
        CERBERUS_BLE_CONNECTION_DISCONNECTING,
    };
    for (size_t i = 0U; i < sizeof(ble_states); ++i) {
        hardware_ble_connection.state = ble_states[i];
        (void)live_observe(200U);
        assert(live_radio_busy);
    }
    hardware_ble_connection.state = CERBERUS_BLE_CONNECTION_IDLE;
    (void)live_observe(300U);
    assert(!live_radio_busy && live_scan_observation_valid);
}

static void test_unknown_other_radio_and_capabilities(void)
{
    reset();
    live_session.active = SENTINEL_SCAN_WIFI;
    hardware_wifi.state = CERBERUS_WIFI_SCAN_RUNNING;
    status_errors[SENTINEL_SCAN_ACTIVITY] = ESP_FAIL;
    (void)live_observe(100U);
    assert(live_radio_busy); /* unknown other radio prevents a new START */
    assert(live_scan_observation_valid); /* known active Wi-Fi may still STOP */
    sentinel_scan_command_t command = sentinel_scan_session_step(&live_session, 100U,
        (sentinel_scan_observation_t){.valid = live_scan_observation_valid,
            .radio_busy = live_radio_busy, .tool = live_session.active,
            .state = live_records[live_session.active].state});
    assert(command.kind == SENTINEL_SCAN_COMMAND_STOP && command.tool == SENTINEL_SCAN_WIFI);

    reset();
    c5_status.capabilities = CERBERUS_C5_CAP_WIFI_2_4_GHZ;
    for (unsigned tool = SENTINEL_SCAN_BLE; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        status_errors[tool] = ESP_ERR_NOT_SUPPORTED;
    }
    (void)live_observe(100U);
    assert(live_scan_observation_valid && !live_radio_busy);
    assert(status_calls == 1U); /* absent radio features are not polled */
}

static void test_connect_resolves_immutable_identity(void)
{
    reset();
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.generation = 1U;
    hardware_wifi.stored_count = 3U;
    assert(live_observe(100U));
    wifi_selected_result = live_wifi[2];
    wifi_selection_valid = true;
    hardware_wifi.generation = 2U;
    reordered_rows = true; /* previous row 2 is now row 0 */
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_OK);
    assert(connect_calls == 1U && connected_generation == 2U && connected_index == 0U);

    ++wifi_selected_result.security;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_NOT_FOUND);
    assert(connect_calls == 1U); /* matching BSSID alone is insufficient */
    --wifi_selected_result.security;
    wifi_prefix = "Changed";
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_NOT_FOUND);
    assert(connect_calls == 1U);
    wifi_prefix = "Example";
    connect_race = true;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
    assert(connect_calls == 2U); /* reject a generation race; no fallback index */
    connect_race = false;
    hardware_wifi.state = CERBERUS_WIFI_SCAN_RUNNING;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
    assert(connect_calls == 2U);
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    wifi_selection_valid = false;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
    assert(connect_calls == 2U);
}

static void test_connect_waits_for_radio_release(void)
{
    const sentinel_scan_tool_t scans[] = {
        SENTINEL_SCAN_ENERGY, SENTINEL_SCAN_BEACON, SENTINEL_SCAN_ACTIVITY,
        SENTINEL_SCAN_BLE, SENTINEL_SCAN_DEFENSE,
    };
    for (size_t i = 0U; i < sizeof(scans) / sizeof(scans[0]); ++i) {
        reset();
        hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
        hardware_wifi.generation = 1U;
        hardware_wifi.stored_count = 1U;
        assert(live_observe(100U));
        wifi_selected_result = live_wifi[0];
        wifi_selection_valid = true;
        /* This is the old page's bounded scan still draining after a switch.
         * It is scheduler-owned, so ordinary OTHER-work busy exclusion omits
         * it. An explicit connection must nevertheless wait for release. */
        live_session.active = scans[i];
        live_session.desired = SENTINEL_SCAN_NONE;
        uint8_t *state = scans[i] == SENTINEL_SCAN_ENERGY ? &hardware_energy.state :
            scans[i] == SENTINEL_SCAN_BEACON ? &hardware_beacon.state :
            scans[i] == SENTINEL_SCAN_ACTIVITY ? &hardware_activity.state :
            scans[i] == SENTINEL_SCAN_BLE ? &hardware_ble.state : &hardware_defense.state;
        *state = SENTINEL_SCAN_RUNNING;
        assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
        assert(connect_calls == 0U);
        *state = SENTINEL_SCAN_IDLE;
        assert(live_connect_selected_wifi("ExamplePassword") == ESP_OK);
        assert(connect_calls == 1U);
    }

    reset();
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.generation = 1U;
    hardware_wifi.stored_count = 1U;
    assert(live_observe(100U));
    wifi_selected_result = live_wifi[0];
    wifi_selection_valid = true;
    status_errors[SENTINEL_SCAN_ACTIVITY] = ESP_FAIL;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
    assert(connect_calls == 0U); /* unknown state is not evidence of release */
    status_errors[SENTINEL_SCAN_ACTIVITY] = ESP_OK;
    hardware_ble_connection.state = CERBERUS_BLE_CONNECTION_CONNECTED;
    assert(live_connect_selected_wifi("ExamplePassword") == ESP_ERR_INVALID_STATE);
    assert(connect_calls == 0U); /* an explicit existing connection is retained */
}

static void test_tick_single_shot_and_scan_again(void)
{
    reset();
    fixture_selected_tool = SENTINEL_SCAN_WIFI;
    live_scan_tick(1000U, false);
    assert(scan_start_calls == 1U && !live_session.completed);
    live_scan_tick(1500U, false);
    assert(scan_start_calls == 1U);
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.stored_count = 2U;
    live_scan_tick(2000U, false);
    assert(live_session.completed && live_session.active == SENTINEL_SCAN_NONE);
    assert(live_records[SENTINEL_SCAN_WIFI].captured);
    assert(live_records[SENTINEL_SCAN_WIFI].count == 2U);
    for (uint32_t now = 3000U; now < 100000U; now += 1000U) { live_scan_tick(now, false); }
    assert(scan_start_calls == 1U && detail_draws == 0U && view_updates > 90U);

    /* A completed view may sleep/wake repeatedly without arming another scan. */
    const unsigned updates_before_sleep = view_updates;
    live_scan_tick(100000U, true);
    live_scan_tick(101000U, true);
    assert(view_updates == updates_before_sleep && live_session.completed);
    live_scan_tick(102000U, false);
    assert(scan_start_calls == 1U && live_session.completed);

    sentinel_scan_session_pause(&live_session, false); /* explicit Scan again */
    live_scan_tick(103000U, false);
    assert(scan_start_calls == 2U && !live_session.completed);
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    live_scan_tick(103500U, false);
    assert(live_session.completed);
    live_scan_tick(105000U, false);
    live_scan_tick(200000U, false);
    assert(scan_start_calls == 2U && detail_draws == 0U);
}

static void test_tick_stop_sleep_and_explicit_rearm(void)
{
    reset();
    fixture_selected_tool = SENTINEL_SCAN_WIFI;
    live_scan_tick(1000U, false);
    sentinel_scan_session_pause(&live_session, true);
    live_scan_tick(1500U, false);
    assert(scan_start_calls == 1U && scan_stop_calls == 1U);
    live_scan_tick(2000U, false);
    assert(live_session.paused && live_session.active == SENTINEL_SCAN_NONE);
    live_scan_tick(3000U, true);
    live_scan_tick(4000U, true);
    live_scan_tick(5000U, false);
    assert(live_session.paused && scan_start_calls == 1U && scan_stop_calls == 1U);
    sentinel_scan_session_pause(&live_session, false);
    live_scan_tick(6000U, false);
    assert(scan_start_calls == 2U);
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    live_scan_tick(6500U, false);
    assert(live_session.completed);
    live_scan_tick(100000U, false);
    assert(scan_start_calls == 2U && detail_draws == 0U);
}

static void test_tick_internal_phase_change_waits_for_wake(void)
{
    reset();
    fixture_selected_tool = SENTINEL_SCAN_WIFI; /* prerequisite inventory */
    live_scan_tick(1000U, false);
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.stored_count = 1U;
    live_scan_tick(1500U, false);
    assert(live_session.completed && scan_start_calls == 1U);
    /* The same Defense page now maps to its second phase. Sleep must retain
     * the completed request, rather than treating promotion as user input. */
    fixture_selected_tool = SENTINEL_SCAN_DEFENSE;
    live_scan_tick(2500U, true);
    live_scan_tick(3500U, true);
    assert(scan_start_calls == 1U && live_session.completed);
    assert(live_session.desired == SENTINEL_SCAN_WIFI);
    live_scan_tick(4500U, false);
    assert(scan_start_calls == 2U && live_session.active == SENTINEL_SCAN_DEFENSE);
}

static void test_tick_legacy_progress_does_not_clear_page(void)
{
    reset();
    fixture_selected_tool = SENTINEL_SCAN_ENERGY;
    fixture_live_view = false; /* model a legacy graph/statistics page */
    current_page = UI_PAGE_IEEE802154;
    ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
    live_scan_tick(1000U, false);
    assert(scan_start_calls == 1U && detail_draws == 1U);
    live_scan_tick(1500U, false); /* settle generation change from START */
    const unsigned settled_draws = detail_draws;
    for (uint8_t completed = 1U; completed < 16U; ++completed) {
        hardware_energy.completed_count = completed;
        live_scan_tick(1500U + completed * 500U, false);
        assert(detail_draws == settled_draws); /* progress-only changes */
    }
    hardware_energy.state = CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE;
    hardware_energy.completed_count = 16U;
    hardware_energy_result.generation = hardware_energy.generation;
    hardware_energy_result.first_channel = 11U;
    hardware_energy_result.channel_count = 16U;
    memset(hardware_energy_result.power_dbm, -80, sizeof(hardware_energy_result.power_dbm));
    live_scan_tick(10000U, false);
    assert(detail_draws == settled_draws + 1U); /* actual new capture draws once */
    assert(live_session.completed && scan_start_calls == 1U);
    live_scan_tick(11000U, false);
    live_scan_tick(100000U, false);
    assert(detail_draws == settled_draws + 1U && view_updates == 0U);
}

static void seed_cached_wifi(void)
{
    hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
    hardware_wifi.generation = 7U;
    hardware_wifi.stored_count = 2U;
    (void)live_observe(100U);
    assert(live_records[SENTINEL_SCAN_WIFI].captured);
    /* Stop can discard C5's current inventory while S3 retains the snapshot. */
    hardware_wifi.state = CERBERUS_WIFI_SCAN_IDLE;
}

static void test_derived_views_reuse_capture(void)
{
    const int modes[] = {UI_WIFI_MODE_STATS, UI_WIFI_MODE_CHANNELS_24, UI_WIFI_MODE_CHANNELS_5};
    for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        reset();
        seed_cached_wifi();
        fixture_selected_tool = SENTINEL_SCAN_WIFI;
        fixture_live_view = false;
        wifi_mode = modes[i];
        live_scan_tick(1000U, false);
        assert(scan_start_calls == 0U && live_session.completed);
        assert(live_records[SENTINEL_SCAN_WIFI].count == 2U);
        assert(live_records[SENTINEL_SCAN_WIFI].captured_ms == 100U);
        const unsigned draws = detail_draws;
        live_scan_tick(2000U, false);
        live_scan_tick(200000U, false);
        assert(scan_start_calls == 0U && detail_draws == draws);

        sentinel_scan_session_pause(&live_session, false); /* Scan Again */
        live_scan_tick(201000U, false);
        assert(scan_start_calls == 1U && !live_session.completed);
        hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
        hardware_wifi.stored_count = 1U;
        live_scan_tick(201500U, false);
        live_scan_tick(300000U, false);
        assert(scan_start_calls == 1U && live_session.completed);
        assert(live_records[SENTINEL_SCAN_WIFI].count == 1U);

        /* No prior capture still requests exactly one sweep. A successful
         * empty capture also counts as a reusable observation on reentry. */
        reset();
        fixture_selected_tool = SENTINEL_SCAN_WIFI;
        fixture_live_view = false;
        wifi_mode = modes[i];
        live_scan_tick(1000U, false);
        assert(scan_start_calls == 1U);
        hardware_wifi.state = CERBERUS_WIFI_SCAN_COMPLETE;
        hardware_wifi.stored_count = 0U;
        live_scan_tick(1500U, false);
        assert(live_session.completed && live_records[SENTINEL_SCAN_WIFI].captured);
        fixture_selected_tool = SENTINEL_SCAN_NONE;
        live_scan_tick(2000U, false);
        fixture_selected_tool = SENTINEL_SCAN_WIFI;
        live_scan_tick(2500U, false);
        assert(scan_start_calls == 1U && live_session.completed);
    }

    /* Baseline comparison reads current C5 inventory, so stale host-only
     * results cannot suppress its fresh prerequisite sweep. */
    reset();
    seed_cached_wifi();
    fixture_selected_tool = SENTINEL_SCAN_WIFI;
    fixture_live_view = false;
    wifi_mode = UI_WIFI_MODE_BASELINE;
    live_scan_tick(1000U, false);
    assert(scan_start_calls == 1U && !live_session.completed);
}

static void test_cached_view_drains_previous_radio(void)
{
    reset();
    seed_cached_wifi();
    fixture_selected_tool = SENTINEL_SCAN_BLE;
    live_scan_tick(1000U, false);
    assert(scan_start_calls == 1U && live_session.active == SENTINEL_SCAN_BLE);
    fixture_selected_tool = SENTINEL_SCAN_WIFI;
    fixture_live_view = false;
    wifi_mode = UI_WIFI_MODE_STATS;
    live_scan_tick(1500U, false);
    assert(scan_start_calls == 1U && scan_stop_calls == 1U && live_session.completed);
    assert(live_session.active == SENTINEL_SCAN_BLE);
    live_scan_tick(2500U, false);
    assert(live_session.active == SENTINEL_SCAN_NONE && scan_start_calls == 1U);
    sentinel_scan_session_pause(&live_session, false);
    live_scan_tick(3500U, false);
    assert(scan_start_calls == 2U && live_session.active == SENTINEL_SCAN_WIFI);
}

int main(void)
{
    test_wifi_snapshot();
    test_activity_atomicity();
    test_defense_generations();
    test_connection_exclusion();
    test_unknown_other_radio_and_capabilities();
    test_connect_resolves_immutable_identity();
    test_connect_waits_for_radio_release();
    test_tick_single_shot_and_scan_again();
    test_tick_stop_sleep_and_explicit_rearm();
    test_tick_internal_phase_change_waits_for_wake();
    test_tick_legacy_progress_does_not_clear_page();
    test_derived_views_reuse_capture();
    test_cached_view_drains_previous_radio();
    puts("PASS: real adapter snapshots/identity/radio guards, single-shot/sleep, incremental refresh, retained Summary/Channels, no-cache and empty captures, explicit Scan Again, and prior-radio drain");
    return 0;
}
