#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t size;
    uint8_t id;
} cerberus_touch_point_t;

esp_err_t cerberus_touch_init(void);
esp_err_t cerberus_touch_read(cerberus_touch_point_t *point, bool *touched);
esp_err_t cerberus_touch_set_calibration(
    bool enabled,
    uint16_t x_min,
    uint16_t x_max,
    uint16_t y_min,
    uint16_t y_max);
bool cerberus_touch_get_last_raw(cerberus_touch_point_t *point);

#ifdef __cplusplus
}
#endif
