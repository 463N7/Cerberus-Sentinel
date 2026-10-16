#include "display.h"

#include "cerberus_pins.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "display";
static uint8_t backlight_percent = 75;

#define LCD_SPI_HOST SPI2_HOST
#define LCD_SPI_CLOCK_HZ (40 * 1000 * 1000)
#define LCD_DRAW_BUFFER_ROWS 28
#define LCD_MAX_TEXT_GLYPHS ((CERBERUS_LCD_WIDTH + 5) / 6)
#define LCD_FRAME_PIXELS ((size_t)CERBERUS_LCD_WIDTH * CERBERUS_LCD_HEIGHT)
#define LCD_BAND_COUNT ((CERBERUS_LCD_HEIGHT + LCD_DRAW_BUFFER_ROWS - 1) / LCD_DRAW_BUFFER_ROWS)
#define LCD_ALL_BANDS ((1UL << LCD_BAND_COUNT) - 1UL)

_Static_assert(LCD_BAND_COUNT < 32, "display bands must fit the dirty mask");

static uint8_t lcd_draw_buffer[CERBERUS_LCD_WIDTH * LCD_DRAW_BUFFER_ROWS * 2];
static spi_device_handle_t lcd_spi;
/* PSRAM frames are CPU-only; LCD transfers always use lcd_draw_buffer. */
static uint16_t *lcd_back_frame;
static uint16_t *lcd_front_frame;
static uint32_t lcd_dirty_bands;
static uint32_t lcd_valid_bands;
static uint32_t lcd_top_revision;

static void lcd_note_top_draw(uint16_t y)
{
    if (y < 16U) ++lcd_top_revision;
}

static void lcd_mark_dirty(uint16_t y, uint16_t height)
{
    if (height == 0U) return;
    const unsigned last = (y + height - 1U) / LCD_DRAW_BUFFER_ROWS;
    for (unsigned band = y / LCD_DRAW_BUFFER_ROWS; band <= last; ++band) {
        lcd_dirty_bands |= 1UL << band;
    }
}

static esp_err_t lcd_spi_write_bytes(const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_OK;
    }

    spi_transaction_t transaction = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(lcd_spi, &transaction);
}

static uint32_t backlight_percent_to_duty(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }
    return (uint32_t)(100U - percent) * 1023U / 100U;
}

static esp_err_t lcd_write_command(uint8_t command)
{
    gpio_set_level(CERBERUS_LCD_DC_GPIO, 0);
    return lcd_spi_write_bytes(&command, 1);
}

static esp_err_t lcd_write_data(const void *data, size_t len)
{
    if (len == 0) {
        return ESP_OK;
    }

    gpio_set_level(CERBERUS_LCD_DC_GPIO, 1);
    return lcd_spi_write_bytes((const uint8_t *)data, len);
}

static esp_err_t lcd_write_command_data(uint8_t command, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_ERROR(lcd_write_command(command), TAG, "command 0x%02x failed", command);
    return lcd_write_data(data, len);
}

static esp_err_t lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t column[] = {
        (uint8_t)(x0 >> 8), (uint8_t)x0,
        (uint8_t)(x1 >> 8), (uint8_t)x1,
    };
    uint8_t row[] = {
        (uint8_t)(y0 >> 8), (uint8_t)y0,
        (uint8_t)(y1 >> 8), (uint8_t)y1,
    };

    ESP_RETURN_ON_ERROR(lcd_write_command_data(0x2A, column, sizeof(column)), TAG, "set column failed");
    ESP_RETURN_ON_ERROR(lcd_write_command_data(0x2B, row, sizeof(row)), TAG, "set row failed");
    return lcd_write_command(0x2C);
}

