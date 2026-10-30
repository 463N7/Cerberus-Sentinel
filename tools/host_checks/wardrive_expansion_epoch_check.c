#ifdef NDEBUG
#undef NDEBUG
#endif
#include "field_survey.h"
#include "field_survey_stream.h"
#include "wardrive_core.h"
#include "wardrive_service.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static field_survey_snapshot_t state;
static uint8_t replace_at;
#include "wardrive_expansion_add_impl.h"

#define portENTER_CRITICAL(lock) ((void)0)
#define portEXIT_CRITICAL(lock) ((void)0)
static field_survey_snapshot_t expansion_stage;
static expansion_record_t expansion_event;
static uint32_t expansion_dropped_at_start_by_slot[6];
static cerberus_wardrive_status_t wardrive_status;
static field_survey_stream_t stream;
static bool queue_open, stop_flag, stop_after_first;
static uint8_t appended_ids[FIELD_SURVEY_STREAM_CAPACITY];
static unsigned appended_count;

void field_survey_snapshot(field_survey_snapshot_t *out) { *out = state; }
size_t field_survey_stream_read(expansion_record_t *out, size_t capacity,
    uint32_t *overflow_dropped, uint32_t *invalid_dropped, uint16_t *queued,
    uint32_t overflow_by_radio[6])
{
    field_survey_stream_t *current = queue_open ? &stream : NULL;
    size_t count = field_survey_stream_pop(current, out, capacity);
    if (overflow_dropped) *overflow_dropped = current ? current->overflow_dropped : 0U;
    if (invalid_dropped) *invalid_dropped = current ? current->invalid_dropped : 0U;
    if (queued) *queued = current ? current->count : 0U;
    if (overflow_by_radio) {
        if (current) memcpy(overflow_by_radio, current->overflow_by_radio,
            sizeof(current->overflow_by_radio));
        else memset(overflow_by_radio, 0, 6U * sizeof(uint32_t));
    }
    return count;
}
static bool wants_stop(void) { return stop_flag; }
static esp_err_t append_observation(const cerberus_wardrive_observation_t *observation)
{
    assert(appended_count < sizeof(appended_ids));
    appended_ids[appended_count++] = observation->address[5];
    if (stop_after_first && appended_count == 1U) stop_flag = true;
    return ESP_OK;
}
#include "wardrive_expansion_capture_impl.h"

static expansion_record_t report(unsigned id, uint8_t radio)
{
    expansion_record_t record = {
        .bssid = {0x10, 0x20, 0x30, 0x40, 0x50, (uint8_t)id},
        .radio = radio, .channel = 6, .rssi = -60,
        .ssid_length = 4, .ssid = "Test",
    };
    return record;
}

int main(void)
{
    /* The manual viewer cache still replaces identities after 32 results. */
    for (unsigned i = 1U; i <= FIELD_SURVEY_CAPACITY + 8U; ++i) {
        expansion_record_t record = report(i, (uint8_t)(1U + i % 6U));
        add(&record);
    }
    assert(state.count == FIELD_SURVEY_CAPACITY);
    assert(state.evicted == 8U);

    /* A disconnected expansion group produces no phantom Wardrive rows. */
    assert(capture_expansions(true) == ESP_OK);
    assert(appended_count == 0U && wardrive_status.expansion_queue_pending == 0U);

    queue_open = true;
    state.present_mask = state.scanning_mask = 0x3FU;
    state.radio_dropped_by_slot[0] = 3U;
    expansion_dropped_at_start_by_slot[0] = 1U;
    for (unsigned i = 1U; i <= FIELD_SURVEY_CAPACITY + 8U; ++i) {
        expansion_record_t record = report(i, (uint8_t)(1U + i % 6U));
        assert(field_survey_stream_push(&stream, &record));
    }
    stop_after_first = true;
    assert(capture_expansions(false) == ESP_ERR_NOT_FINISHED);
    assert(appended_count == 1U && appended_ids[0] == 1U);
    assert(stream.count == FIELD_SURVEY_CAPACITY + 7U);
    stop_after_first = false;
    assert(capture_expansions(true) == ESP_OK);
    assert(appended_count == FIELD_SURVEY_CAPACITY + 8U);
    for (unsigned i = 0U; i < appended_count; ++i)
        assert(appended_ids[i] == i + 1U);
    assert(stream.count == 0U && wardrive_status.expansion_queue_pending == 0U);
    assert(wardrive_status.expansion_radio_dropped_by_slot[0] == 2U);
    assert(wardrive_status.expansion_radio_dropped == 2U);
    assert(wardrive_status.expansion_present_mask == 0x3FU);

    memset(&stream, 0, sizeof(stream));
    appended_count = 0U;
    stop_flag = false;
    for (unsigned i = 0U; i < FIELD_SURVEY_STREAM_CAPACITY; ++i) {
        expansion_record_t record = report(i, 2U);
        assert(field_survey_stream_push(&stream, &record));
    }
    expansion_record_t extra = report(999U, 2U);
    assert(!field_survey_stream_push(&stream, &extra));
    assert(capture_expansions(true) == ESP_OK);
    assert(appended_count == FIELD_SURVEY_STREAM_CAPACITY);
    assert(stream.count == 0U);
    assert(wardrive_status.expansion_queue_overflow == 1U);
    assert(wardrive_status.expansion_queue_overflow_by_slot[1] == 1U);
    puts("wardrive expansion queue, overflow, missing slots and Stop drain: pass");
    return 0;
}
