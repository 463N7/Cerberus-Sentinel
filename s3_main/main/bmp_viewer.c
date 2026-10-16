#include "bmp_viewer.h"

#include "display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "storage.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BMP_HEADER_SIZE 54
#define BMP_MAX_SOURCE_DIMENSION 4096

static uint16_t read_u16_le(const uint8_t *value)
{
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *value)
{
    return (uint32_t)value[0] |
        ((uint32_t)value[1] << 8U) |
        ((uint32_t)value[2] << 16U) |
        ((uint32_t)value[3] << 24U);
}

static int32_t read_i32_le(const uint8_t *value)
{
    return (int32_t)read_u32_le(value);
}

bool cerberus_bmp_is_file_name(const char *file_name)
{
    if (file_name == NULL) {
        return false;
    }
    const char *extension = strrchr(file_name, '.');
    return extension != NULL && strcasecmp(extension, ".bmp") == 0;
}

esp_err_t cerberus_bmp_render_file(
    const char *relative_path,
    const char *file_name,
    uint16_t area_x,
    uint16_t area_y,
    uint16_t area_width,
    uint16_t area_height,
    cerberus_bmp_info_t *info)
{
    if (relative_path == NULL || file_name == NULL || info == NULL || area_width == 0 || area_height == 0 ||
        area_x >= CERBERUS_LCD_WIDTH || area_y >= CERBERUS_LCD_HEIGHT ||
        area_width > CERBERUS_LCD_WIDTH - area_x || area_height > CERBERUS_LCD_HEIGHT - area_y) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(*info));
    uint8_t header[BMP_HEADER_SIZE];
    size_t bytes_read = 0;
    uint64_t file_size = 0;
    esp_err_t err = cerberus_storage_read_file_chunk(
        relative_path,
        file_name,
        0,
        header,
        sizeof(header),
        &bytes_read,
        &file_size);
    if (err != ESP_OK) {
        return err;
    }
    if (bytes_read != sizeof(header) || header[0] != 'B' || header[1] != 'M') {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t pixel_offset = read_u32_le(&header[10]);
    uint32_t dib_size = read_u32_le(&header[14]);
    int32_t width_raw = read_i32_le(&header[18]);
    int32_t height_raw = read_i32_le(&header[22]);
    uint16_t planes = read_u16_le(&header[26]);
    uint16_t bits_per_pixel = read_u16_le(&header[28]);
    uint32_t compression = read_u32_le(&header[30]);
    if (dib_size < 40U || width_raw <= 0 || height_raw == 0 || height_raw == INT32_MIN || planes != 1U ||
        (bits_per_pixel != 24U && bits_per_pixel != 32U) || compression != 0U) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint32_t source_width = (uint32_t)width_raw;
    uint32_t source_height = height_raw < 0 ? (uint32_t)(-height_raw) : (uint32_t)height_raw;
    if (source_width > BMP_MAX_SOURCE_DIMENSION || source_height > BMP_MAX_SOURCE_DIMENSION) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t bytes_per_pixel = bits_per_pixel / 8U;
    uint64_t row_stride_64 = (((uint64_t)source_width * bits_per_pixel + 31U) / 32U) * 4U;
    uint64_t header_end = 14ULL + dib_size;
    uint64_t pixel_end = (uint64_t)pixel_offset + row_stride_64 * source_height;
    if (row_stride_64 == 0 || row_stride_64 > SIZE_MAX || pixel_offset < header_end || pixel_end > file_size) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint16_t rendered_width = source_width > area_width ? area_width : (uint16_t)source_width;
    uint16_t rendered_height = source_height > area_height ? area_height : (uint16_t)source_height;
    if ((uint64_t)source_width * rendered_height > (uint64_t)source_height * rendered_width) {
        rendered_height = (uint16_t)(((uint64_t)source_height * rendered_width) / source_width);
    } else {
        rendered_width = (uint16_t)(((uint64_t)source_width * rendered_height) / source_height);
    }
    if (rendered_width == 0) {
        rendered_width = 1;
    }
    if (rendered_height == 0) {
        rendered_height = 1;
    }

    size_t row_stride = (size_t)row_stride_64;
    uint8_t *source_row = malloc(row_stride);
    uint16_t *rendered_row = malloc((size_t)rendered_width * sizeof(rendered_row[0]));
    if (source_row == NULL || rendered_row == NULL) {
        free(source_row);
        free(rendered_row);
        return ESP_ERR_NO_MEM;
    }

    uint16_t render_x = area_x + (area_width - rendered_width) / 2U;
    uint16_t render_y = area_y + (area_height - rendered_height) / 2U;
    uint32_t cached_source_y = UINT32_MAX;
    bool top_down = height_raw < 0;

    for (uint16_t output_y = 0; output_y < rendered_height; ++output_y) {
        uint32_t source_y = ((uint64_t)output_y * source_height) / rendered_height;
        if (source_y != cached_source_y) {
            uint32_t file_row = top_down ? source_y : source_height - 1U - source_y;
            uint64_t row_offset = (uint64_t)pixel_offset + (uint64_t)file_row * row_stride;
            uint64_t current_file_size = 0;
            err = cerberus_storage_read_file_chunk(
                relative_path,
                file_name,
                row_offset,
                source_row,
                row_stride,
                &bytes_read,
                &current_file_size);
            if (err != ESP_OK || bytes_read != row_stride || current_file_size != file_size) {
                err = err == ESP_OK ? ESP_FAIL : err;
                break;
            }
            cached_source_y = source_y;
        }

        for (uint16_t output_x = 0; output_x < rendered_width; ++output_x) {
            uint32_t source_x = ((uint64_t)output_x * source_width) / rendered_width;
            const uint8_t *pixel = &source_row[(size_t)source_x * bytes_per_pixel];
            uint16_t red = (uint16_t)(pixel[2] >> 3U);
            uint16_t green = (uint16_t)(pixel[1] >> 2U);
            uint16_t blue = (uint16_t)(pixel[0] >> 3U);
            rendered_row[output_x] = (uint16_t)((red << 11U) | (green << 5U) | blue);
        }

        err = cerberus_display_draw_rgb565_bitmap(render_x, render_y + output_y, rendered_width, 1, rendered_row);
        if (err != ESP_OK) {
            break;
        }
        if ((output_y & 0x0FU) == 0x0FU) {
            vTaskDelay(1);
        }
    }

    free(source_row);
    free(rendered_row);
    if (err == ESP_OK) {
        info->source_width = source_width;
        info->source_height = source_height;
        info->rendered_width = rendered_width;
        info->rendered_height = rendered_height;
    }
    return err;
}
