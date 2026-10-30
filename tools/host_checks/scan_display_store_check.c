#ifdef NDEBUG
#undef NDEBUG
#endif
#include "scan_display_store.h"
#include "s3_findmy_monitor.h"
#include "s3_ble_scanner.h"
#include "sentinel_scan_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scan_display_record_type.h"

enum { UI_PAGE_WIFI, UI_PAGE_BLE, UI_PAGE_IEEE802154, UI_PAGE_WATCH };
enum { UI_WIFI_MODE_LIST, UI_WIFI_MODE_BASELINE };
enum { MALLOC_CAP_SPIRAM = 1U, MALLOC_CAP_8BIT = 2U };
static unsigned current_page, wifi_mode;
static live_scan_record_t live_records[SENTINEL_SCAN_TOOL_COUNT];
static uint32_t live_capture_pending;
static bool live_status_changed;
static bool live_supported(sentinel_scan_tool_t tool){return tool!=SENTINEL_SCAN_NONE;}
static uint16_t live_page[SENTINEL_SCAN_TOOL_COUNT];
static cerberus_scan_display_store_t *live_store;
static cerberus_findmy_monitor_record_t *live_ble_evidence;
/* Legacy snapshot capture has no attached live-discovery owner in this suite. */
static struct { struct { uint64_t epoch; } session; } *ble_discovery_live;
static bool live_ble_order_dirty;
static int64_t live_ble_published_ms, clock_ms;
static int64_t esp_timer_get_time(void) { return clock_ms * 1000LL; }
static uint8_t live_ble_focus_bits[CERBERUS_BLE_SCAN_MAX_RESULTS];
static uint8_t live_activity[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static uint8_t ieee802154_activity_counts[16][CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static int8_t live_energy[16];
static uint16_t wifi_defense_loaded_count;
static cerberus_ieee802154_energy_scan_result_payload_t ieee802154_scan_result;
static cerberus_wifi_scan_status_payload_t wifi_scan_status, wifi_end;
static cerberus_ble_scan_status_payload_t ble_scan_status, ble_end;
static cerberus_ieee802154_beacon_scan_status_payload_t ieee802154_beacon_status, beacon_end;

static struct { void *pointer; size_t bytes; bool live; } allocations[128];
static size_t allocation_calls, live_allocations, freed_allocations, fail_allocation_call;
static unsigned result_calls, end_status_calls, refresh_calls, baseline_calls;
static unsigned relation_before_calls, relation_after_calls, relation_publications;
static unsigned relation_result_start, relation_status_start;
static cerberus_c5_wifi_relation_bank_t relation_proof, published_relation_proof;
static esp_err_t relation_before_error, relation_after_error, published_relation_error;
static uint8_t published_relation_generation;
static cerberus_c5_wifi_result_t wifi_source[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_s3_wifi_observation_t wifi_timing_source[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_c5_ble_result_t ble_source[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_s3_ble_focus_t focus_source[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_c5_ble_advertisement_t advertisement_source[CERBERUS_BLE_SCAN_MAX_RESULTS];
static uint32_t observation_source[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_ieee802154_beacon_scan_result_payload_t beacon_source[CERBERUS_IEEE802154_BEACON_MAX_RESULTS];
enum failure_point {
    FAIL_NONE, FAIL_WIFI_RESULT, FAIL_WIFI_END, FAIL_BLE_RESULT, FAIL_BLE_FOCUS,
    FAIL_BLE_ADV, FAIL_BLE_TIME, FAIL_BLE_END, FAIL_BEACON_RESULT, FAIL_BEACON_END,
};
static enum failure_point failure;
static uint8_t failure_index = 1U;

/* Actual capture code calls these adapter boundaries. The dedicated relation
 * UI suite exercises their SPI implementation; here verify AP commit ordering,
 * full-width proof forwarding and failure isolation from the old AP bank. */
static esp_err_t wifi_relation_capture_before(uint8_t generation,
    cerberus_c5_wifi_relation_bank_t *out)
{
    assert(out && generation == wifi_scan_status.generation);
    ++relation_before_calls;
    relation_result_start = result_calls;
    relation_status_start = end_status_calls;
    memset(out, 0, sizeof(*out));
    if (!relation_before_error) *out = relation_proof;
    return relation_before_error;
}
static esp_err_t wifi_relation_capture_after(const cerberus_c5_wifi_relation_bank_t *proof)
{
    assert(!relation_before_error && !memcmp(proof, &relation_proof, sizeof(*proof)));
    assert(result_calls == relation_result_start + wifi_scan_status.stored_count);
    assert(end_status_calls == relation_status_start + 1U);
    ++relation_after_calls;
    return relation_after_error;
}
static void wifi_relation_publish(uint8_t generation,
    const cerberus_c5_wifi_relation_bank_t *proof, esp_err_t error)
{
    const live_scan_record_t *r = &live_records[SENTINEL_SCAN_WIFI];
    assert(r->captured && r->saved_generation == generation &&
        r->count == wifi_scan_status.stored_count);
    assert(end_status_calls == relation_status_start + 1U);
    if (r->count) assert(!memcmp(live_store->wifi, wifi_source, r->count * sizeof(wifi_source[0])));
    ++relation_publications;
    published_relation_generation = generation;
    published_relation_proof = *proof;
    published_relation_error = error;
}

static void *heap_caps_calloc(size_t count, size_t bytes, unsigned capabilities)
{
    assert(capabilities == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    ++allocation_calls;
    if (allocation_calls == fail_allocation_call) return NULL;
    assert(allocation_calls < sizeof(allocations) / sizeof(allocations[0]));
    void *pointer = calloc(count, bytes);
    assert(pointer);
    allocations[allocation_calls].pointer = pointer;
    allocations[allocation_calls].bytes = count * bytes;
    allocations[allocation_calls].live = true;
    ++live_allocations;
    return pointer;
}

static void heap_caps_free(void *pointer)
{
    if (!pointer) return;
    for (size_t i = 1U; i <= allocation_calls; ++i) {
        if (allocations[i].live && allocations[i].pointer == pointer) {
            allocations[i].live = false;
            --live_allocations;
            ++freed_allocations;
            free(pointer);
            return;
        }
    }
    assert(!"unowned or already freed allocation");
}

static esp_err_t refresh_wifi_scan_status(void) { ++refresh_calls; return ESP_OK; }
static esp_err_t refresh_wifi_baseline(void) { ++baseline_calls; return ESP_OK; }
static esp_err_t refresh_wifi_defense_survey(void) { return ESP_ERR_NOT_SUPPORTED; }

esp_err_t c5_spi_link_get_wifi_scan_observed_result(uint8_t generation, uint8_t index,
    cerberus_c5_wifi_result_t *out, cerberus_s3_wifi_observation_t *observation)
{
    assert(generation == wifi_scan_status.generation && index < wifi_scan_status.stored_count);
    assert(relation_before_calls);
    assert(observation);
    ++result_calls;
    if (failure == FAIL_WIFI_RESULT && index == failure_index) return ESP_FAIL;
    *observation = wifi_timing_source[index];
    *out = wifi_source[index]; return ESP_OK;
}
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *out)
{
    ++end_status_calls;
    if (failure == FAIL_WIFI_END) return ESP_FAIL;
    *out = wifi_end; return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_get_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_result_t *out)
{
    assert(generation == ble_scan_status.generation && index < ble_scan_status.stored_count);
    ++result_calls;
    if (failure == FAIL_BLE_RESULT && index == failure_index) return ESP_FAIL;
    *out = ble_source[index]; return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_get_focus(uint8_t generation, uint8_t index,
    cerberus_s3_ble_focus_t *out)
{
    assert(generation == ble_scan_status.generation && index < ble_scan_status.stored_count);
    if (failure == FAIL_BLE_FOCUS && index == failure_index) return ESP_FAIL;
    *out = focus_source[index]; return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_get_advertisement(uint8_t generation, uint8_t index,
    cerberus_c5_ble_advertisement_t *out)
{
    assert(generation == ble_scan_status.generation && index < ble_scan_status.stored_count);
    if (failure == FAIL_BLE_ADV && index == failure_index) return ESP_FAIL;
    *out = advertisement_source[index]; return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_get_advertisement_time(uint8_t generation,
    uint8_t index, uint32_t *out)
{
    assert(generation == ble_scan_status.generation && index < ble_scan_status.stored_count);
    if (failure == FAIL_BLE_TIME && index == failure_index) return ESP_FAIL;
    *out = observation_source[index]; return ESP_OK;
}
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *out)
{
    ++end_status_calls;
    if (failure == FAIL_BLE_END) return ESP_FAIL;
    *out = ble_end; return ESP_OK;
}
esp_err_t c5_spi_link_get_ieee802154_beacon_scan_result(uint8_t generation,
    uint8_t index, cerberus_ieee802154_beacon_scan_result_payload_t *out)
{
    assert(generation == ieee802154_beacon_status.generation &&
        index < ieee802154_beacon_status.stored_count);
    ++result_calls;
    if (failure == FAIL_BEACON_RESULT && index == failure_index) return ESP_FAIL;
    *out = beacon_source[index]; return ESP_OK;
}
esp_err_t c5_spi_link_get_ieee802154_beacon_scan_status(
    cerberus_ieee802154_beacon_scan_status_payload_t *out)
{
    ++end_status_calls;
    if (failure == FAIL_BEACON_END) return ESP_FAIL;
    *out = beacon_end; return ESP_OK;
}
esp_err_t c5_spi_link_get_ieee802154_energy_scan_result(uint8_t generation,
    cerberus_ieee802154_energy_scan_result_payload_t *out)
{ (void)generation; (void)out; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t c5_spi_link_get_ieee802154_activity_scan_result(uint8_t generation,
    uint8_t index, cerberus_ieee802154_activity_scan_result_payload_t *out)
{ (void)generation; (void)index; (void)out; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t c5_spi_link_get_ieee802154_activity_scan_status(
    cerberus_ieee802154_activity_scan_status_payload_t *out)
{ (void)out; return ESP_ERR_NOT_SUPPORTED; }

#include "scan_display_capture_impl.h"

static void reset(void)
{
    heap_caps_free(live_ble_evidence); live_ble_evidence = NULL;
    heap_caps_free(live_store); live_store = NULL;
    assert(live_allocations == 0U);
    memset(allocations, 0, sizeof(allocations));
    allocation_calls = live_allocations = freed_allocations = fail_allocation_call = 0U;
    result_calls = end_status_calls = refresh_calls = baseline_calls = 0U;
    relation_before_calls = relation_after_calls = relation_publications = 0U;
    relation_result_start = relation_status_start = 0U;
    relation_before_error = relation_after_error = published_relation_error = ESP_OK;
    memset(&relation_proof, 0, sizeof(relation_proof));
    memset(&published_relation_proof, 0, sizeof(published_relation_proof));
    memset(live_records, 0, sizeof(live_records));
    memset(live_page, 0, sizeof(live_page));
    memset(live_ble_focus_bits, 0, sizeof(live_ble_focus_bits));
    live_ble_order_dirty = false;
    live_capture_pending = 0; live_status_changed = false;
    live_ble_published_ms = 0; clock_ms = 0;
    current_page = UI_PAGE_WIFI; wifi_mode = UI_WIFI_MODE_LIST;
    failure = FAIL_NONE;
}

static void prepare(sentinel_scan_tool_t tool, uint8_t generation, uint8_t count)
{
    live_records[tool].valid = true;
    live_records[tool].state = SENTINEL_SCAN_COMPLETE;
    live_records[tool].generation = generation;
    live_records[tool].error = ESP_OK;
    if (tool == SENTINEL_SCAN_WIFI) {
        wifi_scan_status = (cerberus_wifi_scan_status_payload_t){
            .generation = generation, .state = CERBERUS_WIFI_SCAN_COMPLETE, .stored_count = count};
        wifi_end = wifi_scan_status;
        relation_proof.status.generation = generation;
        relation_proof.status.epoch = UINT64_C(0x1234567800000000) + generation;
        relation_proof.request_started_us = UINT64_C(0x2345678900000000) + generation;
        relation_proof.source_now_us = UINT64_C(0x3456789a00000000) + generation;
        for (size_t i = 0U; i < CERBERUS_WIFI_SCAN_MAX_RESULTS; ++i) {
            memset(&wifi_source[i], 0, sizeof(wifi_source[i]));
            wifi_source[i].bssid[0] = generation;
            wifi_source[i].bssid[1] = (uint8_t)i;
            wifi_source[i].channel = 1U;
            snprintf(wifi_source[i].ssid, sizeof(wifi_source[i].ssid), "wifi-%u-%u", generation, (unsigned)i);
            memset(&wifi_timing_source[i], 0, sizeof(wifi_timing_source[i]));
            wifi_timing_source[i].request_started_us = UINT64_C(4294967296000) + 1000U * generation + i;
            wifi_timing_source[i].source_age_ms = 100U * generation + (uint32_t)i;
            wifi_timing_source[i].valid = (i % 2U) == 0U;
        }
    } else if (tool == SENTINEL_SCAN_BLE) {
        ble_scan_status = (cerberus_ble_scan_status_payload_t){
            .generation = generation, .state = CERBERUS_BLE_SCAN_COMPLETE, .stored_count = count};
        ble_end = ble_scan_status;
        for (size_t i = 0U; i < CERBERUS_BLE_SCAN_MAX_RESULTS; ++i) {
            memset(&ble_source[i], 0, sizeof(ble_source[i]));
            ble_source[i].address[0] = generation; ble_source[i].address[1] = (uint8_t)i;
            ble_source[i].address_type = 1U; ble_source[i].rssi = -60;
            snprintf(ble_source[i].name, sizeof(ble_source[i].name), "ble-%u-%u", generation, (unsigned)i);
            focus_source[i] = (cerberus_s3_ble_focus_t){.category_bits = (uint8_t)(1U << (i % 6U))};
            advertisement_source[i] = (cerberus_c5_ble_advertisement_t){
                .address_type = 1U, .length = 3U, .data = {2U,1U,6U}};
            memcpy(advertisement_source[i].address, ble_source[i].address, 6U);
            observation_source[i] = 100U * generation + (uint32_t)i;
        }
    } else if (tool == SENTINEL_SCAN_BEACON) {
        ieee802154_beacon_status = (cerberus_ieee802154_beacon_scan_status_payload_t){
            .generation = generation, .state = CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE, .stored_count = count};
        beacon_end = ieee802154_beacon_status;
        for (size_t i = 0U; i < CERBERUS_IEEE802154_BEACON_MAX_RESULTS; ++i) {
            memset(&beacon_source[i], (int)(generation + i), sizeof(beacon_source[i]));
        }
    } else assert(false);
}

static void check_first_and_reuse(void)
{
    reset(); prepare(SENTINEL_SCAN_WIFI, 1U, 2U);
    current_page = UI_PAGE_WIFI; wifi_mode = UI_WIFI_MODE_BASELINE;
    assert(live_capture(SENTINEL_SCAN_WIFI, 1000U));
    assert(allocation_calls == 1U && live_allocations == 1U &&
        allocations[1].bytes == sizeof(*live_store));
    assert(refresh_calls == 1U && baseline_calls == 1U);
    cerberus_scan_display_store_t *pool = live_store;
    cerberus_c5_wifi_result_t retained_wifi[2];
    cerberus_s3_wifi_observation_t retained_wifi_timing[2];
    memcpy(retained_wifi, live_store->wifi, sizeof(retained_wifi));
    memcpy(retained_wifi_timing, live_store->wifi_observations, sizeof(retained_wifi_timing));
    assert(memcmp(live_store->wifi, wifi_source, sizeof(retained_wifi)) == 0);
    assert(memcmp(retained_wifi_timing, wifi_timing_source, sizeof(retained_wifi_timing)) == 0);
    current_page = UI_PAGE_BLE;
    prepare(SENTINEL_SCAN_BLE, 2U, 2U);
    cerberus_c5_ble_result_t original_ble[2]; memcpy(original_ble, ble_source, sizeof(original_ble));
    clock_ms = INT64_C(4294967296) + 2000;
    assert(live_capture(SENTINEL_SCAN_BLE, 2000U));
    assert(live_ble_published_ms == clock_ms); /* Independent full-width publication clock. */
    assert(live_ble_order_dirty);
    assert(live_store == pool && allocation_calls == 2U && live_allocations == 2U);
    assert(memcmp(retained_wifi, live_store->wifi, sizeof(retained_wifi)) == 0);
    assert(memcmp(retained_wifi_timing, live_store->wifi_observations, sizeof(retained_wifi_timing)) == 0);
    assert(memcmp(original_ble, ble_source, sizeof(original_ble)) == 0);
    assert(memcmp(original_ble, live_store->ble, sizeof(original_ble)) == 0);
    assert(live_ble_evidence[1].observed_ms == 201U &&
        live_ble_evidence[1].generation == 2U && live_ble_evidence[1].index == 1U);
    assert(memcmp(&live_ble_evidence[1].advertisement, &advertisement_source[1], sizeof(advertisement_source[1])) == 0);
    current_page = UI_PAGE_IEEE802154;
    prepare(SENTINEL_SCAN_BEACON, 3U, 2U);
    assert(live_capture(SENTINEL_SCAN_BEACON, 3000U));
    assert(live_store == pool && allocation_calls == 2U && live_allocations == 2U);
    assert(memcmp(retained_wifi, live_store->wifi, sizeof(retained_wifi)) == 0);
    assert(memcmp(retained_wifi_timing, live_store->wifi_observations, sizeof(retained_wifi_timing)) == 0);
    assert(memcmp(original_ble, live_store->ble, sizeof(original_ble)) == 0);
    assert(memcmp(live_store->beacons, beacon_source, 2U * sizeof(beacon_source[0])) == 0);
    current_page = UI_PAGE_WATCH;
    assert(live_store_acquire() && live_store == pool && allocation_calls == 2U);
    unsigned previous_calls = result_calls;
    assert(!live_capture(SENTINEL_SCAN_BLE, 4000U)); /* already captured generation */
    assert(result_calls == previous_calls && allocation_calls == 2U);
    assert(live_ble_published_ms == INT64_C(4294967296) + 2000);
}

static void check_empty_and_limits(void)
{
    const sentinel_scan_tool_t tools[] = {SENTINEL_SCAN_WIFI,SENTINEL_SCAN_BLE,SENTINEL_SCAN_BEACON};
    const uint8_t limits[] = {CERBERUS_WIFI_SCAN_MAX_RESULTS,CERBERUS_BLE_SCAN_MAX_RESULTS,CERBERUS_IEEE802154_BEACON_MAX_RESULTS};
    for (size_t i = 0U; i < 3U; ++i) {
        reset(); prepare(tools[i], 1U, 0U);
        clock_ms = 1000;
        assert(live_capture(tools[i], 1000U));
        if (tools[i] == SENTINEL_SCAN_BLE) assert(live_ble_published_ms == 1000);
        assert(!live_store && !live_ble_evidence && allocation_calls == 0U && result_calls == 0U);
        assert(live_records[tools[i]].captured && live_records[tools[i]].count == 0U);
        prepare(tools[i], 2U, (uint8_t)(limits[i] + 1U));
        assert(!live_capture(tools[i], 2000U));
        assert(live_records[tools[i]].error == ESP_ERR_INVALID_RESPONSE &&
            live_records[tools[i]].saved_generation == 1U && allocation_calls == 0U);
        prepare(tools[i], 3U, limits[i]);
        live_page[tools[i]] = 250U;
        assert(live_capture(tools[i], 3000U));
        assert(live_records[tools[i]].count == limits[i]);
        assert(live_page[tools[i]] == (limits[i] - 1U) / 4U);
        cerberus_scan_display_store_t *pool = live_store;
        prepare(tools[i], 4U, 0U);
        clock_ms = 4000;
        assert(live_capture(tools[i], 4000U));
        assert(live_store == pool && live_records[tools[i]].count == 0U && live_page[tools[i]] == 0U);
        if (tools[i] == SENTINEL_SCAN_BLE) assert(!live_ble_evidence && live_allocations == 1U && live_ble_published_ms == 4000);
    }
}

static void check_allocation_failures(void)
{
    const sentinel_scan_tool_t tools[] = {SENTINEL_SCAN_WIFI,SENTINEL_SCAN_BLE,SENTINEL_SCAN_BEACON};
    for (size_t i = 0U; i < 3U; ++i) {
        reset(); prepare(tools[i], 1U, 2U); fail_allocation_call = 1U;
        assert(!live_capture(tools[i], 1000U));
        assert(!live_store && !live_records[tools[i]].captured && result_calls == 0U &&
            live_records[tools[i]].error == ESP_ERR_NO_MEM && live_allocations == 0U);
        prepare(tools[i], 1U, 2U); fail_allocation_call = 0U;
        assert(live_capture(tools[i], 2000U));
        assert(live_records[tools[i]].captured && live_records[tools[i]].captured_ms == 2000U);
    }
    reset(); prepare(SENTINEL_SCAN_BLE, 1U, 2U); fail_allocation_call = 2U;
    assert(!live_capture(SENTINEL_SCAN_BLE, 1000U));
    assert(live_store && !live_ble_evidence && live_allocations == 1U &&
        !live_records[SENTINEL_SCAN_BLE].captured && result_calls == 0U);
    prepare(SENTINEL_SCAN_BLE, 1U, 2U); fail_allocation_call = 0U;
    assert(live_capture(SENTINEL_SCAN_BLE, 2000U));
    cerberus_findmy_monitor_record_t *evidence = live_ble_evidence;
    prepare(SENTINEL_SCAN_BLE, 2U, 3U); fail_allocation_call = allocation_calls + 1U;
    assert(!live_capture(SENTINEL_SCAN_BLE, 3000U));
    assert(live_ble_evidence == evidence && live_allocations == 2U &&
        live_records[SENTINEL_SCAN_BLE].saved_generation == 1U &&
        live_records[SENTINEL_SCAN_BLE].count == 2U);
}

static void check_failed_capture_retains_published(void)
{
    const sentinel_scan_tool_t tools[] = {SENTINEL_SCAN_WIFI,SENTINEL_SCAN_BLE,SENTINEL_SCAN_BEACON};
    for (size_t t = 0U; t < 3U; ++t) {
        for (unsigned variant = 0U; variant < (t == 1U ? 9U : 5U); ++variant) {
            reset(); prepare(tools[t], 7U, 2U);
            clock_ms = INT64_C(4294967296) + 5000;
            assert(live_capture(tools[t], 5000U));
            int64_t published_before = live_ble_published_ms;
            cerberus_scan_display_store_t retained = *live_store;
            cerberus_findmy_monitor_record_t evidence[2];
            cerberus_findmy_monitor_record_t *evidence_pointer = live_ble_evidence;
            uint8_t focus_bits[sizeof(live_ble_focus_bits)];
            memcpy(focus_bits, live_ble_focus_bits, sizeof(focus_bits));
            if (evidence_pointer) memcpy(evidence, evidence_pointer, sizeof(evidence));
            size_t allocations_before = live_allocations;
            live_ble_order_dirty = false;
            prepare(tools[t], 8U, 3U);
            if (t == 0U) {
                if (variant == 0U) failure = FAIL_WIFI_RESULT;
                if (variant == 1U) failure = FAIL_WIFI_END;
                if (variant == 2U) ++wifi_end.generation;
                if (variant == 3U) wifi_end.state = CERBERUS_WIFI_SCAN_RUNNING;
                if (variant == 4U) ++wifi_end.stored_count;
            } else if (t == 1U) {
                const enum failure_point points[] = {FAIL_BLE_RESULT,FAIL_BLE_FOCUS,FAIL_BLE_ADV,FAIL_BLE_TIME,FAIL_BLE_END};
                if (variant < 5U) failure = points[variant];
                if (variant == 5U) ++ble_end.generation;
                if (variant == 6U) ble_end.state = CERBERUS_BLE_SCAN_RUNNING;
                if (variant == 7U) ++ble_end.stored_count;
                if (variant == 8U) --ble_end.stored_count;
            } else {
                if (variant == 0U) failure = FAIL_BEACON_RESULT;
                if (variant == 1U) failure = FAIL_BEACON_END;
                if (variant == 2U) ++beacon_end.generation;
                if (variant == 3U) beacon_end.state = CERBERUS_IEEE802154_BEACON_SCAN_RUNNING;
                if (variant == 4U) ++beacon_end.stored_count;
            }
            clock_ms += 1000;
            assert(!live_capture(tools[t], 6000U));
            assert(live_ble_published_ms == published_before);
            assert(live_records[tools[t]].saved_generation == 7U &&
                live_records[tools[t]].count == 2U && live_records[tools[t]].captured_ms == 5000U);
            assert(memcmp(retained.wifi, live_store->wifi, sizeof(retained.wifi)) == 0);
            assert(memcmp(retained.wifi_observations, live_store->wifi_observations,
                sizeof(retained.wifi_observations)) == 0);
            assert(memcmp(retained.ble, live_store->ble, sizeof(retained.ble)) == 0);
            assert(memcmp(retained.beacons, live_store->beacons, sizeof(retained.beacons)) == 0);
            assert(live_ble_evidence == evidence_pointer && live_allocations == allocations_before);
            assert(!live_ble_order_dirty);
            assert(memcmp(focus_bits, live_ble_focus_bits, sizeof(focus_bits)) == 0);
            if (evidence_pointer) assert(memcmp(evidence, evidence_pointer, sizeof(evidence)) == 0);
        }
    }
}

static void check_same_generation_cycle(void)
{
    const sentinel_scan_tool_t tools[]={SENTINEL_SCAN_WIFI,SENTINEL_SCAN_BLE,SENTINEL_SCAN_BEACON};
    const enum failure_point failures[]={FAIL_WIFI_RESULT,FAIL_BLE_RESULT,FAIL_BEACON_RESULT};
    for(unsigned t=0;t<3;++t){
        sentinel_scan_tool_t tool=tools[t];uint32_t bit=UINT32_C(1)<<tool;
        reset();prepare(tool,7,2);clock_ms=1000;assert(live_capture(tool,1000));
        cerberus_scan_display_store_t frozen=*live_store;
        int64_t published=live_ble_published_ms;
        size_t allocations_before=allocation_calls;
        /* A complete poll is not a new sweep, even after a query failure. */
        live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        assert(!(live_capture_pending&bit)&&!live_capture(tool,1100));
        live_status(tool,ESP_FAIL,SENTINEL_SCAN_IDLE,0,0,0);
        live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        assert(!(live_capture_pending&bit)&&!live_capture(tool,1200));
        assert(allocation_calls==allocations_before&&live_ble_published_ms==published);
        /* A new witnessed cycle can wrap back to the saved generation. */
        live_status(tool,ESP_OK,SENTINEL_SCAN_RUNNING,7,0,0);
        assert((live_capture_pending&bit)&&!live_capture(tool,1300));
        assert(live_records[tool].captured&&live_records[tool].saved_generation==7&&live_records[tool].count==2);
        assert(!memcmp(&frozen,live_store,sizeof(frozen))&&live_ble_published_ms==published);
        prepare(tool,7,1);memset(wifi_source[0].ssid,'N',8);memset(ble_source[0].name,'N',8);beacon_source[0].channel=26;
        wifi_timing_source[0].request_started_us+=1000000U;wifi_timing_source[0].source_age_ms=11;
        observation_source[0]=1500;clock_ms=2000;
        live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        failure=failures[t];failure_index=0;
        assert(!live_capture(tool,2000)&&(live_capture_pending&bit));
        assert(live_records[tool].count==2&&live_records[tool].captured_ms==1000&&live_ble_published_ms==published);
        assert(!memcmp(frozen.wifi,live_store->wifi,sizeof(frozen.wifi)));
        assert(!memcmp(frozen.wifi_observations,live_store->wifi_observations,sizeof(frozen.wifi_observations)));
        assert(!memcmp(frozen.ble,live_store->ble,sizeof(frozen.ble)));
        assert(!memcmp(frozen.beacons,live_store->beacons,sizeof(frozen.beacons)));
        failure=FAIL_NONE;failure_index=1;
        live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        assert(live_capture(tool,2000)&&!(live_capture_pending&bit));
        assert(live_records[tool].count==1&&live_records[tool].captured_ms==2000);
        if(tool==SENTINEL_SCAN_WIFI){
            assert(!memcmp(live_store->wifi,wifi_source,sizeof(wifi_source[0])));
            assert(!memcmp(live_store->wifi_observations,wifi_timing_source,sizeof(wifi_timing_source[0])));
        }
        if(tool==SENTINEL_SCAN_BLE)assert(live_ble_evidence[0].observed_ms==1500&&live_ble_published_ms==2000);
        if(tool==SENTINEL_SCAN_BEACON)assert(live_store->beacons[0].channel==26);
        allocations_before=allocation_calls;clock_ms=2500;
        live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        assert(!live_capture(tool,2500)&&allocation_calls==allocations_before);
        assert(live_records[tool].captured_ms==2000);
        /* An observed different cycle remains pending if its byte later wraps,
         * even when the final running state was missed between polls. */
        live_status(tool,ESP_OK,SENTINEL_SCAN_IDLE,6,0,0);assert(live_capture_pending&bit);
        prepare(tool,7,0);clock_ms=3000;live_status(tool,ESP_OK,SENTINEL_SCAN_COMPLETE,7,0,0);
        assert(live_capture(tool,3000)&&!(live_capture_pending&bit)&&live_records[tool].count==0);
        if(tool==SENTINEL_SCAN_BLE)assert(!live_ble_evidence&&live_ble_published_ms==3000);
    }
}

static void check_relation_binding_publication(void)
{
    const esp_err_t errors[] = {ESP_OK, ESP_ERR_NOT_SUPPORTED, ESP_ERR_TIMEOUT, ESP_ERR_INVALID_RESPONSE};
    for (unsigned phase = 0U; phase < 2U; ++phase) {
        for (unsigned i = 0U; i < sizeof(errors) / sizeof(errors[0]); ++i) {
            reset(); prepare(SENTINEL_SCAN_WIFI, 241U, 2U);
            if (!phase) relation_before_error = errors[i];
            else relation_after_error = errors[i];
            assert(live_capture(SENTINEL_SCAN_WIFI, 1234U));
            assert(relation_before_calls == 1U && relation_publications == 1U);
            assert(relation_after_calls == (relation_before_error == ESP_OK));
            assert(published_relation_error == errors[i] && published_relation_generation == 241U);
            if (!relation_before_error)
                assert(!memcmp(&published_relation_proof, &relation_proof, sizeof(relation_proof)));
            else {
                cerberus_c5_wifi_relation_bank_t zero = {0};
                assert(!memcmp(&published_relation_proof, &zero, sizeof(zero)));
            }
            /* Proof failure does not lose legitimate AP rows, and repeated
             * complete polls neither republish nor refresh the proof. */
            assert(!memcmp(live_store->wifi, wifi_source, 2U * sizeof(wifi_source[0])));
            assert(!live_capture(SENTINEL_SCAN_WIFI, 2345U));
            assert(relation_before_calls == 1U && relation_publications == 1U);
        }
    }
    for (unsigned why = 0U; why < 6U; ++why) {
        reset(); prepare(SENTINEL_SCAN_WIFI, 7U, 2U);
        assert(live_capture(SENTINEL_SCAN_WIFI, 1000U));
        const cerberus_c5_wifi_relation_bank_t frozen = published_relation_proof;
        prepare(SENTINEL_SCAN_WIFI, 8U, 3U);
        if (why == 0U) failure = FAIL_WIFI_RESULT;
        if (why == 1U) failure = FAIL_WIFI_END;
        if (why == 2U) ++wifi_end.generation;
        if (why == 3U) wifi_end.state = CERBERUS_WIFI_SCAN_RUNNING;
        if (why == 4U) ++wifi_end.stored_count;
        if (why == 5U) wifi_scan_status.stored_count = CERBERUS_WIFI_SCAN_MAX_RESULTS + 1U;
        assert(!live_capture(SENTINEL_SCAN_WIFI, 2000U));
        assert(relation_publications == 1U && relation_after_calls == 1U);
        assert(!memcmp(&published_relation_proof, &frozen, sizeof(frozen)));
        assert(live_records[SENTINEL_SCAN_WIFI].saved_generation == 7U);
    }
    /* Empty and byte-wrapped AP banks still carry their own exact epoch. */
    reset(); prepare(SENTINEL_SCAN_WIFI, 255U, 0U);
    assert(live_capture(SENTINEL_SCAN_WIFI, 1U));
    prepare(SENTINEL_SCAN_WIFI, 0U, 0U);
    relation_proof.status.epoch += UINT64_C(1) << 40;
    assert(live_capture(SENTINEL_SCAN_WIFI, 2U));
    assert(relation_publications == 2U && published_relation_generation == 0U &&
        published_relation_proof.status.epoch == relation_proof.status.epoch);
    reset(); prepare(SENTINEL_SCAN_BLE, 3U, 0U);
    assert(live_capture(SENTINEL_SCAN_BLE, 1U));
    assert(!relation_before_calls && !relation_after_calls && !relation_publications);
}

int main(void)
{
    check_first_and_reuse();
    check_empty_and_limits();
    check_allocation_failures();
    check_failed_capture_retains_published();
    check_same_generation_cycle();
    check_relation_binding_publication();
    reset();
    printf("Production scan snapshot store: PSRAM-only %zu-byte pool, empty/max captures, publication guards, evidence lifetime and reuse: pass\n", sizeof(cerberus_scan_display_store_t));
    return 0;
}