static esp_err_t lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t rgb565)
{
    if (width == 0 || height == 0 || x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT) {
        return ESP_OK;
    }

    if (x + width > CERBERUS_LCD_WIDTH) {
        width = CERBERUS_LCD_WIDTH - x;
    }
    if (y + height > CERBERUS_LCD_HEIGHT) {
        height = CERBERUS_LCD_HEIGHT - y;
    }

    lcd_note_top_draw(y);

    if (lcd_back_frame != NULL) {
        for (uint16_t row = 0; row < height; ++row) {
            uint16_t *destination = lcd_back_frame + (size_t)(y + row) * CERBERUS_LCD_WIDTH + x;
            for (uint16_t col = 0; col < width; ++col) destination[col] = rgb565;
        }
        lcd_mark_dirty(y, height);
        return ESP_OK;
    }

    const uint16_t rows_per_chunk = LCD_DRAW_BUFFER_ROWS;
    const size_t pixels_per_chunk = width * rows_per_chunk;

    const uint8_t hi = (uint8_t)(rgb565 >> 8);
    const uint8_t lo = (uint8_t)rgb565;
    for (size_t i = 0; i < pixels_per_chunk; ++i) {
        lcd_draw_buffer[i * 2] = hi;
        lcd_draw_buffer[i * 2 + 1] = lo;
    }

    ESP_RETURN_ON_ERROR(lcd_set_window(x, y, x + width - 1, y + height - 1), TAG, "set fill window failed");

    uint16_t remaining_rows = height;
    while (remaining_rows > 0) {
        const uint16_t rows = remaining_rows > rows_per_chunk ? rows_per_chunk : remaining_rows;
        esp_err_t err = lcd_write_data(lcd_draw_buffer, width * rows * 2);
        if (err != ESP_OK) {
            return err;
        }
        remaining_rows -= rows;
    }

    return ESP_OK;
}

static esp_err_t lcd_backlight_init(void)
{
    ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 5000,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "backlight timer failed");

    ledc_channel_config_t channel = {
        .gpio_num = CERBERUS_LCD_BL_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = LEDC_TIMER_0,
        .duty = 768,
        .hpoint = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&channel), TAG, "backlight channel failed");

    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, backlight_percent_to_duty(backlight_percent)), TAG, "backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

static esp_err_t lcd_gpio_init(void)
{
    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << CERBERUS_LCD_DC_GPIO) |
                        (1ULL << CERBERUS_LCD_RST_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&output_config);
}

static void lcd_hard_reset(void)
{
    gpio_set_level(CERBERUS_LCD_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(CERBERUS_LCD_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(120));
}

static esp_err_t lcd_spi_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = CERBERUS_LCD_MOSI_GPIO,
        .miso_io_num = -1,
        .sclk_io_num = CERBERUS_LCD_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = sizeof(lcd_draw_buffer),
    };
    ESP_RETURN_ON_ERROR(
        spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO),
        TAG, "LCD SPI bus init failed");

    spi_device_interface_config_t device = {
        .clock_speed_hz = LCD_SPI_CLOCK_HZ,
        .mode = 0,
        .spics_io_num = CERBERUS_LCD_CS_GPIO,
        .queue_size = 1,
    };
    return spi_bus_add_device(LCD_SPI_HOST, &device, &lcd_spi);
}

static esp_err_t st7796_init_sequence(void)
{
    ESP_RETURN_ON_ERROR(lcd_write_command(0x01), TAG, "software reset failed");
    vTaskDelay(pdMS_TO_TICKS(150));

    ESP_RETURN_ON_ERROR(lcd_write_command(0x11), TAG, "sleep out failed");
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t pixel_format = 0x55;
    ESP_RETURN_ON_ERROR(lcd_write_command_data(0x3A, &pixel_format, 1), TAG, "pixel format failed");

    const uint8_t madctl = 0x48;
    ESP_RETURN_ON_ERROR(lcd_write_command_data(0x36, &madctl, 1), TAG, "memory access control failed");

    ESP_RETURN_ON_ERROR(lcd_write_command(0x21), TAG, "display inversion failed");
    ESP_RETURN_ON_ERROR(lcd_write_command(0x13), TAG, "normal display mode failed");
    ESP_RETURN_ON_ERROR(lcd_write_command(0x29), TAG, "display on failed");
    vTaskDelay(pdMS_TO_TICKS(50));

    return ESP_OK;
}

esp_err_t cerberus_display_init(void)
{
    ESP_LOGI(TAG, "initializing ST7796 display");

    ESP_RETURN_ON_ERROR(lcd_gpio_init(), TAG, "LCD GPIO init failed");
    ESP_RETURN_ON_ERROR(lcd_backlight_init(), TAG, "LCD backlight init failed");
    ESP_RETURN_ON_ERROR(lcd_spi_init(), TAG, "LCD SPI init failed");

    lcd_hard_reset();
    ESP_RETURN_ON_ERROR(st7796_init_sequence(), TAG, "ST7796 init failed");

    ESP_LOGI(TAG, "display initialized");
    return ESP_OK;
}

