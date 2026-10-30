#include "remote_id_session.h"
#include <limits.h>
#include <string.h>

static uint8_t radio_id(unsigned slot)
{ return slot ? CERBERUS_WATCH_RADIO_BLE : CERBERUS_WATCH_RADIO_WIFI; }
static sentinel_scan_tool_t tool_id(unsigned slot)
{ return slot ? SENTINEL_SCAN_BLE : SENTINEL_SCAN_WIFI; }

void cerberus_remote_id_session_init(cerberus_remote_id_session_t *session)
{
    if (!session) return;
    memset(session, 0, sizeof(*session));
    for (unsigned i = 0; i < 2; ++i) sentinel_scan_session_init(&session->radios[i].scan);
}

bool cerberus_remote_id_session_released(const cerberus_remote_id_session_t *session)
{
    if (!session) return true;
    for (unsigned i = 0; i < 2; ++i) {
        const cerberus_remote_id_radio_session_t *r = &session->radios[i];
        if (r->owned || r->scan.active != SENTINEL_SCAN_NONE ||
            r->scan.pending.kind != SENTINEL_SCAN_COMMAND_NONE) return false;
    }
    return true;
}

bool cerberus_remote_id_session_start(cerberus_remote_id_session_t *session)
{
    if (!session || session->running || !cerberus_remote_id_session_released(session)) return false;
    session->running = true;
    for (unsigned i = 0; i < 2; ++i) {
        cerberus_remote_id_radio_session_t *r = &session->radios[i];
        sentinel_scan_session_init(&r->scan);
        sentinel_scan_session_select(&r->scan, tool_id(i));
        r->capture_pending = r->faulted = r->identity_pending = false;
        r->rejected_starts = 0;
        r->retry_after_ms = 0;
        r->error = 0;
    }
    return true;
}

void cerberus_remote_id_session_stop(cerberus_remote_id_session_t *session)
{
    if (!session) return;
    session->running = false;
    for (unsigned i = 0; i < 2; ++i) {
        cerberus_remote_id_radio_session_t *r = &session->radios[i];
        r->capture_pending = false;
        /* Re-arm only the pure scheduler's retry state, immediately pause it
         * before any step. No START command can be emitted by this operation. */
        if (r->scan.faulted) sentinel_scan_session_pause(&r->scan, false);
        sentinel_scan_session_pause(&r->scan, true);
    }
}

static void reject_start(cerberus_remote_id_radio_session_t *r, unsigned slot, uint64_t now)
{
    /* A known rejected START did not create an owned job. Reset only our local
     * scheduler; a foreign RUNNING scan must never become ours via ack(BUSY). */
    sentinel_scan_session_init(&r->scan);
    sentinel_scan_session_select(&r->scan, tool_id(slot));
    r->owned = false;
    r->identity_pending = false;
    if (r->rejected_starts < 3) ++r->rejected_starts;
    r->faulted = r->rejected_starts >= 3;
    r->retry_after_ms = now + (UINT64_C(1000) << (r->rejected_starts - 1U));
}

static int32_t capture(cerberus_remote_id_radio_session_t *r, unsigned slot,
    cerberus_remote_id_inventory_t *inventory, const cerberus_remote_id_ops_t *ops)
{
    uint8_t radio = radio_id(slot);
    cerberus_watch_scan_status_payload_t before = {0}, after = {0};
    int32_t error = ops->status(ops->context, radio, r->expected_generation, &before);
    if (error) return error;
    if (before.radio != radio || before.generation != r->expected_generation ||
        before.state != CERBERUS_WATCH_COMPLETE || before.stored_count > CERBERUS_WATCH_MAX_OBSERVATIONS)
        return CERBERUS_REMOTE_ID_ERROR_SNAPSHOT;
    for (uint8_t i = 0; i < before.stored_count; ++i) {
        memset(&inventory->staging_timings[i], 0, sizeof(inventory->staging_timings[i]));
        memset(&inventory->staging_motions[i], 0, sizeof(inventory->staging_motions[i]));
        memset(&inventory->staging_motion_timings[i], 0, sizeof(inventory->staging_motion_timings[i]));
        error = ops->row_detailed ?
            ops->row_detailed(ops->context, radio, r->expected_generation, i,
                &inventory->staging[i], &inventory->staging_timings[i],
                &inventory->staging_motions[i], &inventory->staging_motion_timings[i]) : ops->row_observed ?
            ops->row_observed(ops->context, radio, r->expected_generation, i,
                &inventory->staging[i], &inventory->staging_timings[i]) :
            ops->row(ops->context, radio, r->expected_generation, i, &inventory->staging[i]);
        if (error) return error;
    }
    error = ops->status(ops->context, radio, r->expected_generation, &after);
    if (error) return error;
    cerberus_remote_id_radio_observation_t end = {0};
    error = ops->poll(ops->context, radio, false, &end);
    if (error) return error;
    if (!end.supported || !end.released || end.state != SENTINEL_SCAN_COMPLETE ||
        end.radio_error || end.generation != r->expected_generation || end.epoch != r->expected_epoch)
        return CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED;
    cerberus_remote_id_publish_result_t result = cerberus_remote_id_inventory_publish_detailed(inventory,
        r->token, &before, &after, inventory->staging, inventory->staging_timings,
        inventory->staging_motions, inventory->staging_motion_timings,
        before.stored_count, ops->now_ms(ops->context));
    return result == CERBERUS_REMOTE_ID_PUBLISHED ? 0 : CERBERUS_REMOTE_ID_ERROR_SNAPSHOT;
}

