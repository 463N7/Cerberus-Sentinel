#include "remote_id_channel_plan.h"

#include <limits.h>
#include <string.h>

bool cerberus_remote_id_channel_plan_init(cerberus_remote_id_channel_plan_t *plan,
    const uint8_t *channels, size_t count, uint32_t dwell_ms,
    uint32_t duration_ms, uint32_t now_ms)
{
    if (!plan) return false;
    cerberus_remote_id_channel_plan_t next = {0};
    if (!channels || count == 0U || count > CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX ||
        dwell_ms == 0U || dwell_ms > INT32_MAX ||
        duration_ms == 0U || duration_ms > INT32_MAX) {
        *plan = next;
        return false;
    }
    for (size_t i = 0U; i < count; ++i) {
        uint8_t channel = channels[i];
        if (channel == 0U) {
            memset(plan, 0, sizeof(*plan));
            return false;
        }
        bool duplicate = false;
        for (uint8_t j = 0U; j < next.count; ++j)
            if (next.channels[j] == channel) duplicate = true;
        if (!duplicate) next.channels[next.count++] = channel;
        if (channel == 6U) next.has_6 = true;
        if (channel == 149U) next.has_149 = true;
    }
    next.dwell_ms = dwell_ms;
    next.budget_remaining_ms = duration_ms;
    next.last_poll_ms = now_ms;
    next.valid = true;
    *plan = next;
    return true;
}

uint8_t cerberus_remote_id_channel_plan_next_background(
    const cerberus_remote_id_channel_plan_t *plan)
{
    if (!plan || !plan->valid || plan->count == 0U ||
        plan->count > CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX ||
        plan->background_index >= plan->count) return 0U;
    return plan->channels[plan->background_index];
}

bool cerberus_remote_id_channel_plan_seek_background(
    cerberus_remote_id_channel_plan_t *plan, uint8_t channel)
{
    if (channel == 0U || cerberus_remote_id_channel_plan_next_background(plan) == 0U ||
        plan->kind != CERBERUS_REMOTE_ID_CHANNEL_NONE || plan->finished) return false;
    for (uint8_t i = 0U; i < plan->count; ++i) {
        if (plan->channels[i] == channel) {
            plan->background_index = i;
            return true;
        }
    }
    return false;
}

bool cerberus_remote_id_channel_plan_configure_follow(
    cerberus_remote_id_channel_plan_t *plan, uint8_t channel)
{
    if (cerberus_remote_id_channel_plan_next_background(plan) == 0U ||
        plan->kind != CERBERUS_REMOTE_ID_CHANNEL_NONE || plan->finished) return false;
    if (channel != 0U) {
        bool found = false;
        for (uint8_t i = 0U; i < plan->count; ++i)
            if (plan->channels[i] == channel) found = true;
        if (!found) return false;
    }
    plan->target_channel = channel;
    plan->target_next = channel != 0U;
    return true;
}

static void next_channel(cerberus_remote_id_channel_plan_t *plan)
{
    /* At most four phases: background is always present in a valid plan. */
    for (unsigned attempt = 0U; attempt < 4U; ++attempt) {
        uint8_t phase = plan->phase;
        plan->phase = (uint8_t)((phase + 1U) & 3U);
        if ((phase == 0U || phase == 2U) && plan->has_6) {
            plan->channel = 6U;
            plan->kind = CERBERUS_REMOTE_ID_CHANNEL_PRIORITY;
            return;
        }
        if (phase == 1U && plan->has_149) {
            plan->channel = 149U;
            plan->kind = CERBERUS_REMOTE_ID_CHANNEL_PRIORITY;
            return;
        }
        if (phase == 3U) {
            if (plan->target_channel && plan->target_next) {
                plan->channel = plan->target_channel;
                plan->kind = CERBERUS_REMOTE_ID_CHANNEL_TARGET;
                plan->target_next = false;
                return;
            }
            plan->channel = plan->channels[plan->background_index];
            plan->background_index = (uint8_t)((plan->background_index + 1U) % plan->count);
            plan->kind = CERBERUS_REMOTE_ID_CHANNEL_BACKGROUND;
            plan->target_next = plan->target_channel != 0U;
            return;
        }
    }
}

cerberus_remote_id_channel_plan_result_t cerberus_remote_id_channel_plan_poll(
    cerberus_remote_id_channel_plan_t *plan, uint32_t now_ms,
    cerberus_remote_id_channel_slot_t *out)
{
    if (!out) return CERBERUS_REMOTE_ID_PLAN_INVALID;
    memset(out, 0, sizeof(*out));
    if (!plan || !plan->valid || plan->count == 0U ||
        plan->count > CERBERUS_REMOTE_ID_CHANNEL_PLAN_MAX ||
        plan->background_index >= plan->count || plan->phase > 3U)
        return CERBERUS_REMOTE_ID_PLAN_INVALID;
    if (plan->finished) return CERBERUS_REMOTE_ID_PLAN_DONE;
    uint32_t elapsed = now_ms - plan->last_poll_ms;
    if (elapsed > INT32_MAX) {
        plan->valid = false;
        return CERBERUS_REMOTE_ID_PLAN_INVALID;
    }
    plan->last_poll_ms = now_ms;
    if (elapsed >= plan->budget_remaining_ms) {
        plan->budget_remaining_ms = 0U;
        plan->dwell_remaining_ms = 0U;
        plan->finished = true;
        return CERBERUS_REMOTE_ID_PLAN_DONE;
    }
    plan->budget_remaining_ms -= elapsed;
    cerberus_remote_id_channel_plan_result_t result = CERBERUS_REMOTE_ID_PLAN_WAIT;
    if (plan->kind == CERBERUS_REMOTE_ID_CHANNEL_NONE || elapsed >= plan->dwell_remaining_ms) {
        next_channel(plan);
        plan->dwell_remaining_ms = plan->dwell_ms < plan->budget_remaining_ms
            ? plan->dwell_ms : plan->budget_remaining_ms;
        result = CERBERUS_REMOTE_ID_PLAN_TUNE;
    } else {
        plan->dwell_remaining_ms -= elapsed;
    }
    *out = (cerberus_remote_id_channel_slot_t){
        .dwell_remaining_ms = plan->dwell_remaining_ms,
        .budget_remaining_ms = plan->budget_remaining_ms,
        .channel = plan->channel,
        .kind = plan->kind,
    };
    return result;
}
