#include "s3_ble_discovery_session.h"

#include <stddef.h>
#include <string.h>

static uint64_t after(uint64_t now, uint32_t delay)
{
    return UINT64_MAX - now < delay ? UINT64_MAX : now + delay;
}

static void fault(cerberus_ble_discovery_session_t *s, int32_t error)
{
    s->error = error ? error : CERBERUS_BLE_DISCOVERY_BAD_SOURCE;
    s->faulted = true;
    s->enabled = false;
    s->drain_requested = s->epoch != 0U;
}

static void forget_attachment(cerberus_ble_discovery_session_t *s)
{
    s->epoch = 0;
    s->have_observation = false;
    s->drain_requested = false;
    s->stop_sent = false;
    s->final_copied = false;
    s->poll_next = false;
    s->next_action_ms = 0;
    s->next_stop_ms = 0;
    memset(&s->observation, 0, sizeof(s->observation));
}

static bool valid_observation(const cerberus_ble_discovery_session_t *s,
    const cerberus_ble_discovery_observation_t *o)
{
    return o->scan_epoch == s->epoch && o->family == s->active_family &&
        o->state <= CERBERUS_BLE_DISCOVERY_ERROR &&
        (!s->have_observation || o->revision >= s->observation.revision) &&
        (o->state != CERBERUS_BLE_DISCOVERY_RUNNING || o->owned || o->stopping);
}

static bool accept(cerberus_ble_discovery_session_t *s,
    const cerberus_ble_discovery_observation_t *o)
{
    if (!valid_observation(s, o)) {
        fault(s, CERBERUS_BLE_DISCOVERY_BAD_SOURCE);
        s->poll_next = true;
        return false;
    }
    s->observation = *o;
    s->have_observation = true;
    if (o->state == CERBERUS_BLE_DISCOVERY_ERROR) fault(s, o->error);
    if (o->state == CERBERUS_BLE_DISCOVERY_IDLE && s->enabled && !s->drain_requested)
        fault(s, o->error);
    return true;
}

static void failed(cerberus_ble_discovery_session_t *s, int32_t error,
    uint64_t now)
{
    s->error = error ? error : CERBERUS_BLE_DISCOVERY_BAD_SOURCE;
    if (s->failures < UINT8_MAX) ++s->failures;
    if (s->failures >= CERBERUS_BLE_DISCOVERY_FAILURE_LIMIT) fault(s, s->error);
    s->poll_next = true;
    s->next_action_ms = after(now, CERBERUS_BLE_DISCOVERY_RETRY_MS);
}

static void absent(cerberus_ble_discovery_session_t *s,
    cerberus_ble_discovery_operation_t result)
{
    const bool expected = s->drain_requested || !s->enabled;
    forget_attachment(s);
    if (!expected) fault(s, result.error ? result.error : CERBERUS_BLE_DISCOVERY_BAD_SOURCE);
}

void cerberus_ble_discovery_session_init(cerberus_ble_discovery_session_t *s,
    uint8_t family)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    if (family < CERBERUS_BLE_DISCOVERY_FAMILY_COUNT) s->requested_family = family;
}

bool cerberus_ble_discovery_session_select(cerberus_ble_discovery_session_t *s,
    uint8_t family)
{
    if (!s || family >= CERBERUS_BLE_DISCOVERY_FAMILY_COUNT) return false;
    if (family == s->requested_family) return true;
    s->requested_family = family;
    if (s->epoch) {
        s->drain_requested = true;
        s->next_action_ms = 0;
        s->next_stop_ms = 0;
    }
    return true;
}

bool cerberus_ble_discovery_session_start(cerberus_ble_discovery_session_t *s)
{
    if (!s) return false;
    if (s->enabled && !s->faulted) return true;
    s->enabled = true;
    s->faulted = false;
    s->failures = 0;
    s->error = 0;
    s->next_action_ms = 0;
    return true;
}

void cerberus_ble_discovery_session_stop(cerberus_ble_discovery_session_t *s)
{
    if (!s) return;
    const bool changed = s->enabled || (s->epoch && !s->drain_requested);
    s->enabled = false;
    s->drain_requested = s->epoch != 0U;
    if (changed) {
        s->next_action_ms = 0;
        s->next_stop_ms = 0;
    }
}

bool cerberus_ble_discovery_session_released(const cerberus_ble_discovery_session_t *s)
{
    return !s || s->epoch == 0U;
}

