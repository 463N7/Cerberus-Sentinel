#pragma once

/* Original allocation-free receive-only protocol readers. All outputs are
 * unauthenticated broadcast claims, never proof of identity or a GPS fix. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sentinel_watch_motion.h"

#define SENTINEL_WATCH_PROTOCOL_BLE_MAX 255U
#define SENTINEL_WATCH_PROTOCOL_WIFI_MAX 2304U
#define SENTINEL_WATCH_PROTOCOL_ID_MAX 20U

enum {
    SENTINEL_WATCH_PROTOCOL_NONE = 0,
    SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI = 1,
    SENTINEL_WATCH_PROTOCOL_DRONE = 2,
};
enum {
    SENTINEL_WATCH_TRANSPORT_BLE = 1,
    SENTINEL_WATCH_TRANSPORT_WIFI_BEACON = 2,
    SENTINEL_WATCH_TRANSPORT_WIFI_NAN = 3,
};
enum {
    SENTINEL_WATCH_PROTOCOL_HAS_ID = 1U,
    SENTINEL_WATCH_PROTOCOL_HAS_POSITION = 2U,
    SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE = 4U,
};
enum {
    SENTINEL_WATCH_ALTITUDE_NONE = 0,
    SENTINEL_WATCH_ALTITUDE_GEODETIC = 1, /* WGS84 HAE, NOT altitude MSL */
    SENTINEL_WATCH_ALTITUDE_PRESSURE = 2, /* 1013.25 hPa reference */
};
typedef struct {
    uint8_t kind;
    uint8_t transport;
    uint8_t flags;
    uint8_t altitude_kind;
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t altitude_dm;
    /* Drone serial/registration ID when printable; Pwnagotchi display name.
     * Never truncate a binary Remote ID into a misleading text identifier. */
    char id[SENTINEL_WATCH_PROTOCOL_ID_MAX + 1U];
    sentinel_watch_motion_t motion; /* Separate Location group; not legacy row flags. */
} sentinel_watch_protocol_record_t;

/* BLE: one complete advertisement, no fragment/scan-response concatenation.
 * Wi-Fi: raw management frame WITHOUT radiotap or the trailing four-byte FCS.
 * Remote ID Wi-Fi accepts FA:0B:BC and legacy 90:3A:E6 beacon vendor IEs, or
 * NAN service descriptors addressed to 51:6F:9A:01:00:00. NAN permits service
 * types Publish/Subscribe/Follow-up, any instance/requestor, range-limited
 * flags and bounded bitmap/matching/response filters. Service information
 * must consume its descriptor exactly and contain a counter + MessagePack.
 * Wi-Fi never accepts bare single-message payloads or fragment reassembly.
 * Both functions clear output on rejection and validate enclosing TLV lengths.
 * Merge ID/location fields only across identical MAC, address type and kind.
 * Unknown/missing fields remain absent; do not substitute receiver position. */
bool sentinel_watch_protocol_decode_ble(const uint8_t *data, size_t length,
    sentinel_watch_protocol_record_t *record);
bool sentinel_watch_protocol_decode_wifi(const uint8_t *data, size_t length,
    sentinel_watch_protocol_record_t *record);
