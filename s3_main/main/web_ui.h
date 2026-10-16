#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool running;
    bool radio_active;
    bool password_configured;
    uint8_t connected_clients;
    char ssid[33];
    char access_point_password[65];
    char address[32];
} cerberus_web_ui_status_t;

esp_err_t cerberus_web_ui_start(void);
esp_err_t cerberus_web_ui_stop(void);
esp_err_t cerberus_web_ui_factory_reset(void);
void cerberus_web_ui_get_status(cerberus_web_ui_status_t *status);

#ifdef __cplusplus
}
#endif
