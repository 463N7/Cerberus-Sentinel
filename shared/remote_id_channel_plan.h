#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX 64U

enum {
    CERBERUS_REMOTE_ID_CHANNEL_NONE = 0,
    CERBERUS_REMOTE_ID_CHANNEL_PRIORITY,
    CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND,
    CERBERUS_REMOTE_ID_CHANNEL_TARGET,
};

typedef enum {
    CERBERUS_REMOTE_ID_PLAN_INVALID = 0,
    CERBERUS_REMOTE_ID_PLAN_WAIT,
    CERBERUS_REMOTE_ID_PLAN_TUNE,
    CERBERUS_REMOTE_ID_PLAN_DONE,
} cerberus_remote_id_channel_plan_result_t;

typedef struct {
    uint32_t dwell_remaining_ms;
    uint32_t budget_remaining_ms;
    uint8_t channel;
    uint8_t kind;
} cerberus_remote_id_channel_slot_t;

/* Caller-owned bounded state; no allocation, radio calls or clock sampling.
 * Treat fields as private. Initialization copies the channel plan. */
typedef struct {
    uint32_t last_poll_ms;
    uint32_t dwell_ms;
    uint32_t dwell_remaining_ms;
    uint32_t budget_remaining_ms;
    uint8_t channels[CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX];
    uint8_t count;
    uint8_t background_index;
    uint8_t phase;
    uint8_t channel;
    uint8_t kind;
    bool has_6;
    bool has_149;
    bool valid;
    bool finished;
    uint8_t target_channel;
    bool target_next;
} cerberus_remote_id_channel_plan_t;

/* channels must already be validated against the configured country/radio
 * plan and accepted by the SDK. This planner is NOT a regulatory validator:
 * it never adds a channel, even the preferred 6/149 slots. Rejects NULL,
 * zero channels, empty/oversized input and durations outside 1..INT32_MAX.
 * Duplicate channels coalesce in first-occurrence order. Failure clears plan.
 * Caller may impose tighter operational dwell/duration limits.
 * Budget starts at now_ms, including delay before the first poll. */
bool cerberus_remote_id_channel_plan_init(cerberus_remote_id_channel_plan_t *plan,
    const uint8_t *channels, size_t count, uint32_t dwell_ms,
    uint32_t duration_ms, uint32_t now_ms);

/* Preserve background progress across finite windows by saving the next
 * channel at completion and seeking it after the next init. Channel identity
 * survives a reordered legal plan; if absent, seek fails without mutation and
 * the caller can retain init's first-channel fallback. Seek is allowed only
 * before the first TUNE. Getter remains available after DONE, returns 0 for
 * an invalid plan, and never claims that a hardware tune succeeded. */
bool cerberus_remote_id_channel_plan_seek_background(
    cerberus_remote_id_channel_plan_t *plan, uint8_t channel);
uint8_t cerberus_remote_id_channel_plan_next_background(
    const cerberus_remote_id_channel_plan_t *plan);

/* Before the first TUNE only: 0 selects the generic plan; otherwise channel
 * must already be in the caller-approved list. Follow alternates TARGET and
 * BACKGROUND in the fourth phase, beginning with TARGET. Only BACKGROUND
 * advances its cursor. Fresh source proof/deadline remain the caller's job;
 * this function does not establish source identity or regulatory permission.
 * Failed configuration does not mutate the plan. */
bool cerberus_remote_id_channel_plan_configure_follow(
    cerberus_remote_id_channel_plan_t *plan, uint8_t channel);

/* Schedule: 6,149,6,fourth phase; unavailable priority slots are omitted.
 * Generic fourth phases use the next background channel; Follow alternates
 * the configured target with the next background channel.
 * Every supplied channel remains in the advancing background rotation.
 * First poll requests a tune. WAIT returns the current slot and remaining
 * dwell; TUNE advances at most once, starting dwell at this poll (no catch-up
 * bursts). Overall budget wins and clips the final dwell. Caller must abort
 * on a rejected hardware tune rather than treating it as successful coverage.
 * Poll at intervals <= INT32_MAX ms using one monotonic modulo-2^32 clock.
 * Ordinary wrap is safe; a detected backward/ambiguous interval fails closed.
 * DONE/clock failure are sticky until reinitialized. INVALID/DONE zero out.
 * A NULL output returns INVALID without advancing the plan. */
cerberus_remote_id_channel_plan_result_t cerberus_remote_id_channel_plan_poll(
    cerberus_remote_id_channel_plan_t *plan, uint32_t now_ms,
    cerberus_remote_id_channel_slot_t *out);
