#pragma once

#include "esp_err.h"
#include "wifi_capture_session.h"
#include "wifi_beacon_analysis.h"
#include "wifi_beacon_session_types.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* One capture worker: C5 receives complete beacons, S3 retains and analyzes
 * their evidence in PSRAM. Neither entry transmits or joins an AP. */
typedef struct {
    cerberus_wifi_multissid_record_t evidence;
    uint32_t generation;
    /* Age since delivery to the S3, NOT RF age. Original C5 receive-callback
     * timestamps remain in evidence and may be used for source-relative spans. */
    uint32_t first_received_age_ms;
    uint32_t last_received_age_ms;
} cerberus_s3_multissid_row_t;

typedef cerberus_s3_beacon_page_t cerberus_s3_multissid_page_t;

typedef struct {
    cerberus_s3_capture_status_t capture;
    uint32_t generation; /* Broad analysis generation; frozen rows use this. */
    bool following;
    uint16_t transmitters;
    uint16_t confirmed;
    uint16_t capacity;
    uint32_t valid_beacons;
    uint32_t rejected_frames;
    uint32_t capacity_drops;
    uint32_t stale_frames;
    uint32_t received_24ghz;
    uint32_t received_5ghz;
} cerberus_s3_multissid_status_t;

typedef cerberus_s3_beacon_follow_t cerberus_s3_multissid_follow_t;

/* Initialize once before UI/task entry. Large buffers remain lazy PSRAM. */
esp_err_t cerberus_s3_multissid_init(void);
/* Broad, dual-band, beacon-only hopping. save_pcap=false needs no SD card;
 * save_pcap=true requires a successful transactional capture file. */
esp_err_t cerberus_s3_multissid_start(bool save_pcap);
void cerberus_s3_multissid_stop(void);
esp_err_t cerberus_s3_multissid_status(cerberus_s3_multissid_status_t *out);
/* Atomically copy up to four confirmed records, in discovery order. */
esp_err_t cerberus_s3_multissid_page(uint16_t offset,
    cerberus_s3_multissid_row_t *rows, size_t capacity,
    cerberus_s3_multissid_page_t *page);
/* Stop the broad receiver first. Start a fresh, fixed-channel receive-only
 * session for the exact addr2 in a copied row from the retained generation.
 * Broad evidence stays unchanged; selected follow signal is separate. */
esp_err_t cerberus_s3_multissid_follow(
    const cerberus_s3_multissid_row_t *selected, bool save_pcap);
esp_err_t cerberus_s3_multissid_follow_status(cerberus_s3_multissid_follow_t *out);
/* No-op while capture owns radio/storage. After cleanup, release PSRAM rows. */
esp_err_t cerberus_s3_multissid_release(void);
