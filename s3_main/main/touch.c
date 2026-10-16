#include "touch.h"

#include "cerberus_pins.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_bus.h"

#include <string.h>

#define GT911_REG_PRODUCT_ID 0x8140
#define GT911_REG_TOUCH_STATUS 0x814E
#define GT911_REG_POINT1 0x814F
#define GT911_MAX_TOUCHES 5

static const char *TAG = "touch";
static bool touch_initialized;
static uint8_t touch_i2c_addr = CERBERUS_TOUCH_I2C_ADDR;
static i2c_master_dev_handle_t touch_device;
static bool calibration_enabled;
static uint16_t calibration_x_min;
static uint16_t calibration_x_max;
static uint16_t calibration_y_min;
static uint16_t calibration_y_max;
static cerberus_touch_point_t last_raw_point;
static bool last_raw_valid;

static uint16_t map_axis(uint16_t value, uint16_t minimum, uint16_t maximum, uint16_t output_max)
{
    if (value <= minimum) {
        return 0U;
    }
    if (value >= maximum) {
        return output_max;
    }
    return (uint16_t)(((uint32_t)(value - minimum) * output_max) / (maximum - minimum));
}

static esp_err_t gt911_write(uint16_t reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[10];
    if (len > sizeof(buffer) - 2U) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = (uint8_t)(reg >> 8);
    buffer[1] = (uint8_t)reg;
    if (len > 0) {
        memcpy(&buffer[2], data, len);
    }

    return i2c_master_transmit(touch_device, buffer, len + 2U, 100);
}

static esp_err_t gt911_read(uint16_t reg, uint8_t *data, size_t len)
{
    const uint8_t reg_bytes[] = {
        (uint8_t)(reg >> 8),
        (uint8_t)reg,
    };

    return i2c_master_transmit_receive(
        touch_device,
        reg_bytes,
        sizeof(reg_bytes),
        data,
        len,
        100);
}

static esp_err_t prepare_gt911_interrupt_pin(void)
{
    uint64_t pin_mask = 1ULL << CERBERUS_TOUCH_INT_GPIO;
#if CERBERUS_TOUCH_RST_GPIO >= 0
    pin_mask |= 1ULL << CERBERUS_TOUCH_RST_GPIO;
#endif

    gpio_config_t output_config = {
        .pin_bit_mask = pin_mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "touch GPIO init failed");

#if CERBERUS_TOUCH_RST_GPIO >= 0
    gpio_set_level(CERBERUS_TOUCH_RST_GPIO, 0);
    gpio_set_level(CERBERUS_TOUCH_INT_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(CERBERUS_TOUCH_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(60));
#else
    /* The current PCB has no GT911 reset line. Holding INT low briefly keeps
       the controller on its normal 0x5D address during power-up. */
    gpio_set_level(CERBERUS_TOUCH_INT_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
#endif

    gpio_config_t input_config = {
        .pin_bit_mask = 1ULL << CERBERUS_TOUCH_INT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&input_config), TAG, "touch INT input config failed");

    vTaskDelay(pdMS_TO_TICKS(80));
    return ESP_OK;
}

static esp_err_t gt911_probe(uint8_t *product_id)
{
    const uint8_t addresses[] = {CERBERUS_TOUCH_I2C_ADDR, 0x14};

    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        touch_i2c_addr = addresses[i];
        esp_err_t err = cerberus_i2c_bus_add_device(touch_i2c_addr, &touch_device);
        if (err != ESP_OK) {
            continue;
        }

        err = gt911_read(GT911_REG_PRODUCT_ID, product_id, 4);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "GT911 found at 0x%02x", touch_i2c_addr);
            return ESP_OK;
        }

        (void)i2c_master_bus_rm_device(touch_device);
        touch_device = NULL;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t cerberus_touch_init(void)
{
    if (touch_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "initializing GT911 touch");
    ESP_RETURN_ON_ERROR(prepare_gt911_interrupt_pin(), TAG, "GT911 startup failed");

    uint8_t product_id[5] = {0};
    ESP_RETURN_ON_ERROR(gt911_probe(product_id), TAG, "GT911 product ID read failed");
    ESP_LOGI(TAG, "GT911 product id: %c%c%c%c", product_id[0], product_id[1], product_id[2], product_id[3]);

    const uint8_t clear = 0;
    ESP_RETURN_ON_ERROR(gt911_write(GT911_REG_TOUCH_STATUS, &clear, 1), TAG, "GT911 status clear failed");

    touch_initialized = true;
    return ESP_OK;
}

esp_err_t cerberus_touch_read(cerberus_touch_point_t *point, bool *touched)
{
    if (point == NULL || touched == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *touched = false;
    memset(point, 0, sizeof(*point));

    if (!touch_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t status = 0;
    ESP_RETURN_ON_ERROR(gt911_read(GT911_REG_TOUCH_STATUS, &status, 1), TAG, "GT911 status read failed");

    const uint8_t touch_count = status & 0x0F;
    if ((status & 0x80) == 0 || touch_count == 0 || touch_count > GT911_MAX_TOUCHES) {
        if ((status & 0x80) != 0) {
            const uint8_t clear = 0;
            ESP_RETURN_ON_ERROR(gt911_write(GT911_REG_TOUCH_STATUS, &clear, 1), TAG, "GT911 status clear failed");
        }
        return ESP_OK;
    }

    uint8_t data[8] = {0};
    ESP_RETURN_ON_ERROR(gt911_read(GT911_REG_POINT1, data, sizeof(data)), TAG, "GT911 point read failed");

    point->id = data[0];
    point->x = (uint16_t)data[1] | ((uint16_t)data[2] << 8);
    point->y = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    point->size = (uint16_t)data[5] | ((uint16_t)data[6] << 8);
    last_raw_point = *point;
    last_raw_valid = true;
    if (calibration_enabled) {
        point->x = map_axis(point->x, calibration_x_min, calibration_x_max, 319U);
        point->y = map_axis(point->y, calibration_y_min, calibration_y_max, 479U);
    }
    *touched = true;

    const uint8_t clear = 0;
    return gt911_write(GT911_REG_TOUCH_STATUS, &clear, 1);
}

esp_err_t cerberus_touch_set_calibration(
    bool enabled,
    uint16_t x_min,
    uint16_t x_max,
    uint16_t y_min,
    uint16_t y_max)
{
    if (enabled && (x_min >= x_max || y_min >= y_max ||
        x_max - x_min < 200U || y_max - y_min < 300U)) {
        return ESP_ERR_INVALID_ARG;
    }
    calibration_enabled = enabled;
    calibration_x_min = x_min;
    calibration_x_max = x_max;
    calibration_y_min = y_min;
    calibration_y_max = y_max;
    return ESP_OK;
}

bool cerberus_touch_get_last_raw(cerberus_touch_point_t *point)
{
    if (point == NULL || !last_raw_valid) {
        return false;
    }
    *point = last_raw_point;
    return true;
}
