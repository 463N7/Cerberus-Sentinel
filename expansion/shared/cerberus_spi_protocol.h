#pragma once

#include "cerberus_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_SPI_MAGIC 0x43535049u
#define CERBERUS_SPI_CONFIG_GUARD 0x43464731u
#define CERBERUS_SPI_REQUEST_MAGIC 0x31514552u
#define CERBERUS_SPI_REQUEST_GUARD 0xA55Au
#define CERBERUS_SPI_TRANSFER_SIZE 64
#define CERBERUS_SPI_REQUEST_SIZE 8
#define CERBERUS_SPI_REQUEST_OFFSET (CERBERUS_SPI_TRANSFER_SIZE - CERBERUS_SPI_REQUEST_SIZE)
#define CERBERUS_SPI_RESPONSE_SIZE CERBERUS_SPI_REQUEST_OFFSET

#define CERBERUS_SPI_REQUEST_GUARD_VALUE(command, sequence) \
    ((uint16_t)(CERBERUS_SPI_REQUEST_GUARD ^ ((uint16_t)(command) << 8) ^ (uint16_t)(sequence)))

typedef enum {
    CERBERUS_SPI_CMD_NOP = 0,
    CERBERUS_SPI_CMD_STATUS = 1,
    CERBERUS_SPI_CMD_CONFIG = 2,
    CERBERUS_SPI_CMD_READ_RECORDS = 4, /* Command 3 was the retired single-record read. */
    CERBERUS_SPI_CMD_CONFIG_ARM = 5,
} cerberus_spi_command_t;

typedef enum {
    CERBERUS_SPI_RESP_EMPTY = 0,
    CERBERUS_SPI_RESP_STATUS = 1,
    CERBERUS_SPI_RESP_RECORD_BATCH = 3, /* Response 2 was the retired single-record reply. */
    CERBERUS_SPI_RESP_ERROR = 255,
} cerberus_spi_response_t;

#define CERBERUS_SPI_BATCH_HEADER_SIZE 12
#define CERBERUS_SPI_BATCH_PAYLOAD_SIZE (CERBERUS_SPI_RESPONSE_SIZE - CERBERUS_SPI_BATCH_HEADER_SIZE)
#define CERBERUS_SPI_COMPACT_RECORD_BASE_SIZE 14

typedef struct __attribute__((packed)) {
    uint32_t request_magic;
    uint8_t command;
    uint8_t sequence;
    uint16_t request_guard;
} cerberus_spi_request_footer_t;

typedef struct __attribute__((packed)) {
    uint8_t reserved[CERBERUS_SPI_REQUEST_OFFSET];
    cerberus_spi_request_footer_t footer;
} cerberus_spi_request_t;

typedef struct __attribute__((packed)) {
    cerberus_scan_config_t config;
    uint32_t config_guard;
    uint8_t reserved[CERBERUS_SPI_REQUEST_OFFSET - sizeof(cerberus_scan_config_t) - sizeof(uint32_t)];
    cerberus_spi_request_footer_t footer;
} cerberus_spi_config_request_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t response;
    uint8_t node_id;
    uint8_t protocol_version;
    uint8_t scan_enabled;
    uint8_t sequence;
    uint8_t queued_records;
    uint8_t scan_phase;
    uint8_t scan_channel;
    uint32_t firmware_version;
    uint32_t records_sent;
    uint32_t scan_cycles;
    uint32_t records_dropped;
    uint8_t queue_high_water;
    uint8_t reserved[CERBERUS_SPI_TRANSFER_SIZE - 29];
} cerberus_spi_status_response_t;

typedef struct __attribute__((packed)) {
    uint8_t node_id;
    uint32_t sequence;
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    uint8_t auth_mode;
    char ssid[CERBERUS_MAX_SSID_LEN + 1];
} cerberus_spi_scan_record_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t response;
    uint8_t node_id;
    uint8_t protocol_version;
    uint8_t sequence;
    uint8_t queued_records;
    uint8_t record_count;
    uint8_t payload_len;
    uint8_t reserved0;
    uint8_t payload[CERBERUS_SPI_BATCH_PAYLOAD_SIZE];
    uint8_t reserved1[CERBERUS_SPI_REQUEST_SIZE];
} cerberus_spi_record_batch_response_t;

#ifdef __cplusplus
static_assert(sizeof(cerberus_spi_request_t) == CERBERUS_SPI_TRANSFER_SIZE);
static_assert(sizeof(cerberus_spi_config_request_t) == CERBERUS_SPI_TRANSFER_SIZE);
static_assert(sizeof(cerberus_spi_status_response_t) == CERBERUS_SPI_TRANSFER_SIZE);
static_assert(sizeof(cerberus_spi_record_batch_response_t) == CERBERUS_SPI_TRANSFER_SIZE);
#else
_Static_assert(sizeof(cerberus_spi_request_t) == CERBERUS_SPI_TRANSFER_SIZE, "SPI request size changed");
_Static_assert(sizeof(cerberus_spi_config_request_t) == CERBERUS_SPI_TRANSFER_SIZE, "SPI config size changed");
_Static_assert(sizeof(cerberus_spi_status_response_t) == CERBERUS_SPI_TRANSFER_SIZE, "SPI status size changed");
_Static_assert(sizeof(cerberus_spi_record_batch_response_t) == CERBERUS_SPI_TRANSFER_SIZE, "SPI batch size changed");
#endif

#ifdef __cplusplus
}
#endif
