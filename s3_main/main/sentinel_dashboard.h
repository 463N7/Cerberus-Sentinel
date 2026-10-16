#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool c5_online;
    bool touch_online;
    bool privacy_masked;
    uint8_t wifi_count;
    uint8_t ble_count;
    uint8_t lessons_complete;
    uint8_t radio_state;
    bool wifi_observed;
    bool ble_observed;
    uint32_t uptime_seconds;
} sentinel_dashboard_status_t;

typedef enum {
    SENTINEL_DASHBOARD_NONE = 0,
    SENTINEL_DASHBOARD_WIFI,
    SENTINEL_DASHBOARD_BLE,
    SENTINEL_DASHBOARD_RADIO,
    SENTINEL_DASHBOARD_GUIDE,
    SENTINEL_DASHBOARD_WATCH,
    SENTINEL_DASHBOARD_DEMO = SENTINEL_DASHBOARD_WATCH, /* Legacy value. */
    SENTINEL_DASHBOARD_TOOLS,
} sentinel_dashboard_action_t;

/* These draw functions share one bounded stripe buffer. Call from the UI task. */
esp_err_t sentinel_dashboard_draw(const sentinel_dashboard_status_t *status, uint32_t frame);
/* Updates the eyes, chest light, status and observation counts only. */
esp_err_t sentinel_dashboard_animate(const sentinel_dashboard_status_t *status, uint32_t frame);
sentinel_dashboard_action_t sentinel_dashboard_hit_test(uint16_t x, uint16_t y);

/* Draw the original guardian in the dashboard's fixed x=12..307, y=64..211 area. */
esp_err_t sentinel_guardian_draw(uint32_t frame, bool c5_online);

#ifdef __cplusplus
}
#endif
