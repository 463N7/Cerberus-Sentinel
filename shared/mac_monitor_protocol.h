#pragma once

/* Original passive MAC observation contract. Counts are admitted receive
 * callbacks, not unique packets, physical devices, associations or identities. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_MAC_VERSION 1U
#define CERBERUS_MAC_MIN_FIRMWARE 59U
#define CERBERUS_MAC_CAPACITY 32U
#define CERBERUS_MAC_CHUNK_BYTES 20U
#define CERBERUS_MAC_LEASE_MS 5000U
#define CERBERUS_MAC_LEASE_MIN_MS 1000U
#define CERBERUS_MAC_LEASE_MAX_MS 30000U
#define CERBERUS_MAC_DWELL_MS 250U
#define CERBERUS_MAC_DWELL_MIN_MS 50U
#define CERBERUS_MAC_DWELL_MAX_MS 1000U

enum { CERBERUS_MAC_WIFI = 0U, CERBERUS_MAC_BLE = 1U };
enum { CERBERUS_MAC_IDLE, CERBERUS_MAC_STARTING, CERBERUS_MAC_RUNNING,
    CERBERUS_MAC_STOPPING, CERBERUS_MAC_COMPLETE, CERBERUS_MAC_ERROR };
enum { CERBERUS_MAC_OWNED = 1U, CERBERUS_MAC_RECEIVING = 2U };
enum { CERBERUS_MAC_RSSI_VALID = 1U };
enum { CERBERUS_MAC_WIFI_MGMT = 1U, CERBERUS_MAC_WIFI_CONTROL = 2U,
    CERBERUS_MAC_WIFI_DATA = 4U };
enum { CERBERUS_MAC_BLE_ORIGINAL = 1U, CERBERUS_MAC_BLE_RESPONSE = 2U,
    CERBERUS_MAC_BLE_PARTIAL = 4U };
enum { CERBERUS_MAC_PAGE_CORE, CERBERUS_MAC_PAGE_INFO,
    CERBERUS_MAC_PAGE_COUNTERS, CERBERUS_MAC_PAGE_PROFILE };

/* Latest RSSI/time describe the latest admitted report, including a response
 * or partial BLE report. They are never labeled as original-ADV evidence.
 * responses is a subset of observations; Wi-Fi always sets it to zero.
 * Incarnations never repeat within one full-width producer epoch. */
typedef struct __attribute__((packed)) {
    uint64_t first_us, last_us;
    uint32_t incarnation, observations, responses;
    uint8_t address[6];
    int8_t rssi;
    uint8_t radio, address_type, channel, flags, type_mask;
} cerberus_mac_row_t;

typedef struct __attribute__((packed)) {
    uint32_t snapshot_id;
    uint64_t source_now_us;
    uint32_t observations, invalid, replacements;
    uint8_t count, version, radio, reserved;
} cerberus_mac_snapshot_header_t;

#define CERBERUS_MAC_SNAPSHOT_MAX_BYTES \
    (sizeof(cerberus_mac_snapshot_header_t) + CERBERUS_MAC_CAPACITY * sizeof(cerberus_mac_row_t))
typedef struct __attribute__((packed)) {
    cerberus_mac_snapshot_header_t header;
    cerberus_mac_row_t rows[CERBERUS_MAC_CAPACITY];
} cerberus_mac_snapshot_t;

typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint16_t lease_ms, dwell_ms;
    uint8_t version, reserved[5];
} cerberus_mac_start_request_t;
typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint64_t epoch;
} cerberus_mac_binding_t;
typedef struct __attribute__((packed)) {
    cerberus_mac_binding_t binding;
    uint8_t page;
} cerberus_mac_status_request_t;
typedef struct __attribute__((packed)) {
    cerberus_mac_binding_t binding;
    uint16_t lease_ms;
} cerberus_mac_renew_request_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    int32_t error_code;
    uint8_t state, channel, flags, version;
    uint16_t reserved;
} cerberus_mac_status_t;
typedef struct __attribute__((packed)) {
    uint64_t boot_id, source_now_us;
    uint8_t capacity, row_bytes, version, reserved[3];
} cerberus_mac_info_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint32_t observations, invalid, replacements;
    uint8_t version, reserved;
} cerberus_mac_counters_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint16_t lease_ms, dwell_ms;
    uint8_t channel_count, version, reserved[8];
} cerberus_mac_profile_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint32_t snapshot_id, crc32;
    uint16_t length;
    uint8_t count, version, reserved[2];
} cerberus_mac_peek_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    uint32_t snapshot_id;
    uint16_t offset;
} cerberus_mac_read_request_t;
typedef struct __attribute__((packed)) {
    uint16_t offset;
    uint8_t data[CERBERUS_MAC_CHUNK_BYTES];
} cerberus_mac_chunk_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    uint32_t snapshot_id;
} cerberus_mac_ack_request_t;

static inline uint32_t cerberus_mac_crc32(const void *bytes, size_t length)
{
    const uint8_t *p = (const uint8_t *)bytes;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= p[i];
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1U) ^ (UINT32_C(0xEDB88320) & (0U - (crc & 1U)));
    }
    return ~crc;
}

_Static_assert(sizeof(cerberus_mac_row_t) == 40U, "MAC row layout");
_Static_assert(sizeof(cerberus_mac_snapshot_header_t) == 28U, "MAC snapshot metadata");
_Static_assert(sizeof(cerberus_mac_snapshot_t) == 1308U, "MAC snapshot capacity");
_Static_assert(sizeof(cerberus_mac_start_request_t) == 22U, "MAC START payload");
_Static_assert(sizeof(cerberus_mac_binding_t) == 20U, "MAC binding");
_Static_assert(sizeof(cerberus_mac_status_request_t) == 21U, "MAC status query");
_Static_assert(sizeof(cerberus_mac_renew_request_t) == 22U, "MAC renew query");
_Static_assert(sizeof(cerberus_mac_status_t) == 22U, "MAC core status");
_Static_assert(sizeof(cerberus_mac_info_t) == 22U, "MAC info page");
_Static_assert(sizeof(cerberus_mac_counters_t) == 22U, "MAC counters page");
_Static_assert(sizeof(cerberus_mac_profile_t) == 22U, "MAC profile page");
_Static_assert(sizeof(cerberus_mac_peek_t) == 22U, "MAC pinned snapshot descriptor");
_Static_assert(sizeof(cerberus_mac_read_request_t) == 18U, "MAC chunk request");
_Static_assert(sizeof(cerberus_mac_chunk_t) == 22U, "MAC chunk payload");
_Static_assert(sizeof(cerberus_mac_ack_request_t) == 16U, "MAC snapshot acknowledgement");
