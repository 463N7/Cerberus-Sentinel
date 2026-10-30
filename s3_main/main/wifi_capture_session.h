#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CERBERUS_S3_CAPTURE_IDLE,
    CERBERUS_S3_CAPTURE_STARTING,
    CERBERUS_S3_CAPTURE_RUNNING,
    CERBERUS_S3_CAPTURE_STOPPING,
    CERBERUS_S3_CAPTURE_SAVED,
    CERBERUS_S3_CAPTURE_ERROR,
    CERBERUS_S3_CAPTURE_COMPLETE,
} cerberus_s3_capture_state_t;

typedef struct {
    uint32_t generation, sequence;
    uint8_t channel;
    int8_t rssi_dbm;
    uint64_t observed_us; /* C5 receive-callback monotonic clock, not S3/UTC. */
} cerberus_s3_capture_observation_t;

/* CRC-verified complete bytes without FCS. Metadata/frame pointers are valid
 * only during this callback. Runs once per pinned generation/sequence on the
 * capture worker, outside its status lock. Do not block on UI/SD or retain them. */
typedef void (*cerberus_s3_capture_observer_t)(
    const cerberus_s3_capture_observation_t *metadata,
    const uint8_t *frame, size_t length, void *context);

typedef struct {
    uint8_t channel, mode_mask;
    bool filter_bssid, hopping, save_pcap;
    uint8_t bssid[6];
    cerberus_s3_capture_observer_t observer;
    void *observer_context; /* Remains caller-owned until status.busy is false. */
} cerberus_s3_capture_options_t;

typedef struct {
    cerberus_s3_capture_state_t state;
    uint32_t generation;
    uint8_t channel, mode_mask, pending;
    bool filter_bssid;
    bool busy, hopping, save_pcap;
    uint32_t received, dropped_full, dropped_other, written;
    uint64_t bytes_written;
    uint32_t elapsed_ms;
    esp_err_t error_code;
    char file_name[32];
} cerberus_s3_capture_status_t;

/* One C5 receive stream and one SD PCAPNG file. The S3 owns transport,
 * verified per-frame CRC, file writes, stop/drain and a five-second C5 lease.
 * A manual Stop or leaving the page ends the session; no duration is imposed. */
esp_err_t cerberus_s3_capture_start(uint8_t channel, uint8_t mode_mask,
    const uint8_t bssid[6], bool filter_bssid);
/* Hopping is generic beacon-only: channel=0, no BSSID filter. Without
 * save_pcap no SD access/lease is required. One shared worker; buffers are
 * allocated in PSRAM only for its lifetime. Stop/Back callers must retain
 * observer_context while busy, including a delayed radio/file close. */
esp_err_t cerberus_s3_capture_start_options(const cerberus_s3_capture_options_t *options);
/* Exact assigned generation, even if this worker finishes and another starts
 * before the call returns. Reservations remain unique within this S3 boot,
 * including starts that fail before reaching C5. No wrap; optional output is
 * zero when this call fails (a worker's later failure preserves its ID). */
esp_err_t cerberus_s3_capture_start_options_generation(
    const cerberus_s3_capture_options_t *options, uint32_t *generation_out);
void cerberus_s3_capture_stop(void);
/* Atomic ownership check for callers sharing this worker. A stale session
 * cannot stop a later capture; generation zero never claims a session. */
bool cerberus_s3_capture_stop_generation(uint32_t generation);
void cerberus_s3_capture_status(cerberus_s3_capture_status_t *out);
