/* These are externally observable workflow tests of the actual scheduler.
 * No radio calls are linked: commands drive an independent simulated radio. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sentinel_scan_session.h"

static sentinel_scan_observation_t idle(void)
{
    return (sentinel_scan_observation_t){.valid = true, .radio_busy = false, .tool = SENTINEL_SCAN_NONE, .state = SENTINEL_SCAN_IDLE};
}

static sentinel_scan_observation_t observed(sentinel_scan_tool_t tool, sentinel_scan_state_t state)
{
    return (sentinel_scan_observation_t){.valid = true, .radio_busy = false, .tool = tool, .state = state};
}

static sentinel_scan_command_t expect(sentinel_scan_session_t *session, uint32_t now, sentinel_scan_observation_t observation, sentinel_scan_command_kind_t kind, sentinel_scan_tool_t tool)
{
    const sentinel_scan_command_t command = sentinel_scan_session_step(session, now, observation);
    if (command.kind != kind || command.tool != tool) {
        fprintf(stderr, "At %u: wanted command %d/%d, got %d/%d (active=%d desired=%d paused=%d fault=%d failures=%u)\n", now, kind, tool, command.kind, command.tool, session->active, session->desired, session->paused, session->faulted, session->failure_count);
        assert(false);
    }
    return command;
}

static void nothing(sentinel_scan_session_t *session, uint32_t now, sentinel_scan_observation_t observation)
{
    (void)expect(session, now, observation, SENTINEL_SCAN_COMMAND_NONE, SENTINEL_SCAN_NONE);
}

static void acknowledge(sentinel_scan_session_t *session, sentinel_scan_command_t command, sentinel_scan_result_t result, uint32_t now)
{
    assert(sentinel_scan_session_ack(session, command, result, now));
    assert(!sentinel_scan_session_ack(session, command, result, now)); /* duplicate rejected */
}

static sentinel_scan_session_t start(sentinel_scan_tool_t tool, uint32_t now)
{
    sentinel_scan_session_t session;
    sentinel_scan_session_init(&session);
    sentinel_scan_session_select(&session, tool);
    sentinel_scan_command_t command = expect(&session, now, idle(), SENTINEL_SCAN_COMMAND_START, tool);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, now);
    return session;
}

static void test_single_sweep_and_pause(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_WIFI, 100U);
    nothing(&session, 200U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    nothing(&session, 800U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE));
    assert(session.active == SENTINEL_SCAN_NONE && session.completed);
    nothing(&session, 1999U, idle());
    nothing(&session, 2000U, idle());
    sentinel_scan_session_select(&session, SENTINEL_SCAN_WIFI);
    assert(session.completed); /* rendering the same page does not rearm */
    nothing(&session, 3600000U, idle());
    nothing(&session, UINT32_MAX, idle());
    nothing(&session, 100U, idle()); /* completion survives timer wrap too */
    sentinel_scan_session_pause(&session, false);
    assert(!session.completed);
    sentinel_scan_command_t command = expect(&session, 2000U, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 2000U);

    sentinel_scan_session_pause(&session, true);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_WIFI);
    assert(session.paused); /* repeated page mapping cannot unpause */
    command = expect(&session, 2100U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 2100U);
    nothing(&session, 2200U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    nothing(&session, 2300U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_IDLE));
    nothing(&session, 100000U, idle());
    sentinel_scan_session_pause(&session, false);
    command = expect(&session, 100001U, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 100001U);
    nothing(&session, 100500U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE));
    assert(session.completed);
    nothing(&session, 1000000U, idle());
}

static void test_switch_requires_release(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_WIFI, 0U);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_BLE);
    sentinel_scan_command_t command = expect(&session, 10U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    nothing(&session, 20U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING)); /* ack outstanding */
    sentinel_scan_command_t stale = command;
    ++stale.token;
    assert(!sentinel_scan_session_ack(&session, stale, SENTINEL_SCAN_ACCEPTED, 20U));
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 30U);
    nothing(&session, 40U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    nothing(&session, 50U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_IDLE)); /* wrong observation */
    command = expect(&session, 60U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_BLE);
    assert(!session.completed); /* Wi-Fi completion cannot complete BLE */
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 60U);

    sentinel_scan_session_select(&session, SENTINEL_SCAN_NONE);
    command = expect(&session, 70U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_BLE);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 70U);
    nothing(&session, 80U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_IDLE));
    nothing(&session, 100000U, idle());
}

