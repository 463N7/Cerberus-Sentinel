#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* A coherent, plausible gauge reading, not proof a cell is physically fitted:
     * a charger may power the battery node even without a cell. False means
     * unavailable, not an empty battery; numeric readings are then zero.
     */
    bool valid;
    /* Gauge estimate, floored to whole percent and capped at 100. */
    uint8_t percent;
    uint16_t voltage_mv;
    uint16_t version;
    /* Monotonic milliseconds supplied to poll; -1 when there is no valid sample. */
    int64_t last_read_ms;
    /* Most recent attempted sample, including failures; -1 before the first poll. */
    int64_t last_attempt_ms;
    esp_err_t last_error;
} cerberus_battery_status_t;

/* UI task owns init/poll. Init clears the cache without accessing the shared bus. */
void cerberus_battery_init(void);

/* First call samples immediately; then every 5s when valid or 30s after failure.
 * Returns true when a scheduled attempt ran, including an unsuccessful attempt.
 * At most three register reads, each with a 10ms timeout. Call outside rendering.
 */
bool cerberus_battery_poll(int64_t now_ms);

/* Copies one complete cached snapshot under a short lock. Never accesses I2C.
 * Safe from other tasks; a NULL output is ignored. No charging state is inferred.
 */
void cerberus_battery_get_status(cerberus_battery_status_t *out);

#ifdef __cplusplus
}
#endif
