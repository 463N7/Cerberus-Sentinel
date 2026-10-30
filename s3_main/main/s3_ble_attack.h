#pragma once

#include "ble_attack_payload.h"
#include "esp_err.h"
#include <stdint.h>

/* This is a local S3 service. Controller-accepted burst starts are counted;
 * individual advertisements and over-the-air reception are not measurable
 * through NimBLE's legacy advertiser API. */
typedef enum {
    CERBERUS_S3_BLE_ATTACK_IDLE = 0,
    CERBERUS_S3_BLE_ATTACK_STARTING,
    CERBERUS_S3_BLE_ATTACK_RUNNING,
    CERBERUS_S3_BLE_ATTACK_STOPPING,
    CERBERUS_S3_BLE_ATTACK_STOPPED,
    CERBERUS_S3_BLE_ATTACK_LEASE_EXPIRED,
    CERBERUS_S3_BLE_ATTACK_GATT_REQUIRED,
    CERBERUS_S3_BLE_ATTACK_ERROR,
} cerberus_s3_ble_attack_state_t;

typedef struct {
    uint16_t job_id;
    cerberus_ble_attack_mode_t mode;
    uint32_t seed;
    bool has_capture;
    cerberus_ble_attack_capture_t capture;
} cerberus_s3_ble_attack_request_t;

typedef struct {
    uint16_t job_id;
    cerberus_ble_attack_mode_t mode;
    cerberus_s3_ble_attack_state_t state;
    esp_err_t error;
    int32_t last_controller_rc;
    uint32_t elapsed_ms;
    uint32_t planned_bursts;
    uint32_t controller_accepted_bursts;
    uint32_t controller_rejected_bursts;
    uint32_t completed_bursts;
    uint32_t connections_accepted;
    uint32_t connections_closed;
    uint32_t connection_terminate_errors;
    bool connection_active;
    uint16_t effective_interval_units;
    uint16_t minimum_free_heap_kib;
    cerberus_ble_payload_kind_t last_payload;
    bool packet_count_available; /* Always false for this controller API. */
} cerberus_s3_ble_attack_status_t;

/* Continuous until explicit Stop, page lease expiry, or a radio error.
 * A UI should touch at least once every five seconds while its job is visible.
 * Find My Sound returns ESP_ERR_NOT_SUPPORTED with GATT_REQUIRED in *status. */
esp_err_t cerberus_s3_ble_attack_start(const cerberus_s3_ble_attack_request_t *request,
    cerberus_s3_ble_attack_status_t *status);
esp_err_t cerberus_s3_ble_attack_touch(uint16_t job_id);
esp_err_t cerberus_s3_ble_attack_stop(uint16_t job_id);
void cerberus_s3_ble_attack_get_status(cerberus_s3_ble_attack_status_t *status);
/* For another S3 radio owner to request Stop and wait for checked release. */
esp_err_t cerberus_s3_ble_attack_quiesce(uint32_t timeout_ms);
