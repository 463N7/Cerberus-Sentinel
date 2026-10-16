#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sentinel_watch_activity.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static sentinel_watch_activity_t state;
static sentinel_watch_match_t match;
static sentinel_watch_activity_sample_t sample(uint32_t rate, uint16_t identities)
{
    return (sentinel_watch_activity_sample_t){6000U, rate * 6U, rate * 6U, identities, rate};
}
static void calibrate(uint32_t rate, uint32_t now)
{
    sentinel_watch_activity_init(&state);
    sentinel_watch_activity_sample_t s = sample(rate, 5);
    for (unsigned i = 0; i < 3; ++i) assert(!sentinel_watch_activity_observe(&state, &s, now + i * 20000U, &match));
    assert(state.learned == 3 && state.baseline_rate == rate);
}
int main(void)
{
    calibrate(10, 1000);
    sentinel_watch_activity_sample_t high = sample(120, 24);
    assert(!sentinel_watch_activity_observe(&state, &high, 61000, &match));
    assert(!sentinel_watch_activity_observe(&state, &high, 61000, &match));
    assert(sentinel_watch_activity_observe(&state, &high, 81000, &match));
    assert(match.category == SENTINEL_WATCH_BLE_ACTIVITY && match.confidence == SENTINEL_WATCH_CONFIDENCE_LOW);
    assert(state.baseline_rate == 10);
    sentinel_watch_activity_sample_t normal = sample(10, 5);
    assert(!sentinel_watch_activity_observe(&state, &normal, 101000, &match));
    assert(!sentinel_watch_activity_observe(&state, &high, 121000, &match));
    assert(state.elevated == 1 && match.category == SENTINEL_WATCH_NONE);
    assert(!sentinel_watch_activity_observe(&state, &high, 300000, &match));
    assert(state.learned == 1 && state.elevated == 0);

    /* A busy but stable environment, or one noisy address, does not qualify. */
    calibrate(200, 0);
    for (unsigned i = 0; i < 10; ++i) assert(!sentinel_watch_activity_observe(&state, &high, 60000 + i * 10000, &match));
    calibrate(10, 0);
    high.identities = 1;
    assert(!sentinel_watch_activity_observe(&state, &high, 60000, &match));
    assert(!sentinel_watch_activity_observe(&state, &high, 80000, &match));

    /* Malformed samples, overflow-sized counts, and clock rollover. */
    sentinel_watch_activity_sample_t bad = high;
    bad.duration_ms = 999;
    assert(!sentinel_watch_activity_observe(&state, &bad, 100000, &match));
    bad = high; bad.valid_reports = bad.reports + 1U;
    assert(!sentinel_watch_activity_observe(&state, &bad, 100000, &match));
    bad = high; bad.identities = bad.valid_reports + 1U;
    assert(!sentinel_watch_activity_observe(&state, &bad, 100000, &match));
    bad = high; bad.peak_reports_per_second = bad.reports + 1U;
    assert(!sentinel_watch_activity_observe(&state, &bad, 100000, &match));
    calibrate(10, UINT32_MAX - 60000U);
    high = sample(120, 24);
    assert(!sentinel_watch_activity_observe(&state, &high, UINT32_MAX - 10000U, &match));
    assert(sentinel_watch_activity_observe(&state, &high, 10000U, &match));
    assert(state.learned == 3);
    bad = (sentinel_watch_activity_sample_t){1000U, UINT32_MAX, UINT32_MAX, 128U, UINT16_MAX};
    assert(sentinel_watch_activity_observe(&state, &bad, 30000U, &match));
    assert(!sentinel_watch_activity_observe(NULL, &high, 40000, &match));
    assert(!sentinel_watch_activity_observe(&state, NULL, 40000, &match));
    puts("PASS: BLE activity baseline, sustained rise, busy-environment negatives, invalid metrics, duplicate samples, gaps and rollover.");
    return 0;
}
