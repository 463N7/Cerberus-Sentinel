#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SENTINEL_GUIDE_LESSON_COUNT 6U

typedef enum {
    SENTINEL_GUIDE_NONE = 0,
    SENTINEL_GUIDE_REDRAW,
    SENTINEL_GUIDE_HOME,
    SENTINEL_GUIDE_OPEN_WIFI,
    SENTINEL_GUIDE_OPEN_WIFI_CHANNELS,
    SENTINEL_GUIDE_OPEN_WIFI_BASELINE,
    SENTINEL_GUIDE_OPEN_BLE,
    SENTINEL_GUIDE_OPEN_BLE_SIGNAL,
    SENTINEL_GUIDE_OPEN_RADIO,
    SENTINEL_GUIDE_OPEN_RADIO_ACTIVITY,
} sentinel_guide_action_t;

/* NVS must already be initialized. Progress errors are recoverable: the guide
 * remains usable, displays the failure, and can retry saving from a lesson. */
esp_err_t sentinel_guide_init(void);
/* Erases learning badges only after the replacement record is committed. */
esp_err_t sentinel_guide_reset_progress(void);
void sentinel_guide_open(void);
void sentinel_guide_open_lesson(uint8_t lesson);
void sentinel_guide_open_demo(void);
esp_err_t sentinel_guide_draw(void);
sentinel_guide_action_t sentinel_guide_handle_touch(uint16_t x, uint16_t y);
uint8_t sentinel_guide_completed_count(void);

/* Call while the guide/demo page is visible. A true result requests a redraw.
 * Open-tool actions retain lesson state; draw again to resume after returning. */
bool sentinel_guide_tick(uint32_t now_ms);

#ifdef __cplusplus
}
#endif
