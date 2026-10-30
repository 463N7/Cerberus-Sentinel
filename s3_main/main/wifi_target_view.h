#pragma once
#include "ui_widgets.h"
#include <stdbool.h>

typedef struct {
    const char *ssid;
    const char *identifier;
    const char *metadata;
    const char *message;
    const char *station_label;
    bool selected;
    bool connection_matches;
    bool connection_active;
    bool can_connect;
    bool can_scan_ports;
    bool can_scan_stations;
    bool can_capture;
    bool can_packet_monitor;
    bool can_csi;
} cerberus_wifi_target_view_t;

typedef enum {
    CERBERUS_WIFI_TARGET_NONE,
    CERBERUS_WIFI_TARGET_ATTACK,
    CERBERUS_WIFI_TARGET_CONNECT,
    CERBERUS_WIFI_TARGET_PORTS,
    CERBERUS_WIFI_TARGET_DETAILS,
    CERBERUS_WIFI_TARGET_CHANGE,
    CERBERUS_WIFI_TARGET_STATIONS,
    CERBERUS_WIFI_TARGET_CAPTURE,
    CERBERUS_WIFI_TARGET_PACKET_MONITOR,
    CERBERUS_WIFI_TARGET_CSI,
} cerberus_wifi_target_action_t;

esp_err_t cerberus_wifi_target_draw(const cerberus_wifi_target_view_t *view,
    const cerberus_ui_widget_theme_t *theme);
cerberus_wifi_target_action_t cerberus_wifi_target_tap(
    const cerberus_wifi_target_view_t *view, uint16_t x, uint16_t y);
