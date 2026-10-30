#pragma once
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/* Initialize on the startup task before starting any radio clients. All users
 * hold this mutex for a complete protocol exchange, including READY and CS. */
esp_err_t radio_bus_init(void);
SemaphoreHandle_t radio_bus_mutex(void);
spi_device_handle_t radio_bus_onboard(void);
esp_err_t radio_bus_expansion_transfer(unsigned slot, const void *tx, void *rx, size_t size);
