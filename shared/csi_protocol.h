#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Explicit ESP32-C5 forced LLTF, signed 8-bit imaginary/real pairs. Native
 * 12-bit mode is deliberately not inferred from a sample's length. */
#define CERBERUS_CSI_MIN_FIRMWARE 58U
#define CERBERUS_CSI_VERSION 1U
#define CERBERUS_CSI_BYTES 106U
#define CERBERUS_CSI_RING_CAPACITY 16U
#define CERBERUS_CSI_CHUNK_BYTES 20U
#define CERBERUS_CSI_LEASE_MS 5000U
#define CERBERUS_CSI_LEASE_MIN_MS 1000U
#define CERBERUS_CSI_LEASE_MAX_MS 10000U
#define CERBERUS_CSI_FORMAT_LLTF_I8 1U
#define CERBERUS_CSI_SAMPLE_ESTIMATE_VALID 1U
#define CERBERUS_CSI_SAMPLE_FIRST_INVALID 2U
#define CERBERUS_CSI_FLAG_OWNED 1U
#define CERBERUS_CSI_FLAG_RECEIVING 2U

enum {
    CERBERUS_CSI_IDLE = 0, CERBERUS_CSI_STARTING, CERBERUS_CSI_RUNNING,
    CERBERUS_CSI_STOPPING, CERBERUS_CSI_COMPLETE, CERBERUS_CSI_ERROR,
};
enum {
    CERBERUS_CSI_PAGE_STATUS = 0,
    CERBERUS_CSI_PAGE_CLOCK = 1,
    CERBERUS_CSI_PAGE_COUNTERS = 2,
    CERBERUS_CSI_PAGE_INFO = 3,
    CERBERUS_CSI_PAGE_TARGET = 4,
};

/* All integer fields use the existing little-endian packed SPI convention.
 * START retries for the same nonzero job must have identical parameters.
 * A new job is rejected while any previous owner is held. Epoch is private
 * C5 capture identity, not a timestamp; source_us is C5 callback uptime. */
typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint8_t bssid[6];
    uint8_t channel, version;
    uint16_t lease_ms;
} cerberus_csi_start_request_t;
typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint64_t epoch;
} cerberus_csi_binding_t;
typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint64_t epoch;
    uint8_t page;
} cerberus_csi_status_request_t;
typedef struct __attribute__((packed)) {
    uint64_t boot_id;
    uint32_t job_id;
    uint64_t epoch;
    uint16_t lease_ms;
} cerberus_csi_renew_request_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    int32_t error_code;
    uint8_t state, channel, flags, queued, version, reserved;
} cerberus_csi_status_t;
/* INFO is the only unbound query (all query identities zero). Its random
 * nonzero boot identity fences even an ambiguous START across a C5 reboot.
 * Per-boot caller job IDs increase without wrapping; no boot-time NVS write. */
typedef struct __attribute__((packed)) {
    uint64_t boot_id, source_now_us;
    uint8_t version, format, capacity, reserved[3];
} cerberus_csi_info_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t bssid[6], channel, version;
    uint16_t lease_ms;
    uint8_t reserved[4];
} cerberus_csi_target_payload_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch, source_now_us;
    uint32_t accepted;
    /* Queued records plus callbacks admitted before source_now_us that have
     * not yet published/rejected. Zero is a source-time advance watermark. */
    uint8_t version, pending;
} cerberus_csi_clock_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint32_t invalid, overflow, acked;
    uint8_t version, reserved;
} cerberus_csi_counters_t;

/* A pinned record stays byte-identical until its matching ACK. PEEK on an
 * empty owned ring succeeds with sequence/length/CRC zero. CRC32 covers all
 * 124 packed sample bytes. READ and ACK require full job+epoch+sequence;
 * duplicate ACK for the most recently consumed sequence is idempotent. */
typedef struct __attribute__((packed)) {
    uint64_t source_us;
    uint32_t sequence;
    int8_t rssi;
    uint8_t channel, secondary, phy, flags, format;
    int8_t data[CERBERUS_CSI_BYTES];
} cerberus_csi_sample_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint32_t sequence, crc32;
    uint16_t length;
    uint8_t queued, format, flags, reserved;
} cerberus_csi_peek_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    uint32_t sequence;
    uint8_t offset;
} cerberus_csi_read_request_t;
typedef struct __attribute__((packed)) {
    uint8_t offset, length;
    uint8_t data[CERBERUS_CSI_CHUNK_BYTES];
} cerberus_csi_chunk_t;
typedef struct __attribute__((packed)) {
    uint32_t job_id;
    uint64_t epoch;
    uint32_t sequence;
} cerberus_csi_ack_request_t;

static inline uint32_t cerberus_csi_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; ++i) {
        crc ^= bytes[i];
        for (unsigned bit = 0; bit < 8U; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

_Static_assert(sizeof(cerberus_csi_sample_t) == 124U, "CSI record layout");
_Static_assert(sizeof(cerberus_csi_start_request_t) <= 22U, "CSI START payload");
_Static_assert(sizeof(cerberus_csi_binding_t) <= 22U, "CSI binding payload");
_Static_assert(sizeof(cerberus_csi_status_request_t) <= 22U, "CSI status query");
_Static_assert(sizeof(cerberus_csi_renew_request_t) <= 22U, "CSI renew payload");
_Static_assert(sizeof(cerberus_csi_status_t) == 22U, "CSI status payload");
_Static_assert(sizeof(cerberus_csi_info_t) == 22U, "CSI boot info payload");
_Static_assert(sizeof(cerberus_csi_target_payload_t) == 22U, "CSI target payload");
_Static_assert(sizeof(cerberus_csi_clock_t) == 22U, "CSI source clock payload");
_Static_assert(sizeof(cerberus_csi_counters_t) == 22U, "CSI counters payload");
_Static_assert(sizeof(cerberus_csi_peek_t) == 22U, "CSI peek payload");
_Static_assert(sizeof(cerberus_csi_read_request_t) <= 22U, "CSI read request");
_Static_assert(sizeof(cerberus_csi_chunk_t) == 22U, "CSI chunk payload");
_Static_assert(sizeof(cerberus_csi_ack_request_t) <= 22U, "CSI ACK payload");
