#pragma once

#include "c5_spi_link.h"

/* UI-owned snapshots are ordinary CPU data, not radio/DMA buffers. Keep the
 * last completed snapshots and their shared staging area in external RAM.
 * One allocation survives page changes so Back still shows the same sweep. */
typedef struct {
    cerberus_c5_wifi_result_t wifi[CERBERUS_WIFI_SCAN_MAX_RESULTS];
    cerberus_s3_wifi_observation_t wifi_observations[CERBERUS_WIFI_SCAN_MAX_RESULTS];
    cerberus_s3_wifi_observation_t wifi_staging_observations[CERBERUS_WIFI_SCAN_MAX_RESULTS];
    cerberus_c5_ble_result_t ble[CERBERUS_BLE_SCAN_MAX_RESULTS];
    cerberus_ieee802154_beacon_scan_result_payload_t
        beacons[CERBERUS_IEEE802154_BEACON_MAX_RESULTS];
    union {
        cerberus_c5_wifi_result_t wifi[CERBERUS_WIFI_SCAN_MAX_RESULTS];
        cerberus_c5_ble_result_t ble[CERBERUS_BLE_SCAN_MAX_RESULTS];
        cerberus_ieee802154_beacon_scan_result_payload_t
            beacon[CERBERUS_IEEE802154_BEACON_MAX_RESULTS];
    } staging;
} cerberus_scan_display_store_t;
