#include "c5_control.h"

#include "cerberus_pins.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

esp_err_t c5_control_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << CERBERUS_C5_EN_GPIO) | (1ULL << CERBERUS_C5_BOOT_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    esp_err_t err = gpio_config(&output_config);
    if (err != ESP_OK) {
        return err;
    }

    c5_control_hold_reset();
    return ESP_OK;
}

void c5_control_hold_reset(void)
{
    gpio_set_level(CERBERUS_C5_BOOT_GPIO, 0);
    gpio_set_level(CERBERUS_C5_EN_GPIO, 1);
}

void c5_control_reset_normal(void)
{
    gpio_set_level(CERBERUS_C5_BOOT_GPIO, 0);
    gpio_set_level(CERBERUS_C5_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CERBERUS_C5_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
}

void c5_control_enter_bootloader(void)
{
    gpio_set_level(CERBERUS_C5_BOOT_GPIO, 1);
    gpio_set_level(CERBERUS_C5_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CERBERUS_C5_EN_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(150));
}
