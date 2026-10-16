#include "qr_renderer.h"

#include "display.h"
#include "esp_log.h"
#include "qrcode.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define QR_QUIET_ZONE_MODULES 4
#define QR_MAX_VERSION 10
#define QR_BLACK 0x0000
#define QR_WHITE 0xFFFF
#define QR_ENCODER_LOG_TAG "QRCODE"

typedef struct {
    uint16_t area_x;
    uint16_t area_y;
    uint16_t area_width;
    uint16_t area_height;
    cerberus_qr_info_t *info;
    esp_err_t draw_error;
    bool callback_ran;
} qr_render_context_t;

typedef struct {
    uint8_t *modules;
    size_t module_capacity;
    uint16_t *module_count;
    esp_err_t encode_error;
    bool callback_ran;
} qr_encode_context_t;

static void encode_qr_code(esp_qrcode_handle_t qrcode, void *user_data)
{
    qr_encode_context_t *context = user_data;
    context->callback_ran = true;

    int module_count_raw = esp_qrcode_get_size(qrcode);
    if (module_count_raw <= 0 || module_count_raw > CERBERUS_QR_MAX_MODULE_COUNT) {
        context->encode_error = ESP_ERR_INVALID_SIZE;
        return;
    }

    size_t required = (size_t)module_count_raw * (size_t)module_count_raw;
    if (required > context->module_capacity) {
        context->encode_error = ESP_ERR_INVALID_SIZE;
        return;
    }

    for (int y = 0; y < module_count_raw; ++y) {
        for (int x = 0; x < module_count_raw; ++x) {
            context->modules[(size_t)y * (size_t)module_count_raw + (size_t)x] =
                esp_qrcode_get_module(qrcode, x, y) ? 1U : 0U;
        }
    }
    *context->module_count = (uint16_t)module_count_raw;
}

static void draw_qr_code(esp_qrcode_handle_t qrcode, void *user_data)
{
    qr_render_context_t *context = user_data;
    context->callback_ran = true;

    int module_count_raw = esp_qrcode_get_size(qrcode);
    if (module_count_raw <= 0 || module_count_raw > UINT16_MAX) {
        context->draw_error = ESP_ERR_INVALID_SIZE;
        return;
    }

    uint16_t module_count = (uint16_t)module_count_raw;
    uint16_t bounded_modules = module_count + QR_QUIET_ZONE_MODULES * 2U;
    uint16_t horizontal_scale = context->area_width / bounded_modules;
    uint16_t vertical_scale = context->area_height / bounded_modules;
    uint16_t scale = horizontal_scale < vertical_scale ? horizontal_scale : vertical_scale;
    if (scale == 0) {
        context->draw_error = ESP_ERR_INVALID_SIZE;
        return;
    }

    uint16_t rendered_size = bounded_modules * scale;
    uint16_t render_x = context->area_x + (context->area_width - rendered_size) / 2U;
    uint16_t render_y = context->area_y + (context->area_height - rendered_size) / 2U;
    uint16_t *pixel_row = malloc((size_t)rendered_size * sizeof(pixel_row[0]));
    if (pixel_row == NULL) {
        context->draw_error = ESP_ERR_NO_MEM;
        return;
    }

    for (uint16_t output_y = 0; output_y < rendered_size; ++output_y) {
        int module_y = (int)(output_y / scale) - QR_QUIET_ZONE_MODULES;
        for (uint16_t output_x = 0; output_x < rendered_size; ++output_x) {
            int module_x = (int)(output_x / scale) - QR_QUIET_ZONE_MODULES;
            bool black = module_x >= 0 && module_x < module_count_raw &&
                module_y >= 0 && module_y < module_count_raw &&
                esp_qrcode_get_module(qrcode, module_x, module_y);
            pixel_row[output_x] = black ? QR_BLACK : QR_WHITE;
        }

        context->draw_error = cerberus_display_draw_rgb565_bitmap(
            render_x,
            render_y + output_y,
            rendered_size,
            1,
            pixel_row);
        if (context->draw_error != ESP_OK) {
            break;
        }
    }

    free(pixel_row);
    if (context->draw_error == ESP_OK) {
        context->info->module_count = module_count;
        context->info->scale = scale;
        context->info->rendered_size = rendered_size;
    }
}

esp_err_t cerberus_qr_render_text(
    const char *text,
    uint16_t area_x,
    uint16_t area_y,
    uint16_t area_width,
    uint16_t area_height,
    cerberus_qr_info_t *info)
{
    if (text == NULL || text[0] == '\0' || info == NULL || area_width == 0 || area_height == 0 ||
        area_x >= CERBERUS_LCD_WIDTH || area_y >= CERBERUS_LCD_HEIGHT ||
        area_width > CERBERUS_LCD_WIDTH - area_x || area_height > CERBERUS_LCD_HEIGHT - area_y) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    qr_render_context_t context = {
        .area_x = area_x,
        .area_y = area_y,
        .area_width = area_width,
        .area_height = area_height,
        .info = info,
        .draw_error = ESP_OK,
        .callback_ran = false,
    };
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.display_func_with_cb = draw_qr_code;
    config.max_qrcode_version = QR_MAX_VERSION;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    config.user_data = &context;

    esp_log_level_t previous_log_level = esp_log_level_get(QR_ENCODER_LOG_TAG);
    esp_log_level_set(QR_ENCODER_LOG_TAG, ESP_LOG_WARN);
    esp_err_t err = esp_qrcode_generate(&config, text);
    esp_log_level_set(QR_ENCODER_LOG_TAG, previous_log_level);
    if (err != ESP_OK) {
        return err;
    }
    if (!context.callback_ran) {
        return ESP_FAIL;
    }
    return context.draw_error;
}

esp_err_t cerberus_qr_encode_text(
    const char *text,
    uint8_t *modules,
    size_t module_capacity,
    uint16_t *module_count)
{
    if (text == NULL || text[0] == '\0' || modules == NULL || module_count == NULL ||
        module_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    *module_count = 0U;
    qr_encode_context_t context = {
        .modules = modules,
        .module_capacity = module_capacity,
        .module_count = module_count,
        .encode_error = ESP_OK,
        .callback_ran = false,
    };
    esp_qrcode_config_t config = ESP_QRCODE_CONFIG_DEFAULT();
    config.display_func_with_cb = encode_qr_code;
    config.max_qrcode_version = QR_MAX_VERSION;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_MED;
    config.user_data = &context;

    esp_log_level_t previous_log_level = esp_log_level_get(QR_ENCODER_LOG_TAG);
    esp_log_level_set(QR_ENCODER_LOG_TAG, ESP_LOG_WARN);
    esp_err_t err = esp_qrcode_generate(&config, text);
    esp_log_level_set(QR_ENCODER_LOG_TAG, previous_log_level);
    if (err != ESP_OK) {
        return err;
    }
    if (!context.callback_ran) {
        return ESP_FAIL;
    }
    return context.encode_error;
}
