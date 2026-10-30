#pragma once

#include "cerberus_protocol.h"
#include <limits.h>

/* One 100 ms passive dwell. No frame bytes or source identities are retained. */
static inline void cerberus_wifi_channel_survey_metrics_update(
    cerberus_wifi_channel_survey_result_payload_t *result,
    uint32_t packets, int64_t rssi_sum, int8_t strongest, uint32_t updated_ms)
{
    if (!result) return;
    result->seen = 1U;
    result->total_packets = UINT32_MAX - result->total_packets < packets
        ? UINT32_MAX : result->total_packets + packets;
    result->last_packets = packets > UINT16_MAX ? UINT16_MAX : (uint16_t)packets;
    const uint32_t multiplier = 1000U / CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS;
    result->rate_per_sec = packets > UINT16_MAX / multiplier
        ? UINT16_MAX : (uint16_t)(packets * multiplier);
    result->average_rssi = packets ? (int8_t)(rssi_sum / (int64_t)packets) : -127;
    result->strongest_rssi = packets ? strongest : -127;
    if (result->sample_count < UINT16_MAX) ++result->sample_count;
    result->updated_ms = updated_ms;
}
