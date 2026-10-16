#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Call on the drawing task immediately before presenting the completed page.
 * Unknown measurements show --%; percentages above 100 are clamped. The small
 * bitmap is redrawn only after a status change or another top-of-screen draw. */
esp_err_t sentinel_battery_view_update(bool valid, uint8_t percent, uint16_t background);
void sentinel_battery_view_invalidate(void);

#ifdef __cplusplus
}
#endif
