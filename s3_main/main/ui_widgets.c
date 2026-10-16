#include "ui_widgets.h"

#include "display.h"

#include "esp_check.h"

#include <string.h>

static const char *TAG = "ui_widget";

static uint16_t blend_color(uint16_t surface, uint16_t accent, uint8_t eighths)
{
    uint16_t red = (((surface >> 11U) & 31U) * (8U - eighths) +
        ((accent >> 11U) & 31U) * eighths) / 8U;
    uint16_t green = (((surface >> 5U) & 63U) * (8U - eighths) +
        ((accent >> 5U) & 63U) * eighths) / 8U;
    uint16_t blue = ((surface & 31U) * (8U - eighths) +
        (accent & 31U) * eighths) / 8U;
    return (uint16_t)((red << 11U) | (green << 5U) | blue);
}

/* Text is fitted inside the caller's existing rectangle. Narrow buttons and
   unusually long device/file names cannot paint across adjacent controls. */
static esp_err_t draw_fitted_text(
    cerberus_ui_rect_t area,
    const char *text,
    uint8_t scale,
    bool centered,
    bool right_aligned,
    uint16_t foreground,
    uint16_t background)
{
    if (text == NULL || text[0] == '\0' || area.width < 6U || area.height < 7U) {
        return ESP_OK;
    }
    size_t length = strlen(text);
    while (scale > 1U &&
        (length > area.width / (6U * scale) || 7U * scale > area.height)) {
        --scale;
    }
    char fitted[CERBERUS_LCD_WIDTH / 6U + 1U];
    size_t capacity = area.width / (6U * scale);
    if (capacity == 0U || 7U * scale > area.height) return ESP_OK;
    if (capacity >= sizeof(fitted)) capacity = sizeof(fitted) - 1U;
    size_t visible = length < capacity ? length : capacity;
    memcpy(fitted, text, visible);
    if (length > visible && visible >= 3U) {
        memcpy(fitted + visible - 3U, "...", 3U);
    }
    fitted[visible] = '\0';
    uint16_t width = (uint16_t)(visible * 6U * scale);
    uint16_t x = centered ? (uint16_t)(area.x + (area.width - width) / 2U) : area.x;
    if (right_aligned) x = (uint16_t)(area.x + area.width - width);
    uint16_t y = (uint16_t)(area.y + (area.height - 7U * scale) / 2U);
    return cerberus_display_draw_text(x, y, fitted, foreground, background, scale);
}

static bool valid_rect(cerberus_ui_rect_t bounds)
{
    return bounds.width > 0U && bounds.height > 0U &&
        bounds.x < CERBERUS_LCD_WIDTH && bounds.y < CERBERUS_LCD_HEIGHT &&
        (uint32_t)bounds.x + bounds.width <= CERBERUS_LCD_WIDTH &&
        (uint32_t)bounds.y + bounds.height <= CERBERUS_LCD_HEIGHT;
}

esp_err_t cerberus_ui_widget_grid_cell(
    cerberus_ui_rect_t grid,
    uint8_t columns,
    uint16_t horizontal_gap,
    uint16_t vertical_gap,
    uint16_t cell_height,
    size_t index,
    cerberus_ui_rect_t *cell)
{
    if (!valid_rect(grid) || columns == 0U || cell_height == 0U || cell == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint32_t total_gap = (uint32_t)(columns - 1U) * horizontal_gap;
    if (total_gap >= grid.width) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t cell_width = (uint16_t)((grid.width - total_gap) / columns);
    size_t row = index / columns;
    size_t column = index % columns;
    uint32_t x = grid.x + column * (cell_width + horizontal_gap);
    uint32_t y = grid.y + row * (cell_height + vertical_gap);
    if (x + cell_width > (uint32_t)grid.x + grid.width ||
        y + cell_height > (uint32_t)grid.y + grid.height) {
        return ESP_ERR_INVALID_SIZE;
    }
    *cell = (cerberus_ui_rect_t){
        .x = (uint16_t)x,
        .y = (uint16_t)y,
        .width = cell_width,
        .height = cell_height,
    };
    return ESP_OK;
}

esp_err_t cerberus_ui_widget_draw_button(
    cerberus_ui_rect_t bounds,
    const char *label,
    uint8_t text_scale,
    bool enabled,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_rect(bounds) || bounds.width < 5U || bounds.height < 5U ||
        label == NULL || text_scale == 0U || theme == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t edge = blend_color(theme->panel, enabled ? theme->border : theme->muted, 2U);
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, edge),
        TAG,
        "button border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(
            bounds.x + 1U,
            bounds.y + 1U,
            bounds.width - 2U,
            bounds.height - 2U,
            theme->panel),
        TAG,
        "button panel failed");

    if (bounds.width >= 18U && bounds.height >= 18U) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
            bounds.x + 1U, bounds.y + 7U, 2U, bounds.height - 14U,
            enabled ? theme->border : edge), TAG, "button accent failed");
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
            bounds.x, bounds.y, 2U, 2U, theme->background), TAG, "button corner failed");
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
            bounds.x + bounds.width - 2U, bounds.y + bounds.height - 2U,
            2U, 2U, theme->background), TAG, "button corner failed");
    }
    uint16_t inset = bounds.width >= 18U ? 7U : 2U;
    return draw_fitted_text(
        (cerberus_ui_rect_t){bounds.x + inset, bounds.y + 2U,
            bounds.width - 2U * inset, bounds.height - 4U},
        label,
        text_scale, true, false, enabled ? theme->text : theme->muted, theme->panel);
}

