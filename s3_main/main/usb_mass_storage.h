#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool active;
    bool host_attached;
    bool safe_to_release;
    esp_err_t last_error;
} cerberus_usb_mass_storage_status_t;

esp_err_t cerberus_usb_mass_storage_start(void);
esp_err_t cerberus_usb_mass_storage_stop(void);
void cerberus_usb_mass_storage_get_status(cerberus_usb_mass_storage_status_t *status);

#ifdef __cplusplus
}
#endif
