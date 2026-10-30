#pragma once

#include "cerberus_protocol.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_S3_WIFI_RECENT_WINDOW_MS 30000U

/* Presence of a matching BSSID/SSID/channel frame, not the age of the driver's
 * aggregate RSSI/security result. C5 and S3 uptime origins are not compared. */
typedef struct {
    uint64_t request_started_us;
    uint32_t source_age_ms;
    bool valid;
} cerberus_s3_wifi_observation_t;

/* A missing/legacy observation is valid metadata with unavailable timing.
 * Invalid extensions fail closed. Publish this only with the complete SSID. */
static inline bool cerberus_s3_wifi_observation_decode(
    const cerberus_wifi_scan_result_payload_t *metadata, uint32_t firmware,
    uint64_t request_started_us, cerberus_s3_wifi_observation_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!metadata) return false;
    if (firmware < CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE) return true;
    uint8_t flags = metadata->observation_flags;
    if (!flags)
        return !metadata->observation_age_ms && !metadata->observation_source_ms &&
            !metadata->observation_channel;
    if ((flags != (CERBERUS_WIFI_OBSERVATION_VALID | CERBERUS_WIFI_OBSERVATION_BEACON) &&
         flags != (CERBERUS_WIFI_OBSERVATION_VALID | CERBERUS_WIFI_OBSERVATION_PROBE_RESPONSE)) ||
        !metadata->channel || metadata->observation_channel != metadata->channel)
        return false;
    out->request_started_us = request_started_us;
    out->source_age_ms = metadata->observation_age_ms;
    out->valid = true;
    return true;
}

/* C5 rounds its full-width callback age upward when producing the response.
 * Adding elapsed S3 time since request START conservatively includes all bus
 * delay. No clock synchronization, signed timer wrap or publication refresh. */
static inline bool cerberus_s3_wifi_observation_recent(
    const cerberus_s3_wifi_observation_t *observation, uint64_t now_us)
{
    if (!observation || !observation->valid ||
        now_us < observation->request_started_us ||
        observation->source_age_ms > CERBERUS_S3_WIFI_RECENT_WINDOW_MS) return false;
    uint64_t elapsed_us = now_us - observation->request_started_us;
    uint64_t remaining_ms = CERBERUS_S3_WIFI_RECENT_WINDOW_MS - observation->source_age_ms;
    return elapsed_us <= remaining_ms * 1000U;
}

/* Stable original result indexes; filtering never moves or modifies RF rows. */
static inline size_t cerberus_s3_wifi_observation_indexes(
    const cerberus_s3_wifi_observation_t *observations, size_t count,
    bool recent_only, uint64_t now_us, uint8_t *indexes, size_t capacity)
{
    if (count > CERBERUS_WIFI_SCAN_MAX_RESULTS || (recent_only && !observations)) return 0U;
    size_t matches = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (recent_only && !cerberus_s3_wifi_observation_recent(&observations[i], now_us)) continue;
        if (indexes && matches < capacity) indexes[matches] = (uint8_t)i;
        ++matches;
    }
    return matches;
}
