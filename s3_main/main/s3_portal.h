#pragma once

#include "esp_err.h"
#include "portal_protocol.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_PORTAL_SUBMISSIONS 4U

typedef struct {
    uint8_t source_bssid[6];
    uint8_t source_ssid[32];
    uint8_t source_ssid_length;
    uint8_t source_channel;
    uint8_t local_channel; /* S3 SoftAP: 1-11, even for a 5 GHz source AP. */
    uint32_t c5_scan_generation;
    bool c5_preflight_passed; /* Caller compared a fresh C5 result to this snapshot. */
    uint16_t duration_seconds; /* 15-600; the DNS worker enforces the lease. */
} cerberus_s3_portal_request_t;

typedef struct {
    bool running;
    bool resources_held;
    bool report_pending;
    bool lease_expired;
    bool c5_preflight_passed;
    bool c5_observation_available;
    uint8_t source_bssid[6];
    uint8_t local_bssid[6];
    uint8_t source_channel;
    uint8_t local_channel;
    uint8_t connected_clients;
    uint32_t client_joins;
    uint32_t c5_scan_generation;
    uint32_t c5_observed_scans;
    uint32_t dns_queries;
    uint32_t http_requests;
    uint32_t form_submissions;
    uint32_t forms_dropped;
    uint32_t elapsed_ms;
    esp_err_t error_code;
} cerberus_s3_portal_status_t;

/* This is the S3 transport service, not the selected-AP UI. The UI must
 * preflight the frozen target on C5 before start and may report bounded C5
 * passive observations through note_c5_observation while it runs. */
esp_err_t cerberus_s3_portal_start(const cerberus_s3_portal_request_t *request);
esp_err_t cerberus_s3_portal_stop(void);
void cerberus_s3_portal_get_status(cerberus_s3_portal_status_t *out);
esp_err_t cerberus_s3_portal_note_c5_observation(uint32_t scan_generation,
    const uint8_t source_bssid[6], uint32_t observed_scans);

/* A stopped session remains in bounded RAM until explicitly saved/discarded.
 * Save writes a dedicated PORTnnnn.CSV under /CERBERUS/LOGS. */
esp_err_t cerberus_s3_portal_save(char *file_name, size_t file_name_capacity);
esp_err_t cerberus_s3_portal_discard(void);
