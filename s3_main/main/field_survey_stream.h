#pragma once

#include "expansion_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* An opt-in FIFO for the integrated Wardriving session. The field survey's
 * 32-identity viewer cache remains independent; that cache can replace an
 * identity while the FIFO still preserves every accepted report in order.
 * Callers serialize these operations with field_survey's state_lock. */
#define FIELD_SURVEY_STREAM_CAPACITY 256U
typedef struct {
    expansion_record_t records[FIELD_SURVEY_STREAM_CAPACITY];
    uint16_t head, count;
    uint32_t produced, consumed, overflow_dropped, invalid_dropped;
    uint32_t overflow_by_radio[6];
} field_survey_stream_t;

static inline void field_survey_stream_increment(uint32_t *counter)
{
    if (*counter != UINT32_MAX) ++*counter;
}

static inline bool field_survey_stream_push(field_survey_stream_t *stream,
    const expansion_record_t *record)
{
    if (stream == NULL || record == NULL) return false;
    if (record->radio < 1U || record->radio > 6U ||
        record->ssid_length > sizeof(record->ssid) - 1U) {
        field_survey_stream_increment(&stream->invalid_dropped);
        return false;
    }
    if (stream->count == FIELD_SURVEY_STREAM_CAPACITY) {
        /* Preserve FIFO order and count a new report that cannot be kept. */
        field_survey_stream_increment(&stream->overflow_dropped);
        field_survey_stream_increment(&stream->overflow_by_radio[record->radio - 1U]);
        return false;
    }
    uint16_t tail = (uint16_t)((stream->head + stream->count) %
        FIELD_SURVEY_STREAM_CAPACITY);
    stream->records[tail] = *record;
    ++stream->count;
    field_survey_stream_increment(&stream->produced);
    return true;
}

static inline size_t field_survey_stream_pop(field_survey_stream_t *stream,
    expansion_record_t *out, size_t capacity)
{
    if (stream == NULL || out == NULL || capacity == 0U) return 0U;
    size_t count = stream->count < capacity ? stream->count : capacity;
    for (size_t i = 0U; i < count; ++i) {
        out[i] = stream->records[stream->head];
        stream->head = (uint16_t)((stream->head + 1U) %
            FIELD_SURVEY_STREAM_CAPACITY);
        --stream->count;
        field_survey_stream_increment(&stream->consumed);
    }
    return count;
}
