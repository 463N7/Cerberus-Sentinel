#include "sentinel_battery_view.h"
#include "display.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define BATTERY_X 244U
#define BATTERY_Y 3U
#define BATTERY_WIDTH 68U
#define BATTERY_HEIGHT 13U
#define BATTERY_CYAN 0x3F1BU
#define BATTERY_AMBER 0xFDEAU
#define BATTERY_RED 0xF471U
#define BATTERY_MUTED 0x84B5U

/* A single completed tile prevents erase/draw flicker even without PSRAM. */
static uint16_t pixels[BATTERY_WIDTH * BATTERY_HEIGHT];
static bool cached;
static bool previous_valid;
static uint8_t previous_percent;
static uint16_t previous_background;
static uint32_t previous_revision;

static void rectangle(unsigned x, unsigned y, unsigned width, unsigned height, uint16_t color)
{
    for (unsigned row = y; row < y + height; ++row) {
        for (unsigned col = x; col < x + width; ++col) {
            pixels[row * BATTERY_WIDTH + col] = color;
        }
    }
}

static const uint8_t *glyph(char character)
{
    /* The same 5x7 numeral, dash and percent shapes used by display.c. */
    static const uint8_t shapes[][7] = {
        {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
        {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
        {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
        {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
    };
    return shapes[character >= '0' && character <= '9'
        ? (unsigned)(character - '0') : character == '-' ? 10U : 11U];
}

static void label(const char *text, uint16_t color)
{
    const unsigned left = 66U - (unsigned)strlen(text) * 6U;
    for (unsigned index = 0; text[index] != '\0'; ++index) {
        const uint8_t *shape = glyph(text[index]);
        for (unsigned row = 0; row < 7U; ++row) {
            for (unsigned col = 0; col < 5U; ++col) {
                if ((shape[row] & (1U << (4U - col))) != 0U) {
                    pixels[(row + 3U) * BATTERY_WIDTH + left + index * 6U + col] = color;
                }
            }
        }
    }
}

void sentinel_battery_view_invalidate(void)
{
    cached = false;
}

esp_err_t sentinel_battery_view_update(bool valid, uint8_t percent, uint16_t background)
{
    percent = !valid ? 0U : percent > 100U ? 100U : percent;
    const uint32_t revision = cerberus_display_top_revision();
    if (cached && valid == previous_valid && percent == previous_percent &&
        background == previous_background && revision == previous_revision) {
        return ESP_OK;
    }

    const uint16_t color = !valid ? BATTERY_MUTED : percent <= 10U ? BATTERY_RED :
        percent <= 20U ? BATTERY_AMBER : BATTERY_CYAN;
    rectangle(0U, 0U, BATTERY_WIDTH, BATTERY_HEIGHT, background);
    rectangle(8U, 2U, 21U, 1U, color);
    rectangle(8U, 10U, 21U, 1U, color);
    rectangle(8U, 3U, 1U, 7U, color);
    rectangle(28U, 3U, 1U, 7U, color);
    rectangle(29U, 5U, 2U, 3U, color);
    if (valid && percent != 0U) {
        rectangle(10U, 4U, ((unsigned)percent * 17U + 99U) / 100U, 5U, color);
    } else if (!valid) {
        rectangle(16U, 6U, 5U, 1U, color);
    }
    char text[5];
    if (valid) snprintf(text, sizeof(text), "%u%%", (unsigned)percent);
    else memcpy(text, "--%", sizeof("--%"));
    label(text, color);

    cached = false;
    const esp_err_t error = cerberus_display_draw_rgb565_bitmap(
        BATTERY_X, BATTERY_Y, BATTERY_WIDTH, BATTERY_HEIGHT, pixels);
    if (error != ESP_OK) return error;
    previous_valid = valid;
    previous_percent = percent;
    previous_background = background;
    /* Include this tile's own write so the next unchanged frame is a no-op. */
    previous_revision = cerberus_display_top_revision();
    cached = true;
    return ESP_OK;
}