static void test_all_tools_single_capture(void)
{
    for (sentinel_scan_tool_t tool = SENTINEL_SCAN_WIFI; tool < SENTINEL_SCAN_TOOL_COUNT; ++tool) {
        sentinel_scan_session_t session = start(tool, 0U);
        nothing(&session, 100U, observed(tool, SENTINEL_SCAN_RUNNING));
        nothing(&session, 1000U, observed(tool, SENTINEL_SCAN_COMPLETE));
        assert(session.completed && session.active == SENTINEL_SCAN_NONE);
        for (unsigned poll = 0U; poll < 100U; ++poll) {
            sentinel_scan_session_select(&session, tool);
            nothing(&session, 1000U + poll * 1000000U, idle());
        }
        sentinel_scan_session_pause(&session, false);
        const sentinel_scan_command_t command = expect(&session, 100000001U, idle(), SENTINEL_SCAN_COMMAND_START, tool);
        acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 100000001U);
        nothing(&session, 100001000U, observed(tool, SENTINEL_SCAN_COMPLETE));
        assert(session.completed);
        nothing(&session, 100002000U, idle());
    }
}

static void test_explicit_rearm_while_draining(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_WIFI, 0U);
    sentinel_scan_session_pause(&session, true);
    sentinel_scan_command_t command = expect(&session, 10U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    sentinel_scan_session_pause(&session, false); /* while STOP acknowledgement is pending */
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 20U);
    nothing(&session, 30U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    assert(!session.completed);
    command = expect(&session, 40U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_IDLE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 40U);
    nothing(&session, 100U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE));
    assert(session.completed);
    nothing(&session, 100000U, idle());

    session = start(SENTINEL_SCAN_ACTIVITY, 0U);
    sentinel_scan_session_pause(&session, true);
    nothing(&session, 100U, observed(SENTINEL_SCAN_ACTIVITY, SENTINEL_SCAN_RUNNING));
    sentinel_scan_session_pause(&session, false);
    nothing(&session, 200U, observed(SENTINEL_SCAN_ACTIVITY, SENTINEL_SCAN_RUNNING));
    command = expect(&session, 16000U, observed(SENTINEL_SCAN_ACTIVITY, SENTINEL_SCAN_COMPLETE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_ACTIVITY);
    assert(!session.completed); /* old bounded sweep cannot satisfy new arm */
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 16000U);
    nothing(&session, 32000U, observed(SENTINEL_SCAN_ACTIVITY, SENTINEL_SCAN_COMPLETE));
    assert(session.completed);
    nothing(&session, 100000U, idle());
}

static void test_bounded_modes_drain(void)
{
    const sentinel_scan_tool_t modes[] = {SENTINEL_SCAN_ENERGY, SENTINEL_SCAN_BEACON, SENTINEL_SCAN_ACTIVITY};
    for (size_t index = 0U; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        sentinel_scan_session_t session = start(modes[index], 0U);
        sentinel_scan_session_select(&session, SENTINEL_SCAN_WIFI);
        nothing(&session, 1000U, observed(modes[index], SENTINEL_SCAN_RUNNING));
        nothing(&session, 16000U, observed(modes[index], SENTINEL_SCAN_RUNNING));
        sentinel_scan_command_t command = expect(&session, 17000U, observed(modes[index], SENTINEL_SCAN_COMPLETE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
        acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 17000U);

        session = start(modes[index], 0U);
        sentinel_scan_session_pause(&session, true);
        nothing(&session, 500U, observed(modes[index], SENTINEL_SCAN_RUNNING));
        nothing(&session, 17000U, observed(modes[index], SENTINEL_SCAN_COMPLETE));
        nothing(&session, 100000U, idle());

        session = start(modes[index], 0U);
        sentinel_scan_session_set_stop_supported(&session, modes[index], true);
        sentinel_scan_session_select(&session, SENTINEL_SCAN_NONE);
        command = expect(&session, 10U, observed(modes[index], SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, modes[index]);
        acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 10U);
        nothing(&session, 20U, observed(modes[index], SENTINEL_SCAN_IDLE));
    }
}

static void test_failures_and_busy(void)
{
    sentinel_scan_session_t session;
    sentinel_scan_session_init(&session);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_BLE);
    sentinel_scan_observation_t blocked = idle();
    blocked.radio_busy = true;
    nothing(&session, 0U, blocked); /* existing connection is untouched */
    sentinel_scan_observation_t unknown = idle();
    unknown.valid = false;
    nothing(&session, 10U, unknown);
    uint32_t now = 20U;
    for (unsigned attempt = 0U; attempt < SENTINEL_SCAN_FAILURE_LIMIT; ++attempt) {
        const sentinel_scan_command_t command = expect(&session, now, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_BLE);
        acknowledge(&session, command, SENTINEL_SCAN_FAILED, now);
        assert(session.active == SENTINEL_SCAN_BLE); /* timeout delivery is ambiguous */
        nothing(&session, now + 1U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_ERROR));
        assert(session.failure_count == attempt + 1U); /* not double charged */
        const uint32_t backoff = 1000U << attempt;
        nothing(&session, now + backoff - 1U, idle());
        now += backoff;
    }
    assert(session.faulted);
    nothing(&session, now + 100000U, idle());
    sentinel_scan_session_select(&session, SENTINEL_SCAN_BLE);
    assert(session.faulted); /* page refresh cannot accidentally defeat cap */
    sentinel_scan_session_pause(&session, false);
    sentinel_scan_command_t command = expect(&session, now, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_BLE);
    acknowledge(&session, command, SENTINEL_SCAN_BUSY, now);
    nothing(&session, now + 1U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_IDLE));
    assert(!session.faulted && session.failure_count == 0U);
    nothing(&session, now + 999U, idle());
    command = expect(&session, now + 1000U, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_BLE);
    acknowledge(&session, command, SENTINEL_SCAN_FAILED, now + 1000U);
    nothing(&session, now + 1010U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING));
    assert(session.failure_count == 0U); /* START reached radio despite timeout */
    assert(session.active == SENTINEL_SCAN_BLE);
}

