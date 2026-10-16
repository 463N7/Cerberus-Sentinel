#include "sentinel_hub.h"
#include "display.h"
#include "esp_check.h"

#include <stddef.h>

typedef struct {
    const char *title;
    const char *description;
} hub_item_t;

static const hub_item_t wifi_items[] = {
    {"Nearby networks", "PASSIVE 2.4 + 5 GHZ DISCOVERY"},
    {"Survey summary", "COUNTS, SECURITY AND SCAN HEALTH"},
    {"Channel view", "COMPARE OBSERVED NETWORKS"},
    {"Trusted baseline", "REVIEW YOUR SAVED NETWORKS"},
    {"Activity review", "BOUNDED DEFENSIVE OBSERVATIONS"},
    {"My connection", "CONNECT, INSPECT AND RUN CHECKS"},
};
static const hub_item_t ble_items[] = {
    {"Nearby devices", "PASSIVE ADVERTISEMENT DISCOVERY"},
    {"Device + signal", "INSPECT A DEVICE AND ITS HISTORY"},
    {"Services + values", "USER-INITIATED GATT CONNECTION"},
    {"Sensor viewer", "TEMPERATURE, HUMIDITY, PRESSURE"},
};
static const hub_item_t radio_items[] = {
    {"Channel energy", "EXPLORE THE 2.4 GHZ AIRSPACE"},
    {"Zigbee beacons", "OBSERVED CLASSIC ZIGBEE BEACONS"},
    {"Frame activity", "PASSIVE 802.15.4 FRAME COUNTS"},
    {"Compare snapshots", "ENERGY + ACTIVITY EXPLAINED"},
};

static const hub_item_t *items_for(sentinel_hub_t hub, size_t *count)
{
    if (hub == SENTINEL_HUB_WIFI) {
        *count = sizeof(wifi_items) / sizeof(wifi_items[0]);
        return wifi_items;
    }
    if (hub == SENTINEL_HUB_BLE) {
        *count = sizeof(ble_items) / sizeof(ble_items[0]);
        return ble_items;
    }
    *count = sizeof(radio_items) / sizeof(radio_items[0]);
    return radio_items;
}

static uint16_t item_y(sentinel_hub_t hub, size_t index)
{
    return (uint16_t)(70U + index * (hub == SENTINEL_HUB_WIFI ? 48U : 66U));
}

esp_err_t sentinel_hub_draw(sentinel_hub_t hub, const cerberus_ui_widget_theme_t *theme)
{
    if (theme == NULL || hub > SENTINEL_HUB_RADIO) return ESP_ERR_INVALID_ARG;
    size_t count;
    const hub_item_t *items = items_for(hub, &count);
    uint16_t accent = hub == SENTINEL_HUB_WIFI ? 0x2EFD :
        (hub == SENTINEL_HUB_BLE ? 0xBBBF : 0xFDAB);
    const char *subtitle = hub == SENTINEL_HUB_WIFI ? "WATCH / UNDERSTAND YOUR NETWORK" :
        (hub == SENTINEL_HUB_BLE ? "SCOUT / FOLLOW THE ADVERTISEMENTS" :
                                 "SENSE / EXPLORE SHARED AIRSPACE");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 52, subtitle,
        theme->muted, theme->background, 1), "hub", "subtitle");
    for (size_t i = 0; i < count; ++i) {
        uint16_t y = item_y(hub, i);
        uint16_t height = hub == SENTINEL_HUB_WIFI ? 42U : 54U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(12, y, 296, height,
            theme->panel), "hub", "panel");
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(12, y, 3, height,
            accent), "hub", "accent");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(24, y + 6, items[i].title,
            theme->text, theme->panel, 2), "hub", "title");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(24, y + 28, items[i].description,
            theme->muted, theme->panel, 1), "hub", "description");
    }
    if (hub != SENTINEL_HUB_WIFI) {
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(24, 335,
            hub == SENTINEL_HUB_BLE ? "RSSI SHOWS A TREND, NOT EXACT DISTANCE" :
                                     "ENERGY ALONE DOES NOT IDENTIFY A DEVICE",
            theme->muted, theme->background, 1), "hub", "note");
    }
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){12, 364, 296, 42},
        "LEARN WITH CERBERUS", 2, true, theme);
}

int sentinel_hub_hit_test(sentinel_hub_t hub, uint16_t x, uint16_t y)
{
    if (hub > SENTINEL_HUB_RADIO || x < 12 || x >= 308) return -1;
    if (y >= 364 && y < 406) return 6;
    size_t count;
    (void)items_for(hub, &count);
    for (size_t i = 0; i < count; ++i) {
        uint16_t top = item_y(hub, i);
        uint16_t height = hub == SENTINEL_HUB_WIFI ? 42U : 54U;
        if (y >= top && y < top + height) return (int)i;
    }
    return -1;
}
