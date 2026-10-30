#pragma once

#include "esp_err.h"
#include "wardrive_gps_core.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    CERBERUS_WARDRIVE_IDLE = 0,
    CERBERUS_WARDRIVE_STARTING,
    CERBERUS_WARDRIVE_RUNNING,
    CERBERUS_WARDRIVE_STOPPING,
    CERBERUS_WARDRIVE_STOPPED,
    CERBERUS_WARDRIVE_ERROR,
} cerberus_wardrive_state_t;

typedef enum {
    CERBERUS_WARDRIVE_PROBE_WAITING = 0,
    CERBERUS_WARDRIVE_PROBE_AVAILABLE,
    CERBERUS_WARDRIVE_PROBE_UNAVAILABLE,
    CERBERUS_WARDRIVE_PROBE_UNSUPPORTED,
} cerberus_wardrive_probe_state_t;

typedef enum {
    CERBERUS_WARDRIVE_RELATION_WAITING = 0,
    CERBERUS_WARDRIVE_RELATION_AVAILABLE,
    CERBERUS_WARDRIVE_RELATION_UNAVAILABLE,
    CERBERUS_WARDRIVE_RELATION_UNSUPPORTED,
} cerberus_wardrive_relation_state_t;

typedef struct {
    cerberus_wardrive_state_t state;
    bool gps_requested; /* UART2 is used only when explicitly enabled. */
    bool gps_present; /* Checksum-valid NMEA was received recently. */
    cerberus_wardrive_gps_snapshot_t gps;
    esp_err_t gps_error; /* Optional UART failure does not stop radio logging. */
    bool expansions_requested;
    uint32_t elapsed_ms;
    uint32_t c5_wifi_sweeps, c5_wifi_observations, c5_wifi_logged;
    uint32_t c5_wifi_scan_dropped;
    uint32_t c5_wifi_status_timeouts; /* Recovered SPI status-poll timeouts. */
    /* Probe hints are unauthenticated transmitter/requested-SSID observations,
     * not APs, associated stations or unique physical devices. */
    cerberus_wardrive_probe_state_t c5_probe_state;
    uint32_t c5_probe_observations, c5_probe_logged;
    uint32_t c5_probe_frames, c5_probe_dropped, c5_probe_unavailable_sweeps;
    /* DS1/DS2 address-header pairs, not authenticated associations. Summary
     * callbacks/drops count only fully drained immutable C5 banks. */
    cerberus_wardrive_relation_state_t c5_relation_state;
    uint32_t c5_relation_observations, c5_relation_logged;
    uint32_t c5_relation_frames, c5_relation_dropped, c5_relation_unavailable_sweeps;
    uint32_t s3_ble_sweeps, s3_ble_observations, s3_ble_logged;
    uint32_t s3_ble_scan_dropped;
    /* One classified typed-address row per processed sweep, including SD
     * duplicates. Disjoint strongest separate-report levels; not devices,
     * RF packet totals or proof of product identity. Failed writes do not count. */
    uint32_t s3_ble_flock_observations, s3_ble_flock_clue;
    uint32_t s3_ble_flock_signature, s3_ble_flock_corroborated;
    uint32_t expansion_observations, expansion_logged, expansion_radio_dropped;
    uint32_t expansion_radio_dropped_by_slot[6];
    uint32_t expansion_queue_overflow, expansion_queue_invalid;
    uint32_t expansion_queue_overflow_by_slot[6];
    uint16_t expansion_queue_pending;
    uint8_t expansion_present_mask;
    uint32_t duplicate_sightings, invalid_sightings, dedupe_evicted;
    uint32_t sd_rows, files_saved;
    uint32_t geotagged_rows, unlocated_rows, gps_fallback_rows;
    uint64_t sd_bytes;
    esp_err_t c5_error, s3_ble_error, expansion_error, sd_error;
    esp_err_t error_code;
    char last_file_name[24];
} cerberus_wardrive_status_t;

/* Start requires exclusive access to the onboard C5 scan and S3 BLE scanner.
 * The UI must suspend/drain Recon before calling. R1-R6 are opt-in; enabling
 * them does not imply that expansion radios are installed. GPS is opt-in;
 * without a wired receiver, leave enable_gps false. Scans and unlocated CSV
 * continue with no GPS reader or UART driver. With GPS enabled, fresh GPS/RMC
 * results enter WiGLE; other valid observations enter an unlocated sidecar. */
esp_err_t cerberus_wardrive_start(bool include_expansions, bool enable_gps);
/* Stop is asynchronous. Both terminal states mean the worker has closed its
 * SD file. STOPPED confirms release of all started scans; ERROR reports a
 * radio or storage fault, including an expansion STOP that remains uncertain.
 * A USB route may wait for either terminal state before handoff. */
void cerberus_wardrive_request_stop(void);
esp_err_t cerberus_wardrive_wait_stopped(uint32_t timeout_ms);
void cerberus_wardrive_get_status(cerberus_wardrive_status_t *out);