esp_err_t cerberus_display_enable_buffering(void)
{
    if (lcd_back_frame != NULL) return ESP_OK;
    const uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    uint16_t *back = heap_caps_calloc(LCD_FRAME_PIXELS, sizeof(uint16_t), caps);
    if (back == NULL) return ESP_ERR_NO_MEM;
    uint16_t *front = heap_caps_calloc(LCD_FRAME_PIXELS, sizeof(uint16_t), caps);
    if (front == NULL) {
        heap_caps_free(back);
        return ESP_ERR_NO_MEM;
    }
    lcd_back_frame = back;
    lcd_front_frame = front;
    lcd_valid_bands = 0U;
    lcd_dirty_bands = LCD_ALL_BANDS;
    ++lcd_top_revision;
    return ESP_OK;
}

bool cerberus_display_buffering_enabled(void)
{
    return lcd_back_frame != NULL;
}

uint32_t cerberus_display_top_revision(void)
{
    return lcd_top_revision;
}

esp_err_t cerberus_display_present(void)
{
    if (lcd_back_frame == NULL) return ESP_OK;
    for (unsigned band = 0; band < LCD_BAND_COUNT; ++band) {
        const uint32_t mask = 1UL << band;
        if ((lcd_dirty_bands & mask) == 0U) continue;
        const uint16_t top = band * LCD_DRAW_BUFFER_ROWS;
        const uint16_t bottom = top + LCD_DRAW_BUFFER_ROWS < CERBERUS_LCD_HEIGHT
            ? top + LCD_DRAW_BUFFER_ROWS : CERBERUS_LCD_HEIGHT;
        uint16_t left = 0U, right = CERBERUS_LCD_WIDTH;
        uint16_t first_row = top, end_row = bottom;
        if ((lcd_valid_bands & mask) != 0U) {
            const size_t offset = (size_t)top * CERBERUS_LCD_WIDTH;
            const size_t bytes = (size_t)(bottom - top) * CERBERUS_LCD_WIDTH * sizeof(uint16_t);
            if (memcmp(lcd_back_frame + offset, lcd_front_frame + offset, bytes) == 0) {
                lcd_dirty_bands &= ~mask;
                continue;
            }
            left = CERBERUS_LCD_WIDTH;
            right = 0U;
            first_row = bottom;
            end_row = top;
            for (uint16_t row = top; row < bottom; ++row) {
                for (uint16_t col = 0; col < CERBERUS_LCD_WIDTH; ++col) {
                    const size_t index = (size_t)row * CERBERUS_LCD_WIDTH + col;
                    if (lcd_back_frame[index] == lcd_front_frame[index]) continue;
                    if (col < left) left = col;
                    if (col + 1U > right) right = col + 1U;
                    if (row < first_row) first_row = row;
                    if (row + 1U > end_row) end_row = row + 1U;
                }
            }
        }

        const uint16_t width = right - left;
        size_t destination = 0U;
        for (uint16_t row = first_row; row < end_row; ++row) {
            const uint16_t *source = lcd_back_frame + (size_t)row * CERBERUS_LCD_WIDTH + left;
            for (uint16_t col = 0; col < width; ++col) {
                lcd_draw_buffer[destination++] = (uint8_t)(source[col] >> 8);
                lcd_draw_buffer[destination++] = (uint8_t)source[col];
            }
        }
        esp_err_t err = lcd_set_window(left, first_row, right - 1U, end_row - 1U);
        if (err == ESP_OK) err = lcd_write_data(lcd_draw_buffer, destination);
        if (err != ESP_OK) {
            /* A failed transfer may have written a prefix. Even if the next
               redraw reverts to the old frame, this entire band must be sent. */
            lcd_valid_bands &= ~mask;
            return err;
        }
        for (uint16_t row = first_row; row < end_row; ++row) {
            const size_t offset = (size_t)row * CERBERUS_LCD_WIDTH + left;
            memcpy(lcd_front_frame + offset, lcd_back_frame + offset, (size_t)width * sizeof(uint16_t));
        }
        lcd_valid_bands |= mask;
        lcd_dirty_bands &= ~mask;
    }
    return ESP_OK;
}

