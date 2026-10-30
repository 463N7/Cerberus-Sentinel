#pragma once

#include "c5_spi_link.h" /* Shared UI record shapes; no C5 transport is used. */
#include "s3_findmy_core.h"

/* One GATT connection on the S3. Operations are asynchronous on the
 * shared NimBLE event queue; status and result getters copy bounded snapshots. */
esp_err_t cerberus_s3_ble_central_connect(const cerberus_c5_ble_result_t *peer,
    cerberus_ble_connection_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_disconnect(cerberus_ble_connection_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_status(cerberus_ble_connection_status_payload_t *status);
typedef struct {
    uint8_t state, generation, host_owner;
    bool handle_present;
    uint32_t host_epoch;
    int32_t error_code;
    uint8_t service_state, characteristic_state, read_state, reserved;
} cerberus_s3_ble_central_diagnostics_t;
/* Read-only aggregate: no host/mutex initialization, GAP operation or retry.
 * A stale host lease is represented as ERROR/INVALID_STATE with no handle and
 * idle GATT states in this copy only. Concurrent reset churn fails with a
 * cleared output; ownership may legitimately outlive completed GATT work. */
esp_err_t cerberus_s3_ble_central_get_diagnostics(
    cerberus_s3_ble_central_diagnostics_t *diagnostics);
esp_err_t cerberus_s3_ble_central_start_service_discovery(
    cerberus_ble_gatt_service_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_service_status(
    cerberus_ble_gatt_service_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_service_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_service_t *result);
esp_err_t cerberus_s3_ble_central_start_characteristic_discovery(
    const cerberus_c5_ble_service_t *service,
    cerberus_ble_gatt_characteristic_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_characteristic_status(
    cerberus_ble_gatt_characteristic_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_characteristic_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_characteristic_t *result);
esp_err_t cerberus_s3_ble_central_start_read(
    const cerberus_c5_ble_characteristic_t *characteristic,
    cerberus_ble_gatt_read_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_read_status(cerberus_ble_gatt_read_status_payload_t *status);
esp_err_t cerberus_s3_ble_central_get_read_value(uint8_t generation,
    uint8_t *value, size_t capacity, size_t *length);

typedef enum {
    CERBERUS_S3_BLE_SOUND_IDLE = 0,
    CERBERUS_S3_BLE_SOUND_CONNECTING,
    CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE,
    CERBERUS_S3_BLE_SOUND_DISCOVERING_CHARACTERISTIC,
    CERBERUS_S3_BLE_SOUND_DISCOVERING_CCCD,
    CERBERUS_S3_BLE_SOUND_SUBSCRIBING,
    CERBERUS_S3_BLE_SOUND_WRITING,
    CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE,
    CERBERUS_S3_BLE_SOUND_STOPPING,
    CERBERUS_S3_BLE_SOUND_COMPLETE,
    CERBERUS_S3_BLE_SOUND_STOPPED,
    CERBERUS_S3_BLE_SOUND_ERROR,
} cerberus_s3_ble_sound_state_t;

typedef struct {
    cerberus_s3_ble_sound_state_t state;
    cerberus_findmy_profile_t profile;
    uint8_t generation;
    uint8_t address[6];
    uint8_t address_type;
    bool write_accepted;       /* ATT write-with-response acknowledged. */
    bool response_received;    /* Exact characteristic notification/indication. */
    bool completion_reported;  /* DULT 02 03 response; no acoustic proof. */
    cerberus_s3_ble_sound_state_t failed_stage;
    int32_t error_code;        /* ESP error or NimBLE host status. */
    int32_t last_host_rc;
} cerberus_s3_ble_sound_status_t;

/* Starts one exact selected peer and profile. The caller polls get_status to
 * advance stage/response deadlines; there is no overall timed job limit.
 * COMPLETE means the command write was accepted and the connection released,
 * not that a physical tag emitted sound. Stop is idempotent for an active job. */
esp_err_t cerberus_s3_ble_central_sound_start(const cerberus_c5_ble_result_t *peer,
    cerberus_findmy_profile_t profile, cerberus_s3_ble_sound_status_t *status);
esp_err_t cerberus_s3_ble_central_sound_stop(cerberus_s3_ble_sound_status_t *status);
esp_err_t cerberus_s3_ble_central_sound_get_status(cerberus_s3_ble_sound_status_t *status);
