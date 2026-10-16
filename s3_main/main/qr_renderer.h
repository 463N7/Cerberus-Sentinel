#pragma once

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t module_count;
    uint16_t scale;
    uint16_t rendered_size;
} cerberus_qr_info_t;

#define CERBERUS_QR_MAX_MODULE_COUNT 57U
#define CERBERUS_QR_MAX_BITMAP_BYTES \
    (CERBERUS_QR_MAX_MODULE_COUNT * CERBERUS_QR_MAX_MODULE_COUNT)

esp_err_t cerberus_qr_render_text(
    const char *text,
    uint16_t area_x,
    uint16_t area_y,
    uint16_t area_width,
    uint16_t area_height,
    cerberus_qr_info_t *info);
esp_err_t cerberus_qr_encode_text(
    const char *text,
    uint8_t *modules,
    size_t module_capacity,
    uint16_t *module_count);

#ifdef __cplusplus
}
#endif
