#include "wifi_multissid_session.h"
#include "wifi_pinescan_session.h"
#include "cerberus_protocol.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int lock_token, lock_depth;
static bool fail_mutex = true, fail_take, fail_alloc;
static unsigned allocations, frees, live_allocations, stop_calls, mutex_creations;
static size_t largest_bank_bytes;
static int64_t clock_us = 50000000;
static esp_err_t start_error;
static cerberus_s3_capture_status_t capture;
static cerberus_s3_capture_options_t options;
static uint32_t next_generation = 40;
static bool switch_owner_after_status;
static bool switch_owner_before_start_return;

int64_t esp_timer_get_time(void) { return clock_us; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++mutex_creations; return fail_mutex ? NULL : &lock_token; }
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t ticks)
{
    (void)ticks;
    assert(mutex == &lock_token && lock_depth == 0);
    if (fail_take) return 0;
    lock_depth = 1;
    return 1;
}
int xSemaphoreGive(SemaphoreHandle_t mutex)
{
    assert(mutex == &lock_token && lock_depth == 1);
    lock_depth = 0;
    return 1;
}
void *heap_caps_calloc(size_t count, size_t size, unsigned caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (fail_alloc) return NULL;
    void *p = calloc(count, size);
    assert(p);
    if (count * size > largest_bank_bytes) largest_bank_bytes = count * size;
    ++allocations;
    ++live_allocations;
    return p;
}
void heap_caps_free(void *p)
{
    if (p) { ++frees; --live_allocations; free(p); }
}
void cerberus_s3_capture_status(cerberus_s3_capture_status_t *out)
{
    *out = capture;
    if (switch_owner_after_status) {
        switch_owner_after_status = false;
        capture.generation = ++next_generation;
        capture.busy = true;
    }
}
esp_err_t cerberus_s3_capture_start_options(const cerberus_s3_capture_options_t *request)
{
    assert(!capture.busy);
    if (start_error != ESP_OK) return start_error;
    options = *request;
    assert(options.observer && options.observer_context);
    capture = (cerberus_s3_capture_status_t){
        .generation = ++next_generation, .state = CERBERUS_S3_CAPTURE_RUNNING,
        .busy = true, .hopping = options.hopping, .save_pcap = options.save_pcap,
        .mode_mask = options.mode_mask, .channel = options.channel,
    };
    return ESP_OK;
}
esp_err_t cerberus_s3_capture_start_options_generation(
    const cerberus_s3_capture_options_t *request, uint32_t *generation)
{
    *generation = 0;
    esp_err_t error = cerberus_s3_capture_start_options(request);
    if (error != ESP_OK) return error;
    *generation = capture.generation;
    if (switch_owner_before_start_return) {
        switch_owner_before_start_return = false;
        capture.generation = ++next_generation;
    }
    return ESP_OK;
}
void cerberus_s3_capture_stop(void)
{
    ++stop_calls;
    assert(capture.busy);
    capture.state = CERBERUS_S3_CAPTURE_STOPPING;
}
bool cerberus_s3_capture_stop_generation(uint32_t generation)
{
    if (!generation || !capture.busy || capture.generation != generation) return false;
    cerberus_s3_capture_stop();
    return true;
}
static void finish(void)
{
    capture.busy = false;
    capture.state = capture.save_pcap ? CERBERUS_S3_CAPTURE_SAVED : CERBERUS_S3_CAPTURE_COMPLETE;
}
static size_t beacon(uint8_t frame[96], uint8_t identity, const uint8_t *ssid, size_t n,
    uint8_t bssid_id)
{
    assert(n <= 32);
    memset(frame, 0, 96);
    frame[0] = 0x80;
    memset(frame + 4, 0xff, 6);
    const uint8_t tx[6] = {0x20, 1, 2, 3, 4, identity};
    const uint8_t bssid[6] = {0x30, 1, 2, 3, 4, bssid_id};
    memcpy(frame + 10, tx, 6);
    memcpy(frame + 16, bssid, 6);
    frame[34] = 1;
    frame[36] = 0;
    frame[37] = (uint8_t)n;
    if (n) memcpy(frame + 38, ssid, n);
    frame[38 + n] = 3;
    frame[39 + n] = 1;
    frame[40 + n] = 6;
    return 41 + n;
}
static void send_frame(uint32_t generation, uint32_t sequence, uint8_t identity,
    const char *ssid, uint64_t source_us, uint8_t rx_channel, int8_t rssi)
{
    uint8_t frame[96];
    size_t size = beacon(frame, identity, (const uint8_t *)ssid, strlen(ssid), identity);
    uint8_t original[96]; memcpy(original, frame, sizeof(frame));
    cerberus_s3_capture_observation_t meta = {.generation = generation,
        .sequence = sequence, .channel = rx_channel, .rssi_dbm = rssi, .observed_us = source_us};
    options.observer(&meta, frame, size, options.observer_context);
    assert(!memcmp(original, frame, sizeof(frame)));
}
static cerberus_s3_multissid_status_t state(void)
{
    cerberus_s3_multissid_status_t out;
    assert(cerberus_s3_multissid_status(&out) == ESP_OK);
    return out;
}

