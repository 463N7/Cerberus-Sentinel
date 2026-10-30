#pragma once

#include <stddef.h>
#include <stdint.h>
#include "wifi_ap_job.h"

enum {
    CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES = 128,
    CERBERUS_WIFI_SAE_COMMIT_TOKEN_MAX = 126,
    CERBERUS_WIFI_SAE_COMMIT_MAX_BYTES = 256,
};

/* Encode a group-19 SAE authentication commit for a selected AP. Scalar and
 * element must be generated together by the caller with a checked P-256
 * implementation; this serializer checks bounds, not curve membership.
 * Returns zero on invalid input or insufficient output capacity. Input and
 * output buffers must not overlap. An absent token uses NULL, length zero. */
size_t cerberus_wifi_sae_commit_frame(const cerberus_wifi_ap_job_t *job,
    const uint8_t source[6], const uint8_t scalar[32],
    const uint8_t element_xy[64], const uint8_t *token,
    size_t token_length, uint8_t *out, size_t capacity);
