#pragma once

#include "cerberus_protocol.h"
#include "sentinel_watch_protocol.h"

/* Pure bounded collectors. The caller serializes access with its radio lock. */
typedef struct {
    cerberus_watch_scan_status_payload_t status;
    cerberus_watch_observation_t records[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint32_t started_ms;
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
void cerberus_watch_capture_finish(cerberus_watch_capture_t *capture,
    uint8_t state, uint32_t now_ms);
/* Count every delivered report; NULL protocol means no recognized format. */
void cerberus_watch_capture_observe(cerberus_watch_capture_t *capture,
    const sentinel_watch_protocol_record_t *protocol, const uint8_t address[6],
    uint8_t address_type, int8_t rssi, uint8_t channel);
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
