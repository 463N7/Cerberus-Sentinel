#pragma once

/* Private lifecycle shared by the typed MultiSSID/PineScan wrappers. */
#include "wifi_multissid_session.h"
#include "wifi_pinescan_session.h"

typedef enum {
    CERBERUS_BEACON_SESSION_MULTISSID,
    CERBERUS_BEACON_SESSION_PINESCAN,
} cerberus_beacon_session_mode_t;

typedef struct {
    cerberus_s3_capture_status_t capture;
    uint32_t generation;
    bool following;
    uint16_t transmitters, matches, capacity;
    uint32_t valid_beacons, matching_beacons, rejected_frames;
    uint32_t capacity_drops, stale_frames, received_24ghz, received_5ghz;
} cerberus_beacon_session_status_t;

esp_err_t cerberus_beacon_session_init(void);
esp_err_t cerberus_beacon_session_start(cerberus_beacon_session_mode_t mode, bool save_pcap);
void cerberus_beacon_session_stop(cerberus_beacon_session_mode_t mode);
esp_err_t cerberus_beacon_session_status(cerberus_beacon_session_mode_t mode,
    cerberus_beacon_session_status_t *out);
/* rows must point to the public row type for mode; only typed wrappers call. */
esp_err_t cerberus_beacon_session_page(cerberus_beacon_session_mode_t mode,
    uint16_t offset, void *rows, size_t capacity, cerberus_s3_beacon_page_t *page);
esp_err_t cerberus_beacon_session_follow(cerberus_beacon_session_mode_t mode,
    uint32_t generation, const cerberus_wifi_beacon_observation_t *selected, bool save_pcap);
esp_err_t cerberus_beacon_session_follow_status(cerberus_beacon_session_mode_t mode,
    cerberus_s3_beacon_follow_t *out);
esp_err_t cerberus_beacon_session_release(cerberus_beacon_session_mode_t mode);
