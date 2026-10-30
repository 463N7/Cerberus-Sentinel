#pragma once
#include "mac_monitor_session.h"

/* Caller owns this storage (normally within the view's single PSRAM store).
 * Initial zeroing is required. Never free/reinitialize an owned context.
 * The adapter detaches it under the scanner mutex before reporting release. */
typedef struct {
    cerberus_mac_observation_t capture;
    cerberus_mac_binding_t binding;
    uint32_t scan_epoch;
    unsigned host_epoch;
    int32_t error;
    uint8_t state;
    bool reserved, started, owned, receiving, stopping;
} cerberus_s3_ble_mac_monitor_t;

/* Creates local radio1 operations without allocating or starting RF. Explicit
 * START owns repeated finite passive sweeps; poll advances that existing job.
 * Read/ACK do not start RF. Lease/dwell/channel-count are zero for this local
 * scanner; a caller that stops polling cannot start further sweeps. */
void cerberus_s3_ble_scanner_mac_ops(cerberus_s3_ble_mac_monitor_t *context,
    cerberus_mac_monitor_ops_t *out);

/* Read existing lease state only. No initialization, allocation, RF or polling.
 * No attached lease yields canonical IDLE/version1 and zero counters. */
void cerberus_s3_ble_scanner_mac_diagnostics(cerberus_mac_status_t *status,
    cerberus_mac_counters_t *counters);
