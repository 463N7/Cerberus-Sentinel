#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool started;
    bool c5_online;
    bool supported;
    bool flow_supported;
    bool flow_configured;
    bool batch_supported;
    uint32_t received_events;
    uint32_t local_publish_failures;
    uint32_t batch_transfers;
    uint32_t batch_failures;
    uint32_t single_event_transfers;
    cerberus_c5_event_queue_status_payload_t remote;
    cerberus_c5_event_payload_t last_event;
} cerberus_c5_event_pump_status_t;

esp_err_t cerberus_c5_event_pump_start(void);
esp_err_t cerberus_c5_event_pump_get_status(cerberus_c5_event_pump_status_t *status);

#ifdef __cplusplus
}
#endif