esp_err_t cerberus_ui_widget_draw_list_row(
    cerberus_ui_rect_t bounds,
    const char *primary,
    const char *trailing,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_rect(bounds) || theme == NULL || bounds.width < 12U || bounds.height < 12U) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, theme->panel),
        TAG,
        "list row panel failed");
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
        bounds.x, bounds.y + bounds.height - 1U, bounds.width, 1U,
        blend_color(theme->panel, theme->muted, 1U)), TAG, "list separator failed");
    bool has_primary = primary != NULL && primary[0] != '\0';
    bool has_trailing = trailing != NULL && trailing[0] != '\0';
    if (!has_primary && !has_trailing) return ESP_OK;
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
        bounds.x, bounds.y + 4U, 2U, bounds.height - 8U,
        blend_color(theme->panel, theme->border, 5U)), TAG, "list accent failed");
    uint16_t inset = bounds.width >= 20U ? 8U : 3U;
    cerberus_ui_rect_t content = {bounds.x + inset, bounds.y + 2U,
        bounds.width - 2U * inset, bounds.height - 4U};
    if (has_primary && has_trailing && bounds.height >= 27U) {
        cerberus_ui_rect_t primary_area = {content.x, bounds.y + 5U, content.width, 8U};
        cerberus_ui_rect_t trailing_area = {content.x, bounds.y + bounds.height - 14U,
            content.width, 8U};
        ESP_RETURN_ON_ERROR(draw_fitted_text(primary_area, primary, 1U, false, false,
            theme->text, theme->panel), TAG, "list row primary failed");
        return draw_fitted_text(trailing_area, trailing, 1U, false, true,
            theme->muted, theme->panel);
    }
    if (has_primary && has_trailing && content.width >= 24U) {
        uint16_t trailing_width = (uint16_t)(content.width / 3U);
        cerberus_ui_rect_t primary_area = content;
        primary_area.width = content.width - trailing_width - 6U;
        cerberus_ui_rect_t trailing_area = content;
        trailing_area.x += content.width - trailing_width;
        trailing_area.width = trailing_width;
        ESP_RETURN_ON_ERROR(draw_fitted_text(primary_area, primary, 1U, false, false,
            theme->text, theme->panel), TAG, "list row primary failed");
        return draw_fitted_text(trailing_area, trailing, 1U, false, true,
            theme->muted, theme->panel);
    }
    ESP_RETURN_ON_ERROR(draw_fitted_text(content, has_primary ? primary : trailing,
        1U, false, !has_primary, has_primary ? theme->text : theme->muted, theme->panel),
        TAG, "list text failed");
    return ESP_OK;
}

