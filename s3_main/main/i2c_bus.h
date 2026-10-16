#pragma once

#include "driver/i2c_master.h"
#include "esp_err.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t cerberus_i2c_bus_add_device(uint8_t address, i2c_master_dev_handle_t *device);

#ifdef __cplusplus
}
#endif
