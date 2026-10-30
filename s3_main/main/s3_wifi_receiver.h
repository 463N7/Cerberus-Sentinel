#pragma once
#include "esp_err.h"
#include "radio_test.h"
#include "wifi_ap_rx.h"
#include <stddef.h>
typedef struct {
    uint16_t job_id;
    uint8_t state,channel;
    uint32_t elapsed_ms,observed_frames,target_beacons,matching_frames;
    int32_t error_code;
    uint16_t minimum_heap_kib;
    int8_t last_match_rssi;
    bool reserved;
} cerberus_s3_rx_status_t;
esp_err_t cerberus_s3_rx_start(const cerberus_wifi_ap_job_t *request);
esp_err_t cerberus_s3_rx_start_stations(const cerberus_wifi_ap_job_t *request,
    const cerberus_wifi_ap_station_set_t *stations);
/* The cloned BSSID is frozen in request->station. The source SSID is copied
 * before the worker is created; an empty SSID is a valid hidden-source case. */
esp_err_t cerberus_s3_rx_start_clone(const cerberus_wifi_ap_job_t *request,
    const uint8_t *source_ssid,size_t source_ssid_length);
void cerberus_s3_rx_stop(uint16_t job_id);
void cerberus_s3_rx_status(uint16_t lease_job_id,cerberus_s3_rx_status_t *status);
