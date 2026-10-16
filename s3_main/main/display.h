#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_LCD_WIDTH 320
#define CERBERUS_LCD_HEIGHT 480

esp_err_t cerberus_display_init(void);
/* Call from the drawing task before its first full redraw. Allocates 614400
   bytes from PSRAM only. On failure the original direct drawing mode remains.
   Drawing and presentation must stay on that same task. Repeated enable is OK. */
esp_err_t cerberus_display_enable_buffering(void);
bool cerberus_display_buffering_enabled(void);
/* Revision of attempted drawing in the top 16 rows, including failed direct
   transfers. UI overlays use this to repair themselves after a page/header
   repaint without polling or repainting unchanged pixels. Drawing task only. */
uint32_t cerberus_display_top_revision(void);
/* Presents the final changed pixels, with no LCD writes for identical frames.
   SPI failures remain retryable; call again even if no further drawing occurs.
   In direct drawing mode this is a no-op. */
esp_err_t cerberus_display_present(void);
esp_err_t cerberus_display_set_brightness(uint8_t percent);
uint8_t cerberus_display_get_brightness(void);
esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t rgb565);
esp_err_t cerberus_display_draw_mono_bitmap(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint8_t *bitmap,
    uint16_t color,
    uint16_t background);
esp_err_t cerberus_display_draw_rgb565_bitmap(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t *pixels);
esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t background, uint8_t scale);

#ifdef __cplusplus
}
#endif
