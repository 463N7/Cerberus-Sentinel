#pragma once

/* Single-caller controller for the ordinary BLE list. No SDK, radio, heap or
 * clock dependencies. Each tick executes at most one bounded adapter call. */
#include <stdbool.h>
#include <stdint.h>

#define CERBERUS_BLE_DISCOVERY_REFRESH_MS 200U
#define CERBERUS_BLE_DISCOVERY_RETRY_MS 250U
#define CERBERUS_BLE_DISCOVERY_STOP_RETRY_MS 1000U
#define CERBERUS_BLE_DISCOVERY_FAILURE_LIMIT 3U
#define CERBERUS_BLE_DISCOVERY_FAMILY_COUNT 7U

enum {
    CERBERUS_BLE_DISCOVERY_IDLE = 0,
    CERBERUS_BLE_DISCOVERY_RUNNING = 1,
    CERBERUS_BLE_DISCOVERY_COMPLETE = 2,
    CERBERUS_BLE_DISCOVERY_ERROR = 3,
};

typedef struct {
    uint64_t scan_epoch, revision;
    int32_t error;
    uint8_t family, state;
    bool owned, stopping;
} cerberus_ble_discovery_observation_t;

typedef struct {
    int32_t error;
    /* Authoritative proof that the requested attachment no longer exists.
     * A failed query or a different byte generation is NOT such proof. */
    bool released;
} cerberus_ble_discovery_operation_t;

typedef struct {
    void *context;
    /* expected_epoch=0 starts a new attachment. Otherwise rearm only that
     * exact completed attachment. Any accepted reservation returns its exact
     * new epoch even if subsequent dispatch is uncertain or fails. */
    cerberus_ble_discovery_operation_t (*start)(void *, uint8_t family,
        uint64_t expected_epoch, cerberus_ble_discovery_observation_t *);
    cerberus_ble_discovery_operation_t (*poll)(void *, uint64_t,
        cerberus_ble_discovery_observation_t *);
    /* Atomically copy and commit a caller-owned presentation bank only after
     * validating exact epoch/family. Duplicate revisions must not refresh
     * source or publication times. Return the committed bank's metadata. */
    cerberus_ble_discovery_operation_t (*copy)(void *, uint64_t, uint8_t family,
        cerberus_ble_discovery_observation_t *);
    cerberus_ble_discovery_operation_t (*stop)(void *, uint64_t,
        cerberus_ble_discovery_observation_t *);
    cerberus_ble_discovery_operation_t (*release)(void *, uint64_t);
} cerberus_ble_discovery_ops_t;

enum {
    CERBERUS_BLE_DISCOVERY_BAD_OPS = -3701,
    CERBERUS_BLE_DISCOVERY_BAD_SOURCE = -3702,
    CERBERUS_BLE_DISCOVERY_BAD_CLOCK = -3703,
};

typedef struct {
    cerberus_ble_discovery_observation_t observation;
    uint64_t epoch, published_epoch, published_revision;
    uint64_t last_tick_ms, next_action_ms, next_stop_ms;
    uint32_t completed_sweeps;
    int32_t error;
    uint8_t requested_family, active_family, failures;
    bool enabled, faulted, drain_requested, stop_sent;
    bool have_clock, have_observation, final_copied, poll_next;
} cerberus_ble_discovery_session_t;

void cerberus_ble_discovery_session_init(cerberus_ble_discovery_session_t *, uint8_t family);
/* Family selection never starts an idle/stopped controller. While enabled it
 * drains/releases the prior family before starting the new request. */
bool cerberus_ble_discovery_session_select(cerberus_ble_discovery_session_t *, uint8_t family);
/* Explicit Start enables continuous finite sweeps; Stop/Back/sleep/selection
 * must call stop immediately. Repeated Start cannot cancel ongoing cleanup. */
bool cerberus_ble_discovery_session_start(cerberus_ble_discovery_session_t *);
void cerberus_ble_discovery_session_stop(cerberus_ble_discovery_session_t *);
bool cerberus_ble_discovery_session_released(const cerberus_ble_discovery_session_t *);
void cerberus_ble_discovery_session_tick(cerberus_ble_discovery_session_t *,
    uint64_t now_ms, const cerberus_ble_discovery_ops_t *);
