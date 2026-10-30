#pragma once

#include "remote_id_inventory.h"
#include "sentinel_scan_session.h"

/* Pure UI-task orchestration. The adapters perform bounded calls on existing
 * scan workers; this state owns neither a task nor a new radio implementation. */
enum {
    CERBERUS_REMOTE_ID_ERROR_SOURCE_CHANGED = -2001,
    CERBERUS_REMOTE_ID_ERROR_SNAPSHOT = -2002,
    CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED = -2003,
};
typedef struct {
    uint64_t epoch;
    uint8_t generation, state;
    bool supported, busy, released;
    bool identity_verified; /* Exact requested job ID, not merely a scan byte. */
    int32_t radio_error;
} cerberus_remote_id_radio_observation_t;

typedef struct {
    int32_t error;
    bool rejected; /* Authoritative no-delivery failure; never adopt other work. */
    bool identity_valid;
    bool identity_pending; /* Delivered state unknown; exact-ID poll can bind it. */
    uint8_t generation;
    uint64_t epoch;
} cerberus_remote_id_command_result_t;

typedef struct {
    void *context;
    int32_t (*poll)(void *, uint8_t radio, bool owned, cerberus_remote_id_radio_observation_t *);
    cerberus_remote_id_command_result_t (*command)(void *, uint8_t radio, bool start,
        const cerberus_remote_id_radio_observation_t *before);
    int32_t (*status)(void *, uint8_t radio, uint8_t generation, cerberus_watch_scan_status_payload_t *);
    int32_t (*row)(void *, uint8_t radio, uint8_t generation, uint8_t index, cerberus_watch_observation_t *);
    uint64_t (*now_ms)(void *);
    /* Optional atomic payload/timing getter. If present it replaces row;
     * transport failure cannot fall back to an untimed read. */
    int32_t (*row_observed)(void *, uint8_t radio, uint8_t generation, uint8_t index,
        cerberus_watch_observation_t *, cerberus_watch_receive_timing_t *);
    /* Preferred atomic row/source-age/Location-group/Location-age getter.
     * Failure never falls back to an older partial getter. */
    int32_t (*row_detailed)(void *, uint8_t radio, uint8_t generation, uint8_t index,
        cerberus_watch_observation_t *, cerberus_watch_receive_timing_t *,
        sentinel_watch_motion_t *, cerberus_watch_receive_timing_t *);
} cerberus_remote_id_ops_t;

typedef struct {
    sentinel_scan_session_t scan;
    cerberus_remote_id_radio_observation_t observation;
    uint64_t token, expected_epoch, capture_since_ms, retry_after_ms;
    uint32_t published_sweeps;
    uint8_t expected_generation, rejected_starts;
    int32_t error;
    bool observed, owned, capture_pending, faulted, identity_pending;
} cerberus_remote_id_radio_session_t;

typedef struct {
    cerberus_remote_id_radio_session_t radios[2]; /* Wi-Fi, BLE */
    bool running;
} cerberus_remote_id_session_t;

void cerberus_remote_id_session_init(cerberus_remote_id_session_t *session);
/* Explicit START only; rejected while prior owned work is still draining. */
bool cerberus_remote_id_session_start(cerberus_remote_id_session_t *session);
/* Explicit repeated STOP retries faulted cleanup without starting a scan. */
void cerberus_remote_id_session_stop(cerberus_remote_id_session_t *session);
bool cerberus_remote_id_session_released(const cerberus_remote_id_session_t *session);
void cerberus_remote_id_session_tick(cerberus_remote_id_session_t *session,
    cerberus_remote_id_inventory_t *inventory, const cerberus_remote_id_ops_t *ops);
