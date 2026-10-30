#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Value-only aggregate: no UI/PSRAM pointer, source address, claimed ID,
 * coordinates, or RF timestamp. Radio order is Wi-Fi, then S3 BLE. */
typedef struct {
    uint32_t completed_sweeps;
    int32_t error;
    uint16_t dropped_reports;
    uint8_t retained_sources;
    bool owned;
} cerberus_remote_id_radio_diagnostics_t;

typedef struct {
    cerberus_remote_id_radio_diagnostics_t radios[2];
    uint64_t wifi_job_id;
    uint32_t wifi_window_elapsed_ms;
    uint16_t wifi_lease_remaining_ms;
    uint8_t wifi_job_state, wifi_channel;
    bool wifi_priority, wifi_profile_valid;
    bool open, running, stopping, selected, present;
    uint8_t wifi_follow_flags;
} cerberus_remote_id_diagnostics_t;

enum {
    CERBERUS_REMOTE_ID_DIAG_FOLLOW_ENABLED = 1U, /* UI desired profile. */
    CERBERUS_REMOTE_ID_DIAG_FOLLOW_REQUESTED = 2U, /* Current valid C5 status. */
    CERBERUS_REMOTE_ID_DIAG_FOLLOW_BOUND = 4U,
};

/* The UI publishes a private value copy after changes and clears on close.
 * NULL clears the snapshot. No radio, storage, allocation, or callbacks occur.
 * get copies one coherent value under a bounded critical section; NULL is a
 * no-op. Initial state is closed/zero, including before the UI first opens. */
void cerberus_remote_id_diagnostics_publish(const cerberus_remote_id_diagnostics_t *snapshot);
void cerberus_remote_id_diagnostics_get(cerberus_remote_id_diagnostics_t *snapshot);
