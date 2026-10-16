#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_c5_event_queue_init(void);
void cerberus_c5_event_queue_get_status(cerberus_c5_event_queue_status_payload_t *status);
bool cerberus_c5_event_queue_pop(cerberus_c5_event_payload_t *event);
uint8_t cerberus_c5_event_queue_pop_batch(
    cerberus_c5_event_payload_t *events,
    uint8_t max_events);
esp_err_t cerberus_c5_event_queue_configure_flow_control(
    const cerberus_c5_event_flow_control_request_t *request,
    cerberus_c5_event_queue_status_payload_t *status);

#ifdef __cplusplus
}
#endif