static void test_failed_stop_and_timeout(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_DEFENSE, 0U);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_BLE);
    uint32_t now = 10U;
    for (unsigned attempt = 0U; attempt < SENTINEL_SCAN_FAILURE_LIMIT; ++attempt) {
        const sentinel_scan_command_t command = expect(&session, now, observed(SENTINEL_SCAN_DEFENSE, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_DEFENSE);
        acknowledge(&session, command, SENTINEL_SCAN_FAILED, now);
        nothing(&session, now + (1000U << attempt) - 1U, observed(SENTINEL_SCAN_DEFENSE, SENTINEL_SCAN_RUNNING));
        now += 1000U << attempt;
    }
    assert(session.faulted);
    nothing(&session, now + 100000U, observed(SENTINEL_SCAN_DEFENSE, SENTINEL_SCAN_RUNNING));
    /* A failed STOP may still have reached the radio. Fresh completion is
     * sufficient evidence to release it, even after retries were exhausted. */
    sentinel_scan_command_t command = expect(&session, now + 100001U, observed(SENTINEL_SCAN_DEFENSE, SENTINEL_SCAN_COMPLETE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_BLE);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, now + 100001U);
    assert(!session.faulted);

    session = start(SENTINEL_SCAN_WIFI, 0U);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_BLE);
    command = expect(&session, 10U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 10U);
    nothing(&session, 3009U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    nothing(&session, 3010U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    assert(session.failure_count == 1U);
    nothing(&session, 4009U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    command = expect(&session, 4010U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 4010U);
}

static void test_scan_error_results(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_ENERGY, 0U);
    uint32_t now = 100U;
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        nothing(&session, now, observed(SENTINEL_SCAN_ENERGY, SENTINEL_SCAN_ERROR));
        assert(session.failure_count == attempt + 1U);
        now += 1000U << attempt;
        if (attempt < 2U) {
            const sentinel_scan_command_t command = expect(&session, now, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_ENERGY);
            acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, now);
            now += 100U;
        }
    }
    assert(session.faulted);
    nothing(&session, now + 100000U, idle());
}

static void test_rollover_and_navigation_during_command(void)
{
    sentinel_scan_session_t session = start(SENTINEL_SCAN_WIFI, UINT32_MAX - 1000U);
    nothing(&session, UINT32_MAX - 500U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE));
    nothing(&session, 698U, idle());
    assert(session.completed);
    sentinel_scan_session_pause(&session, false);
    sentinel_scan_command_t command = expect(&session, 699U, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_NONE);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 700U);
    command = expect(&session, 701U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_WIFI);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 702U);
    nothing(&session, 703U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_RUNNING));
    command = expect(&session, 704U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_IDLE), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_WIFI);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 704U);
    nothing(&session, 1904U, observed(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE));
    assert(session.completed);
    nothing(&session, 200000U, idle());

    session = start(SENTINEL_SCAN_BLE, UINT32_MAX - 1000U);
    sentinel_scan_session_select(&session, SENTINEL_SCAN_NONE);
    command = expect(&session, UINT32_MAX - 999U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING), SENTINEL_SCAN_COMMAND_STOP, SENTINEL_SCAN_BLE);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, UINT32_MAX - 999U);
    nothing(&session, 1999U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING));
    assert(session.failure_count == 0U);
    nothing(&session, 2000U, observed(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING));
    assert(session.failure_count == 1U);

    session = start(SENTINEL_SCAN_ENERGY, UINT32_MAX - 800U);
    nothing(&session, UINT32_MAX - 500U, observed(SENTINEL_SCAN_ENERGY, SENTINEL_SCAN_ERROR));
    nothing(&session, 498U, idle());
    command = expect(&session, 499U, idle(), SENTINEL_SCAN_COMMAND_START, SENTINEL_SCAN_ENERGY);
    acknowledge(&session, command, SENTINEL_SCAN_ACCEPTED, 499U);
    nothing(&session, 1000U, observed(SENTINEL_SCAN_ENERGY, SENTINEL_SCAN_COMPLETE));
    assert(session.completed);
}

