#pragma once

#include "sentinel_watch_detect.h"

/* Per-sweep environmental measurements; identities is a bounded lower bound,
 * not a device count. No inference about a transmitter or attack tool. */
typedef struct {
    uint32_t duration_ms, reports, valid_reports;
    uint16_t identities, peak_reports_per_second;
} sentinel_watch_activity_sample_t;

typedef struct {
    uint32_t baseline_rate, last_sample_ms;
    uint8_t learned, elevated;
    bool have_sample;
} sentinel_watch_activity_t;

void sentinel_watch_activity_init(sentinel_watch_activity_t *state);
/* Learns three complete sweeps, then requires two elevated sweeps. A long gap
 * restarts calibration. Invalid samples never alert or train the baseline. */
bool sentinel_watch_activity_observe(sentinel_watch_activity_t *state,
    const sentinel_watch_activity_sample_t *sample, uint32_t now_ms,
    sentinel_watch_match_t *match);
