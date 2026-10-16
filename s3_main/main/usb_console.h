#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Starts the bounded, read-only native USB diagnostic console. */
esp_err_t cerberus_usb_console_start(void);
esp_err_t cerberus_usb_console_stop(void);

#ifdef __cplusplus
}
#endif
