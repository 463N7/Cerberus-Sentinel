#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t source_width;
    uint32_t source_height;
    uint16_t rendered_width;
    uint16_t rendered_height;
} cerberus_bmp_info_t;

bool cerberus_bmp_is_file_name(const char *file_name);
esp_err_t cerberus_bmp_render_file(
    const char *relative_path,
    const char *file_name,
    uint16_t area_x,
    uint16_t area_y,
    uint16_t area_width,
    uint16_t area_height,
    cerberus_bmp_info_t *info);

#ifdef __cplusplus
}
#endif
