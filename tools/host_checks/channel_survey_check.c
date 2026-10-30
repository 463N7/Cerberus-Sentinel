#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_channel_survey_metrics.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_START == 81);
    assert(CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_RESULT == 84);
    assert(sizeof(cerberus_wifi_channel_survey_status_payload_t) == 22U);
    assert(sizeof(cerberus_wifi_channel_survey_result_payload_t) == 22U);
    cerberus_wifi_channel_survey_result_payload_t result = {
        .generation = 4, .index = 2, .channel = 11,
        .dwell_ms = CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS,
    };
    cerberus_wifi_channel_survey_metrics_update(&result, 0, 0, -127, 100);
    assert(result.seen == 1 && result.rate_per_sec == 0);
    assert(result.average_rssi == -127 && result.strongest_rssi == -127);
    assert(result.sample_count == 1 && result.updated_ms == 100);
    cerberus_wifi_channel_survey_metrics_update(&result, 17, -1020, -42, 1210);
    assert(result.last_packets == 17 && result.rate_per_sec == 170);
    assert(result.total_packets == 17 && result.average_rssi == -60);
    assert(result.strongest_rssi == -42 && result.sample_count == 2);
    assert(result.updated_ms == 1210 && result.channel == 11);
    result.total_packets = UINT32_MAX - 3U;
    result.sample_count = UINT16_MAX;
    cerberus_wifi_channel_survey_metrics_update(&result, 100000U,
        -6000000LL, -30, 2300);
    assert(result.total_packets == UINT32_MAX);
    assert(result.last_packets == UINT16_MAX && result.rate_per_sec == UINT16_MAX);
    assert(result.sample_count == UINT16_MAX && result.average_rssi == -60);
    puts("channel survey protocol and dwell metrics: pass");
    return 0;
}
