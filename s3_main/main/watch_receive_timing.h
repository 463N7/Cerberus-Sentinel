#pragma once

#include "cerberus_protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

/* Source-computed elapsed age and an S3 request-start anchor. These are not
 * synchronized clocks or timestamps for individual merged payload fields. */
typedef struct {
    uint64_t request_started_us;
    uint64_t source_age_ms;
    bool valid;
} cerberus_watch_receive_timing_t;

/* A difference of two uint64 microsecond timestamps cannot exceed this age. */
#define CERBERUS_WATCH_MAX_SOURCE_AGE_MS \
    (UINT64_MAX / UINT64_C(1000) + (UINT64_MAX % UINT64_C(1000) != 0U))

static inline bool cerberus_watch_receive_timing_equal(
    const cerberus_watch_receive_timing_t *a, const cerberus_watch_receive_timing_t *b)
{
    return a && b && a->valid == b->valid &&
        a->request_started_us == b->request_started_us && a->source_age_ms == b->source_age_ms;
}

/* Receipt time is stored in whole milliseconds. A request in the same
 * millisecond is allowed; comparing against receipt_ms*1000 would reject it. */
static inline bool cerberus_watch_receive_timing_valid(
    const cerberus_watch_receive_timing_t *timing, uint64_t receipt_ms)
{
    if (!timing) return false;
    if (!timing->valid) return !timing->request_started_us && !timing->source_age_ms;
    return timing->source_age_ms <= CERBERUS_WATCH_MAX_SOURCE_AGE_MS &&
        timing->request_started_us / UINT64_C(1000) <= receipt_ms;
}

/* Caller separately binds the nonzero capture epoch to the row readout and
 * final status. Unavailable timing is canonical all-zero local metadata. */
static inline bool cerberus_watch_receive_timing_decode(
    const cerberus_watch_scan_timing_payload_t *payload, uint8_t radio,
    uint8_t generation, uint8_t index, uint64_t request_started_us,
    cerberus_watch_receive_timing_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!payload || (radio != CERBERUS_WATCH_RADIO_WIFI && radio != CERBERUS_WATCH_RADIO_BLE) ||
        index >= CERBERUS_WATCH_MAX_OBSERVATIONS || payload->radio != radio ||
        payload->generation != generation || payload->index != index ||
        !payload->capture_epoch || payload->reserved[0] || payload->reserved[1] ||
        (payload->flags & ~CERBERUS_WATCH_TIMING_VALID)) return false;
    if (!payload->flags) return payload->source_age_ms == 0U;
    if (payload->source_age_ms > CERBERUS_WATCH_MAX_SOURCE_AGE_MS) return false;
    out->request_started_us = request_started_us;
    out->source_age_ms = payload->source_age_ms;
    out->valid = true;
    return true;
}

/* The source rounds age up at reply creation. Adding elapsed S3 time since
 * request START conservatively includes transport delay without comparing
 * source uptime to S3 uptime. All arithmetic stays full-width. */
static inline bool cerberus_watch_receive_age_ms(
    const cerberus_watch_receive_timing_t *timing, uint64_t now_us, uint64_t *age_ms)
{
    if (!age_ms) return false;
    *age_ms = 0U;
    if (!timing || !timing->valid || timing->source_age_ms > CERBERUS_WATCH_MAX_SOURCE_AGE_MS ||
        now_us < timing->request_started_us) return false;
    uint64_t elapsed = now_us - timing->request_started_us;
    uint64_t elapsed_ms = elapsed / UINT64_C(1000) + (elapsed % UINT64_C(1000) != 0U);
    if (timing->source_age_ms > UINT64_MAX - elapsed_ms) return false;
    *age_ms = timing->source_age_ms + elapsed_ms;
    return true;
}

/* The renderer has a millisecond sample. Use the upper edge of that bin so
 * quantization cannot make the displayed bound younger than that sample. */
static inline bool cerberus_watch_receive_age_at_ms(
    const cerberus_watch_receive_timing_t *timing, uint64_t now_ms, uint64_t *age_ms)
{
    if (!age_ms) return false;
    *age_ms = 0U;
    if (now_ms > (UINT64_MAX - UINT64_C(999)) / UINT64_C(1000)) return false;
    return cerberus_watch_receive_age_ms(timing, now_ms * UINT64_C(1000) + UINT64_C(999), age_ms);
}
