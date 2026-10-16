#include "sentinel_scan_session.h"

#include <stddef.h>
#include <string.h>

static bool valid_tool(sentinel_scan_tool_t tool)
{
    return tool >= SENTINEL_SCAN_NONE && tool < SENTINEL_SCAN_TOOL_COUNT;
}

static sentinel_scan_command_t no_command(void)
{
    return (sentinel_scan_command_t){SENTINEL_SCAN_COMMAND_NONE, SENTINEL_SCAN_NONE, 0U};
}

static void clear_retry(sentinel_scan_session_t *session)
{
    session->failure_count = 0U;
    session->faulted = false;
    session->waiting = false;
}

static void arm_once(sentinel_scan_session_t *session)
{
    session->completed = false;
    if (++session->arm_generation == 0U) { ++session->arm_generation; }
    clear_retry(session);
}

static void wait_for(sentinel_scan_session_t *session, uint32_t now_ms, uint32_t duration_ms)
{
    session->waiting = true;
    session->wait_since_ms = now_ms;
    session->wait_duration_ms = duration_ms;
}

static bool ready(sentinel_scan_session_t *session, uint32_t now_ms)
{
    if (session->faulted) { return false; }
    if (!session->waiting) { return true; }
    if ((uint32_t)(now_ms - session->wait_since_ms) < session->wait_duration_ms) {
        return false;
    }
    session->waiting = false;
    return true;
}

static void failure(sentinel_scan_session_t *session, uint32_t now_ms)
{
    if (session->failure_count < SENTINEL_SCAN_FAILURE_LIMIT) { ++session->failure_count; }
    session->faulted = session->failure_count >= SENTINEL_SCAN_FAILURE_LIMIT;
    const uint32_t multiplier = 1U << (session->failure_count - 1U);
    wait_for(session, now_ms, SENTINEL_SCAN_RETRY_BASE_MS * multiplier);
}

static sentinel_scan_command_t command(sentinel_scan_session_t *session, sentinel_scan_command_kind_t kind, sentinel_scan_tool_t tool)
{
    if (++session->next_token == 0U) { ++session->next_token; }
    session->pending = (sentinel_scan_command_t){kind, tool, session->next_token};
    if (kind == SENTINEL_SCAN_COMMAND_START) {
        /* A transport timeout can still mean START reached the coprocessor.
         * Track the tool before sending so a fresh query resolves ambiguity. */
        session->active = tool;
        session->active_arm_generation = session->arm_generation;
        session->command_failed = false;
    }
    return session->pending;
}

void sentinel_scan_session_init(sentinel_scan_session_t *session)
{
    if (session == NULL) { return; }
    memset(session, 0, sizeof(*session));
    session->stoppable_mask = (1U << SENTINEL_SCAN_WIFI) |
        (1U << SENTINEL_SCAN_BLE) | (1U << SENTINEL_SCAN_DEFENSE);
}

void sentinel_scan_session_select(sentinel_scan_session_t *session, sentinel_scan_tool_t tool)
{
    if (session == NULL || !valid_tool(tool) || session->desired == tool) { return; }
    session->desired = tool;
    session->paused = false;
    arm_once(session);
}

void sentinel_scan_session_select_snapshot(sentinel_scan_session_t *session, sentinel_scan_tool_t tool)
{
    if (session == NULL || !valid_tool(tool) || tool == SENTINEL_SCAN_NONE || session->desired == tool) { return; }
    sentinel_scan_session_select(session, tool);
    session->completed = true;
}

void sentinel_scan_session_pause(sentinel_scan_session_t *session, bool paused)
{
    if (session == NULL) { return; }
    if (!paused) { arm_once(session); }
    else if (!session->paused) { clear_retry(session); }
    session->paused = paused;
}

void sentinel_scan_session_set_stop_supported(sentinel_scan_session_t *session, sentinel_scan_tool_t tool, bool supported)
{
    if (session == NULL || !valid_tool(tool) || tool == SENTINEL_SCAN_NONE) { return; }
    if (supported) { session->stoppable_mask |= 1U << tool; }
    else { session->stoppable_mask &= ~(1U << tool); }
}

