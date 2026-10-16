#include "i2c_bus.h"

#include "cerberus_pins.h"
#include "esp_check.h"

static i2c_master_bus_handle_t i2c_bus;

static esp_err_t ensure_i2c_bus(void)
{
    if (i2c_bus != NULL) {
        return ESP_OK;
    }

    i2c_master_bus_config_t config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = CERBERUS_I2C_SDA_GPIO,
        .scl_io_num = CERBERUS_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    return i2c_new_master_bus(&config, &i2c_bus);
}

esp_err_t cerberus_i2c_bus_add_device(uint8_t address, i2c_master_dev_handle_t *device)
{
    if (device == NULL || address > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_RETURN_ON_ERROR(ensure_i2c_bus(), "i2c_bus", "shared I2C bus init failed");
    i2c_device_config_t config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(i2c_bus, &config, device);
}
