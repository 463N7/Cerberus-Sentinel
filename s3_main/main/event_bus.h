#pragma once

#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_EVENT_QUEUE_DEPTH 24U
#define CERBERUS_EVENT_SUBSCRIBER_LIMIT 4U

/* Events deliberately contain only a fixed type and one numeric value. Radio
   results, device addresses, credentials, and arbitrary strings cannot enter
   the central bus. */
typedef enum {
    CERBERUS_EVENT_SYSTEM_BOOT = 1,
    CERBERUS_EVENT_STORAGE_MOUNTED,
    CERBERUS_EVENT_STORAGE_UNAVAILABLE,
    CERBERUS_EVENT_STORAGE_UNMOUNTED,
    CERBERUS_EVENT_USB_CONSOLE_READY,
    CERBERUS_EVENT_C5_ONLINE,
    CERBERUS_EVENT_C5_OFFLINE,
    CERBERUS_EVENT_C5_RESTARTED,
    CERBERUS_EVENT_HEAP_PRESSURE,
    CERBERUS_EVENT_STACK_PRESSURE,
    CERBERUS_EVENT_RECOVERY_REQUESTED,
    CERBERUS_EVENT_C5_WIFI_STATE,
    CERBERUS_EVENT_C5_BLE_STATE,
    CERBERUS_EVENT_C5_IEEE802154_STATE,
    CERBERUS_EVENT_C5_EVENT_DROPPED,
    CERBERUS_EVENT_C5_BACKPRESSURE,
} cerberus_event_type_t;

typedef struct {
    cerberus_event_type_t type;
    uint64_t uptime_ms;
    int32_t value;
} cerberus_event_t;

typedef void (*cerberus_event_handler_t)(
    const cerberus_event_t *event,
    void *context);

typedef struct {
    uint32_t publish_attempts;
    uint32_t dispatched_events;
    uint32_t dropped_events;
    uint32_t pending_events;
    uint8_t subscriber_count;
} cerberus_event_bus_status_t;

esp_err_t cerberus_event_bus_init(void);
esp_err_t cerberus_event_bus_subscribe(cerberus_event_handler_t handler, void *context);
esp_err_t cerberus_event_bus_publish(cerberus_event_type_t type, int32_t value);
esp_err_t cerberus_event_bus_get_status(cerberus_event_bus_status_t *status);

#ifdef __cplusplus
}
#endif