static uint32_t random_state = 0xCEB315U;
static uint32_t random_value(void)
{
    random_state = random_state * 1664525U + 1013904223U;
    return random_state;
}

static void test_radio_simulation(void)
{
    sentinel_scan_session_t session;
    sentinel_scan_session_init(&session);
    sentinel_scan_tool_t radio = SENTINEL_SCAN_NONE;
    unsigned work_left = 0U;
    unsigned starts = 0U;
    unsigned stops = 0U;
    uint32_t now = UINT32_MAX - 25000U;
    for (unsigned tick = 0U; tick < 10000U; ++tick, now += 200U) {
        if (work_left != 0U && --work_left == 0U) { radio = SENTINEL_SCAN_NONE; }
        if (tick % 53U == 0U) { sentinel_scan_session_select(&session, (sentinel_scan_tool_t)(random_value() % SENTINEL_SCAN_TOOL_COUNT)); }
        if (tick % 31U == 0U) { sentinel_scan_session_pause(&session, true); }
        if (tick % 47U == 0U) { sentinel_scan_session_pause(&session, false); }
        const bool other_connection = tick % 73U < 3U && radio == SENTINEL_SCAN_NONE;
        sentinel_scan_observation_t observation = {
            .valid = tick % 41U != 0U,
            .radio_busy = other_connection || (radio != SENTINEL_SCAN_NONE && radio != session.active),
            .tool = session.active,
            .state = radio != SENTINEL_SCAN_NONE && radio == session.active ? SENTINEL_SCAN_RUNNING : SENTINEL_SCAN_COMPLETE,
        };
        const sentinel_scan_command_t command = sentinel_scan_session_step(&session, now, observation);
        if (command.kind == SENTINEL_SCAN_COMMAND_NONE) { continue; }
        const unsigned fate = random_value() % 10U;
        if (command.kind == SENTINEL_SCAN_COMMAND_START) {
            ++starts;
            assert(radio == SENTINEL_SCAN_NONE && !other_connection);
            if (fate >= 2U) { radio = command.tool; work_left = 3U + random_value() % 80U; }
            /* Fate 2 delivers START but loses the transport reply. */
            acknowledge(&session, command, fate < 3U ? SENTINEL_SCAN_FAILED : SENTINEL_SCAN_ACCEPTED, now);
        } else {
            ++stops;
            assert(command.kind == SENTINEL_SCAN_COMMAND_STOP && radio == command.tool);
            assert(command.tool == SENTINEL_SCAN_WIFI || command.tool == SENTINEL_SCAN_BLE || command.tool == SENTINEL_SCAN_DEFENSE);
            if (fate >= 2U && work_left > 3U) { work_left = 3U; } /* asynchronous stop */
            acknowledge(&session, command, fate < 3U ? SENTINEL_SCAN_FAILED : SENTINEL_SCAN_ACCEPTED, now);
        }
    }
    assert(starts > 100U && stops > 25U);
    printf("Simulation: %u starts, %u stops, 10,000 transitions; no concurrent starts or implicit connections\n", starts, stops);
}

int main(void)
{
    test_single_sweep_and_pause();
    test_all_tools_single_capture();
    test_explicit_rearm_while_draining();
    test_switch_requires_release();
    test_bounded_modes_drain();
    test_failures_and_busy();
    test_failed_stop_and_timeout();
    test_scan_error_results();
    test_rollover_and_navigation_during_command();
    test_radio_simulation();
    puts("PASS: one sweep per selection, explicit Scan again, pause/rearm while draining, safe switching, ambiguity, retry cap/backoff, busy/unknown status, stale ACK, and time rollover");
    return 0;
}
