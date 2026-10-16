#include "sentinel_watch_activity.h"
#include <string.h>

void sentinel_watch_activity_init(sentinel_watch_activity_t *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool sentinel_watch_activity_observe(sentinel_watch_activity_t *state,
    const sentinel_watch_activity_sample_t *sample, uint32_t now_ms,
    sentinel_watch_match_t *match)
{
    if (match) memset(match, 0, sizeof(*match));
    if (!state || !sample || !match) return false;
    if (sample->duration_ms < 1000U || sample->duration_ms > 120000U ||
        sample->valid_reports > sample->reports || sample->identities > sample->reports ||
        sample->peak_reports_per_second > sample->reports) {
        state->elevated = 0;
        return false;
    }
    if (state->have_sample && (uint32_t)(now_ms - state->last_sample_ms) > 120000U)
        sentinel_watch_activity_init(state);
    /* Ignore accidental repeats even if called outside the UI's generation gate. */
    if (state->have_sample && now_ms == state->last_sample_ms) return false;
    state->have_sample = true;
    state->last_sample_ms = now_ms;
    uint64_t calculated = (uint64_t)sample->valid_reports * 1000U / sample->duration_ms;
    uint32_t rate = calculated > 50000U ? 50000U : (uint32_t)calculated;
    if (state->learned < 3U) {
        state->baseline_rate = (state->baseline_rate * state->learned + rate) / (state->learned + 1U);
        ++state->learned;
        state->elevated = 0;
        return false;
    }
    uint32_t threshold = state->baseline_rate * 4U;
    if (threshold < 80U) threshold = 80U;
    bool elevated = rate >= threshold && sample->identities >= 16U &&
        sample->peak_reports_per_second >= 100U;
    if (!elevated) {
        state->elevated = 0;
        /* Slow adaptation prevents a single crowded sweep from replacing the
         * baseline. Both rising and falling normal environments are learned. */
        state->baseline_rate = (state->baseline_rate * 7U + rate) / 8U;
        return false;
    }
    if (state->elevated < 2U) ++state->elevated;
    if (state->elevated < 2U) return false;
    *match = (sentinel_watch_match_t){SENTINEL_WATCH_BLE_ACTIVITY,
        SENTINEL_WATCH_CONFIDENCE_LOW, "ble.activity-rise",
        "Unusual BLE activity", "Sustained traffic rise above local baseline"};
    return true;
}
