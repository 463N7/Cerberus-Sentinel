#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool checked;
    esp_err_t error;
    uint8_t generation;
    uint8_t exact;
    uint8_t review;
    uint8_t other;
    uint8_t compared;
    size_t saved;
} sentinel_baseline_t;

/* Reads one completed scan; never initiates scanning, connects, or changes trust.
 * A transport error or generation change leaves counts empty, not partly valid. */
esp_err_t sentinel_baseline_compare(sentinel_baseline_t *summary);