esp_err_t cerberus_ui_widget_draw_dialog(
    cerberus_ui_rect_t bounds,
    const char *title,
    const char *const *lines,
    size_t line_count,
    uint16_t title_color,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_rect(bounds) || bounds.width < 8U || bounds.height < 8U ||
        title == NULL || theme == NULL || line_count > 5U ||
        (line_count > 0U && lines == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
            blend_color(theme->panel, theme->border, 2U)),
        TAG,
        "dialog border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(
            bounds.x + 2U,
            bounds.y + 2U,
            bounds.width - 4U,
            bounds.height - 4U,
            theme->panel),
        TAG,
        "dialog panel failed");

    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(bounds.x + 2U, bounds.y + 2U,
        bounds.width - 4U, 2U, title_color), TAG, "dialog accent failed");
    if (bounds.width <= 20U || bounds.height < 40U) return ESP_OK;
    ESP_RETURN_ON_ERROR(draw_fitted_text(
        (cerberus_ui_rect_t){bounds.x + 10U, bounds.y + 18U, bounds.width - 20U, 14U},
        title, 2U, true, false, title_color, theme->panel), TAG, "dialog title failed");
    if (bounds.height > 52U) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(bounds.x + 10U, bounds.y + 49U,
            bounds.width - 20U, 1U, blend_color(theme->panel, theme->muted, 2U)),
            TAG, "dialog separator failed");
    }
    for (size_t index = 0U; index < line_count; ++index) {
        if (lines[index] == NULL) {
            continue;
        }
        uint16_t offset = 70U + (uint16_t)index * 30U;
        if (offset + 7U > bounds.height - 4U) break;
        ESP_RETURN_ON_ERROR(
            draw_fitted_text((cerberus_ui_rect_t){bounds.x + 10U,
                bounds.y + offset, bounds.width - 20U, 7U},
                lines[index], 1U, true, false,
                index == 0U ? theme->text : theme->muted, theme->panel),
            TAG,
            "dialog line failed");
    }
    return ESP_OK;
}

esp_err_t cerberus_ui_widget_draw_progress(
    cerberus_ui_rect_t bounds,
    uint32_t value,
    uint32_t maximum,
    uint16_t fill_color,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_rect(bounds) || bounds.width < 7U || bounds.height < 7U ||
        maximum == 0U || theme == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (value > maximum) {
        value = maximum;
    }
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height,
            blend_color(theme->panel, theme->border, 2U)),
        TAG,
        "progress border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(
            bounds.x + 3U,
            bounds.y + 3U,
            bounds.width - 6U,
            bounds.height - 6U,
            blend_color(theme->panel, theme->background, 4U)),
        TAG,
        "progress track failed");
    uint16_t fill_width = (uint16_t)(((uint64_t)(bounds.width - 6U) * value) / maximum);
    if (fill_width == 0U) {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
        bounds.x + 3U,
        bounds.y + 3U,
        fill_width,
        bounds.height - 6U,
        fill_color), TAG, "progress fill failed");
    if (bounds.height >= 10U) {
        return cerberus_display_fill_rect(bounds.x + 3U, bounds.y + 3U,
            fill_width, 1U, blend_color(fill_color, theme->text, 2U));
    }
    return ESP_OK;
}

esp_err_t cerberus_ui_widget_draw_bar_graph(
    cerberus_ui_rect_t bounds,
    const int16_t *values,
    const uint16_t *colors,
    size_t count,
    int16_t minimum,
    int16_t maximum,
    uint16_t fill_color,
    const cerberus_ui_widget_theme_t *theme)
{
    if (!valid_rect(bounds) || values == NULL || count == 0U || theme == NULL ||
        minimum >= maximum || count > bounds.width) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(bounds.x, bounds.y, bounds.width, bounds.height, theme->panel),
        TAG,
        "graph clear failed");
    if (bounds.height >= 20U) {
        for (uint16_t division = 1U; division <= 3U; ++division) {
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(bounds.x,
                bounds.y + (bounds.height * division) / 4U, bounds.width, 1U,
                blend_color(theme->panel, theme->muted, 1U)), TAG, "graph grid failed");
        }
    }
    uint16_t slot_width = (uint16_t)(bounds.width / count);
    uint16_t bar_width = slot_width > 2U ? slot_width - 2U : 1U;
    int32_t range = maximum - minimum;
    for (size_t index = 0U; index < count; ++index) {
        int32_t value = values[index];
        if (value < minimum) {
            value = minimum;
        } else if (value > maximum) {
            value = maximum;
        }
        uint16_t height = (uint16_t)(((value - minimum) * bounds.height) / range);
        if (height == 0U) {
            height = 1U;
        }
        ESP_RETURN_ON_ERROR(
            cerberus_display_fill_rect(
                bounds.x + (uint16_t)index * slot_width + (slot_width - bar_width) / 2U,
                bounds.y + bounds.height - height,
                bar_width,
                height,
                colors == NULL ? fill_color : colors[index]),
            TAG,
            "graph bar failed");
    }
    return ESP_OK;
}
