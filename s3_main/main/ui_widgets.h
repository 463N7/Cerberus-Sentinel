#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t x;
    uint16_t y;
    uint16_t width;
    uint16_t height;
} cerberus_ui_rect_t;

typedef struct {
    uint16_t background;
    uint16_t panel;
    uint16_t border;
    uint16_t text;
    uint16_t muted;
} cerberus_ui_widget_theme_t;

esp_err_t cerberus_ui_widget_grid_cell(
    cerberus_ui_rect_t grid,
    uint8_t columns,
    uint16_t horizontal_gap,
    uint16_t vertical_gap,
    uint16_t cell_height,
    size_t index,
    cerberus_ui_rect_t *cell);
esp_err_t cerberus_ui_widget_draw_button(
    cerberus_ui_rect_t bounds,
    const char *label,
    uint8_t text_scale,
    bool enabled,
    const cerberus_ui_widget_theme_t *theme);
esp_err_t cerberus_ui_widget_draw_list_row(
    cerberus_ui_rect_t bounds,
    const char *primary,
    const char *trailing,
    const cerberus_ui_widget_theme_t *theme);
esp_err_t cerberus_ui_widget_draw_dialog(
    cerberus_ui_rect_t bounds,
    const char *title,
    const char *const *lines,
    size_t line_count,
    uint16_t title_color,
    const cerberus_ui_widget_theme_t *theme);
esp_err_t cerberus_ui_widget_draw_progress(
    cerberus_ui_rect_t bounds,
    uint32_t value,
    uint32_t maximum,
    uint16_t fill_color,
    const cerberus_ui_widget_theme_t *theme);
esp_err_t cerberus_ui_widget_draw_bar_graph(
    cerberus_ui_rect_t bounds,
    const int16_t *values,
    const uint16_t *colors,
    size_t count,
    int16_t minimum,
    int16_t maximum,
    uint16_t fill_color,
    const cerberus_ui_widget_theme_t *theme);

#ifdef __cplusplus
}
#endif
