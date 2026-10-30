#pragma once
#include "mac_monitor_inventory.h"

typedef struct {
    cerberus_mac_status_t status;
    cerberus_mac_profile_t profile;
    bool valid;
} cerberus_mac_monitor_observation_t;
typedef struct { int32_t error; bool no_ownership; } cerberus_mac_monitor_operation_t;
typedef struct {
    uint64_t request_started_us;
    bool complete, timing_valid;
} cerberus_mac_monitor_transfer_t;
/* One UI task owns this controller. Operations are bounded adapter calls;
 * read_step advances at most four C5 chunks, or one atomic local BLE copy.
 * No getter is allowed to start a radio or adopt an unrequested owner. */
typedef struct {
    void *context;
    uint64_t (*now_us)(void *);
    int32_t (*reserve)(void *, uint8_t radio, cerberus_mac_binding_t *);
    cerberus_mac_monitor_operation_t (*start)(void *, uint8_t, const cerberus_mac_start_request_t *, cerberus_mac_monitor_observation_t *);
    cerberus_mac_monitor_operation_t (*poll)(void *, uint8_t, const cerberus_mac_binding_t *, cerberus_mac_monitor_observation_t *);
    cerberus_mac_monitor_operation_t (*renew)(void *, uint8_t, const cerberus_mac_renew_request_t *, cerberus_mac_monitor_observation_t *);
    cerberus_mac_monitor_operation_t (*read_step)(void *, uint8_t, const cerberus_mac_binding_t *, cerberus_mac_snapshot_t *, cerberus_mac_monitor_transfer_t *);
    cerberus_mac_monitor_operation_t (*ack)(void *, uint8_t, const cerberus_mac_ack_request_t *);
    cerberus_mac_monitor_operation_t (*stop)(void *, uint8_t, const cerberus_mac_binding_t *, cerberus_mac_monitor_observation_t *);
} cerberus_mac_monitor_ops_t;
enum { CERBERUS_MAC_MONITOR_BAD_SOURCE=-3301, CERBERUS_MAC_MONITOR_BAD_SNAPSHOT=-3302,
    CERBERUS_MAC_MONITOR_BAD_CLOCK=-3303, CERBERUS_MAC_MONITOR_BAD_OPS=-3304 };
typedef struct {
    cerberus_mac_binding_t binding;
    cerberus_mac_monitor_observation_t observation;
    cerberus_mac_snapshot_t staging;
    uint64_t last_poll_us, last_renew_us, last_read_us;
    uint32_t pending_ack;
    int32_t error;
    bool owned, attempted, stop_requested, observed, faulted, poll_due, reading;
} cerberus_mac_monitor_radio_t;
typedef struct { cerberus_mac_monitor_radio_t radios[2]; bool running; } cerberus_mac_monitor_session_t;
void cerberus_mac_monitor_session_init(cerberus_mac_monitor_session_t *session);
bool cerberus_mac_monitor_session_start(cerberus_mac_monitor_session_t *session);
void cerberus_mac_monitor_session_stop(cerberus_mac_monitor_session_t *session);
bool cerberus_mac_monitor_session_released(const cerberus_mac_monitor_session_t *session);
void cerberus_mac_monitor_session_tick(cerberus_mac_monitor_session_t *session,
    cerberus_mac_monitor_inventory_t *inventory, const cerberus_mac_monitor_ops_t *ops);