sentinel_scan_command_t sentinel_scan_session_step(sentinel_scan_session_t *session, uint32_t now_ms, sentinel_scan_observation_t observation)
{
    if (session == NULL || session->pending.kind != SENTINEL_SCAN_COMMAND_NONE || !observation.valid) {
        return no_command();
    }
    if (session->active != SENTINEL_SCAN_NONE) {
        if (observation.tool != session->active || observation.state < SENTINEL_SCAN_IDLE ||
            observation.state > SENTINEL_SCAN_ERROR) {
            return no_command();
        }
        const bool keep_scanning = !session->paused && !session->completed && session->desired == session->active;
        const bool current_arm = session->active_arm_generation == session->arm_generation;
        if (observation.state == SENTINEL_SCAN_RUNNING) {
            if (session->command_failed) {
                /* Fresh RUNNING proves a failed/ambiguous START was delivered. */
                session->command_failed = false;
                clear_retry(session);
            }
            if (session->stop_requested) {
                if ((uint32_t)(now_ms - session->stop_since_ms) >= SENTINEL_SCAN_STOP_WAIT_MS) {
                    session->stop_requested = false;
                    failure(session, now_ms);
                }
                return no_command();
            }
            if (!keep_scanning && (session->stoppable_mask & (1U << session->active)) != 0U && ready(session, now_ms)) {
                return command(session, SENTINEL_SCAN_COMMAND_STOP, session->active);
            }
            /* A non-stoppable bounded operation drains naturally. No new mode
             * is ever started while the prior operation is still running. */
            return no_command();
        }

        const bool commanded_stop = session->stop_requested;
        const bool failed_start = session->command_failed;
        session->active = SENTINEL_SCAN_NONE;
        session->stop_requested = false;
        session->command_failed = false;
        if (!keep_scanning || !current_arm || commanded_stop) {
            /* A fresh idle/complete/error state confirms release, even if a
             * preceding STOP transport call failed. Switching is now safe. */
            clear_retry(session);
        } else if (failed_start) {
            /* Preserve the backoff already recorded by ack(). Do not charge
             * the same failure twice for its subsequent ERROR status. */
        } else if (observation.state == SENTINEL_SCAN_ERROR || observation.state == SENTINEL_SCAN_IDLE) {
            /* Unexpected IDLE did not produce a completed capture. Retry with
             * the same bounded policy as a reported scan error. */
            failure(session, now_ms);
        } else {
            clear_retry(session);
            session->completed = true;
        }
    }

    if (session->desired == SENTINEL_SCAN_NONE || session->paused || session->completed || observation.radio_busy || !ready(session, now_ms)) {
        return no_command();
    }
    return command(session, SENTINEL_SCAN_COMMAND_START, session->desired);
}

bool sentinel_scan_session_ack(sentinel_scan_session_t *session, sentinel_scan_command_t completed, sentinel_scan_result_t result, uint32_t now_ms)
{
    if (session == NULL || completed.kind == SENTINEL_SCAN_COMMAND_NONE ||
        completed.kind != session->pending.kind || completed.tool != session->pending.tool ||
        completed.token != session->pending.token || result < SENTINEL_SCAN_ACCEPTED || result > SENTINEL_SCAN_FAILED) {
        return false;
    }
    session->pending = no_command();
    if (completed.kind == SENTINEL_SCAN_COMMAND_START) {
        session->command_failed = result != SENTINEL_SCAN_ACCEPTED;
        if (result == SENTINEL_SCAN_FAILED) { failure(session, now_ms); }
        else if (result == SENTINEL_SCAN_BUSY) { wait_for(session, now_ms, SENTINEL_SCAN_RETRY_BASE_MS); }
    } else if (result == SENTINEL_SCAN_ACCEPTED) {
        session->stop_requested = true;
        session->stop_since_ms = now_ms;
    } else {
        session->stop_requested = false;
        if (result == SENTINEL_SCAN_FAILED) { failure(session, now_ms); }
        else { wait_for(session, now_ms, SENTINEL_SCAN_RETRY_BASE_MS); }
    }
    return true;
}
