#pragma once

/* Receive-only beacon evidence. No ESP, radio, allocation, or transmit APIs.
 * A shared transmitter address is an unauthenticated packet claim, not a
 * physical-device identity. See docs/WIFI_BEACON_ANALYSIS.md. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_WIFI_BEACON_MAX_BYTES 2304U
#define CERBERUS_WIFI_BEACON_SSID_BYTES 32U
#define CERBERUS_WIFI_MULTISSID_THRESHOLD 3U
#define CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS 32U
#define CERBERUS_WIFI_MULTISSID_MAX_ROWS 128U

typedef struct {
    uint8_t transmitter[6];
    uint8_t bssid[6];
    uint8_t ssid[CERBERUS_WIFI_BEACON_SSID_BYTES];
    uint8_t ssid_length;
    bool hidden_like; /* Empty or all-zero SSID, not proof of AP settings. */
    uint8_t rx_channel; /* Supplied receiver channel, never replaced by an IE. */
    uint8_t advertised_channel; /* DS parameter value, zero when absent. */
    int8_t rssi;
    uint16_t sequence;
    uint16_t capabilities;
    uint64_t observed_us; /* Source monotonic callback time, not a hardware RF timestamp. */
} cerberus_wifi_beacon_observation_t;

typedef struct {
    uint8_t length;
    uint8_t bytes[CERBERUS_WIFI_BEACON_SSID_BYTES];
} cerberus_wifi_ssid_variant_t;

typedef struct {
    cerberus_wifi_beacon_observation_t latest;
    cerberus_wifi_ssid_variant_t variants[CERBERUS_WIFI_MULTISSID_THRESHOLD];
    uint64_t first_observed_us;
    uint32_t frames;
    int8_t peak_rssi;
    uint8_t variant_count; /* Capped at 3: show "at least 3" once confirmed. */
    bool multiple_bssids;
} cerberus_wifi_multissid_record_t;

typedef struct {
    cerberus_wifi_multissid_record_t *records; /* Caller-owned, preferably PSRAM. */
    uint16_t capacity;
    uint16_t count;
    uint16_t confirmed_count;
    uint32_t valid_beacons;
    uint32_t rejected_frames;
    uint32_t capacity_drops;
    uint32_t stale_frames;
} cerberus_wifi_multissid_t;

typedef enum {
    CERBERUS_WIFI_MULTISSID_REJECTED = 0,
    CERBERUS_WIFI_MULTISSID_UPDATED,
    CERBERUS_WIFI_MULTISSID_ADDED,
    CERBERUS_WIFI_MULTISSID_CONFIRMED,
    CERBERUS_WIFI_MULTISSID_FULL,
    CERBERUS_WIFI_MULTISSID_STALE,
} cerberus_wifi_multissid_event_t;

/* Complete 802.11 beacon WITHOUT radiotap/FCS, successful RX only. Validate
 * every IE before publishing output. Invalid input clears output. Receiver
 * channel must be nonzero; supported/regulatory channels remain the caller's
 * radio contract. An advertised channel remains an untrusted byte value. */
bool cerberus_wifi_beacon_decode(const uint8_t *frame, size_t length,
    uint8_t rx_channel, int8_t rssi, uint64_t observed_us,
    cerberus_wifi_beacon_observation_t *out);

/* Reset per capture session/source clock. Does not allocate. Invalid arguments
 * leave a cleared, unusable table. Storage must not overlap the table object. */
bool cerberus_wifi_multissid_init(cerberus_wifi_multissid_t *table,
    cerberus_wifi_multissid_record_t *storage, uint16_t capacity);

/* Caller serializes mutation/copy. Exact addr2 identity and exact SSID bytes
 * determine matches; blank SSID encodings share one hidden-like sentinel.
 * Distinct BSSIDs with a shared transmitter are retained as a separate flag.
 * Existing rows keep updating when full; no silent eviction or lost matches.
 * row_index is UINT16_MAX when input is rejected, stale, or cannot be stored. */
cerberus_wifi_multissid_event_t cerberus_wifi_multissid_observe(
    cerberus_wifi_multissid_t *table, const uint8_t *frame, size_t length,
    uint8_t rx_channel, int8_t rssi, uint64_t observed_us, uint16_t *row_index);

/* Immutable value copy: later observations cannot change a selected record. */
bool cerberus_wifi_multissid_copy(const cerberus_wifi_multissid_t *table,
    uint16_t index, cerberus_wifi_multissid_record_t *out);

#ifdef __cplusplus
}
#endif
