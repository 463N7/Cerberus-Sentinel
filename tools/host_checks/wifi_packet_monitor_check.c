#undef NDEBUG
#include <assert.h>
#include <limits.h>
#include <string.h>
#include "wifi_packet_monitor_core.h"

static void management_frame(uint8_t frame[24], uint8_t subtype)
{
    memset(frame, 0, 24U);
    frame[0] = (uint8_t)(subtype << 4U);
}

int main(void)
{
    assert(cerberus_wifi_packet_monitor_channel_valid(1U));
    assert(cerberus_wifi_packet_monitor_channel_valid(14U));
    assert(cerberus_wifi_packet_monitor_channel_valid(36U));
    assert(cerberus_wifi_packet_monitor_channel_valid(165U));
    assert(!cerberus_wifi_packet_monitor_channel_valid(0U));
    assert(!cerberus_wifi_packet_monitor_channel_valid(37U));
    assert(!cerberus_wifi_packet_monitor_channel_valid(166U));

    cerberus_wifi_packet_monitor_counters_t counters = {0};
    uint8_t frame[24];
    management_frame(frame, 8U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, 23U);
    assert(counters.beacons == 0U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    management_frame(frame, 4U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    management_frame(frame, 10U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    management_frame(frame, 12U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    assert(counters.beacons == 1U && counters.probe_requests == 1U &&
        counters.disconnections == 2U);
    frame[0] = 0x88U; /* Data subtype 8 must not count as a beacon. */
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    frame[0] = 0x81U; /* Nonzero 802.11 version. */
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    assert(counters.beacons == 1U);
    counters.beacons = UINT16_MAX;
    management_frame(frame, 8U);
    cerberus_wifi_packet_monitor_observe(&counters, frame, sizeof(frame));
    assert(counters.beacons == UINT16_MAX);

    cerberus_wifi_packet_monitor_ring_t ring = {0};
    cerberus_wifi_packet_monitor_sample_payload_t sample = {0};
    assert(!cerberus_wifi_packet_monitor_ring_get(&ring, 0U, &sample));
    for (uint16_t index = 1U; index <= 70U; ++index) {
        counters.beacons = index;
        cerberus_wifi_packet_monitor_ring_push(&ring, 7U, 36U,
            &counters, (uint32_t)index * 200U);
    }
    assert(ring.stored_count == CERBERUS_WIFI_PACKET_MONITOR_SAMPLES);
    assert(ring.latest_sequence == 70U);
    assert(!cerberus_wifi_packet_monitor_ring_get(&ring, 6U, &sample));
    assert(cerberus_wifi_packet_monitor_ring_get(&ring, 7U, &sample));
    assert(sample.generation == 7U && sample.channel == 36U &&
        sample.sequence == 7U && sample.beacons == 7U &&
        sample.updated_ms == 1400U);
    ring.latest_sequence = UINT16_MAX;
    counters.beacons = 99U;
    cerberus_wifi_packet_monitor_ring_push(&ring, 7U, 36U,
        &counters, 15000U);
    assert(ring.latest_sequence == 0U);
    assert(cerberus_wifi_packet_monitor_ring_get(&ring, 0U, &sample));
    assert(sample.beacons == 99U);
    return 0;
}
