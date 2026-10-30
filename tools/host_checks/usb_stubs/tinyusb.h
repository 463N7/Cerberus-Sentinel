#pragma once
#include <stdbool.h>
#include "esp_err.h"
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED };
typedef struct { int id; } tinyusb_event_t;
typedef struct { void (*event_cb)(tinyusb_event_t *, void *); } tinyusb_config_t;
esp_err_t tinyusb_driver_install(const tinyusb_config_t *config);
esp_err_t tinyusb_driver_uninstall(void);
bool tud_disconnect(void);
