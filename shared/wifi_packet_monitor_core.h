#pragma once

#include "cerberus_protocol.h"

typedef struct {
    uint16_t beacons;
    uint16_t disconnections;
    uint16_t probe_requests;
} cerberus_wifi_packet_monitor_counters_t;

typedef struct {
    cerberus_wifi_packet_monitor_sample_payload_t
        samples[CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
    uint16_t latest_sequence;
    uint8_t stored_count;
} cerberus_wifi_packet_monitor_ring_t;

static inline bool cerberus_wifi_packet_monitor_channel_valid(uint8_t channel)
{
    if (channel >= 1U && channel <= 14U) return true;
    static const uint8_t five_ghz[] = {
        36U, 40U, 44U, 48U, 52U, 56U, 60U, 64U, 100U, 104U, 108U,
        112U, 116U, 120U, 124U, 128U, 132U, 136U, 140U, 144U,
        149U, 153U, 157U, 161U, 165U,
    };
    for (size_t index = 0U; index < sizeof(five_ghz); ++index)
        if (five_ghz[index] == channel) return true;
    return false;
}

static inline void cerberus_wifi_packet_monitor_observe(
    cerberus_wifi_packet_monitor_counters_t *counters,
    const uint8_t *frame, size_t length)
{
    /* A management header is 24 bytes. The caller removes the four-byte FCS
     * and checks RX success before passing a frame here. */
    if (!counters || !frame || length < 24U) return;
    uint16_t control = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8U);
    if ((control & 0x000FU) != 0U) return; /* version 0, management type */
    uint8_t subtype = (uint8_t)((control >> 4U) & 0x0FU);
    uint16_t *counter = subtype == 8U ? &counters->beacons :
        (subtype == 10U || subtype == 12U) ? &counters->disconnections :
        subtype == 4U ? &counters->probe_requests : NULL;
    if (counter && *counter != UINT16_MAX) ++*counter;
}

static inline void cerberus_wifi_packet_monitor_ring_push(
    cerberus_wifi_packet_monitor_ring_t *ring, uint8_t generation,
    uint8_t channel, const cerberus_wifi_packet_monitor_counters_t *counters,
    uint32_t updated_ms)
{
    if (!ring || !counters) return;
    uint16_t sequence = (uint16_t)(ring->latest_sequence + 1U);
    cerberus_wifi_packet_monitor_sample_payload_t *sample =
        &ring->samples[sequence % CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
    *sample = (cerberus_wifi_packet_monitor_sample_payload_t){
        .generation = generation,
        .channel = channel,
        .sequence = sequence,
        .beacons = counters->beacons,
        .disconnections = counters->disconnections,
        .probe_requests = counters->probe_requests,
        .updated_ms = updated_ms,
    };
    ring->latest_sequence = sequence;
    if (ring->stored_count < CERBERUS_WIFI_PACKET_MONITOR_SAMPLES)
        ++ring->stored_count;
}

static inline bool cerberus_wifi_packet_monitor_ring_get(
    const cerberus_wifi_packet_monitor_ring_t *ring, uint16_t sequence,
    cerberus_wifi_packet_monitor_sample_payload_t *sample)
{
    if (!ring || !sample || ring->stored_count == 0U ||
        (uint16_t)(ring->latest_sequence - sequence) >= ring->stored_count)
        return false;
    *sample = ring->samples[sequence % CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
    return sample->sequence == sequence;
}
