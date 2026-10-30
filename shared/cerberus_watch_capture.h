#pragma once

#include "cerberus_protocol.h"
#include "sentinel_watch_protocol.h"

/* Pure bounded collectors. The caller serializes access with its radio lock. */
typedef struct {
    cerberus_watch_scan_status_payload_t status;
    cerberus_watch_observation_t records[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint32_t started_ms;
    uint64_t capture_epoch;
    uint64_t qualifying_us[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint16_t qualifying_valid;
    sentinel_watch_motion_t motion[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint64_t motion_us[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint16_t motion_valid; /* Own Location callback, distinct from source reports. */
    uint8_t protected_transport; /* 0 disables protection; Wi-Fi only. */
    uint8_t protected_address[6];
} cerberus_watch_capture_t;

typedef struct {
    cerberus_ble_activity_payload_t status;
    uint8_t addresses[CERBERUS_BLE_ACTIVITY_MAX_IDENTITIES][7];
    uint32_t started_ms;
    uint32_t bucket_index;
    uint32_t bucket_reports;
} cerberus_ble_activity_capture_t;

void cerberus_watch_capture_begin(cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint32_t now_ms);
/* Caller supplies a nonzero, never-reused private capture epoch. Epoch zero
 * preserves legacy observations while making the optional timing unavailable. */
void cerberus_watch_capture_begin_with_epoch(cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint32_t now_ms, uint64_t epoch);
/* Configure immediately after begin, before any reports. Exact DRONE + Wi-Fi
 * address_type0 + NAN/BEACON transport + unicast nonzero MAC is protected.
 * No row/timing is created until an actual recognized report matches. At a
 * full bank that report replaces the highest-index non-target record, clears
 * its evidence/timing and counts one discarded retained record. Overflow and
 * dropped_records describe discarded-record events, not RF packet loss.
 * Invalid arguments leave the capture unchanged; begin clears protection. */
bool cerberus_watch_capture_protect_wifi_source(cerberus_watch_capture_t *capture,
    uint8_t transport, const uint8_t address[6]);
void cerberus_watch_capture_finish(cerberus_watch_capture_t *capture,
    uint8_t state, uint32_t now_ms);
/* Count every delivered report; NULL protocol means no recognized format. */
void cerberus_watch_capture_observe(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel);
/* Untimed qualifying updates clear timing. Timed backward updates also clear
 * validity; unrelated/malformed reports and capacity drops never refresh it. */
void cerberus_watch_capture_observe_timed(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel, uint64_t callback_us);
bool cerberus_watch_capture_get_timing(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint64_t now_us,
    cerberus_watch_scan_timing_payload_t *out);
/* Caller holds the radio lock. Same COMPLETE row/epoch gates as timing.
 * Returns a canonical absent tuple for a source without Location. Basic ID-only
 * reports retain the latest Location tuple and its own callback age. Unknown or
 * backwards time retains values with unavailable timing; never use source age. */
bool cerberus_watch_capture_get_motion(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint64_t now_us,
    sentinel_watch_motion_t *motion, cerberus_watch_scan_timing_payload_t *timing);
bool cerberus_watch_capture_get_chunk(const cerberus_watch_capture_t *capture,
    uint8_t radio, uint8_t generation, uint8_t index, uint8_t chunk,
    cerberus_watch_scan_data_payload_t *out);

void cerberus_ble_activity_begin(cerberus_ble_activity_capture_t *capture,
    uint8_t generation, uint32_t now_ms, bool extended);
void cerberus_ble_activity_observe(cerberus_ble_activity_capture_t *capture,
    const uint8_t address[6], uint8_t address_type, const uint8_t *data,
    size_t length, bool complete, uint32_t now_ms);
void cerberus_ble_activity_finish(cerberus_ble_activity_capture_t *capture,
    uint8_t state, uint32_t now_ms);
