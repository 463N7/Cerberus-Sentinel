#pragma once

#include "ui_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SENTINEL_TOOLS_MAIN,
    SENTINEL_TOOLS_CARE,
    SENTINEL_TOOLS_EXTRAS,
} sentinel_tools_group_t;

typedef enum {
    SENTINEL_TOOLS_NONE,
    SENTINEL_TOOLS_SETTINGS,
    SENTINEL_TOOLS_FILES,
    SENTINEL_TOOLS_LOGS,
    SENTINEL_TOOLS_WEB,
    SENTINEL_TOOLS_OPEN_CARE,
    SENTINEL_TOOLS_OPEN_EXTRAS,
    SENTINEL_TOOLS_STATUS,
    SENTINEL_TOOLS_CHECKS,
    SENTINEL_TOOLS_USB,
    SENTINEL_TOOLS_QR,
    SENTINEL_TOOLS_SCRIPTS,
    SENTINEL_TOOLS_BACK,
} sentinel_tools_action_t;

/* The caller draws the background, header and battery. This renderer owns
 * content below y=48, including the shared Home/Device Tools button. */
esp_err_t sentinel_tools_draw(
    sentinel_tools_group_t group,
    const cerberus_ui_widget_theme_t *theme);
sentinel_tools_action_t sentinel_tools_hit_test(
    sentinel_tools_group_t group, uint16_t x, uint16_t y);

#ifdef __cplusplus
}
#endif