esp_err_t cerberus_display_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        percent = 100;
    }

    backlight_percent = percent;
    ESP_RETURN_ON_ERROR(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, backlight_percent_to_duty(backlight_percent)), TAG, "backlight duty failed");
    return ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

uint8_t cerberus_display_get_brightness(void)
{
    return backlight_percent;
}

esp_err_t cerberus_display_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t rgb565)
{
    return lcd_fill_rect(x, y, width, height, rgb565);
}

esp_err_t cerberus_display_draw_mono_bitmap(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint8_t *bitmap,
    uint16_t color,
    uint16_t background)
{
    if (bitmap == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT ||
        width > CERBERUS_LCD_WIDTH - x || height > CERBERUS_LCD_HEIGHT - y) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd_note_top_draw(y);
    const size_t bitmap_stride = (width + 7U) / 8U;
    if (lcd_back_frame != NULL) {
        for (uint16_t row = 0; row < height; ++row) {
            const uint8_t *source = bitmap + (size_t)row * bitmap_stride;
            uint16_t *destination = lcd_back_frame + (size_t)(y + row) * CERBERUS_LCD_WIDTH + x;
            for (uint16_t col = 0; col < width; ++col) {
                destination[col] = (source[col / 8U] & (0x80U >> (col % 8U))) != 0U
                    ? color : background;
            }
        }
        lcd_mark_dirty(y, height);
        return ESP_OK;
    }
    const uint8_t color_hi = (uint8_t)(color >> 8);
    const uint8_t color_lo = (uint8_t)color;
    const uint8_t background_hi = (uint8_t)(background >> 8);
    const uint8_t background_lo = (uint8_t)background;

    ESP_RETURN_ON_ERROR(
        lcd_set_window(x, y, x + width - 1, y + height - 1),
        TAG,
        "set bitmap window failed");

    uint16_t source_row = 0;
    while (source_row < height) {
        const uint16_t rows = height - source_row > LCD_DRAW_BUFFER_ROWS
            ? LCD_DRAW_BUFFER_ROWS
            : height - source_row;

        for (uint16_t row = 0; row < rows; ++row) {
            const uint8_t *bitmap_row = bitmap + (size_t)(source_row + row) * bitmap_stride;
            for (uint16_t col = 0; col < width; ++col) {
                const bool active = (bitmap_row[col / 8U] & (0x80U >> (col % 8U))) != 0;
                const size_t offset = ((size_t)row * width + col) * 2U;
                lcd_draw_buffer[offset] = active ? color_hi : background_hi;
                lcd_draw_buffer[offset + 1] = active ? color_lo : background_lo;
            }
        }

        ESP_RETURN_ON_ERROR(
            lcd_write_data(lcd_draw_buffer, (size_t)width * rows * 2U),
            TAG,
            "bitmap data write failed");
        source_row += rows;
    }

    return ESP_OK;
}

esp_err_t cerberus_display_draw_rgb565_bitmap(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t *pixels)
{
    if (pixels == NULL || width == 0 || height == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT ||
        width > CERBERUS_LCD_WIDTH - x || height > CERBERUS_LCD_HEIGHT - y) {
        return ESP_ERR_INVALID_ARG;
    }

    lcd_note_top_draw(y);
    if (lcd_back_frame != NULL) {
        for (uint16_t row = 0; row < height; ++row) {
            memcpy(lcd_back_frame + (size_t)(y + row) * CERBERUS_LCD_WIDTH + x,
                pixels + (size_t)row * width, (size_t)width * sizeof(uint16_t));
        }
        lcd_mark_dirty(y, height);
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        lcd_set_window(x, y, x + width - 1U, y + height - 1U),
        TAG,
        "set RGB bitmap window failed");

    uint16_t source_row = 0;
    while (source_row < height) {
        uint16_t rows = height - source_row > LCD_DRAW_BUFFER_ROWS
            ? LCD_DRAW_BUFFER_ROWS
            : height - source_row;
        size_t pixel_count = (size_t)width * rows;
        const uint16_t *source = pixels + (size_t)source_row * width;
        for (size_t index = 0; index < pixel_count; ++index) {
            uint16_t color = source[index];
            lcd_draw_buffer[index * 2U] = (uint8_t)(color >> 8U);
            lcd_draw_buffer[index * 2U + 1U] = (uint8_t)color;
        }

        ESP_RETURN_ON_ERROR(
            lcd_write_data(lcd_draw_buffer, pixel_count * 2U),
            TAG,
            "RGB bitmap data write failed");
        source_row += rows;
    }

    return ESP_OK;
}

