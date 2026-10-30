#ifdef NDEBUG
#undef NDEBUG
#endif
#include "field_survey_stream.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static expansion_record_t report(unsigned id, uint8_t radio)
{
    expansion_record_t record = {0};
    record.radio = radio;
    record.bssid[0] = (uint8_t)(id >> 8U);
    record.bssid[1] = (uint8_t)id;
    record.channel = (uint8_t)(1U + id % 11U);
    record.rssi = -40;
    record.ssid_length = 3U;
    memcpy(record.ssid, "lab", 3U);
    return record;
}

int main(void)
{
    field_survey_stream_t *stream = calloc(1U, sizeof(*stream));
    assert(stream != NULL);
    expansion_record_t batch[FIELD_SURVEY_STREAM_CAPACITY];

    /* An unplugged expansion array produces no phantom report. */
    assert(field_survey_stream_pop(stream, batch, 8U) == 0U);
    assert(stream->count == 0U && stream->produced == 0U);

    /* Multiple modules can fill the queue without the viewer's 32-row cap. */
    for (unsigned i = 0U; i < FIELD_SURVEY_STREAM_CAPACITY; ++i) {
        expansion_record_t record = report(i, (uint8_t)(1U + i % 6U));
        assert(field_survey_stream_push(stream, &record));
    }
    expansion_record_t overflow = report(999U, 6U);
    assert(!field_survey_stream_push(stream, &overflow));
    assert(stream->overflow_dropped == 1U);
    assert(stream->overflow_by_radio[5] == 1U);
    assert(stream->count == FIELD_SURVEY_STREAM_CAPACITY);

    /* Stop is allowed to drain every retained report in bounded batches. */
    unsigned consumed = 0U;
    for (;;) {
        size_t count = field_survey_stream_pop(stream, batch, 8U);
        if (count == 0U) break;
        for (size_t i = 0U; i < count; ++i) {
            unsigned id = ((unsigned)batch[i].bssid[0] << 8U) | batch[i].bssid[1];
            assert(id == consumed++);
            assert(batch[i].radio == 1U + id % 6U);
        }
    }
    assert(consumed == FIELD_SURVEY_STREAM_CAPACITY);
    assert(stream->count == 0U && stream->consumed == consumed);
    assert(stream->produced == consumed);

    /* Wraparound keeps FIFO order even after a long running session. */
    for (unsigned i = 0U; i < 1024U; ++i) {
        expansion_record_t record = report(i, 1U);
        assert(field_survey_stream_push(stream, &record));
        assert(field_survey_stream_pop(stream, batch, 1U) == 1U);
        assert(batch[0].bssid[0] == record.bssid[0]);
        assert(batch[0].bssid[1] == record.bssid[1]);
    }

    expansion_record_t bad = report(12U, 0U);
    assert(!field_survey_stream_push(stream, &bad));
    bad = report(12U, 1U);
    bad.ssid_length = sizeof(bad.ssid);
    assert(!field_survey_stream_push(stream, &bad));
    assert(stream->invalid_dropped == 2U);
    assert(stream->count == 0U);
    free(stream);
    return 0;
}
