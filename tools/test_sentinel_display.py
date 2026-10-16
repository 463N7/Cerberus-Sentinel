#!/usr/bin/env python3
"""Exercise the full display.c against a simulated ST7796 SPI panel and PSRAM.

No firmware build or hardware access. Uses the installed ziglang host compiler.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_RETURN_ON_ERROR(expr, tag, ...) do { (void)(tag); esp_err_t e_ = (expr); if (e_ != ESP_OK) return e_; } while (0)
#define ESP_LOGI(tag, ...) ((void)(tag))
#define MALLOC_CAP_SPIRAM 0x400
#define MALLOC_CAP_8BIT 4
void *heap_caps_calloc(size_t count, size_t size, uint32_t caps);
void heap_caps_free(void *memory);
#define GPIO_MODE_OUTPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define GPIO_INTR_DISABLE 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en, pull_down_en, intr_type; } gpio_config_t;
int gpio_set_level(int pin, int level);
int gpio_config(const gpio_config_t *config);
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_10_BIT 10
#define LEDC_TIMER_0 0
#define LEDC_AUTO_CLK 0
#define LEDC_CHANNEL_0 0
#define LEDC_INTR_DISABLE 0
typedef struct { int speed_mode, duty_resolution, timer_num, freq_hz, clk_cfg; } ledc_timer_config_t;
typedef struct { int gpio_num, speed_mode, channel, intr_type, timer_sel; uint32_t duty; int hpoint; } ledc_channel_config_t;
int ledc_timer_config(const ledc_timer_config_t *config);
int ledc_channel_config(const ledc_channel_config_t *config);
int ledc_set_duty(int mode, int channel, uint32_t duty);
int ledc_update_duty(int mode, int channel);
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 3
typedef void *spi_device_handle_t;
typedef struct { size_t length; const void *tx_buffer; } spi_transaction_t;
typedef struct { int mosi_io_num, miso_io_num, sclk_io_num, quadwp_io_num, quadhd_io_num, max_transfer_sz; } spi_bus_config_t;
typedef struct { int clock_speed_hz, mode, spics_io_num, queue_size; } spi_device_interface_config_t;
int spi_bus_initialize(int host, const spi_bus_config_t *bus, int dma);
int spi_bus_add_device(int host, const spi_device_interface_config_t *device, spi_device_handle_t *out);
int spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction);
#define pdMS_TO_TICKS(value) (value)
void vTaskDelay(int ticks);
'''

HARNESS = r'''
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "display.c"
#include "sentinel_battery_view.h"

static uint16_t panel[CERBERUS_LCD_WIDTH * CERBERUS_LCD_HEIGHT];
static uint16_t expected[CERBERUS_LCD_WIDTH * CERBERUS_LCD_HEIGHT];
static void *allocations[2];
static size_t allocation_sizes[2];
static unsigned alloc_calls, free_calls, fail_allocation;
static unsigned transfers, windows, pixel_transfers, pixels_sent, fail_transfer;
static int dc;
static uint8_t command;
static uint16_t x0, x1, y0, y1, cursor_x, cursor_y;
static uint16_t last_x0, last_x1, last_y0, last_y1;

void *heap_caps_calloc(size_t count, size_t size, uint32_t caps)
{
    assert(caps == (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    assert(count * size == 307200);
    if (++alloc_calls == fail_allocation) return NULL;
    void *memory = calloc(count, size); assert(memory);
    for (unsigned i = 0; i < 2; ++i) if (!allocations[i]) {
        allocations[i] = memory; allocation_sizes[i] = count * size; return memory;
    }
    assert(false); return NULL;
}
void heap_caps_free(void *memory)
{
    assert(memory);
    for (unsigned i = 0; i < 2; ++i) if (allocations[i] == memory) {
        allocations[i] = NULL; allocation_sizes[i] = 0; ++free_calls; free(memory); return;
    }
    assert(false);
}
int gpio_set_level(int pin, int level) { if (pin == CERBERUS_LCD_DC_GPIO) dc = level; return ESP_OK; }
int gpio_config(const gpio_config_t *config) { assert(config); return ESP_OK; }
int ledc_timer_config(const ledc_timer_config_t *config) { assert(config); return ESP_OK; }
int ledc_channel_config(const ledc_channel_config_t *config) { assert(config); return ESP_OK; }
int ledc_set_duty(int mode, int channel, uint32_t duty)
{ assert(mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_0 && duty <= 1023); return ESP_OK; }
int ledc_update_duty(int mode, int channel)
{ assert(mode == LEDC_LOW_SPEED_MODE && channel == LEDC_CHANNEL_0); return ESP_OK; }
int spi_bus_initialize(int host, const spi_bus_config_t *bus, int dma)
{ assert(host == SPI2_HOST && dma == SPI_DMA_CH_AUTO && bus->max_transfer_sz == 320 * 28 * 2); return ESP_OK; }
int spi_bus_add_device(int host, const spi_device_interface_config_t *device, spi_device_handle_t *out)
{ assert(host == SPI2_HOST && device->clock_speed_hz == 40000000); *out = (void *)1; return ESP_OK; }
void vTaskDelay(int ticks) { (void)ticks; }

static void apply_pixels(const uint8_t *data, size_t bytes)
{
    assert(bytes % 2 == 0 && bytes <= sizeof(lcd_draw_buffer));
    for (size_t i = 0; i < bytes; i += 2) {
        assert(cursor_x <= x1 && cursor_y <= y1 && cursor_x < 320 && cursor_y < 480);
        panel[(size_t)cursor_y * 320 + cursor_x] = (uint16_t)((data[i] << 8) | data[i + 1]);
        if (++cursor_x > x1) { cursor_x = x0; ++cursor_y; }
    }
}
int spi_device_polling_transmit(spi_device_handle_t device, spi_transaction_t *transaction)
{
    assert(device && transaction->length % 8 == 0);
    const uint8_t *data = transaction->tx_buffer;
    size_t bytes = transaction->length / 8;
    for (unsigned i = 0; i < 2; ++i) if (allocations[i]) {
        uintptr_t address = (uintptr_t)data, start = (uintptr_t)allocations[i];
        assert(address + bytes <= start || address >= start + allocation_sizes[i]);
    }
    if (++transfers == fail_transfer) {
        /* Model a failed pixel transfer whose first half reached the LCD. */
        if (dc && command == 0x2c) apply_pixels(data, (bytes / 4) * 2);
        return ESP_FAIL;
    }
    if (!dc) {
        assert(bytes == 1); command = data[0];
        if (command == 0x2c) {
            assert(x0 <= x1 && y0 <= y1 && x1 < 320 && y1 < 480);
            cursor_x = x0; cursor_y = y0; ++windows;
            last_x0 = x0; last_x1 = x1; last_y0 = y0; last_y1 = y1;
        }
    } else if (command == 0x2a || command == 0x2b) {
        assert(bytes == 4);
        uint16_t first = (uint16_t)((data[0] << 8) | data[1]);
        uint16_t last = (uint16_t)((data[2] << 8) | data[3]);
        if (command == 0x2a) { x0 = first; x1 = last; } else { y0 = first; y1 = last; }
    } else if (command == 0x2c) {
        ++pixel_transfers; pixels_sent += (unsigned)(bytes / 2); apply_pixels(data, bytes);
    }
    return ESP_OK;
}
static void clear_counters(void)
{ transfers = windows = pixel_transfers = pixels_sent = fail_transfer = 0; }
static void release_frames(void)
{
    if (lcd_back_frame) heap_caps_free(lcd_back_frame);
    if (lcd_front_frame) heap_caps_free(lcd_front_frame);
    lcd_back_frame = lcd_front_frame = NULL; lcd_dirty_bands = lcd_valid_bands = 0;
    assert(!allocations[0] && !allocations[1]);
}
static void draw_scene(void)
{
    uint8_t bitmap[31 * 2];
    for (unsigned i = 0; i < sizeof(bitmap); ++i) bitmap[i] = (uint8_t)(i * 73);
    uint16_t rgb[19 * 43];
    for (unsigned i = 0; i < sizeof(rgb) / sizeof(rgb[0]); ++i) rgb[i] = (uint16_t)(i * 511);
    assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x0062) == ESP_OK);
    assert(cerberus_display_fill_rect(14, 101, 279, 62, 0x08a4) == ESP_OK);
    assert(cerberus_display_fill_rect(318, 478, UINT16_MAX, UINT16_MAX, 0xabcd) == ESP_OK);
    assert(cerberus_display_fill_rect(320, 480, 1, 1, 0xffff) == ESP_OK);
    assert(cerberus_display_fill_rect(0, 0, 0, 10, 0xffff) == ESP_OK);
    assert(cerberus_display_draw_mono_bitmap(42, 85, 13, 31, bitmap, 0xa157, 0x0153) == ESP_OK);
    assert(cerberus_display_draw_rgb565_bitmap(174, 201, 19, 43, rgb) == ESP_OK);
    assert(cerberus_display_draw_text(4, 2, "Sentinel 1~9", 0xcaf3, 0x0062, 1) == ESP_OK);
    assert(cerberus_display_draw_text(20, 140, "TEST", 0xaaa3, 0x08a4, 4) == ESP_OK);
    assert(cerberus_display_draw_text(290, 470, "CLIPPED", 0x82e3, 0x00b1, 2) == ESP_OK);
    assert(cerberus_display_draw_text(319, 479, "A", 0x7777, 0xabcd, 255) == ESP_OK);
    assert(cerberus_display_draw_text(320, 480, "OK", 1, 2, 1) == ESP_OK);
    assert(cerberus_display_draw_text(10, 10, "", 1, 2, 1) == ESP_OK);
}
static void check_validation(void)
{
    uint16_t rgb = 0; uint8_t mono = 0;
    unsigned before = transfers;
    uint32_t revision = cerberus_display_top_revision();
    assert(cerberus_display_draw_mono_bitmap(0, 0, 1, 1, NULL, 1, 0) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_mono_bitmap(319, 479, 2, 1, &mono, 1, 0) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_mono_bitmap(0, 480, 1, 1, &mono, 1, 0) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_rgb565_bitmap(0, 0, 0, 1, &rgb) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_rgb565_bitmap(0, 0, 1, 1, NULL) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_rgb565_bitmap(319, 479, 1, 2, &rgb) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_text(0, 0, NULL, 1, 2, 1) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_text(0, 0, "Hi", 1, 2, 0) == ESP_ERR_INVALID_ARG);
    assert(cerberus_display_draw_text(0, 0, "TOO WIDE AT SCALE 5", 1, 2, 5) == ESP_ERR_NO_MEM);
    assert(transfers == before);
    assert(cerberus_display_top_revision() == revision);
}
static void check_top_revision(void)
{
    uint16_t rgb = 0x1357; uint8_t mono = 0x80;
    uint32_t revision = cerberus_display_top_revision();
    assert(cerberus_display_fill_rect(0, 16, 1, 1, rgb) == ESP_OK);
    assert(cerberus_display_draw_text(0, 16, "A", rgb, 0, 1) == ESP_OK);
    assert(cerberus_display_draw_mono_bitmap(0, 16, 1, 1, &mono, rgb, 0) == ESP_OK);
    assert(cerberus_display_draw_rgb565_bitmap(0, 16, 1, 1, &rgb) == ESP_OK);
    assert(cerberus_display_top_revision() == revision);
    assert(cerberus_display_fill_rect(0, 15, 1, 1, rgb) == ESP_OK);
    assert(cerberus_display_top_revision() == ++revision);
    assert(cerberus_display_draw_text(0, 15, "A", rgb, 0, 1) == ESP_OK);
    assert(cerberus_display_top_revision() == ++revision);
    assert(cerberus_display_draw_mono_bitmap(0, 15, 1, 1, &mono, rgb, 0) == ESP_OK);
    assert(cerberus_display_top_revision() == ++revision);
    assert(cerberus_display_draw_rgb565_bitmap(0, 15, 1, 1, &rgb) == ESP_OK);
    assert(cerberus_display_top_revision() == ++revision);
}
static void test_allocations_and_parity(void)
{
    assert(cerberus_display_init() == ESP_OK);
    assert(!cerberus_display_buffering_enabled());
    assert(cerberus_display_present() == ESP_OK);
    check_top_revision();
    fail_allocation = alloc_calls + 1;
    assert(cerberus_display_enable_buffering() == ESP_ERR_NO_MEM);
    assert(!cerberus_display_buffering_enabled() && !allocations[0] && !allocations[1]);
    fail_allocation = alloc_calls + 2;
    unsigned freed = free_calls;
    assert(cerberus_display_enable_buffering() == ESP_ERR_NO_MEM);
    assert(!cerberus_display_buffering_enabled() && free_calls == freed + 1);
    assert(!allocations[0] && !allocations[1]);
    clear_counters(); draw_scene(); check_validation();
    assert(transfers && pixel_transfers && panel[479 * 320 + 319] == 0xabcd);
    memcpy(expected, panel, sizeof(expected));
    fail_allocation = 0; clear_counters();
    assert(cerberus_display_enable_buffering() == ESP_OK);
    assert(cerberus_display_buffering_enabled() && allocations[0] && allocations[1]);
    check_top_revision();
    unsigned allocated = alloc_calls;
    assert(cerberus_display_enable_buffering() == ESP_OK && alloc_calls == allocated);
    draw_scene(); check_validation();
    assert(!transfers && !memcmp(panel, expected, sizeof(panel)));
    memset(panel, 0x55, sizeof(panel)); /* Enabling invalidates unknown existing LCD contents. */
    assert(cerberus_display_present() == ESP_OK);
    assert(windows == 18 && pixel_transfers == 18 && pixels_sent == 320 * 480);
    assert(!memcmp(panel, expected, sizeof(panel)));
    assert(!memcmp(lcd_front_frame, expected, sizeof(panel)));
    clear_counters();
    assert(cerberus_display_present() == ESP_OK && !transfers);
    draw_scene(); assert(!transfers);
    assert(cerberus_display_present() == ESP_OK && !transfers);
    assert(!memcmp(panel, expected, sizeof(panel)));
}
static void test_dirty_bounds(void)
{
    clear_counters();
    assert(cerberus_display_fill_rect(101, 115, 3, 2, 0xdead) == ESP_OK && !transfers);
    assert(cerberus_display_present() == ESP_OK);
    assert(windows == 1 && pixels_sent == 6);
    assert(last_x0 == 101 && last_x1 == 103 && last_y0 == 115 && last_y1 == 116);
    for (unsigned y = 115; y <= 116; ++y) for (unsigned x = 101; x <= 103; ++x) expected[y * 320 + x] = 0xdead;
    assert(!memcmp(panel, expected, sizeof(panel)));
    clear_counters();
    assert(cerberus_display_fill_rect(2, 27, 3, 3, 0x1234) == ESP_OK);
    assert(cerberus_display_present() == ESP_OK);
    assert(windows == 2 && pixels_sent == 9);
    for (unsigned y = 27; y <= 29; ++y) for (unsigned x = 2; x <= 4; ++x) expected[y * 320 + x] = 0x1234;
    assert(!memcmp(panel, expected, sizeof(panel)));
}
static void test_failure_recovery(void)
{
    /* Fail every command/data stage of a window, then retry with no redraw. */
    for (unsigned failure = 1; failure <= 6; ++failure) {
        clear_counters(); draw_scene(); assert(cerberus_display_present() == ESP_OK);
        memcpy(expected, panel, sizeof(panel)); clear_counters();
        assert(cerberus_display_fill_rect(50, 58, 10, 4, 0xfeed) == ESP_OK);
        fail_transfer = failure;
        assert(cerberus_display_present() == ESP_FAIL);
        assert(!memcmp(lcd_front_frame, expected, sizeof(panel)));
        clear_counters(); assert(cerberus_display_present() == ESP_OK);
        assert(windows == 1 && pixels_sent == 320 * 28);
        for (unsigned y = 58; y < 62; ++y) for (unsigned x = 50; x < 60; ++x) expected[y * 320 + x] = 0xfeed;
        assert(!memcmp(panel, expected, sizeof(panel)));
        clear_counters(); assert(cerberus_display_present() == ESP_OK && !transfers);
    }
    /* A partially sent change is still repaired when the CPU reverts to its old frame. */
    clear_counters(); draw_scene(); assert(cerberus_display_present() == ESP_OK);
    memcpy(expected, panel, sizeof(panel)); clear_counters();
    assert(cerberus_display_fill_rect(25, 58, 16, 4, 0xbeef) == ESP_OK);
    fail_transfer = 6; assert(cerberus_display_present() == ESP_FAIL);
    assert(memcmp(panel, expected, sizeof(panel)));
    clear_counters(); draw_scene(); assert(cerberus_display_present() == ESP_OK);
    assert(windows == 1 && !memcmp(panel, expected, sizeof(panel)));
    /* A successful first band is retained; a failed later band and remaining bands retry. */
    clear_counters(); assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x8765) == ESP_OK);
    fail_transfer = 12; assert(cerberus_display_present() == ESP_FAIL);
    assert(lcd_front_frame[0] == 0x8765);
    clear_counters(); assert(cerberus_display_present() == ESP_OK);
    assert(windows == 17);
    for (size_t i = 0; i < 320 * 480; ++i) assert(panel[i] == 0x8765 && lcd_front_frame[i] == 0x8765);
}
static void test_battery_overlay(void)
{
    /* Real renderer + real driver: no direct-mode churn or buffered erase flash. */
    for (unsigned buffered = 0; buffered < 2; ++buffered) {
        if (buffered) assert(cerberus_display_enable_buffering() == ESP_OK);
        assert(cerberus_display_fill_rect(0, 0, 320, 480, 0x0062) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK);
        sentinel_battery_view_invalidate(); clear_counters();
        assert(sentinel_battery_view_update(true, 73, 0x0062) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK);
        assert(windows == 1 && pixels_sent <= 68 * 13);
        assert(last_x0 >= 244 && last_x1 < 312 && last_y0 >= 3 && last_y1 < 16);
        clear_counters();
        for (unsigned frame = 0; frame < 50; ++frame) {
            assert(sentinel_battery_view_update(true, 73, 0x0062) == ESP_OK);
            assert(cerberus_display_present() == ESP_OK && !transfers);
        }
        memcpy(expected, panel, sizeof(panel));
        assert(cerberus_display_fill_rect(244, 3, 68, 13, 0x0062) == ESP_OK);
        clear_counters();
        assert(sentinel_battery_view_update(true, 73, 0x0062) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK);
        assert(!memcmp(expected, panel, sizeof(panel)));
        if (buffered) assert(!transfers); else assert(windows == 1);
        assert(cerberus_display_fill_rect(32, 100, 10, 10, 0x08a4) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK); clear_counters();
        assert(sentinel_battery_view_update(true, 73, 0x0062) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK && !transfers);
        assert(sentinel_battery_view_update(true, 12, 0x0062) == ESP_OK);
        assert(cerberus_display_present() == ESP_OK);
        assert(windows == 1 && pixels_sent <= 68 * 13);
        assert(last_x0 >= 244 && last_x1 < 312 && last_y0 >= 3 && last_y1 < 16);
        if (!buffered) {
            clear_counters(); fail_transfer = 6;
            assert(sentinel_battery_view_update(false, 0, 0x0062) == ESP_FAIL);
            clear_counters();
            assert(sentinel_battery_view_update(false, 0, 0x0062) == ESP_OK && windows == 1);
        }
    }
    release_frames();
}
int main(void)
{
    test_allocations_and_parity(); test_dirty_bounds(); test_failure_recovery();
    release_frames();
    /* First presentation failure keeps the initial unknown frame retryable. */
    assert(cerberus_display_enable_buffering() == ESP_OK);
    clear_counters(); fail_transfer = 6; assert(cerberus_display_present() == ESP_FAIL);
    clear_counters(); assert(cerberus_display_present() == ESP_OK && windows == 18);
    for (size_t i = 0; i < 320 * 480; ++i) assert(panel[i] == 0);
    release_frames();
    test_battery_overlay();
    puts("PASS: actual display driver PSRAM allocation/fallback, buffered/direct pixel parity, clipping/validation, no intermediate or identical-frame transfers, dirty bounds, partial-write retries, top revision, and battery overlay restoration/transfer bounds.");
    return 0;
}
'''


def main():
    with tempfile.TemporaryDirectory(prefix="sentinel-display-") as directory:
        path = Path(directory)
        (path / "stubs.h").write_text(STUBS)
        for name in ("esp_err.h", "esp_check.h", "esp_log.h", "esp_heap_caps.h",
                     "driver/gpio.h", "driver/ledc.h", "driver/spi_master.h",
                     "freertos/FreeRTOS.h", "freertos/task.h"):
            header = path / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "stubs.h"\n')
        source = path / "test_display.c"
        source.write_text(HARNESS)
        executable = path / ("display.exe" if os.name == "nt" else "display")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path),
            "-I", str(ROOT / "s3_main/main"), "-I", str(ROOT / "shared"),
            str(source), str(ROOT / "s3_main/main/sentinel_battery_view.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
