#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Portable, allocation-free PCAPNG serialization for raw 802.11 observations.
 * The caller owns capture scheduling, time-source verification, storage and
 * truncation/drop accounting. No function here accesses a radio or a file. */
enum {
    CERBERUS_WIFI_PCAP_FRAME_MAX = 4096U,
    CERBERUS_WIFI_PCAP_RADIOTAP_SIZE = 16U,
    CERBERUS_WIFI_PCAP_SNAPLEN = CERBERUS_WIFI_PCAP_FRAME_MAX + CERBERUS_WIFI_PCAP_RADIOTAP_SIZE,
    CERBERUS_WIFI_PCAP_FILE_HEADER_SIZE = 68U,
    CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE = 32U + CERBERUS_WIFI_PCAP_SNAPLEN,
};

typedef enum {
    CERBERUS_WIFI_PCAP_TIME_MONOTONIC = 1, /* no trustworthy UTC; timestamp omitted from PCAPNG */
    CERBERUS_WIFI_PCAP_TIME_UTC = 2,       /* verified Unix microseconds */
} cerberus_wifi_pcap_time_kind_t;

typedef enum {
    CERBERUS_WIFI_PCAP_RADIO_C5 = 0,
    CERBERUS_WIFI_PCAP_RADIO_S3 = 1,
    CERBERUS_WIFI_PCAP_RADIO_R1 = 2,
    CERBERUS_WIFI_PCAP_RADIO_R2 = 3,
    CERBERUS_WIFI_PCAP_RADIO_R3 = 4,
    CERBERUS_WIFI_PCAP_RADIO_R4 = 5,
    CERBERUS_WIFI_PCAP_RADIO_R5 = 6,
    CERBERUS_WIFI_PCAP_RADIO_R6 = 7,
} cerberus_wifi_pcap_radio_t;

typedef struct {
    uint8_t channel;
    int8_t rssi_dbm;
    bool rssi_known;
    bool frame_has_fcs;
    cerberus_wifi_pcap_time_kind_t time_kind;
    uint64_t capture_time_us;
} cerberus_wifi_pcap_metadata_t;

/* Section Header Block + one named Interface Description Block, LinkType 127
 * (IEEE 802.11 plus radiotap). One file belongs to exactly one radio and
 * is written once at creation. R1..R6 are reserved for optional expansions. */
size_t cerberus_wifi_pcapng_file_header(cerberus_wifi_pcap_radio_t radio,
    uint8_t *out, size_t capacity);

/* Returns a complete Enhanced Packet Block for trusted UTC or a Simple Packet
 * Block without timestamp for monotonic-only time. `original_length` is the
 * 802.11 length before any capture truncation, excluding synthetic radiotap.
 * For untimed SPB, truncation is representable only when captured_length
 * reaches FRAME_MAX; otherwise this rejects the record rather than lying.
 * Returns zero without writing on invalid metadata or insufficient capacity.
 * The caller should retain capture_time_us in its own session index when the
 * time kind is MONOTONIC, because standard SPB has no timestamp field. */
size_t cerberus_wifi_pcapng_record(const cerberus_wifi_pcap_metadata_t *metadata,
    const uint8_t *frame, size_t captured_length, size_t original_length,
    uint8_t *out, size_t capacity);