static void start(cerberus_ble_discovery_session_t *s, uint64_t now,
    const cerberus_ble_discovery_ops_t *ops)
{
    cerberus_ble_discovery_observation_t o = {0};
    const uint64_t previous = s->epoch;
    const uint8_t family = s->requested_family;
    cerberus_ble_discovery_operation_t result = ops->start(ops->context, family, previous, &o);
    if (result.released) {
        forget_attachment(s);
        fault(s, result.error);
        return;
    }
    /* The local Start contract returns its reservation even on dispatch
     * failure. Never adopt a guessed generation or a global running status. */
    if (o.scan_epoch > previous) {
        s->epoch = o.scan_epoch;
        s->active_family = family;
        s->have_observation = false;
        s->final_copied = false;
        s->stop_sent = false;
        s->poll_next = false;
        s->next_stop_ms = 0;
        (void)accept(s, &o);
    } else if (!result.error) {
        fault(s, CERBERUS_BLE_DISCOVERY_BAD_SOURCE);
    }
    if (result.error) {
        fault(s, result.error);
        s->poll_next = s->epoch != 0U;
    } else if (!s->faulted) {
        s->failures = 0;
        s->error = 0;
    }
    s->next_action_ms = now;
}

void cerberus_ble_discovery_session_tick(cerberus_ble_discovery_session_t *s,
    uint64_t now, const cerberus_ble_discovery_ops_t *ops)
{
    if (!s) return;
    if (!ops || !ops->start || !ops->poll || !ops->copy || !ops->stop || !ops->release) {
        fault(s, CERBERUS_BLE_DISCOVERY_BAD_OPS);
        return;
    }
    if (s->have_clock && now < s->last_tick_ms) {
        fault(s, CERBERUS_BLE_DISCOVERY_BAD_CLOCK);
        s->next_action_ms = 0;
        s->next_stop_ms = 0;
    }
    s->have_clock = true;
    s->last_tick_ms = now;
    if (now < s->next_action_ms) return;
    if (!s->epoch) {
        if (s->enabled) start(s, now, ops);
        return;
    }

    if (!s->enabled || s->requested_family != s->active_family) s->drain_requested = true;
    const bool radio_released = s->have_observation &&
        !s->observation.owned && !s->observation.stopping;
    if (s->drain_requested && radio_released) {
        cerberus_ble_discovery_operation_t result = ops->release(ops->context, s->epoch);
        if (!result.error || result.released) forget_attachment(s);
        else {
            s->error = result.error;
            s->poll_next = true;
            s->next_action_ms = after(now, CERBERUS_BLE_DISCOVERY_RETRY_MS);
        }
        return;
    }
    if (!s->drain_requested && radio_released && s->final_copied) {
        start(s, now, ops);
        return;
    }

    cerberus_ble_discovery_observation_t o = {0};
    if (s->drain_requested && now >= s->next_stop_ms) {
        cerberus_ble_discovery_operation_t result = ops->stop(ops->context, s->epoch, &o);
        s->stop_sent = true;
        s->next_stop_ms = after(now, CERBERUS_BLE_DISCOVERY_STOP_RETRY_MS);
        s->poll_next = true;
        if (result.released) absent(s, result);
        else if (result.error) failed(s, result.error, now);
        else {
            (void)accept(s, &o);
            s->next_action_ms = after(now, CERBERUS_BLE_DISCOVERY_REFRESH_MS);
        }
        return;
    }
    if (s->poll_next || s->drain_requested) {
        cerberus_ble_discovery_operation_t result = ops->poll(ops->context, s->epoch, &o);
        s->poll_next = false;
        if (result.released) absent(s, result);
        else if (result.error) failed(s, result.error, now);
        else {
            (void)accept(s, &o);
            s->next_action_ms = (!o.owned && !o.stopping) ? now :
                after(now, CERBERUS_BLE_DISCOVERY_REFRESH_MS);
        }
        return;
    }

    cerberus_ble_discovery_operation_t result = ops->copy(ops->context, s->epoch,
        s->active_family, &o);
    if (result.released) {
        absent(s, result);
    } else if (result.error) {
        failed(s, result.error, now);
    } else if (accept(s, &o)) {
        s->published_epoch = o.scan_epoch;
        s->published_revision = o.revision;
        s->failures = 0;
        if (!s->faulted) s->error = 0;
        if (o.state == CERBERUS_BLE_DISCOVERY_COMPLETE && !o.owned && !o.stopping) {
            s->final_copied = true;
            if (s->completed_sweeps != UINT32_MAX) ++s->completed_sweeps;
            s->next_action_ms = now;
        } else {
            s->next_action_ms = after(now, CERBERUS_BLE_DISCOVERY_REFRESH_MS);
        }
    }
}
