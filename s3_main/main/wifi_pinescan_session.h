#pragma once

#include "esp_err.h"
#include "wifi_capture_session.h"
#include "wifi_beacon_session_types.h"
#include "wifi_pinescan.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Positive beacon-pattern evidence, not verified device identity. latest is
 * the newest observation; last_match retains the exact qualifying frame's
 * evidence when later ordinary beacons arrive from the same claimed addr2. */
typedef struct {
    cerberus_wifi_pinescan_record_t evidence;
    uint32_t generation;
    uint32_t first_received_age_ms, last_received_age_ms; /* S3 delivery ages. */
} cerberus_s3_pinescan_row_t;

typedef cerberus_s3_beacon_page_t cerberus_s3_pinescan_page_t;
typedef cerberus_s3_beacon_follow_t cerberus_s3_pinescan_follow_t;

typedef struct {
    cerberus_s3_capture_status_t capture;
    uint32_t generation;
    bool following;
    uint16_t candidates, capacity;
    uint32_t valid_beacons, matching_beacons, rejected_frames;
    uint32_t capacity_drops, stale_frames;
    uint32_t received_24ghz, received_5ghz;
} cerberus_s3_pinescan_status_t;

/* Shares the one lazy PSRAM evidence bank and capture worker with MultiSSID.
 * Successful Start replaces previous mode's retained results; failed Start
 * preserves them. Status/Stop/release are scoped strictly to this mode. */
esp_err_t cerberus_s3_pinescan_init(void);
esp_err_t cerberus_s3_pinescan_start(bool save_pcap);
void cerberus_s3_pinescan_stop(void);
esp_err_t cerberus_s3_pinescan_status(cerberus_s3_pinescan_status_t *out);
/* Up to four immutable positive rows, atomically copied in discovery order. */
esp_err_t cerberus_s3_pinescan_page(uint16_t offset,
    cerberus_s3_pinescan_row_t *rows, size_t capacity,
    cerberus_s3_pinescan_page_t *page);
/* Fixed-channel, receive-only follow of the selected copied addr2. The
 * original qualifying evidence stays frozen and live signal is separate. */
esp_err_t cerberus_s3_pinescan_follow(
    const cerberus_s3_pinescan_row_t *selected, bool save_pcap);
esp_err_t cerberus_s3_pinescan_follow_status(cerberus_s3_pinescan_follow_t *out);
esp_err_t cerberus_s3_pinescan_release(void);
