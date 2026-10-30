#pragma once

#include "mac_monitor_protocol.h"

/* Caller serializes this bounded collector. No heap, radio, task or borrowed
 * packet pointers. One capture belongs to one producer clock and radio.
 * Counts mean admitted controller callbacks, including BLE partial responses;
 * malformed AD content alone does not invalidate an observed address. */
typedef struct {
    cerberus_mac_row_t rows[CERBERUS_MAC_CAPACITY];
    uint64_t started_us, latest_us;
    uint32_t observations, invalid, replacements;
    uint32_t next_incarnation, next_snapshot_id;
    uint8_t count, radio;
    bool initialized;
} cerberus_mac_observation_t;

/* Time zero is valid. init is a new producer epoch; callers must never use it
 * to reset counters while retaining the same externally published epoch. */
bool cerberus_mac_observation_init(cerberus_mac_observation_t *capture,
    uint8_t radio, uint64_t source_start_us);
void cerberus_mac_observation_reject(cerberus_mac_observation_t *capture);
/* Wi-Fi: one MGMT/CONTROL/DATA bit, address_type 0, actual channel.
 * BLE: exactly ORIGINAL or RESPONSE, optionally PARTIAL, type 0..3, channel 0.
 * Invalid RSSI is still a report; the latest value becomes unavailable (127).
 * Backward/pre-start source time is rejected, never made newly fresh.
 * New sources replace the least recently received row at capacity; ties use
 * the smallest incarnation. Counts for a newly inserted incarnation start at
 * one. replacements is discarded retained identities, not RF packet loss. */
bool cerberus_mac_observation_observe(cerberus_mac_observation_t *capture,
    const uint8_t address[6], uint8_t address_type, int rssi,
    uint8_t channel, uint8_t report_type, uint64_t source_us);
/* Copies a canonical immutable active or completed view. Snapshot IDs and row
 * incarnations never wrap/reuse within this capture. Failure clears output,
 * does not consume an ID, and does not change counters or source timestamps. */
bool cerberus_mac_observation_snapshot(cerberus_mac_observation_t *capture,
    uint64_t source_now_us, cerberus_mac_snapshot_t *out);
bool cerberus_mac_row_valid(const cerberus_mac_row_t *row,
    uint8_t radio, uint64_t source_now_us);
bool cerberus_mac_snapshot_valid(const cerberus_mac_snapshot_t *snapshot);
