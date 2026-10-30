#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "ble_rssi.h"

/* Fox Hunt follows one exact advertiser identity. A rotating private address
 * becomes stale instead of silently switching to another device. */
#define CERBERUS_S3_BLE_TRACK_STALE_MS 5000U

typedef struct {
    bool enabled;
    bool seen;
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi_dbm;
    uint32_t last_seen_ms;
    uint32_t reports;
} cerberus_s3_ble_tracker_t;

typedef struct {
    bool enabled;
    bool seen;
    bool fresh;
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi_dbm;
    uint32_t age_ms;
    uint32_t reports;
} cerberus_s3_ble_track_status_t;

static inline void cerberus_s3_ble_tracker_begin(cerberus_s3_ble_tracker_t *tracker,
    const uint8_t address[6], uint8_t address_type)
{
    memset(tracker, 0, sizeof(*tracker));
    memcpy(tracker->address, address, 6U);
    tracker->address_type = address_type;
    tracker->enabled = true;
}

static inline void cerberus_s3_ble_tracker_end(cerberus_s3_ble_tracker_t *tracker)
{
    memset(tracker, 0, sizeof(*tracker));
}

static inline void cerberus_s3_ble_tracker_host_reset(cerberus_s3_ble_tracker_t *tracker)
{
    tracker->seen = false;
    tracker->reports = 0U;
    tracker->last_seen_ms = 0U;
}

static inline bool cerberus_s3_ble_tracker_observe(cerberus_s3_ble_tracker_t *tracker,
    const uint8_t address[6], uint8_t address_type, int8_t rssi_dbm,
    uint32_t now_ms)
{
    if (!tracker->enabled || tracker->address_type != address_type ||
        memcmp(tracker->address, address, 6U) != 0 ||
        !cerberus_ble_rssi_valid(rssi_dbm)) return false;
    tracker->seen = true;
    tracker->rssi_dbm = rssi_dbm;
    tracker->last_seen_ms = now_ms;
    if (tracker->reports != UINT32_MAX) ++tracker->reports;
    return true;
}

static inline void cerberus_s3_ble_tracker_snapshot(
    const cerberus_s3_ble_tracker_t *tracker, uint32_t now_ms,
    cerberus_s3_ble_track_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->enabled = tracker->enabled;
    out->seen = tracker->seen;
    memcpy(out->address, tracker->address, 6U);
    out->address_type = tracker->address_type;
    out->rssi_dbm = tracker->rssi_dbm;
    out->reports = tracker->reports;
    if (tracker->seen) {
        out->age_ms = now_ms - tracker->last_seen_ms;
        out->fresh = out->age_ms <= CERBERUS_S3_BLE_TRACK_STALE_MS;
    }
}