static const uint8_t *font5x7(char c)
{
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t glyphs[][7] = {
        ['0'] = {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
        ['1'] = {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['2'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
        ['3'] = {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
        ['4'] = {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
        ['5'] = {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
        ['6'] = {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
        ['7'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
        ['8'] = {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
        ['9'] = {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C},
        ['A'] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        ['B'] = {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},
        ['C'] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},
        ['D'] = {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},
        ['E'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},
        ['F'] = {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},
        ['G'] = {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},
        ['H'] = {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},
        ['I'] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['J'] = {0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C},
        ['K'] = {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},
        ['L'] = {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},
        ['M'] = {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},
        ['N'] = {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},
        ['O'] = {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        ['P'] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},
        ['Q'] = {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},
        ['R'] = {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},
        ['S'] = {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},
        ['T'] = {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        ['U'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
        ['V'] = {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},
        ['W'] = {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},
        ['X'] = {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},
        ['Y'] = {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},
        ['Z'] = {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},
        ['a'] = {0x00, 0x00, 0x0E, 0x01, 0x0F, 0x11, 0x0F},
        ['b'] = {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x1E},
        ['c'] = {0x00, 0x00, 0x0E, 0x10, 0x10, 0x11, 0x0E},
        ['d'] = {0x01, 0x01, 0x0D, 0x13, 0x11, 0x11, 0x0F},
        ['e'] = {0x00, 0x00, 0x0E, 0x11, 0x1F, 0x10, 0x0E},
        ['f'] = {0x06, 0x09, 0x08, 0x1C, 0x08, 0x08, 0x08},
        ['g'] = {0x00, 0x0F, 0x11, 0x11, 0x0F, 0x01, 0x0E},
        ['h'] = {0x10, 0x10, 0x16, 0x19, 0x11, 0x11, 0x11},
        ['i'] = {0x04, 0x00, 0x0C, 0x04, 0x04, 0x04, 0x0E},
        ['j'] = {0x02, 0x00, 0x06, 0x02, 0x02, 0x12, 0x0C},
        ['k'] = {0x10, 0x10, 0x12, 0x14, 0x18, 0x14, 0x12},
        ['l'] = {0x0C, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E},
        ['m'] = {0x00, 0x00, 0x1A, 0x15, 0x15, 0x15, 0x15},
        ['n'] = {0x00, 0x00, 0x16, 0x19, 0x11, 0x11, 0x11},
        ['o'] = {0x00, 0x00, 0x0E, 0x11, 0x11, 0x11, 0x0E},
        ['p'] = {0x00, 0x00, 0x1E, 0x11, 0x1E, 0x10, 0x10},
        ['q'] = {0x00, 0x00, 0x0F, 0x11, 0x0F, 0x01, 0x01},
        ['r'] = {0x00, 0x00, 0x16, 0x19, 0x10, 0x10, 0x10},
        ['s'] = {0x00, 0x00, 0x0F, 0x10, 0x0E, 0x01, 0x1E},
        ['t'] = {0x08, 0x08, 0x1C, 0x08, 0x08, 0x09, 0x06},
        ['u'] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x13, 0x0D},
        ['v'] = {0x00, 0x00, 0x11, 0x11, 0x11, 0x0A, 0x04},
        ['w'] = {0x00, 0x00, 0x11, 0x11, 0x15, 0x15, 0x0A},
        ['x'] = {0x00, 0x00, 0x11, 0x0A, 0x04, 0x0A, 0x11},
        ['y'] = {0x00, 0x00, 0x11, 0x11, 0x0F, 0x01, 0x0E},
        ['z'] = {0x00, 0x00, 0x1F, 0x02, 0x04, 0x08, 0x1F},
        ['!'] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04},
        ['"'] = {0x0A, 0x0A, 0x0A, 0x00, 0x00, 0x00, 0x00},
        ['#'] = {0x0A, 0x0A, 0x1F, 0x0A, 0x1F, 0x0A, 0x0A},
        ['$'] = {0x04, 0x0F, 0x14, 0x0E, 0x05, 0x1E, 0x04},
        ['-'] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
        ['*'] = {0x00, 0x11, 0x0A, 0x1F, 0x0A, 0x11, 0x00},
        ['+'] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00},
        ['%'] = {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03},
        ['&'] = {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D},
        ['\''] = {0x04, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00},
        ['('] = {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02},
        [')'] = {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08},
        [','] = {0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x08},
        ['/'] = {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10},
        [':'] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},
        [';'] = {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x08},
        ['<'] = {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02},
        ['='] = {0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00},
        ['>'] = {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08},
        ['?'] = {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04},
        ['@'] = {0x0E, 0x11, 0x17, 0x15, 0x17, 0x10, 0x0E},
        ['['] = {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E},
        ['\\'] = {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01},
        [']'] = {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E},
        ['^'] = {0x04, 0x0A, 0x11, 0x00, 0x00, 0x00, 0x00},
        ['_'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F},
        ['`'] = {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00},
        ['{'] = {0x02, 0x04, 0x04, 0x08, 0x04, 0x04, 0x02},
        ['|'] = {0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},
        ['}'] = {0x08, 0x04, 0x04, 0x02, 0x04, 0x04, 0x08},
        ['~'] = {0x00, 0x00, 0x09, 0x16, 0x00, 0x00, 0x00},
        ['.'] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C},
    };

    if ((unsigned char)c >= sizeof(glyphs) / sizeof(glyphs[0])) {
        return blank;
    }
    return glyphs[(unsigned char)c];
}

esp_err_t cerberus_display_draw_text(uint16_t x, uint16_t y, const char *text, uint16_t color, uint16_t background, uint8_t scale)
{
    if (text == NULL || scale == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT || text[0] == '\0') {
        return ESP_OK;
    }

    const uint16_t char_width = 6 * scale;
    const uint16_t char_height = 7 * scale;
    const uint16_t available_width = CERBERUS_LCD_WIDTH - x;
    const size_t max_chars = (available_width + char_width - 1) / char_width;
    size_t char_count = 0;
    while (char_count < max_chars && text[char_count] != '\0') {
        char_count++;
    }
    if (char_count == 0) {
        return ESP_OK;
    }

    const uint8_t *text_glyphs[LCD_MAX_TEXT_GLYPHS];
    for (size_t i = 0; i < char_count; ++i) {
        text_glyphs[i] = font5x7(text[i]);
    }

    uint16_t draw_width = char_count * char_width;
    uint16_t draw_height = char_height;
    if (draw_width > available_width) {
        draw_width = available_width;
    }
    if (y + draw_height > CERBERUS_LCD_HEIGHT) {
        draw_height = CERBERUS_LCD_HEIGHT - y;
    }
    if ((size_t)draw_width * draw_height * 2 > sizeof(lcd_draw_buffer)) {
        return ESP_ERR_NO_MEM;
    }

    lcd_note_top_draw(y);
    for (uint16_t row = 0; row < draw_height; ++row) {
        const uint8_t glyph_row = row / scale;
        for (uint16_t col = 0; col < draw_width; ++col) {
            const size_t char_index = col / char_width;
            const uint8_t glyph_col = (col % char_width) / scale;
            const uint8_t *glyph = text_glyphs[char_index];
            const bool active = glyph_col < 5 && (glyph[glyph_row] & (1U << (4 - glyph_col)));
            const uint16_t pixel_color = active ? color : background;
            if (lcd_back_frame != NULL) {
                lcd_back_frame[(size_t)(y + row) * CERBERUS_LCD_WIDTH + x + col] = pixel_color;
            } else {
                const size_t offset = ((size_t)row * draw_width + col) * 2;
                lcd_draw_buffer[offset] = (uint8_t)(pixel_color >> 8);
                lcd_draw_buffer[offset + 1] = (uint8_t)pixel_color;
            }
        }
    }

    if (lcd_back_frame != NULL) {
        lcd_mark_dirty(y, draw_height);
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(lcd_set_window(x, y, x + draw_width - 1, y + draw_height - 1), TAG, "set text window failed");
    return lcd_write_data(lcd_draw_buffer, (size_t)draw_width * draw_height * 2);
}
