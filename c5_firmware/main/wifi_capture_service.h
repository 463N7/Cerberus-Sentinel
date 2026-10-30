#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The Wi-Fi scanner owns radio setup and calls start/stop with its mutex held.
 * Receive is the sole producer and is safe in the promiscuous callback. */
bool cerberus_wifi_capture_service_same_request(
    const cerberus_wifi_capture_start_request_t *request);
esp_err_t cerberus_wifi_capture_service_start(
    const cerberus_wifi_capture_start_request_t *request);
void cerberus_wifi_capture_service_stop(esp_err_t reason);
/* Cleanup timeout is an ERROR with ownership retained, unlike a normal
 * transport lease timeout that has actually stopped the receiver. */
void cerberus_wifi_capture_service_cleanup_error(esp_err_t reason);
/* Radio owner calls this only after cleanup succeeds. Preserve last_error
 * while acknowledging explicit STOP after an already rolled-back failure. */
void cerberus_wifi_capture_service_confirm_stopped(void);
bool cerberus_wifi_capture_service_active(void);
uint32_t cerberus_wifi_capture_service_generation(void);
uint32_t cerberus_wifi_capture_service_deadline_ms(void);
void cerberus_wifi_capture_service_set_deadline(uint32_t monotonic_ms);
uint32_t cerberus_wifi_capture_service_inflight(void);
/* Scanner-owned admission gate. Zero pauses callbacks without stopping the
 * ring/session or its transport lease. Drain inflight before retuning and
 * publish the new channel only after promiscuous reception is enabled. */
void cerberus_wifi_capture_service_set_channel(uint8_t channel);
void cerberus_wifi_capture_service_receive(const uint8_t *frame,size_t length,
    uint8_t channel,int8_t rssi_dbm,uint64_t monotonic_us);
void cerberus_wifi_capture_service_status(cerberus_wifi_capture_status_payload_t *out);
esp_err_t cerberus_wifi_capture_service_peek(uint32_t generation,
    cerberus_wifi_capture_peek_payload_t *out);
esp_err_t cerberus_wifi_capture_service_read(
    const cerberus_wifi_capture_read_request_t *request,
    cerberus_wifi_capture_read_payload_t *out);
esp_err_t cerberus_wifi_capture_service_ack(
    const cerberus_wifi_capture_ack_request_t *request);
