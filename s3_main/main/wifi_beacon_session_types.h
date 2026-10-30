#pragma once

#include "wifi_beacon_analysis.h"
#include <stdbool.h>
#include <stdint.h>

/* Value copies shared by passive beacon-analysis modes. No radio ownership
 * or allocation is implied by keeping one of these UI snapshots. */
typedef struct {
    uint32_t generation;
    uint16_t total, copied;
} cerberus_s3_beacon_page_t;

typedef struct {
    bool valid;
    uint32_t generation;
    uint32_t matching_beacons;
    uint32_t last_received_age_ms; /* S3 delivery age, not cross-chip RF age. */
    int8_t peak_rssi;
    cerberus_wifi_beacon_observation_t latest;
} cerberus_s3_beacon_follow_t;
