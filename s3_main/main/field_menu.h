#pragma once
#include "ui_widgets.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool attack;
    bool bluetooth;
    uint8_t page;
    bool wifi_target_valid;
    bool wifi_deauth_available;
    bool wifi_probe_available;
    bool wifi_bad_message_available,wifi_assoc_sleep_available,wifi_channel_switch_available,wifi_quiet_available;
    bool wifi_ap_clone_available;
    bool wifi_sae_available;
    bool wifi_portal_available;
    char wifi_target[33];
    char wifi_target_detail[48];
    bool ble_advertiser_available;
    bool ble_connectable_available;
    bool ble_gatt_available;
    bool ble_capture_selected;
    bool ble_capture_replayable; /* Raw AD from selected random-static device. */
    char ble_target[33];
} cerberus_field_menu_t;

typedef enum {
    CERBERUS_FIELD_NONE,
    CERBERUS_FIELD_REDRAW,
    CERBERUS_FIELD_HOME,
    CERBERUS_FIELD_RECON,
    CERBERUS_FIELD_WIFI,
    CERBERUS_FIELD_BLE,
    CERBERUS_FIELD_WARDRIVE,
    CERBERUS_FIELD_RADIO,
    CERBERUS_FIELD_SURVEY,
    CERBERUS_FIELD_NETWORK,
    CERBERUS_FIELD_GUIDE,
    CERBERUS_FIELD_RADIO_LAB,
    CERBERUS_FIELD_WIFI_TARGET,
    CERBERUS_FIELD_WIFI_SELECT,
    CERBERUS_FIELD_WIFI_DEAUTH,
    CERBERUS_FIELD_WIFI_PROBE,
    CERBERUS_FIELD_WIFI_BAD_MESSAGE,
    CERBERUS_FIELD_WIFI_ASSOC_SLEEP,
    CERBERUS_FIELD_WIFI_CHANNEL_SWITCH,
    CERBERUS_FIELD_WIFI_QUIET,
    CERBERUS_FIELD_WIFI_AP_CLONE,
    CERBERUS_FIELD_WIFI_SAE,
    CERBERUS_FIELD_WIFI_PORTAL,
    CERBERUS_FIELD_BLE_SOUR_APPLE,
    CERBERUS_FIELD_BLE_APPLE_JUICE,
    CERBERUS_FIELD_BLE_SWIFT_PAIR,
    CERBERUS_FIELD_BLE_SAMSUNG,
    CERBERUS_FIELD_BLE_GOOGLE,
    CERBERUS_FIELD_BLE_FLIPPER,
    CERBERUS_FIELD_BLE_ALL,
    CERBERUS_FIELD_BLE_SPOOF_AIRTAG,
    CERBERUS_FIELD_BLE_FIND_MY_SOUND,
    CERBERUS_FIELD_REMOTE_ID,
    CERBERUS_FIELD_MAC_MONITOR,
} cerberus_field_action_t;

/* UI-only navigation: no radio operations or automatic job starts. */
esp_err_t cerberus_field_menu_draw(const cerberus_field_menu_t *menu,
    const cerberus_ui_widget_theme_t *theme);
cerberus_field_action_t cerberus_field_menu_tap(cerberus_field_menu_t *menu,
    uint16_t x, uint16_t y);