void cerberus_remote_id_session_tick(cerberus_remote_id_session_t *session,
    cerberus_remote_id_inventory_t *inventory, const cerberus_remote_id_ops_t *ops)
{
    if (!session || !inventory || !ops || !ops->poll || !ops->command ||
        !ops->status || (!ops->row && !ops->row_observed && !ops->row_detailed) || !ops->now_ms) return;
    if (!session->running && cerberus_remote_id_session_released(session)) return;
    for (unsigned i = 0; i < 2; ++i) {
        cerberus_remote_id_radio_session_t *r = &session->radios[i];
        uint64_t now = ops->now_ms(ops->context);
        cerberus_remote_id_radio_observation_t observation = {0};
        int32_t error = ops->poll(ops->context, radio_id(i), r->owned, &observation);
        r->observed = error == 0;
        if (error) { r->error = error; continue; }
        r->observation = observation;
        if (observation.state > SENTINEL_SCAN_ERROR) {
            r->error = CERBERUS_REMOTE_ID_ERROR_SNAPSHOT; continue;
        }
        /* Only an adapter's exact requested-ID match may resolve a lost START
         * reply. A peer restart still invalidates ownership before adoption. */
        if (r->owned && r->identity_pending && observation.identity_verified &&
            observation.epoch == r->expected_epoch) {
            r->expected_generation = observation.generation;
            r->identity_pending = false;
            /* Exact job proof also resolves a job that completed before its
             * first successful poll; it need not be observed RUNNING first. */
            r->scan.command_failed = false;
        }
        if (r->owned && (observation.epoch != r->expected_epoch ||
            observation.generation != r->expected_generation)) {
            bool undelivered = r->scan.command_failed && observation.released &&
                observation.epoch == r->expected_epoch && observation.state != SENTINEL_SCAN_RUNNING;
            r->error = CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED;
            reject_start(r, i, now);
            /* A different source generation/epoch is not ours to stop. */
            if (!undelivered) r->faulted = true;
            continue;
        }
        if (!observation.supported) continue;
        if (r->capture_pending) {
            if (!session->running) { r->capture_pending = false; continue; }
            error = capture(r, i, inventory, ops);
            r->error = error;
            if (!error) {
                r->capture_pending = false;
                if (r->published_sweeps != UINT32_MAX) ++r->published_sweeps;
                sentinel_scan_session_pause(&r->scan, false);
            } else if (now < r->capture_since_ms || now - r->capture_since_ms >= 15000U) {
                r->capture_pending = false; r->faulted = true;
            }
            continue;
        }
        if (r->faulted && !r->owned) continue;
        if (now < r->retry_after_ms && !r->owned) continue;
        if (!session->running) sentinel_scan_session_pause(&r->scan, true);
        /* Terminal status is not release while the local host is still
         * finishing its callback/lease cleanup. Preserve ownership until then. */
        if (r->owned && observation.state != SENTINEL_SCAN_RUNNING && !observation.released) continue;
        bool had_owned = r->owned;
        sentinel_scan_command_t command = sentinel_scan_session_step(&r->scan, (uint32_t)now,
            (sentinel_scan_observation_t){.valid = true, .radio_busy = observation.busy,
                .tool = r->scan.active, .state = observation.state});
        if (had_owned && r->scan.active == SENTINEL_SCAN_NONE) {
            r->owned = false;
            if (session->running && r->scan.completed && !observation.radio_error &&
                observation.state == SENTINEL_SCAN_COMPLETE) {
                r->capture_pending = true; r->capture_since_ms = now;
            } else if (observation.radio_error) r->error = observation.radio_error;
        }
        if (command.kind == SENTINEL_SCAN_COMMAND_NONE) continue;
        bool start = command.kind == SENTINEL_SCAN_COMMAND_START;
        if (start && r->token == UINT64_MAX) {
            sentinel_scan_session_init(&r->scan);
            r->error = CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED; r->faulted = true; continue;
        }
        cerberus_remote_id_command_result_t result = ops->command(ops->context, radio_id(i), start, &observation);
        (void)sentinel_scan_session_ack(&r->scan, command,
            result.error ? SENTINEL_SCAN_FAILED : SENTINEL_SCAN_ACCEPTED, (uint32_t)now);
        r->error = result.error;
        if (!start) continue;
        if (result.error && result.rejected) { reject_start(r, i, now); continue; }
        ++r->token;
        r->owned = true;
        r->expected_generation = result.identity_valid ? result.generation : (uint8_t)(observation.generation + 1U);
        r->expected_epoch = result.identity_valid ? result.epoch : observation.epoch;
        r->identity_pending = result.identity_pending && !result.identity_valid;
        if (!result.error) r->rejected_starts = 0;
    }
}