static cerberus_s3_pinescan_status_t pine_state(void)
{
    cerberus_s3_pinescan_status_t out;
    assert(cerberus_s3_pinescan_status(&out) == ESP_OK);
    return out;
}
static void send_pine(uint32_t generation, uint32_t sequence, uint8_t identity,
    bool matching, uint64_t source_us, uint8_t channel, int8_t rssi)
{
    uint8_t frame[96];
    size_t size = beacon(frame, identity, (const uint8_t *)(matching ? "pattern" : "ordinary"),
        matching ? 7U : 8U, matching ? identity : (uint8_t)(identity + 1U));
    if (!matching) frame[34] |= 0x10U; /* No matching prefix; no minimal-open predicate. */
    cerberus_s3_capture_observation_t meta = {.generation = generation,
        .sequence = sequence, .channel = channel, .rssi_dbm = rssi, .observed_us = source_us};
    options.observer(&meta, frame, size, options.observer_context);
}

int main(void)
{
    cerberus_s3_multissid_status_t status;
    assert(cerberus_s3_multissid_status(&status) == ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_multissid_init() == ESP_ERR_NO_MEM);
    fail_mutex = false;
    assert(cerberus_s3_multissid_init() == ESP_OK);
    assert(cerberus_s3_multissid_init() == ESP_OK);
    assert(state().capacity == 32 && !state().capture.busy);
    fail_take = true;
    assert(cerberus_s3_multissid_start(false) == ESP_ERR_TIMEOUT);
    fail_take = false;
    fail_alloc = true;
    assert(cerberus_s3_multissid_start(false) == ESP_ERR_NO_MEM);
    fail_alloc = false;
    assert(cerberus_s3_multissid_start(false) == ESP_OK);
    assert(options.hopping && options.channel == 0 && !options.filter_bssid && !options.save_pcap);
    assert(options.mode_mask == CERBERUS_WIFI_CAPTURE_MODE_BEACON);
    assert(cerberus_s3_pinescan_init() == ESP_OK && mutex_creations == 2U);
    assert(!pine_state().generation && !pine_state().capture.busy && pine_state().capacity == 32U);
    cerberus_s3_pinescan_stop();
    assert(cerberus_s3_pinescan_release() == ESP_OK && live_allocations == 1U && !stop_calls);
    uint32_t broad = capture.generation;
    assert(cerberus_s3_multissid_start(true) == ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_multissid_release() == ESP_ERR_INVALID_STATE && live_allocations == 1);
    send_frame(broad, 1, 1, "A", 1000, 6, -55);
    send_frame(broad, 1, 1, "ignored duplicate", 1100, 6, -20);
    send_frame(broad + 1, 2, 1, "ignored generation", 1200, 6, -20);
    assert(state().valid_beacons == 1 && state().confirmed == 0);
    clock_us += 100000;
    send_frame(broad, 2, 1, "B", 2000, 6, -60);
    clock_us += 100000;
    send_frame(broad, 3, 1, "C", 3000, 6, -40);
    assert(state().confirmed == 1);
    cerberus_s3_multissid_row_t rows[4];
    cerberus_s3_multissid_page_t page;
    assert(cerberus_s3_multissid_page(0, rows, 4, &page) == ESP_OK);
    assert(page.generation == broad && page.total == 1 && page.copied == 1);
    assert(rows[0].generation == broad && rows[0].first_received_age_ms == 200);
    assert(rows[0].last_received_age_ms == 0 && rows[0].evidence.latest.observed_us == 3000);
    cerberus_s3_multissid_row_t frozen = rows[0];
    clock_us += 300000;
    send_frame(broad, 4, 1, "D", 4000, 6, -45);
    clock_us += 300000;
    send_frame(broad, 5, 1, "stale", 500, 6, -10);
    assert(state().stale_frames == 1 && state().valid_beacons == 5);
    assert(state().received_24ghz == 5 && state().received_5ghz == 0);
    assert(cerberus_s3_multissid_page(0, rows, 1, &page) == ESP_OK);
    assert(rows[0].evidence.frames == 4 && rows[0].evidence.peak_rssi == -40);
    assert(rows[0].last_received_age_ms == 300 && frozen.evidence.latest.observed_us == 3000);
    assert(frozen.evidence.variants[0].bytes[0] == 'A' && frozen.evidence.variants[2].bytes[0] == 'C');
    assert(cerberus_s3_multissid_follow(&frozen, false) == ESP_ERR_INVALID_STATE);
    cerberus_s3_multissid_stop();
    assert(stop_calls == 1 && state().capture.busy);
    assert(cerberus_s3_multissid_release() == ESP_ERR_INVALID_STATE);
    finish();
    assert(!state().capture.busy && state().capture.state == CERBERUS_S3_CAPTURE_COMPLETE);
    fail_alloc = true;
    assert(cerberus_s3_multissid_start(false) == ESP_ERR_NO_MEM);
    fail_alloc = false;
    start_error = ESP_FAIL;
    assert(cerberus_s3_multissid_start(true) == ESP_FAIL);
    start_error = ESP_OK;
    assert(state().confirmed == 1 && live_allocations == 1);
    cerberus_s3_multissid_row_t wrong = frozen;
    wrong.generation++;
    assert(cerberus_s3_multissid_follow(&wrong, false) == ESP_ERR_INVALID_STATE);
    wrong = frozen; wrong.evidence.latest.transmitter[5] = 99;
    assert(cerberus_s3_multissid_follow(&wrong, false) == ESP_ERR_INVALID_ARG);
    assert(cerberus_s3_multissid_follow(&frozen, true) == ESP_OK);
    uint32_t follow = capture.generation;
    assert(follow != broad && !options.hopping && options.channel == 6 && options.save_pcap);
    assert(!options.filter_bssid && state().following && state().generation == broad);
    cerberus_s3_multissid_follow_t signal;
    assert(cerberus_s3_multissid_follow_status(&signal) == ESP_OK && !signal.valid);
    send_frame(follow, 1, 2, "other tx", 5000, 6, -10);
    send_frame(follow, 2, 1, "wrong rx channel", 6000, 11, -10);
    assert(cerberus_s3_multissid_follow_status(&signal) == ESP_OK && !signal.valid);
    send_frame(follow, 3, 1, "follow", 7000, 6, -57);
    clock_us += 400000;
    send_frame(follow, 4, 1, "stale", 6500, 6, -10);
    assert(cerberus_s3_multissid_follow_status(&signal) == ESP_OK && signal.valid);
    assert(signal.generation == follow && signal.matching_beacons == 1 && signal.peak_rssi == -57);
    assert(signal.last_received_age_ms == 400 && signal.latest.observed_us == 7000);
    assert(cerberus_s3_multissid_page(0, rows, 1, &page) == ESP_OK);
    assert(page.generation == broad && rows[0].evidence.latest.observed_us == 4000);
    finish();
    assert(state().capture.state == CERBERUS_S3_CAPTURE_SAVED);
    /* A new owner appearing between status-read and Stop cannot be canceled. */
    assert(cerberus_s3_multissid_follow(&frozen, false) == ESP_OK);
    switch_owner_after_status = true;
    unsigned stopped_before = stop_calls;
    cerberus_s3_multissid_stop();
    assert(stop_calls == stopped_before && !state().capture.busy);
    assert(cerberus_s3_multissid_start(false) == ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_multissid_release() == ESP_OK && live_allocations == 0);
    finish();
    assert(cerberus_s3_multissid_start(false) == ESP_OK);
    broad = capture.generation;
    uint32_t sequence = 0;
    for (uint8_t identity = 1; identity <= 5; ++identity)
        for (unsigned variant = 0; variant < 3; ++variant) {
            char name[2] = {(char)('A' + variant), 0};
            ++sequence;
            send_frame(broad, sequence, identity, name, sequence * 1000ULL, 36, -50);
        }
    assert(state().confirmed == 5);
    assert(state().received_24ghz == 0 && state().received_5ghz == 15);
    assert(cerberus_s3_multissid_page(4, rows, 4, &page) == ESP_OK);
    assert(page.total == 5 && page.copied == 1 && rows[0].evidence.latest.transmitter[5] == 5);
    assert(rows[0].evidence.latest.rx_channel == 36 && rows[0].evidence.latest.advertised_channel == 6);
    assert(cerberus_s3_multissid_page(0, NULL, 0, &page) == ESP_OK && page.total == 5 && !page.copied);
    assert(cerberus_s3_multissid_page(0, rows, 5, &page) == ESP_ERR_INVALID_ARG);
    clock_us += ((int64_t)UINT32_MAX + 1000) * 1000;
    assert(cerberus_s3_multissid_page(0, rows, 1, &page) == ESP_OK);
    assert(rows[0].last_received_age_ms == UINT32_MAX && rows[0].first_received_age_ms == UINT32_MAX);
    for (uint8_t identity = 6; identity <= 33; ++identity) {
        ++sequence;
        send_frame(broad, sequence, identity, "one", sequence * 1000ULL, 36, -50);
    }
    assert(state().transmitters == 32 && state().capacity_drops == 1);
    ++sequence;
    send_frame(broad, sequence, 1, "existing", sequence * 1000ULL, 36, -30);
    assert(state().transmitters == 32 && state().confirmed == 5);
    finish();
    assert(cerberus_s3_multissid_release() == ESP_OK);
    assert(!state().generation && !state().confirmed && allocations == frees && !lock_depth);
    switch_owner_before_start_return = true;
    assert(cerberus_s3_multissid_start(false) == ESP_OK);
    status = state();
    assert(status.generation != capture.generation && status.capture.generation == status.generation);
    assert(!status.capture.busy && status.capture.error_code == ESP_ERR_INVALID_STATE);
    stopped_before = stop_calls;
    cerberus_s3_multissid_stop();
    assert(stop_calls == stopped_before && capture.busy);
    assert(cerberus_s3_multissid_release() == ESP_OK && allocations == frees);
    finish();

    /* Both modes share exactly one lazy bank and mutex. Stale calls through
     * the other typed API may not adopt, stop, follow, or free the new mode. */
    assert(cerberus_s3_pinescan_start(false) == ESP_OK);
    uint32_t pine_generation = capture.generation;
    assert(options.hopping && options.channel == 0U && !options.save_pcap && !options.filter_bssid);
    assert(live_allocations == 1U && mutex_creations == 2U);
    assert(!state().generation && !state().capture.busy && state().confirmed == 0U);
    stopped_before = stop_calls;
    cerberus_s3_multissid_stop();
    assert(stop_calls == stopped_before && cerberus_s3_multissid_release() == ESP_OK);
    assert(live_allocations == 1U && capture.busy);
    assert(cerberus_s3_multissid_page(0U, rows, 4U, &page) == ESP_OK && !page.copied && !page.generation);
    assert(cerberus_s3_multissid_follow(&frozen, false) == ESP_ERR_INVALID_STATE);

    send_pine(pine_generation, 1U, 1U, true, 1000U, 6U, -55);
    assert(pine_state().candidates == 1U && pine_state().matching_beacons == 1U);
    cerberus_s3_pinescan_row_t pine_rows[4], pine_frozen;
    cerberus_s3_pinescan_page_t pine_page;
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 4U, &pine_page) == ESP_OK);
    assert(pine_page.generation == pine_generation && pine_page.total == 1U && pine_page.copied == 1U);
    pine_frozen = pine_rows[0];
    assert(pine_frozen.evidence.last_match.reasons == CERBERUS_WIFI_PINESCAN_REASON_MINIMAL);
    assert(pine_frozen.evidence.last_match.beacon.observed_us == 1000U);
    clock_us += 200000;
    send_pine(pine_generation, 2U, 1U, false, 2000U, 36U, -30);
    send_pine(pine_generation, 2U, 1U, true, 2500U, 6U, -10); /* Same SPI sequence. */
    send_pine(pine_generation + 1U, 3U, 1U, true, 3000U, 6U, -10);
    assert(pine_state().valid_beacons == 2U && pine_state().matching_beacons == 1U);
    assert(pine_state().received_24ghz == 1U && pine_state().received_5ghz == 1U);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 1U, &pine_page) == ESP_OK);
    assert(pine_rows[0].evidence.latest.observed_us == 2000U && pine_rows[0].evidence.latest.rx_channel == 36U);
    assert(pine_rows[0].evidence.latest.capabilities == 0x11U && pine_rows[0].evidence.peak_rssi == -30);
    assert(pine_rows[0].evidence.last_match.beacon.observed_us == 1000U);
    assert(pine_rows[0].evidence.last_match.beacon.capabilities == 1U && !pine_rows[0].evidence.last_match.privacy_bit);
    assert(pine_rows[0].evidence.frames == 2U && pine_rows[0].evidence.matching_frames == 1U);
    assert(pine_rows[0].evidence.multiple_bssids && pine_rows[0].first_received_age_ms == 200U);
    assert(!pine_rows[0].last_received_age_ms && pine_frozen.evidence.latest.observed_us == 1000U);
    clock_us += 100000;
    send_pine(pine_generation, 3U, 1U, true, 1500U, 6U, -10);
    send_pine(pine_generation, 4U, 9U, false, 3000U, 6U, -20);
    send_pine(pine_generation, 5U, 2U, true, 2500U, 6U, -10);
    assert(pine_state().candidates == 1U && pine_state().stale_frames == 2U);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 1U, &pine_page) == ESP_OK);
    assert(pine_rows[0].last_received_age_ms == 100U && pine_rows[0].evidence.frames == 2U);
    assert(cerberus_s3_pinescan_follow(&pine_frozen, false) == ESP_ERR_INVALID_STATE);
    cerberus_s3_pinescan_stop();
    assert(stop_calls == stopped_before + 1U && pine_state().capture.busy);
    assert(cerberus_s3_pinescan_release() == ESP_ERR_INVALID_STATE && live_allocations == 1U);
    finish();

    fail_alloc = true;
    assert(cerberus_s3_multissid_start(false) == ESP_ERR_NO_MEM);
    fail_alloc = false; start_error = ESP_FAIL;
    assert(cerberus_s3_multissid_start(false) == ESP_FAIL);
    start_error = ESP_OK;
    assert(pine_state().generation == pine_generation && pine_state().candidates == 1U);
    assert(live_allocations == 1U && !state().generation);
    cerberus_s3_pinescan_row_t wrong_pine = pine_frozen;
    wrong_pine.generation++;
    assert(cerberus_s3_pinescan_follow(&wrong_pine, false) == ESP_ERR_INVALID_STATE);
    wrong_pine = pine_frozen; wrong_pine.evidence.latest.transmitter[5] = 99U;
    assert(cerberus_s3_pinescan_follow(&wrong_pine, false) == ESP_ERR_INVALID_ARG);
    unsigned allocations_before_follow = allocations;
    assert(cerberus_s3_pinescan_follow(&pine_frozen, true) == ESP_OK);
    assert(allocations == allocations_before_follow && live_allocations == 1U);
    uint32_t pine_follow_generation = capture.generation;
    assert(!options.hopping && options.channel == 6U && options.save_pcap && !options.filter_bssid);
    assert(pine_state().following && pine_state().generation == pine_generation);
    send_pine(pine_follow_generation, 1U, 2U, true, 4000U, 6U, -20);
    send_pine(pine_follow_generation, 2U, 1U, true, 5000U, 36U, -20);
    cerberus_s3_pinescan_follow_t pine_signal;
    assert(cerberus_s3_pinescan_follow_status(&pine_signal) == ESP_OK && !pine_signal.valid);
    send_pine(pine_follow_generation, 3U, 1U, false, 6000U, 6U, -49);
    clock_us += 400000;
    send_pine(pine_follow_generation, 4U, 1U, true, 5500U, 6U, -10);
    assert(cerberus_s3_pinescan_follow_status(&pine_signal) == ESP_OK && pine_signal.valid);
    assert(pine_signal.matching_beacons == 1U && pine_signal.latest.observed_us == 6000U);
    assert(pine_signal.peak_rssi == -49 && pine_signal.last_received_age_ms == 400U);
    assert(cerberus_s3_multissid_follow_status(&signal) == ESP_OK && !signal.valid && !signal.generation);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 1U, &pine_page) == ESP_OK);
    assert(pine_rows[0].evidence.latest.observed_us == 2000U && pine_rows[0].evidence.last_match.beacon.observed_us == 1000U);
    finish();

    /* Successful mode switch replaces only the old bank; stale mode calls
     * must leave the new observer and owner completely alone. */
    cerberus_s3_capture_options_t previous_options = options;
    assert(cerberus_s3_multissid_start(false) == ESP_OK && live_allocations == 1U);
    assert(!pine_state().generation && !pine_state().capture.busy && !pine_state().candidates);
    stopped_before = stop_calls;
    cerberus_s3_pinescan_stop();
    assert(cerberus_s3_pinescan_release() == ESP_OK && stop_calls == stopped_before && live_allocations == 1U);
    assert(cerberus_s3_pinescan_follow(&pine_frozen, false) == ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 4U, &pine_page) == ESP_OK && !pine_page.copied);
    uint8_t late_frame[96];size_t late_size = beacon(late_frame, 1U, (const uint8_t *)"late", 4U, 1U);
    cerberus_s3_capture_observation_t late_meta = {.generation = pine_follow_generation,
        .sequence = 10U, .channel = 6U, .rssi_dbm = -10, .observed_us = 10000U};
    previous_options.observer(&late_meta, late_frame, late_size, previous_options.observer_context);
    assert(state().valid_beacons == 0U);
    finish();
    fail_alloc = true;
    assert(cerberus_s3_pinescan_start(false) == ESP_ERR_NO_MEM);
    fail_alloc = false;start_error = ESP_FAIL;
    assert(cerberus_s3_pinescan_start(false) == ESP_FAIL);
    start_error = ESP_OK;assert(state().generation && !pine_state().generation && live_allocations == 1U);
    assert(cerberus_s3_pinescan_start(false) == ESP_OK && live_allocations == 1U);
    pine_generation = capture.generation;
    for (uint8_t identity = 1U; identity <= 33U; ++identity)
        send_pine(pine_generation, identity, identity, true, identity * 1000ULL, 36U, -55);
    assert(pine_state().candidates == 32U && pine_state().capacity_drops == 1U);
    send_pine(pine_generation, 34U, 1U, false, 34000U, 36U, -20);
    assert(pine_state().candidates == 32U && pine_state().received_5ghz == 34U);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 1U, &pine_page) == ESP_OK);
    assert(pine_rows[0].evidence.frames == 2U && pine_rows[0].evidence.last_match.beacon.observed_us == 1000U);
    assert(cerberus_s3_pinescan_page(28U, pine_rows, 4U, &pine_page) == ESP_OK && pine_page.copied == 4U);
    assert(pine_rows[0].evidence.latest.transmitter[5] == 29U && pine_page.total == 32U);
    assert(cerberus_s3_pinescan_page(0U, NULL, 0U, &pine_page) == ESP_OK && !pine_page.copied && pine_page.total == 32U);
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 5U, &pine_page) == ESP_ERR_INVALID_ARG);
    clock_us += ((int64_t)UINT32_MAX + 1000) * 1000;
    assert(cerberus_s3_pinescan_page(0U, pine_rows, 1U, &pine_page) == ESP_OK);
    assert(pine_rows[0].first_received_age_ms == UINT32_MAX && pine_rows[0].last_received_age_ms == UINT32_MAX);
    finish();
    assert(cerberus_s3_pinescan_release() == ESP_OK && live_allocations == 0U && allocations == frees);
    switch_owner_before_start_return = true;
    assert(cerberus_s3_pinescan_start(false) == ESP_OK);
    assert(pine_state().generation != capture.generation && !pine_state().capture.busy);
    stopped_before = stop_calls;cerberus_s3_pinescan_stop();
    assert(stop_calls == stopped_before && capture.busy);
    assert(cerberus_s3_pinescan_release() == ESP_OK && allocations == frees && !lock_depth);
    assert(mutex_creations == 2U);
    puts("S3 beacon sessions: MultiSSID regression, PineScan proof retention/follow, mode isolation, atomic pages, PSRAM lifecycle, ownership and clocks passed");
    printf("Shared lazy PSRAM bank: %zu host-ABI bytes; one retained bank, temporary second bank only during transactional Start.\n", largest_bank_bytes);
    return 0;
}
