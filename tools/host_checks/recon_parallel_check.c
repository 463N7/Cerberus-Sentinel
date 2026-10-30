#include "sentinel_scan_session.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static sentinel_scan_observation_t observation(sentinel_scan_tool_t tool,
    sentinel_scan_state_t state, bool busy)
{
    return (sentinel_scan_observation_t){.valid = true, .radio_busy = busy,
        .tool = tool, .state = state};
}

int main(void)
{
    sentinel_scan_session_t wifi, ble;
    sentinel_scan_session_init(&wifi);
    sentinel_scan_session_init(&ble);
    sentinel_scan_session_select(&wifi, SENTINEL_SCAN_WIFI);
    sentinel_scan_session_select(&ble, SENTINEL_SCAN_BLE);

    sentinel_scan_command_t c5 = sentinel_scan_session_step(&wifi, 0,
        observation(SENTINEL_SCAN_NONE, SENTINEL_SCAN_IDLE, false));
    sentinel_scan_command_t s3 = sentinel_scan_session_step(&ble, 0,
        observation(SENTINEL_SCAN_NONE, SENTINEL_SCAN_IDLE, false));
    assert(c5.kind == SENTINEL_SCAN_COMMAND_START && c5.tool == SENTINEL_SCAN_WIFI);
    assert(s3.kind == SENTINEL_SCAN_COMMAND_START && s3.tool == SENTINEL_SCAN_BLE);
    assert(sentinel_scan_session_ack(&wifi, c5, SENTINEL_SCAN_ACCEPTED, 0));
    assert(sentinel_scan_session_ack(&ble, s3, SENTINEL_SCAN_ACCEPTED, 0));

    /* One radio can finish and rearm while the other keeps listening. */
    assert(sentinel_scan_session_step(&wifi, 8000,
        observation(SENTINEL_SCAN_WIFI, SENTINEL_SCAN_COMPLETE, false)).kind ==
        SENTINEL_SCAN_COMMAND_NONE);
    assert(wifi.completed && wifi.active == SENTINEL_SCAN_NONE);
    assert(sentinel_scan_session_step(&ble, 8000,
        observation(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING, false)).kind ==
        SENTINEL_SCAN_COMMAND_NONE);
    assert(ble.active == SENTINEL_SCAN_BLE && !ble.completed);
    sentinel_scan_session_pause(&wifi, false); /* Watch consumed C5 generation. */
    c5 = sentinel_scan_session_step(&wifi, 8100,
        observation(SENTINEL_SCAN_NONE, SENTINEL_SCAN_IDLE, false));
    assert(c5.kind == SENTINEL_SCAN_COMMAND_START && c5.tool == SENTINEL_SCAN_WIFI);
    assert(sentinel_scan_session_ack(&wifi, c5, SENTINEL_SCAN_ACCEPTED, 8100));

    /* A C5-local busy owner does not prevent the independent S3 observer. */
    sentinel_scan_session_pause(&ble, true);
    s3 = sentinel_scan_session_step(&ble, 8200,
        observation(SENTINEL_SCAN_BLE, SENTINEL_SCAN_RUNNING, false));
    assert(s3.kind == SENTINEL_SCAN_COMMAND_STOP && s3.tool == SENTINEL_SCAN_BLE);
    assert(sentinel_scan_session_ack(&ble, s3, SENTINEL_SCAN_ACCEPTED, 8200));
    assert(sentinel_scan_session_step(&ble, 8300,
        observation(SENTINEL_SCAN_BLE, SENTINEL_SCAN_IDLE, false)).kind ==
        SENTINEL_SCAN_COMMAND_NONE);
    sentinel_scan_session_pause(&ble, false);
    s3 = sentinel_scan_session_step(&ble, 8400,
        observation(SENTINEL_SCAN_NONE, SENTINEL_SCAN_IDLE, false));
    assert(s3.kind == SENTINEL_SCAN_COMMAND_START && s3.tool == SENTINEL_SCAN_BLE);
    assert(sentinel_scan_session_ack(&ble, s3, SENTINEL_SCAN_ACCEPTED, 8400));

    sentinel_scan_session_t blocked_wifi;
    sentinel_scan_session_init(&blocked_wifi);
    sentinel_scan_session_select(&blocked_wifi, SENTINEL_SCAN_WIFI);
    assert(sentinel_scan_session_step(&blocked_wifi, 8500,
        observation(SENTINEL_SCAN_NONE, SENTINEL_SCAN_IDLE, true)).kind ==
        SENTINEL_SCAN_COMMAND_NONE);
    assert(ble.active == SENTINEL_SCAN_BLE);

    puts("PASS: independent C5 Wi-Fi and S3 BLE start, rearm, stop and busy gating");
    return 0;
}
