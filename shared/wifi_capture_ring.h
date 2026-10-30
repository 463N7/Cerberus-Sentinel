#pragma once

#include "wifi_pcap.h"
#include <stdatomic.h>

/* One Wi-Fi RX callback produces; one SPI/service task consumes. The owner
 * allocates this object only for a capture session, calls init once, and
 * starts a strictly increasing generation only after stop, detaching the RX
 * callback, producer quiescence and ACK/drain. A generation cannot wrap.
 * Peeked frame memory stays stable until its exact generation/sequence ACK. */
enum { CERBERUS_WIFI_CAPTURE_RING_SLOTS = 2U };

typedef struct {
    uint32_t sequence;
    uint32_t generation;
    uint16_t captured_length,original_length;
    cerberus_wifi_pcap_metadata_t metadata;
    uint8_t frame[CERBERUS_WIFI_PCAP_FRAME_MAX];
} cerberus_wifi_capture_record_t;

typedef enum {
    CERBERUS_WIFI_CAPTURE_ACCEPTED = 0,
    CERBERUS_WIFI_CAPTURE_DROP_STOPPED,
    CERBERUS_WIFI_CAPTURE_DROP_STALE,
    CERBERUS_WIFI_CAPTURE_DROP_FULL,
    CERBERUS_WIFI_CAPTURE_DROP_OVERSIZE,
    CERBERUS_WIFI_CAPTURE_DROP_TRUNCATED,
    CERBERUS_WIFI_CAPTURE_DROP_INVALID,
} cerberus_wifi_capture_push_result_t;

typedef struct {
    uint32_t generation,accepted,dropped_stopped,dropped_stale,dropped_full;
    uint32_t dropped_oversize,dropped_truncated,dropped_invalid;
    uint32_t producer_inflight;
    uint8_t pending,high_watermark;
    bool active;
} cerberus_wifi_capture_ring_stats_t;

typedef struct {
    cerberus_wifi_capture_record_t slots[CERBERUS_WIFI_CAPTURE_RING_SLOTS];
    atomic_uint head,tail,generation,next_sequence,producer_inflight;
    atomic_uint accepted,dropped_stopped,dropped_stale,dropped_full;
    atomic_uint dropped_oversize,dropped_truncated,dropped_invalid,high_watermark;
    atomic_bool active;
} cerberus_wifi_capture_ring_t;

void cerberus_wifi_capture_ring_init(cerberus_wifi_capture_ring_t *ring);
/* Main-task control only. A generation may never be reused or decreased. */
bool cerberus_wifi_capture_ring_start(cerberus_wifi_capture_ring_t *ring,uint32_t generation);
void cerberus_wifi_capture_ring_stop(cerberus_wifi_capture_ring_t *ring);
/* Nonblocking producer API. `original_length` includes any FCS included in
 * captured bytes, but excludes synthetic radiotap. Arbitrary truncation of
 * monotonic records is rejected because PCAPNG Simple Packet Blocks cannot
 * encode it; a capture at the 4096-byte snaplen can be represented. */
cerberus_wifi_capture_push_result_t cerberus_wifi_capture_ring_push(
    cerberus_wifi_capture_ring_t *ring,uint32_t generation,
    const cerberus_wifi_pcap_metadata_t *metadata,const uint8_t *frame,
    size_t captured_length,size_t original_length);
/* Consumer APIs: do not retain the pointer after ACK. */
const cerberus_wifi_capture_record_t *cerberus_wifi_capture_ring_peek(
    const cerberus_wifi_capture_ring_t *ring);
bool cerberus_wifi_capture_ring_ack(cerberus_wifi_capture_ring_t *ring,
    uint32_t generation,uint32_t sequence);
void cerberus_wifi_capture_ring_stats(const cerberus_wifi_capture_ring_t *ring,
    cerberus_wifi_capture_ring_stats_t *out);
