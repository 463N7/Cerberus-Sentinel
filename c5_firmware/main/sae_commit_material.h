#pragma once

#include <stdint.h>
#include "esp_err.h"

/* Generate a fresh group-19 scalar and matching uncompressed P-256 point.
 * Scalar and X||Y are 32 and 64 big-endian bytes respectively, ready for
 * cerberus_wifi_sae_commit_frame(). Outputs must be separate buffers. On any
 * failure both outputs are zero. This never stores a persistent key. */
esp_err_t cerberus_c5_sae_material_generate(uint8_t scalar[32],
    uint8_t element_xy[64]);
