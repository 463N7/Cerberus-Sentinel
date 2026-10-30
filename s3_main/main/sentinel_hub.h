#pragma once

#include "ui_widgets.h"

typedef enum {
    SENTINEL_HUB_WIFI,
    SENTINEL_HUB_BLE,
    SENTINEL_HUB_RADIO,
} sentinel_hub_t;

/* The hub draws only the region between the existing header and footer. */
esp_err_t sentinel_hub_draw(sentinel_hub_t hub, const cerberus_ui_widget_theme_t *theme);
/* 0..5 = tool, 6 = Learn; Wi-Fi adds 7 = Survey, 8 = MultiSSID, 9 = PineScan.
 * -1 is outside a control, including the gaps between buttons. */
int sentinel_hub_hit_test(sentinel_hub_t hub, uint16_t x, uint16_t y);
