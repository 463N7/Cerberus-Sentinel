#pragma once

#include "csi_motion_model.h"

/* UI-task controller only. The caller keeps this state, the model and the
 * renderer cache in one on-demand PSRAM allocation. No allocator/task/RF call
 * occurs until an explicit Start is serviced by tick through the supplied ops. */
typedef struct {
    uint8_t bssid[6], channel, ssid_length;
    uint8_t ssid[32];
} cerberus_csi_target_t;

typedef struct {
    cerberus_csi_status_t status;
    cerberus_csi_clock_t clock;
    cerberus_csi_counters_t counters;
    cerberus_csi_target_payload_t target;
    bool status_valid, timing_valid;
} cerberus_csi_observation_t;

typedef struct {
    int32_t error;
    /* Only authoritative pre-send rejection, peer replacement or exact-job
     * absence. A timeout/malformed reply must NEVER assert this flag. */
    bool no_ownership;
} cerberus_csi_operation_t;

typedef struct {
    void *context;
    /* Read-only preflight + globally unique job reservation. The adapter owns
     * the expected peer proof and retains it through all following calls. */
    int32_t (*reserve)(void *, const cerberus_csi_target_t *, cerberus_csi_binding_t *);
    cerberus_csi_operation_t (*start)(void *, const cerberus_csi_start_request_t *, cerberus_csi_observation_t *);
    cerberus_csi_operation_t (*poll)(void *, const cerberus_csi_binding_t *, cerberus_csi_observation_t *);
    cerberus_csi_operation_t (*renew)(void *, const cerberus_csi_renew_request_t *, cerberus_csi_observation_t *);
    cerberus_csi_operation_t (*stop)(void *, const cerberus_csi_binding_t *, cerberus_csi_observation_t *);
    /* One complete CRC-verified immutable record, or available=false. */
    cerberus_csi_operation_t (*read)(void *, const cerberus_csi_binding_t *, cerberus_csi_sample_t *, bool *available);
    cerberus_csi_operation_t (*ack)(void *, const cerberus_csi_ack_request_t *);
    uint64_t (*now_us)(void *);
} cerberus_csi_ops_t;

enum {
    CERBERUS_CSI_SESSION_READY,
    CERBERUS_CSI_SESSION_STARTING,
    CERBERUS_CSI_SESSION_RUNNING,
    CERBERUS_CSI_SESSION_STOPPING,
    CERBERUS_CSI_SESSION_STOPPED,
    CERBERUS_CSI_SESSION_ERROR,
};
enum {
    CERBERUS_CSI_SESSION_BAD_SOURCE = -3201,
    CERBERUS_CSI_SESSION_BAD_CLOCK = -3202,
    CERBERUS_CSI_SESSION_BAD_RECORD = -3203,
    CERBERUS_CSI_SESSION_BAD_OPS = -3204,
};

typedef struct {
    cerberus_csi_target_t target;
    cerberus_csi_motion_model_t model;
    cerberus_csi_observation_t observation;
    cerberus_csi_sample_t sample;
    cerberus_csi_binding_t binding;
    uint64_t last_poll_us, last_renew_us, last_transfer_us;
    uint64_t clock_requested_us, latest_sample_us;
    uint32_t last_sequence, pending_ack;
    int32_t error;
    uint8_t state;
    bool valid, owned, start_requested, stop_requested, recalibrate_requested;
    bool observed, model_ready, sample_seen, poll_due, transfer_due;
} cerberus_csi_session_t;

typedef struct {
    cerberus_csi_target_t target;
    cerberus_csi_motion_view_t motion;
    uint64_t job_id, epoch, receive_age_ms;
    uint32_t accepted, invalid, overflow, acked;
    int32_t error;
    uint8_t state;
    bool busy, owned, receive_age_valid, link_stale, recalibrating, source_stale;
} cerberus_csi_session_view_t;

bool cerberus_csi_target_valid(const cerberus_csi_target_t *target);
bool cerberus_csi_session_init(cerberus_csi_session_t *session, const cerberus_csi_target_t *target);
bool cerberus_csi_session_start(cerberus_csi_session_t *session);
void cerberus_csi_session_stop(cerberus_csi_session_t *session);
bool cerberus_csi_session_recalibrate(cerberus_csi_session_t *session);
bool cerberus_csi_session_released(const cerberus_csi_session_t *session);
void cerberus_csi_session_tick(cerberus_csi_session_t *session, const cerberus_csi_ops_t *ops);
/* Pure value copy; no controller work, no model time advancement. */
void cerberus_csi_session_view(const cerberus_csi_session_t *session,
    uint64_t now_us, cerberus_csi_session_view_t *view);
