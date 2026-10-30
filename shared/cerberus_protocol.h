#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "network_scan.h"
#include "radio_test.h"
#include "wifi_ap_job.h"
#include "sentinel_watch_motion.h"
#include "wifi_probe_observation.h"
#include "wifi_relation_observation.h"
#include "csi_protocol.h"
#include "mac_monitor_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_PROTOCOL_VERSION_V1 1U
#define CERBERUS_PROTOCOL_VERSION 2U
#define CERBERUS_C5_FIRMWARE_VERSION 60U
#define CERBERUS_SPI_MAGIC 0x43455242UL
#define CERBERUS_SPI_TRANSFER_SIZE 32U
#define CERBERUS_SPI_FRAME_PAYLOAD_SIZE 22U
#define CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE 64U

typedef enum {
    CERBERUS_SPI_COMMAND_NONE = 0,
    CERBERUS_SPI_COMMAND_STATUS = 1,
    CERBERUS_SPI_COMMAND_WIFI_SCAN_START = 2,
    CERBERUS_SPI_COMMAND_WIFI_SCAN_STOP = 3,
    CERBERUS_SPI_COMMAND_WIFI_SCAN_STATUS = 4,
    CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT = 5,
    CERBERUS_SPI_COMMAND_WIFI_SCAN_SSID = 6,
    CERBERUS_SPI_COMMAND_BLE_SCAN_START = 7,
    CERBERUS_SPI_COMMAND_BLE_SCAN_STOP = 8,
    CERBERUS_SPI_COMMAND_BLE_SCAN_STATUS = 9,
    CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT = 10,
    CERBERUS_SPI_COMMAND_BLE_SCAN_DATA = 11,
    CERBERUS_SPI_COMMAND_BLE_CONNECT = 12,
    CERBERUS_SPI_COMMAND_BLE_DISCONNECT = 13,
    CERBERUS_SPI_COMMAND_BLE_CONNECTION_STATUS = 14,
    CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_DISCOVER = 15,
    CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_STATUS = 16,
    CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_RESULT = 17,
    CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_DISCOVER = 18,
    CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_STATUS = 19,
    CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_RESULT = 20,
    CERBERUS_SPI_COMMAND_BLE_GATT_READ_START = 21,
    CERBERUS_SPI_COMMAND_BLE_GATT_READ_STATUS = 22,
    CERBERUS_SPI_COMMAND_BLE_GATT_READ_DATA = 23,
    CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_START = 24,
    CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_STATUS = 25,
    CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_RESULT = 26,
    CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_START = 27,
    CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_STATUS = 28,
    CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_RESULT = 29,
    CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_START = 30,
    CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_STATUS = 31,
    CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_RESULT = 32,
    CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS = 33,
    CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP = 34,
    CERBERUS_SPI_COMMAND_EVENT_QUEUE_FLOW_CONTROL = 35,
    CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH = 36,
    CERBERUS_SPI_COMMAND_FRAGMENT_OPEN = 37,
    CERBERUS_SPI_COMMAND_FRAGMENT_READ = 38,
    CERBERUS_SPI_COMMAND_WIFI_CONNECT_BEGIN = 39,
    CERBERUS_SPI_COMMAND_WIFI_CONNECT_PASSWORD = 40,
    CERBERUS_SPI_COMMAND_WIFI_CONNECT_COMMIT = 41,
    CERBERUS_SPI_COMMAND_WIFI_DISCONNECT = 42,
    CERBERUS_SPI_COMMAND_WIFI_CONNECTION_STATUS = 43,
    CERBERUS_SPI_COMMAND_WIFI_DNS_STATUS = 44,
    CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_START = 45,
    CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_STATUS = 46,
    CERBERUS_SPI_COMMAND_MDNS_SCAN_START = 47,
    CERBERUS_SPI_COMMAND_MDNS_SCAN_STATUS = 48,
    CERBERUS_SPI_COMMAND_MDNS_SCAN_RESULT = 49,
    CERBERUS_SPI_COMMAND_WIFI_RADIO_STATS = 50,
    CERBERUS_SPI_COMMAND_WIFI_DEFENSE_START = 51,
    CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STOP = 52,
    CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STATUS = 53,
    CERBERUS_SPI_COMMAND_WIFI_DEFENSE_RESULT = 54,
    CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS = 55,
    CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA = 56,
    CERBERUS_SPI_COMMAND_BLE_ACTIVITY = 57,
    CERBERUS_SPI_COMMAND_NET_SCAN_START = 58,
    CERBERUS_SPI_COMMAND_NET_SCAN_STATUS = 59,
    CERBERUS_SPI_COMMAND_NET_SCAN_STOP = 60,
    CERBERUS_SPI_COMMAND_NET_SCAN_RESULT = 61,
    CERBERUS_SPI_COMMAND_RADIO_TEST_BEGIN = 62,
    CERBERUS_SPI_COMMAND_RADIO_TEST_CHUNK = 63,
    CERBERUS_SPI_COMMAND_RADIO_TEST_START = 64,
    CERBERUS_SPI_COMMAND_RADIO_TEST_STATUS = 65,
    CERBERUS_SPI_COMMAND_RADIO_TEST_STOP = 66,
    CERBERUS_SPI_COMMAND_WIFI_STATION_START = 67,
    CERBERUS_SPI_COMMAND_WIFI_STATION_STATUS = 68,
    CERBERUS_SPI_COMMAND_WIFI_STATION_STOP = 69,
    CERBERUS_SPI_COMMAND_WIFI_STATION_RESULT = 70,
    CERBERUS_SPI_COMMAND_WIFI_AP_START = 71,
    CERBERUS_SPI_COMMAND_WIFI_AP_VALIDATE = 72,
    CERBERUS_SPI_COMMAND_WIFI_AP_STATION_BEGIN = 73,
    CERBERUS_SPI_COMMAND_WIFI_AP_STATION_CHUNK = 74,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START = 75,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STATUS = 76,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK = 77,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ = 78,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_ACK = 79,
    CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP = 80,
    CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_START = 81,
    CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STOP = 82,
    CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STATUS = 83,
    CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_RESULT = 84,
    CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_START = 85,
    CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STOP = 86,
    CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STATUS = 87,
    CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_SAMPLE = 88,
    CERBERUS_SPI_COMMAND_WATCH_SCAN_TIMING = 89,
    CERBERUS_SPI_COMMAND_REMOTE_ID_START = 90,
    CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS = 91,
    CERBERUS_SPI_COMMAND_REMOTE_ID_STOP = 92,
    CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW = 93,
    CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION = 94,
    CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING = 95,
    CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS = 96,
    CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK = 97,
    CERBERUS_SPI_COMMAND_CSI_START = 98,
    CERBERUS_SPI_COMMAND_CSI_STATUS = 99,
    CERBERUS_SPI_COMMAND_CSI_RENEW = 100,
    CERBERUS_SPI_COMMAND_CSI_STOP = 101,
    CERBERUS_SPI_COMMAND_CSI_PEEK = 102,
    CERBERUS_SPI_COMMAND_CSI_READ = 103,
    CERBERUS_SPI_COMMAND_CSI_ACK = 104,
    CERBERUS_SPI_COMMAND_MAC_START = 105,
    CERBERUS_SPI_COMMAND_MAC_STATUS = 106,
    CERBERUS_SPI_COMMAND_MAC_RENEW = 107,
    CERBERUS_SPI_COMMAND_MAC_STOP = 108,
    CERBERUS_SPI_COMMAND_MAC_PEEK = 109,
    CERBERUS_SPI_COMMAND_MAC_READ = 110,
    CERBERUS_SPI_COMMAND_MAC_ACK = 111,
    CERBERUS_SPI_COMMAND_WIFI_RELATION_STATUS = 112,
    CERBERUS_SPI_COMMAND_WIFI_RELATION_CHUNK = 113,
    CERBERUS_SPI_COMMAND_WIFI_RELATION_CLOCK = 114,
} cerberus_spi_command_t;

typedef enum {
    CERBERUS_C5_CAP_WIFI_2_4_GHZ = 1UL << 0,
    CERBERUS_C5_CAP_WIFI_5_GHZ = 1UL << 1,
    CERBERUS_C5_CAP_BLE = 1UL << 2,
    CERBERUS_C5_CAP_IEEE802154 = 1UL << 3,
    CERBERUS_C5_CAP_FRAMED_SPI = 1UL << 4,
    CERBERUS_C5_CAP_BLE_GATT_CLIENT = 1UL << 5,
    CERBERUS_C5_CAP_EVENT_QUEUE = 1UL << 6,
    CERBERUS_C5_CAP_EVENT_FLOW_CONTROL = 1UL << 7,
    CERBERUS_C5_CAP_EVENT_BATCH = 1UL << 8,
    CERBERUS_C5_CAP_FRAGMENTED_OBJECTS = 1UL << 9,
    CERBERUS_C5_CAP_WIFI_STATION = 1UL << 10,
    CERBERUS_C5_CAP_WIFI_DNS_STATUS = 1UL << 11,
    CERBERUS_C5_CAP_NETWORK_DIAGNOSTICS = 1UL << 12,
    CERBERUS_C5_CAP_MDNS_DISCOVERY = 1UL << 13,
    CERBERUS_C5_CAP_WIFI_RADIO_STATS = 1UL << 14,
    CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY = 1UL << 15,
    CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT = 1UL << 16,
    CERBERUS_C5_CAP_WATCH_OBSERVATIONS = 1UL << 17,
    CERBERUS_C5_CAP_BLE_ACTIVITY = 1UL << 18,
    CERBERUS_C5_CAP_NETWORK_SCAN = 1UL << 19,
    CERBERUS_C5_CAP_RADIO_TEST = 1UL << 20,
    CERBERUS_C5_CAP_SERVICE_SCANS = 1UL << 21,
    CERBERUS_C5_CAP_HOST_DISCOVERY = 1UL << 22,
    CERBERUS_C5_CAP_WIFI_STATIONS = 1UL << 23,
    CERBERUS_C5_CAP_AP_DEAUTH = 1UL << 24,
    CERBERUS_C5_CAP_AP_PROBE = 1UL << 25,
    CERBERUS_C5_CAP_AP_BAD_MESSAGE = 1UL << 26,
    CERBERUS_C5_CAP_AP_ASSOC_SLEEP = 1UL << 27,
    CERBERUS_C5_CAP_AP_CHANNEL_SWITCH = 1UL << 28,
    CERBERUS_C5_CAP_AP_QUIET = 1UL << 29,
    CERBERUS_C5_CAP_AP_CLONE_SPAM = 1UL << 30,
    CERBERUS_C5_CAP_AP_SAE_COMMIT = 1UL << 31,
} cerberus_c5_capability_t;

typedef enum {
    CERBERUS_C5_RADIO_WIFI_SCAN = 1U << 0,
    CERBERUS_C5_RADIO_BLE_SCAN = 1U << 1,
    CERBERUS_C5_RADIO_BLE_CONNECTION = 1U << 2,
    CERBERUS_C5_RADIO_IEEE802154_SCAN = 1U << 3,
    CERBERUS_C5_RADIO_WIFI_CONNECTED = 1U << 4,
    CERBERUS_C5_RADIO_WIFI_DEFENSE_SURVEY = 1U << 5,
    CERBERUS_C5_RADIO_TEST = 1U << 6,
    CERBERUS_C5_RADIO_WIFI_STATIONS = 1U << 7,
} cerberus_c5_radio_state_t;

#define CERBERUS_C5_CAPABILITIES \
    (CERBERUS_C5_CAP_WIFI_2_4_GHZ | CERBERUS_C5_CAP_WIFI_5_GHZ | \
     CERBERUS_C5_CAP_BLE | CERBERUS_C5_CAP_IEEE802154 | CERBERUS_C5_CAP_FRAMED_SPI | \
     CERBERUS_C5_CAP_BLE_GATT_CLIENT | CERBERUS_C5_CAP_EVENT_QUEUE | \
     CERBERUS_C5_CAP_EVENT_FLOW_CONTROL | CERBERUS_C5_CAP_EVENT_BATCH | \
     CERBERUS_C5_CAP_FRAGMENTED_OBJECTS | CERBERUS_C5_CAP_WIFI_STATION | \
     CERBERUS_C5_CAP_WIFI_DNS_STATUS | CERBERUS_C5_CAP_NETWORK_DIAGNOSTICS | \
     CERBERUS_C5_CAP_MDNS_DISCOVERY | CERBERUS_C5_CAP_WIFI_RADIO_STATS | \
     CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY | CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT | \
     CERBERUS_C5_CAP_WATCH_OBSERVATIONS | CERBERUS_C5_CAP_BLE_ACTIVITY | \
     CERBERUS_C5_CAP_NETWORK_SCAN | CERBERUS_C5_CAP_RADIO_TEST | CERBERUS_C5_CAP_SERVICE_SCANS | \
     CERBERUS_C5_CAP_HOST_DISCOVERY | CERBERUS_C5_CAP_WIFI_STATIONS | CERBERUS_C5_CAP_AP_DEAUTH | CERBERUS_C5_CAP_AP_PROBE | \
     CERBERUS_C5_CAP_AP_BAD_MESSAGE | CERBERUS_C5_CAP_AP_ASSOC_SLEEP | CERBERUS_C5_CAP_AP_CHANNEL_SWITCH | \
     CERBERUS_C5_CAP_AP_QUIET | CERBERUS_C5_CAP_AP_CLONE_SPAM | CERBERUS_C5_CAP_AP_SAE_COMMIT)

#include "wifi_stations.h"
_Static_assert(sizeof(cerberus_wifi_ap_job_t)<=CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"AP job fits SPI");
_Static_assert(sizeof(cerberus_wifi_ap_station_begin_t)<=CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"AP station begin fits SPI");
_Static_assert(sizeof(cerberus_wifi_ap_station_chunk_t)==CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"AP station chunk fills SPI");
_Static_assert(sizeof(cerberus_wifi_station_request_t)<=CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"station request fits SPI");
_Static_assert(sizeof(cerberus_wifi_station_status_t)==CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"station status fits SPI");
_Static_assert(sizeof(cerberus_wifi_station_result_t)==CERBERUS_SPI_FRAME_PAYLOAD_SIZE,"station result fits SPI");

_Static_assert(sizeof(cerberus_radio_test_begin_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "test begin fits SPI");
_Static_assert(sizeof(cerberus_radio_test_status_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "test status fits SPI");
_Static_assert(sizeof(cerberus_radio_test_chunk_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "test chunk fits SPI");

_Static_assert(sizeof(cerberus_net_scan_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "network request fits SPI");
_Static_assert(sizeof(cerberus_net_scan_status_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "network status fits SPI");
_Static_assert(sizeof(cerberus_net_scan_result_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE, "network result fits SPI");

/* Revision 31: receive-only protocol observations retained independently of
   ordinary device inventories. All fields are unauthenticated broadcasts. */
#define CERBERUS_WATCH_MIN_FIRMWARE 31U
#define CERBERUS_WATCH_TIMING_MIN_FIRMWARE 53U
#define CERBERUS_WATCH_MOTION_MIN_FIRMWARE 56U
#define CERBERUS_REMOTE_ID_MIN_FIRMWARE 54U
#define CERBERUS_REMOTE_ID_FOLLOW_MIN_FIRMWARE 55U
#define CERBERUS_REMOTE_ID_FOLLOW_MAX_AGE_MS 30000U
#define CERBERUS_REMOTE_ID_WINDOW_MIN_MS 1000U
#define CERBERUS_REMOTE_ID_WINDOW_MAX_MS 30000U
#define CERBERUS_REMOTE_ID_LEASE_MIN_MS 1000U
#define CERBERUS_REMOTE_ID_LEASE_MAX_MS 5000U
#define CERBERUS_REMOTE_ID_DWELL_MIN_MS 100U
#define CERBERUS_REMOTE_ID_DWELL_MAX_MS 1000U

enum {
    CERBERUS_REMOTE_ID_STATE_IDLE = 0,
    CERBERUS_REMOTE_ID_STATE_STARTING = 1,
    CERBERUS_REMOTE_ID_STATE_RUNNING = 2,
    CERBERUS_REMOTE_ID_STATE_COMPLETE = 3,
    CERBERUS_REMOTE_ID_STATE_STOPPING = 4,
    CERBERUS_REMOTE_ID_STATE_STOPPED = 5,
    CERBERUS_REMOTE_ID_STATE_ERROR = 6,
    CERBERUS_REMOTE_ID_STATE_REJECTED = 7,
};
enum {
    CERBERUS_REMOTE_ID_FLAG_OWNED = 1U,
    CERBERUS_REMOTE_ID_FLAG_RECEIVING = 2U,
    CERBERUS_REMOTE_ID_FLAG_DATA_READY = 4U,
    CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED = 8U,
    CERBERUS_REMOTE_ID_FLAG_FOLLOW_BOUND = 16U,
};
enum {
    CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON = 1U,
    CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN = 2U,
};
/* IDs strictly increase during this C5 boot. Exact last-ID/config retries are
 * idempotent, including terminal jobs. STATUS with ID0 is read-only discovery. */
typedef struct __attribute__((packed)) {
    uint64_t job_id;
    uint16_t duration_ms;
    uint16_t lease_ms;
    uint16_t dwell_ms;
    uint8_t flags;
    union {
        uint8_t reserved[7]; /* Firmware54/generic requests require all zero. */
        struct {
            uint8_t transmitter[6]; /* Exact Wi-Fi source, never claimed UAS ID. */
            uint8_t follow_channel; /* Zero explicitly requests SEARCH. */
        };
    };
} cerberus_remote_id_start_request_t;
typedef struct __attribute__((packed)) {
    uint64_t job_id;
} cerberus_remote_id_job_request_t;
typedef struct __attribute__((packed)) {
    uint64_t job_id;
    int32_t error_code;
    uint32_t elapsed_ms;
    uint16_t lease_remaining_ms;
    uint8_t state;
    uint8_t generation;
    uint8_t channel; /* Actual RX channel; zero while not receiving. */
    uint8_t flags;
} cerberus_remote_id_status_payload_t;
_Static_assert(sizeof(cerberus_remote_id_start_request_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Remote ID start fills SPI payload");
_Static_assert(sizeof(cerberus_remote_id_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Remote ID status fills SPI payload");
_Static_assert(sizeof(cerberus_remote_id_job_request_t) == 8U, "Remote ID job identity query");
#define CERBERUS_WATCH_MAX_OBSERVATIONS 12U
#define CERBERUS_WATCH_DATA_CHUNK_SIZE 17U
#define CERBERUS_BLE_ACTIVITY_MAX_IDENTITIES 128U
enum { CERBERUS_WATCH_RADIO_BLE = 1, CERBERUS_WATCH_RADIO_WIFI = 2 };
enum { CERBERUS_WATCH_IDLE = 0, CERBERUS_WATCH_RUNNING = 1,
       CERBERUS_WATCH_COMPLETE = 2, CERBERUS_WATCH_ERROR = 3 };
enum { CERBERUS_WATCH_CAPTURE_UNAVAILABLE = 1U,
       CERBERUS_WATCH_CAPTURE_OVERFLOW = 2U };
enum { CERBERUS_BLE_ACTIVITY_IDENTITIES_SATURATED = 1U,
       CERBERUS_BLE_ACTIVITY_EXTENDED_ENABLED = 2U,
       CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED = 4U };

typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
} cerberus_watch_scan_request_t;

typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t state;
    uint8_t stored_count;
    uint16_t flags;
    uint16_t dropped_records; /* Unretained matching reports, not unique devices. */
    uint32_t reports;         /* BLE reports or Wi-Fi management frames delivered. */
    uint32_t duration_ms;     /* Elapsed capture time; excludes transfer/readout. */
} cerberus_watch_scan_status_payload_t;

/* kind/transport/flags/altitude_kind match sentinel_watch_protocol.h. Fields
   from separate Remote ID messages merge only for the same broadcast address.
   id contains printable text or is empty. Coordinates are claims, not a fix. */
typedef struct __attribute__((packed)) {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t channel;
    uint8_t kind;
    uint8_t transport;
    uint8_t flags;
    uint8_t altitude_kind;
    int32_t latitude_e7;
    int32_t longitude_e7;
    int32_t altitude_dm;
    char id[21];
} cerberus_watch_observation_t;

typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t index;
    uint8_t chunk;
} cerberus_watch_scan_data_request_t;

typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t index;
    uint8_t chunk;
    uint8_t byte_count;
    uint8_t bytes[CERBERUS_WATCH_DATA_CHUNK_SIZE];
} cerberus_watch_scan_data_payload_t;

/* Optional revision53 sidecar. Existing observations/status/chunks are unchanged.
 * Age is the latest qualifying source callback, not every merged claim's age.
 * Explicit validity permits a genuine callback timestamp of zero. */
enum { CERBERUS_WATCH_TIMING_VALID = 1U };
typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t index;
} cerberus_watch_scan_timing_request_t;
typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t index;
    uint8_t flags;
    uint64_t source_age_ms;
    uint64_t capture_epoch;
    uint8_t reserved[2];
} cerberus_watch_scan_timing_payload_t;
_Static_assert(sizeof(cerberus_watch_scan_timing_request_t) == 3U, "Watch timing request layout");
_Static_assert(sizeof(cerberus_watch_scan_timing_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Watch timing fits one unchanged SPI frame");

/* Revision56 additive motion sidecar. Both commands use the existing 4-byte
 * data request with chunk==0. Timing command95 returns the timing payload
 * above, whose age belongs only to this atomic Location motion tuple.
 * Basic-ID-only new rows still return their valid nonzero capture epoch with
 * an all-zero absent tuple and unavailable motion age. */
typedef struct __attribute__((packed)) {
    uint8_t radio;
    uint8_t generation;
    uint8_t index;
    uint8_t reserved0;
    uint64_t capture_epoch;
    sentinel_watch_motion_t motion;
    uint8_t reserved[2];
} cerberus_watch_scan_motion_payload_t;
_Static_assert(sizeof(cerberus_watch_scan_motion_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Watch motion fits one unchanged SPI frame");

/* All received advertising reports are counted before the 32-entry inventory.
   Controller duplicate filtering is disabled. valid_reports means complete,
   structurally valid AD data, not a recognized device. identities is an exact
   distinct-address lower bound, capped at 128 (MAC plus address type), not people
   or physical-device count. Peak uses completed one-second capture buckets. */
typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t state;
    uint16_t flags;
    uint32_t duration_ms;
    uint32_t reports;
    uint32_t valid_reports;
    uint16_t identities;
    uint16_t peak_reports_per_second;
    uint8_t reserved[2];
} cerberus_ble_activity_payload_t;

/* Protocol V1 is retained for staged S3/C5 firmware upgrades. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t protocol_version;
    uint8_t command;
    uint8_t sequence;
    uint8_t reserved[CERBERUS_SPI_TRANSFER_SIZE - 7U];
} cerberus_spi_v1_request_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t protocol_version;
    uint8_t command;
    uint8_t sequence;
    uint8_t ready;
    uint32_t firmware_version;
    uint32_t uptime_ms;
    uint8_t reserved[CERBERUS_SPI_TRANSFER_SIZE - 16U];
} cerberus_spi_v1_response_t;

/* V2 uses the same fixed transfer size, with an explicit payload length and
   CRC-16/CCITT-FALSE over every byte before crc16. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t protocol_version;
    uint8_t command;
    uint8_t sequence;
    uint8_t payload_length;
    uint8_t payload[CERBERUS_SPI_FRAME_PAYLOAD_SIZE];
    uint16_t crc16;
} cerberus_spi_frame_t;

typedef struct __attribute__((packed)) {
    uint8_t ready;
    uint8_t reset_reason;
    uint8_t boot_count;
    uint8_t ble_phase;
    uint32_t firmware_version;
    uint32_t uptime_ms;
    uint32_t capabilities;
    uint16_t free_heap_kib;
    uint16_t minimum_free_heap_kib;
    uint8_t radio_state;
    uint8_t reserved;
} cerberus_status_payload_t;

/* Revision 45+ command family. One C5 receive session owns the
 * Wi-Fi radio. Captured bytes exclude FCS and have boot-monotonic time only.
 * A frame stays pinned until its generation/sequence ACK. READ chunks are
 * padded to 20 bytes; the consumer copies min(20, length-offset) bytes. */
#define CERBERUS_WIFI_CAPTURE_MIN_FIRMWARE 45U
#define CERBERUS_WIFI_CAPTURE_SAE_MIN_FIRMWARE 48U
#define CERBERUS_WIFI_CAPTURE_BEACON_MIN_FIRMWARE 50U
#define CERBERUS_WIFI_CAPTURE_HOP_MIN_FIRMWARE 50U
#define CERBERUS_WIFI_CAPTURE_HOP_DWELL_MS 250U
#define CERBERUS_WIFI_CAPTURE_CHUNK_SIZE 20U
#define CERBERUS_WIFI_CAPTURE_LEASE_MIN_MS 1000U
#define CERBERUS_WIFI_CAPTURE_LEASE_MAX_MS 300000U
enum {
    CERBERUS_WIFI_CAPTURE_MODE_RAW = 1U,
    CERBERUS_WIFI_CAPTURE_MODE_PROBE = 2U,
    CERBERUS_WIFI_CAPTURE_MODE_DEAUTH = 4U,
    CERBERUS_WIFI_CAPTURE_MODE_EAPOL = 8U,
    CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT = 16U,
    CERBERUS_WIFI_CAPTURE_MODE_BEACON = 32U,
    CERBERUS_WIFI_CAPTURE_MODE_ALL = 63U,
    CERBERUS_WIFI_CAPTURE_FLAG_BSSID = 1U,
    /* v50: channel=0, mode=BEACON only, no BSSID filter. The C5 cycles
     * SDK-supported 2.4/5 GHz channels without restarting generation. */
    CERBERUS_WIFI_CAPTURE_FLAG_HOP = 2U,
};
enum {
    CERBERUS_WIFI_CAPTURE_IDLE = 0U,
    CERBERUS_WIFI_CAPTURE_RUNNING = 1U,
    CERBERUS_WIFI_CAPTURE_STOPPED = 2U,
    CERBERUS_WIFI_CAPTURE_ERROR = 3U,
};
typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint8_t channel; /* fixed RX channel, or zero with FLAG_HOP */
    uint8_t mode_mask;
    uint8_t flags;
    uint8_t bssid[6];
    uint32_t lease_ms;
} cerberus_wifi_capture_start_request_t;
typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint8_t state;
    uint8_t channel; /* current RX channel; zero while hopping retunes */
    uint8_t mode_mask;
    uint8_t pending;
    uint32_t accepted;
    uint32_t dropped_full;
    uint16_t dropped_other;
    int32_t error_code;
} cerberus_wifi_capture_status_payload_t;
typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint32_t sequence; /* zero when no frame is pending */
    uint16_t length;   /* complete 802.11 bytes; no FCS/truncation */
    uint8_t monotonic_us_le48[6];
    uint8_t channel;
    int8_t rssi_dbm;
    uint32_t crc32;    /* full frame bytes, not SPI padding */
} cerberus_wifi_capture_peek_payload_t;
typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint32_t sequence;
    uint16_t offset;
} cerberus_wifi_capture_read_request_t;
typedef struct __attribute__((packed)) {
    uint16_t offset;
    uint8_t data[CERBERUS_WIFI_CAPTURE_CHUNK_SIZE];
} cerberus_wifi_capture_read_payload_t;
typedef struct __attribute__((packed)) {
    uint32_t generation;
    uint32_t sequence;
} cerberus_wifi_capture_ack_request_t;
typedef struct __attribute__((packed)) {
    uint32_t generation;
} cerberus_wifi_capture_stop_request_t;

#define CERBERUS_STATUS_PAYLOAD_LEGACY_SIZE 16U

#define CERBERUS_WIFI_SCAN_MAX_RESULTS 32U
#define CERBERUS_WIFI_SSID_MAX_LENGTH 32U
#define CERBERUS_WIFI_SSID_CHUNK_SIZE 18U

typedef enum {
    CERBERUS_WIFI_SCAN_IDLE = 0,
    CERBERUS_WIFI_SCAN_RUNNING = 1,
    CERBERUS_WIFI_SCAN_COMPLETE = 2,
    CERBERUS_WIFI_SCAN_ERROR = 3,
} cerberus_wifi_scan_state_t;

typedef enum {
    CERBERUS_WIFI_SECURITY_OPEN = 0,
    CERBERUS_WIFI_SECURITY_WEP = 1,
    CERBERUS_WIFI_SECURITY_WPA = 2,
    CERBERUS_WIFI_SECURITY_WPA2 = 3,
    CERBERUS_WIFI_SECURITY_WPA_WPA2 = 4,
    CERBERUS_WIFI_SECURITY_WPA3 = 5,
    CERBERUS_WIFI_SECURITY_WPA2_WPA3 = 6,
    CERBERUS_WIFI_SECURITY_OWE = 7,
    CERBERUS_WIFI_SECURITY_ENTERPRISE = 8,
    CERBERUS_WIFI_SECURITY_OTHER = 9,
} cerberus_wifi_security_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint16_t total_count;
    uint8_t stored_count;
    uint8_t hidden_count;
    uint8_t reserved[2];
    int32_t error_code;
} cerberus_wifi_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_wifi_scan_result_request_t;

#define CERBERUS_WIFI_OBSERVATION_MIN_FIRMWARE 51U
enum {
    CERBERUS_WIFI_OBSERVATION_VALID = 1U << 0,
    CERBERUS_WIFI_OBSERVATION_BEACON = 1U << 1,
    CERBERUS_WIFI_OBSERVATION_PROBE_RESPONSE = 1U << 2,
    CERBERUS_WIFI_OBSERVATION_ALL = 7U,
};

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t ssid_length;
    uint8_t bssid[6];
    /* Firmware51+: same-tuple frame presence, not the time of the driver's
     * aggregated RSSI/security fields. Unknown evidence is all zero. Age is
     * ceil(C5 callback-to-reply elapsed ms), saturated; source_ms is diagnostic
     * only and must never be subtracted from an S3 clock. Wire size stays22. */
    uint32_t observation_age_ms;
    uint32_t observation_source_ms;
    uint8_t observation_flags;
    uint8_t observation_channel;
} cerberus_wifi_scan_result_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t chunk;
} cerberus_wifi_scan_ssid_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t chunk;
    uint8_t byte_count;
    uint8_t bytes[CERBERUS_WIFI_SSID_CHUNK_SIZE];
} cerberus_wifi_scan_ssid_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t analyzed_count;
    uint16_t total_count;
    uint8_t count_24_ghz;
    uint8_t count_5_ghz;
    uint8_t open_count;
    uint8_t secured_count;
    uint8_t hidden_count;
    uint8_t active_channels_24;
    uint8_t active_channels_5;
    int8_t strongest_rssi;
    uint8_t strongest_channel;
    uint8_t truncated;
    uint32_t scan_duration_ms;
    uint16_t successful_scans;
    uint16_t failed_scans;
} cerberus_wifi_radio_stats_payload_t;

#define CERBERUS_WIFI_CHANNEL_SURVEY_DWELL_MS 100U
#define CERBERUS_WIFI_CHANNEL_SURVEY_MAX_RESULTS 25U
#define CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE 46U

typedef enum {
    CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24 = 1,
    CERBERUS_WIFI_CHANNEL_SURVEY_BAND_5 = 2,
} cerberus_wifi_channel_survey_band_t;

typedef enum {
    CERBERUS_WIFI_CHANNEL_SURVEY_IDLE = 0,
    CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING = 1,
    CERBERUS_WIFI_CHANNEL_SURVEY_ERROR = 2,
} cerberus_wifi_channel_survey_state_t;

typedef struct __attribute__((packed)) {
    uint8_t band;
} cerberus_wifi_channel_survey_start_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t band;
    uint8_t current_channel;
    uint8_t result_count;
    uint8_t completed_count;
    uint16_t total_sweeps;
    uint32_t elapsed_ms;
    int32_t error_code;
    uint16_t dwell_ms;
    uint32_t total_packets;
} cerberus_wifi_channel_survey_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_wifi_channel_survey_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t channel;
    uint8_t seen;
    uint32_t total_packets;
    uint16_t last_packets;
    uint16_t rate_per_sec;
    int8_t average_rssi;
    int8_t strongest_rssi;
    uint16_t sample_count;
    uint32_t updated_ms; /* since survey start; compare with status.elapsed_ms */
    uint16_t dwell_ms;
} cerberus_wifi_channel_survey_result_payload_t;

/* Count-only fixed-channel management-frame monitor. Each sample covers one
 * completed 200 ms interval. Sequence plus generation makes ring overwrites
 * explicit to the S3 without transferring frame contents. */
#define CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE 47U
#define CERBERUS_WIFI_PACKET_MONITOR_SAMPLE_MS 200U
#define CERBERUS_WIFI_PACKET_MONITOR_SAMPLES 64U

typedef enum {
    CERBERUS_WIFI_PACKET_MONITOR_IDLE = 0,
    CERBERUS_WIFI_PACKET_MONITOR_RUNNING = 1,
    CERBERUS_WIFI_PACKET_MONITOR_ERROR = 2,
} cerberus_wifi_packet_monitor_state_t;

typedef struct __attribute__((packed)) {
    uint8_t channel;
} cerberus_wifi_packet_monitor_start_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
} cerberus_wifi_packet_monitor_stop_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t channel;
    uint8_t stored_count;
    uint16_t latest_sequence;
    uint16_t sample_ms;
    uint32_t elapsed_ms;
    int32_t error_code;
} cerberus_wifi_packet_monitor_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint16_t sequence;
} cerberus_wifi_packet_monitor_sample_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t channel;
    uint16_t sequence;
    uint16_t beacons;
    uint16_t disconnections; /* Deauth plus disassociation frames. */
    uint16_t probe_requests;
    uint32_t updated_ms;
} cerberus_wifi_packet_monitor_sample_payload_t;

#define CERBERUS_WIFI_DEFENSE_DWELL_MS 1000U

typedef enum {
    CERBERUS_WIFI_DEFENSE_IDLE = 0,
    CERBERUS_WIFI_DEFENSE_RUNNING = 1,
    CERBERUS_WIFI_DEFENSE_COMPLETE = 2,
    CERBERUS_WIFI_DEFENSE_ERROR = 3,
} cerberus_wifi_defense_state_t;

typedef enum {
    CERBERUS_WIFI_DEFENSE_FLAG_DEAUTH_ACTIVITY = 1U << 0,
    CERBERUS_WIFI_DEFENSE_FLAG_HIGH_BEACON_RATE = 1U << 1,
    CERBERUS_WIFI_DEFENSE_FLAG_ELEVATED_NOISE = 1U << 2,
    CERBERUS_WIFI_DEFENSE_FLAG_HIGH_ACTIVITY = 1U << 3,
} cerberus_wifi_defense_flag_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t result_count;
    uint8_t completed_count;
    uint8_t current_index;
    uint8_t current_channel;
    uint8_t flagged_count;
    uint8_t reserved;
    uint32_t elapsed_ms;
    int32_t error_code;
    uint16_t dwell_ms;
    uint16_t reserved2;
} cerberus_wifi_defense_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_wifi_defense_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t channel;
    uint8_t assessment_flags;
    uint16_t management_frames;
    uint16_t control_frames;
    uint16_t data_frames;
    uint16_t beacon_frames;
    uint16_t deauth_frames;
    uint16_t disassoc_frames;
    int8_t average_noise_floor;
    int8_t strongest_rssi;
    uint16_t dwell_ms;
    uint16_t beacon_rate_per_sec;
} cerberus_wifi_defense_result_payload_t;

#define CERBERUS_WIFI_PASSWORD_MAX_LENGTH 63U
#define CERBERUS_WIFI_PASSWORD_CHUNK_SIZE 20U

typedef enum {
    CERBERUS_WIFI_CONNECTION_IDLE = 0,
    CERBERUS_WIFI_CONNECTION_STAGING = 1,
    CERBERUS_WIFI_CONNECTION_CONNECTING = 2,
    CERBERUS_WIFI_CONNECTION_CONNECTED = 3,
    CERBERUS_WIFI_CONNECTION_DISCONNECTING = 4,
    CERBERUS_WIFI_CONNECTION_ERROR = 5,
} cerberus_wifi_connection_state_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t password_length;
} cerberus_wifi_connect_begin_request_t;

typedef struct __attribute__((packed)) {
    uint8_t offset;
    uint8_t byte_count;
    uint8_t bytes[CERBERUS_WIFI_PASSWORD_CHUNK_SIZE];
} cerberus_wifi_connect_password_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t index;
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
    uint8_t ipv4[4];
    uint8_t gateway[4];
    uint8_t netmask[4];
    int32_t error_code;
} cerberus_wifi_connection_status_payload_t;

#define CERBERUS_WIFI_DNS_PRIMARY_VALID (1U << 0)
#define CERBERUS_WIFI_DNS_BACKUP_VALID (1U << 1)
#define CERBERUS_WIFI_DNS_FALLBACK_VALID (1U << 2)

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t primary[4];
    uint8_t backup[4];
    uint8_t fallback[4];
    uint8_t valid_flags;
    uint8_t reserved[4];
    int32_t error_code;
} cerberus_wifi_dns_status_payload_t;

typedef enum {
    CERBERUS_NETWORK_DIAGNOSTICS_IDLE = 0,
    CERBERUS_NETWORK_DIAGNOSTICS_RUNNING = 1,
    CERBERUS_NETWORK_DIAGNOSTICS_COMPLETE = 2,
    CERBERUS_NETWORK_DIAGNOSTICS_ERROR = 3,
} cerberus_network_diagnostics_state_t;

#define CERBERUS_NETWORK_TEST_DNS (1U << 0)
#define CERBERUS_NETWORK_TEST_GATEWAY_PING (1U << 1)
#define CERBERUS_NETWORK_TEST_ROUTE (1U << 2)
#define CERBERUS_NETWORK_TEST_NTP (1U << 3)
#define CERBERUS_NETWORK_TEST_HTTPS (1U << 4)
#define CERBERUS_NETWORK_TEST_ALL 0x1FU

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t completed_mask;
    uint8_t passed_mask;
    uint8_t active_test;
    uint8_t resolved_ipv4[4];
    uint16_t gateway_ping_ms;
    uint16_t https_status;
    uint32_t epoch_utc;
    int32_t error_code;
    uint8_t reserved[2];
} cerberus_network_diagnostics_status_payload_t;

#define CERBERUS_MDNS_MAX_RESULTS 16U
#define CERBERUS_MDNS_HOSTNAME_LENGTH 12U

typedef enum {
    CERBERUS_MDNS_SCAN_IDLE = 0,
    CERBERUS_MDNS_SCAN_RUNNING = 1,
    CERBERUS_MDNS_SCAN_COMPLETE = 2,
    CERBERUS_MDNS_SCAN_ERROR = 3,
} cerberus_mdns_scan_state_t;

typedef enum {
    CERBERUS_MDNS_SERVICE_HTTP = 0,
    CERBERUS_MDNS_SERVICE_HTTPS = 1,
    CERBERUS_MDNS_SERVICE_SSH = 2,
    CERBERUS_MDNS_SERVICE_WORKSTATION = 3,
    CERBERUS_MDNS_SERVICE_PRINTER = 4,
    CERBERUS_MDNS_SERVICE_IPP = 5,
    CERBERUS_MDNS_SERVICE_AIRPLAY = 6,
} cerberus_mdns_service_type_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t result_count;
    uint8_t queried_type_count;
    int32_t error_code;
    uint8_t reserved[4];
} cerberus_mdns_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_mdns_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t service_type;
    uint8_t protocol;
    uint16_t port;
    uint8_t ipv4[4];
    char hostname[CERBERUS_MDNS_HOSTNAME_LENGTH];
} cerberus_mdns_result_payload_t;

#define CERBERUS_BLE_SCAN_MAX_RESULTS 32U
#define CERBERUS_BLE_NAME_MAX_LENGTH 32U
#define CERBERUS_BLE_UUID_MAX_LENGTH 16U
#define CERBERUS_BLE_DATA_CHUNK_SIZE 17U
#define CERBERUS_BLE_MANUFACTURER_UNKNOWN 0xFFFFU
#define CERBERUS_BLE_RESULT_CONNECTABLE (1U << 0)
#define CERBERUS_BLE_ADVERTISEMENT_MAX_LENGTH 31U
#define CERBERUS_BLE_RAW_ADVERTISEMENT_MIN_FIRMWARE 30U
#define CERBERUS_BLE_ADVERTISEMENT_TRUNCATED (1U << 0)
#define CERBERUS_BLE_ADVERTISEMENT_MALFORMED (1U << 1)

typedef enum {
    CERBERUS_BLE_SCAN_IDLE = 0,
    CERBERUS_BLE_SCAN_RUNNING = 1,
    CERBERUS_BLE_SCAN_COMPLETE = 2,
    CERBERUS_BLE_SCAN_ERROR = 3,
} cerberus_ble_scan_state_t;

typedef enum {
    CERBERUS_BLE_DATA_NAME = 1,
    CERBERUS_BLE_DATA_SERVICE_UUID = 2,
    CERBERUS_BLE_DATA_RAW_ADVERTISEMENT = 3,
} cerberus_ble_data_kind_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint16_t total_count;
    uint8_t stored_count;
    uint8_t unnamed_count;
    /* Revision 30 + RAW_ADVERTISEMENT capability: reports dropped at the
       inventory limit, saturating. This is not a unique-device count. */
    uint16_t dropped_reports;
    int32_t error_code;
} cerberus_ble_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_ble_scan_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    int8_t rssi;
    uint8_t address_type;
    uint8_t name_length;
    uint8_t uuid_length;
    uint8_t address[6];
    uint16_t manufacturer_id;
    uint8_t flags;
    /* Revision 30 + RAW_ADVERTISEMENT capability; previously reserved bytes. */
    uint8_t advertisement_length;
    uint8_t advertisement_event_type;
    uint8_t advertisement_flags;
    uint16_t reports_seen; /* Delivered reports, after controller duplicate filtering. */
    uint8_t reserved[2];
} cerberus_ble_scan_result_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t kind;
    uint8_t chunk;
} cerberus_ble_scan_data_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t kind;
    uint8_t chunk;
    uint8_t byte_count;
    uint8_t bytes[CERBERUS_BLE_DATA_CHUNK_SIZE];
} cerberus_ble_scan_data_payload_t;

typedef enum {
    CERBERUS_BLE_CONNECTION_IDLE = 0,
    CERBERUS_BLE_CONNECTION_CONNECTING = 1,
    CERBERUS_BLE_CONNECTION_CONNECTED = 2,
    CERBERUS_BLE_CONNECTION_DISCONNECTING = 3,
    CERBERUS_BLE_CONNECTION_ERROR = 4,
} cerberus_ble_connection_state_t;

typedef struct __attribute__((packed)) {
    uint8_t address_type;
    uint8_t address[6];
} cerberus_ble_connect_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t address_type;
    uint8_t address[6];
    uint16_t connection_handle;
    int32_t error_code;
    uint8_t reserved[7];
} cerberus_ble_connection_status_payload_t;

#define CERBERUS_BLE_GATT_SERVICE_MAX_RESULTS 24U

typedef enum {
    CERBERUS_BLE_GATT_SERVICE_IDLE = 0,
    CERBERUS_BLE_GATT_SERVICE_RUNNING = 1,
    CERBERUS_BLE_GATT_SERVICE_COMPLETE = 2,
    CERBERUS_BLE_GATT_SERVICE_ERROR = 3,
} cerberus_ble_gatt_service_state_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint16_t total_count;
    uint8_t stored_count;
    uint8_t connection_generation;
    uint8_t reserved[2];
    int32_t error_code;
} cerberus_ble_gatt_service_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_ble_gatt_service_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t uuid_length;
    uint8_t uuid[16];
    uint16_t start_handle;
    uint16_t end_handle;
    uint8_t reserved;
} cerberus_ble_gatt_service_result_payload_t;

#define CERBERUS_BLE_GATT_CHARACTERISTIC_MAX_RESULTS 32U

typedef enum {
    CERBERUS_BLE_GATT_CHARACTERISTIC_IDLE = 0,
    CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING = 1,
    CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE = 2,
    CERBERUS_BLE_GATT_CHARACTERISTIC_ERROR = 3,
} cerberus_ble_gatt_characteristic_state_t;

typedef struct __attribute__((packed)) {
    uint16_t start_handle;
    uint16_t end_handle;
} cerberus_ble_gatt_characteristic_discover_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint16_t total_count;
    uint8_t stored_count;
    uint8_t connection_generation;
    uint16_t start_handle;
    uint16_t end_handle;
    int32_t error_code;
} cerberus_ble_gatt_characteristic_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_ble_gatt_characteristic_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t uuid_length;
    uint8_t uuid[16];
    uint16_t definition_handle;
    uint16_t value_handle;
    uint8_t properties;
} cerberus_ble_gatt_characteristic_result_payload_t;

#define CERBERUS_BLE_GATT_READ_MAX_LENGTH 256U
#define CERBERUS_BLE_GATT_READ_CHUNK_LENGTH 19U

typedef enum {
    CERBERUS_BLE_GATT_READ_IDLE = 0,
    CERBERUS_BLE_GATT_READ_RUNNING = 1,
    CERBERUS_BLE_GATT_READ_COMPLETE = 2,
    CERBERUS_BLE_GATT_READ_ERROR = 3,
} cerberus_ble_gatt_read_state_t;

typedef struct __attribute__((packed)) {
    uint16_t value_handle;
} cerberus_ble_gatt_read_request_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t connection_generation;
    uint8_t reserved0;
    uint16_t value_handle;
    uint16_t total_length;
    uint16_t stored_length;
    int32_t error_code;
    uint8_t reserved[8];
} cerberus_ble_gatt_read_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t chunk;
} cerberus_ble_gatt_read_data_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t chunk;
    uint8_t data_length;
    uint8_t data[CERBERUS_BLE_GATT_READ_CHUNK_LENGTH];
} cerberus_ble_gatt_read_data_payload_t;

#define CERBERUS_IEEE802154_FIRST_CHANNEL 11U
#define CERBERUS_IEEE802154_CHANNEL_COUNT 16U
#define CERBERUS_IEEE802154_POWER_UNMEASURED INT8_MIN

typedef enum {
    CERBERUS_IEEE802154_ENERGY_SCAN_IDLE = 0,
    CERBERUS_IEEE802154_ENERGY_SCAN_RUNNING = 1,
    CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE = 2,
    CERBERUS_IEEE802154_ENERGY_SCAN_ERROR = 3,
} cerberus_ieee802154_energy_scan_state_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t completed_count;
    uint8_t current_channel;
    int32_t error_code;
    uint8_t reserved[4];
} cerberus_ieee802154_energy_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
} cerberus_ieee802154_energy_scan_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t first_channel;
    uint8_t channel_count;
    int8_t power_dbm[CERBERUS_IEEE802154_CHANNEL_COUNT];
    uint8_t reserved[3];
} cerberus_ieee802154_energy_scan_result_payload_t;

#define CERBERUS_IEEE802154_BEACON_MAX_RESULTS 16U
#define CERBERUS_IEEE802154_BEACON_FLAG_ROUTER_CAPACITY (1U << 0)
#define CERBERUS_IEEE802154_BEACON_FLAG_END_DEVICE_CAPACITY (1U << 1)
#define CERBERUS_IEEE802154_BEACON_FLAG_ASSOCIATION_PERMIT (1U << 2)
#define CERBERUS_IEEE802154_BEACON_FLAG_PAN_COORDINATOR (1U << 3)

typedef enum {
    CERBERUS_IEEE802154_BEACON_SCAN_IDLE = 0,
    CERBERUS_IEEE802154_BEACON_SCAN_RUNNING = 1,
    CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE = 2,
    CERBERUS_IEEE802154_BEACON_SCAN_ERROR = 3,
} cerberus_ieee802154_beacon_scan_state_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint16_t beacon_count;
    uint8_t stored_count;
    uint8_t completed_count;
    uint8_t current_channel;
    uint8_t reserved;
    int32_t error_code;
} cerberus_ieee802154_beacon_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
} cerberus_ieee802154_beacon_scan_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t index;
    uint8_t channel;
    int8_t rssi;
    uint8_t lqi;
    uint8_t flags;
    uint16_t pan_id;
    uint8_t extended_pan_id[8];
    uint16_t source_short_address;
    uint8_t stack_profile;
    uint8_t protocol_version;
    uint8_t update_id;
    uint8_t reserved;
} cerberus_ieee802154_beacon_scan_result_payload_t;

#define CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT 5U
#define CERBERUS_IEEE802154_ACTIVITY_CHANNELS_PER_RESULT 4U
#define CERBERUS_IEEE802154_ACTIVITY_RESULT_COUNT \
    (CERBERUS_IEEE802154_CHANNEL_COUNT / CERBERUS_IEEE802154_ACTIVITY_CHANNELS_PER_RESULT)

typedef enum {
    CERBERUS_IEEE802154_ACTIVITY_FRAME_BEACON = 0,
    CERBERUS_IEEE802154_ACTIVITY_FRAME_DATA = 1,
    CERBERUS_IEEE802154_ACTIVITY_FRAME_ACK = 2,
    CERBERUS_IEEE802154_ACTIVITY_FRAME_MAC_COMMAND = 3,
    CERBERUS_IEEE802154_ACTIVITY_FRAME_OTHER = 4,
} cerberus_ieee802154_activity_frame_type_t;

typedef enum {
    CERBERUS_IEEE802154_ACTIVITY_SCAN_IDLE = 0,
    CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING = 1,
    CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE = 2,
    CERBERUS_IEEE802154_ACTIVITY_SCAN_ERROR = 3,
} cerberus_ieee802154_activity_scan_state_t;

typedef struct __attribute__((packed)) {
    uint8_t state;
    uint8_t generation;
    uint8_t completed_count;
    uint8_t current_channel;
    uint32_t total_frames;
    int32_t error_code;
} cerberus_ieee802154_activity_scan_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t result_index;
} cerberus_ieee802154_activity_scan_result_request_t;

typedef struct __attribute__((packed)) {
    uint8_t generation;
    uint8_t first_channel;
    uint8_t counts[CERBERUS_IEEE802154_ACTIVITY_CHANNELS_PER_RESULT]
                  [CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
} cerberus_ieee802154_activity_scan_result_payload_t;

#define CERBERUS_C5_EVENT_QUEUE_DEPTH 16U
#define CERBERUS_C5_EVENT_BATCH_MAX_EVENTS 2U
#define CERBERUS_C5_EVENT_FLOW_DEFAULT_LOW_WATERMARK 4U
#define CERBERUS_C5_EVENT_FLOW_DEFAULT_HIGH_WATERMARK 12U
#define CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED (1U << 0)
#define CERBERUS_C5_EVENT_FLOW_FLAG_ACTIVE (1U << 1)

typedef enum {
    CERBERUS_C5_EVENT_SOURCE_NONE = 0,
    CERBERUS_C5_EVENT_SOURCE_SYSTEM = 1,
    CERBERUS_C5_EVENT_SOURCE_WIFI_SCAN = 2,
    CERBERUS_C5_EVENT_SOURCE_BLE_SCAN = 3,
    CERBERUS_C5_EVENT_SOURCE_BLE_CONNECTION = 4,
    CERBERUS_C5_EVENT_SOURCE_BLE_GATT_SERVICE = 5,
    CERBERUS_C5_EVENT_SOURCE_BLE_GATT_CHARACTERISTIC = 6,
    CERBERUS_C5_EVENT_SOURCE_BLE_GATT_READ = 7,
    CERBERUS_C5_EVENT_SOURCE_IEEE802154_ENERGY = 8,
    CERBERUS_C5_EVENT_SOURCE_IEEE802154_BEACON = 9,
    CERBERUS_C5_EVENT_SOURCE_IEEE802154_ACTIVITY = 10,
} cerberus_c5_event_source_t;

typedef struct __attribute__((packed)) {
    uint8_t pending;
    uint8_t capacity;
    uint16_t dropped;
    uint32_t produced;
    uint32_t consumed;
    uint16_t deferred;
    uint8_t low_watermark;
    uint8_t high_watermark;
    uint8_t flow_flags;
    uint8_t control_generation;
} cerberus_c5_event_queue_status_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t low_watermark;
    uint8_t high_watermark;
    uint8_t flow_flags;
    uint8_t reserved;
} cerberus_c5_event_flow_control_request_t;

typedef struct __attribute__((packed)) {
    uint8_t max_events;
} cerberus_c5_event_batch_request_t;

/* Event payloads contain only operation state and aggregate counters. Radio
   identifiers, advertisement data, SSIDs, addresses, and payload bytes are
   deliberately excluded. */
typedef struct __attribute__((packed)) {
    uint16_t event_sequence;
    uint8_t source;
    uint8_t state;
    uint8_t generation;
    uint8_t reserved0;
    uint32_t uptime_ms;
    int32_t error_code;
    uint32_t value;
    uint32_t reserved1;
} cerberus_c5_event_payload_t;

/* The larger batch response retains complete event records. It is used only
   when both processors advertise CERBERUS_C5_CAP_EVENT_BATCH; ordinary V1/V2
   traffic remains the original fixed 32-byte transfer. */
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t protocol_version;
    uint8_t command;
    uint8_t sequence;
    uint8_t event_count;
    uint8_t pending_after;
    uint16_t dropped;
    uint16_t deferred;
    uint8_t flow_flags;
    uint8_t control_generation;
    uint8_t reserved[3];
    cerberus_c5_event_payload_t events[CERBERUS_C5_EVENT_BATCH_MAX_EVENTS];
    uint16_t crc16;
} cerberus_c5_event_batch_frame_t;

#define CERBERUS_FRAGMENT_DATA_SIZE 16U
#define CERBERUS_FRAGMENT_OBJECT_MAX_SIZE 256U
#define CERBERUS_FRAGMENT_FLAG_LAST (1U << 0)

typedef enum {
    CERBERUS_FRAGMENT_OBJECT_NONE = 0,
    CERBERUS_FRAGMENT_OBJECT_C5_DIAGNOSTIC_SNAPSHOT = 1,
} cerberus_fragment_object_t;

typedef struct __attribute__((packed)) {
    uint8_t object_type;
} cerberus_fragment_open_request_t;

typedef struct __attribute__((packed)) {
    uint8_t object_type;
    uint8_t generation;
    uint16_t total_length;
    uint32_t crc32;
    uint8_t fragment_size;
    uint8_t flags;
    uint8_t reserved[12];
} cerberus_fragment_info_payload_t;

typedef struct __attribute__((packed)) {
    uint8_t object_type;
    uint8_t generation;
    uint16_t offset;
} cerberus_fragment_read_request_t;

typedef struct __attribute__((packed)) {
    uint8_t object_type;
    uint8_t generation;
    uint16_t offset;
    uint8_t data_length;
    uint8_t flags;
    uint8_t data[CERBERUS_FRAGMENT_DATA_SIZE];
} cerberus_fragment_read_payload_t;

/* Aggregate-only snapshot used to exercise the generic fragment transport.
   It deliberately contains no SSIDs, addresses, advertisement data, radio
   payloads, credentials, or other variable-length external input. */
typedef struct __attribute__((packed)) {
    uint32_t firmware_version;
    uint32_t uptime_ms;
    uint32_t capabilities;
    uint32_t free_heap_bytes;
    uint32_t minimum_free_heap_bytes;
    uint32_t event_produced;
    uint32_t event_consumed;
    uint16_t event_dropped;
    uint16_t event_deferred;
    uint8_t event_pending;
    uint8_t event_capacity;
    uint8_t event_flow_flags;
    uint8_t radio_state;
    uint8_t reset_reason;
    uint8_t boot_count;
    uint8_t ble_phase;
    uint8_t wifi_state;
    uint8_t wifi_generation;
    uint8_t ble_scan_state;
    uint8_t ble_scan_generation;
    uint8_t ble_connection_state;
    uint8_t ble_connection_generation;
    uint8_t ieee802154_energy_state;
    uint8_t ieee802154_energy_generation;
    uint8_t ieee802154_beacon_state;
    uint8_t ieee802154_beacon_generation;
    uint8_t ieee802154_activity_state;
    uint8_t ieee802154_activity_generation;
    uint8_t reserved[13];
} cerberus_c5_diagnostic_snapshot_t;

static inline uint16_t cerberus_protocol_crc16(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0; index < length; ++index) {
        crc ^= (uint16_t)bytes[index] << 8U;
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 0x8000U) != 0U ? (uint16_t)((crc << 1U) ^ 0x1021U) : (uint16_t)(crc << 1U);
        }
    }
    return crc;
}

static inline uint32_t cerberus_protocol_crc32(const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t index = 0; index < length; ++index) {
        crc ^= bytes[index];
        for (uint8_t bit = 0; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320UL : crc >> 1U;
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

static inline bool cerberus_protocol_self_test(void)
{
    static const uint8_t check[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    return cerberus_protocol_crc16(check, sizeof(check)) == 0x29B1U &&
        cerberus_protocol_crc32(check, sizeof(check)) == 0xCBF43926UL;
}

static inline bool cerberus_protocol_prepare_frame(
    cerberus_spi_frame_t *frame,
    cerberus_spi_command_t command,
    uint8_t sequence,
    const void *payload,
    size_t payload_length)
{
    if (frame == NULL || payload_length > CERBERUS_SPI_FRAME_PAYLOAD_SIZE ||
        (payload_length > 0U && payload == NULL)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->magic = CERBERUS_SPI_MAGIC;
    frame->protocol_version = CERBERUS_PROTOCOL_VERSION;
    frame->command = (uint8_t)command;
    frame->sequence = sequence;
    frame->payload_length = (uint8_t)payload_length;
    if (payload_length > 0U) {
        memcpy(frame->payload, payload, payload_length);
    }
    frame->crc16 = cerberus_protocol_crc16(frame, offsetof(cerberus_spi_frame_t, crc16));
    return true;
}

static inline bool cerberus_protocol_frame_is_valid(const cerberus_spi_frame_t *frame)
{
    return frame != NULL && frame->magic == CERBERUS_SPI_MAGIC &&
        frame->protocol_version == CERBERUS_PROTOCOL_VERSION &&
        frame->payload_length <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE &&
        frame->crc16 == cerberus_protocol_crc16(frame, offsetof(cerberus_spi_frame_t, crc16));
}

static inline bool cerberus_protocol_prepare_event_batch(
    cerberus_c5_event_batch_frame_t *frame,
    uint8_t sequence,
    const cerberus_c5_event_payload_t *events,
    uint8_t event_count,
    uint8_t pending_after,
    uint16_t dropped,
    uint16_t deferred,
    uint8_t flow_flags,
    uint8_t control_generation)
{
    if (frame == NULL || event_count > CERBERUS_C5_EVENT_BATCH_MAX_EVENTS ||
        (event_count > 0U && events == NULL)) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->magic = CERBERUS_SPI_MAGIC;
    frame->protocol_version = CERBERUS_PROTOCOL_VERSION;
    frame->command = CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH;
    frame->sequence = sequence;
    frame->event_count = event_count;
    frame->pending_after = pending_after;
    frame->dropped = dropped;
    frame->deferred = deferred;
    frame->flow_flags = flow_flags;
    frame->control_generation = control_generation;
    if (event_count > 0U) {
        memcpy(frame->events, events, event_count * sizeof(events[0]));
    }
    frame->crc16 = cerberus_protocol_crc16(
        frame,
        offsetof(cerberus_c5_event_batch_frame_t, crc16));
    return true;
}

static inline bool cerberus_protocol_event_batch_is_valid(
    const cerberus_c5_event_batch_frame_t *frame)
{
    return frame != NULL && frame->magic == CERBERUS_SPI_MAGIC &&
        frame->protocol_version == CERBERUS_PROTOCOL_VERSION &&
        frame->command == CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH &&
        frame->event_count <= CERBERUS_C5_EVENT_BATCH_MAX_EVENTS &&
        frame->crc16 == cerberus_protocol_crc16(
            frame,
            offsetof(cerberus_c5_event_batch_frame_t, crc16));
}

_Static_assert(sizeof(cerberus_spi_v1_request_t) == CERBERUS_SPI_TRANSFER_SIZE,
    "SPI V1 request size changed");
_Static_assert(sizeof(cerberus_spi_v1_response_t) == CERBERUS_SPI_TRANSFER_SIZE,
    "SPI V1 response size changed");
_Static_assert(sizeof(cerberus_spi_frame_t) == CERBERUS_SPI_TRANSFER_SIZE,
    "SPI V2 frame size changed");
_Static_assert(sizeof(cerberus_wifi_capture_start_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture start request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_capture_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture status payload size changed");
_Static_assert(sizeof(cerberus_wifi_capture_peek_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture peek payload size changed");
_Static_assert(sizeof(cerberus_wifi_capture_read_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture read request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_capture_read_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture read payload size changed");
_Static_assert(sizeof(cerberus_wifi_capture_ack_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture ACK request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_capture_stop_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi capture stop request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_c5_event_batch_frame_t) == CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE,
    "C5 event batch frame size changed");
_Static_assert(sizeof(cerberus_fragment_open_request_t) == 1U,
    "fragment-open request size changed");
_Static_assert(sizeof(cerberus_fragment_info_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "fragment info payload size changed");
_Static_assert(sizeof(cerberus_fragment_read_request_t) == 4U,
    "fragment-read request size changed");
_Static_assert(sizeof(cerberus_fragment_read_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "fragment-read payload size changed");
_Static_assert(sizeof(cerberus_c5_diagnostic_snapshot_t) == 64U,
    "C5 diagnostic snapshot size changed");
_Static_assert(sizeof(cerberus_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "status payload exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi scan status payload exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_scan_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi scan result request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_scan_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi scan result payload size changed");
_Static_assert(sizeof(cerberus_wifi_scan_ssid_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi SSID request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_scan_ssid_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi SSID payload size changed");
_Static_assert(sizeof(cerberus_wifi_radio_stats_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi radio statistics payload size changed");
_Static_assert(sizeof(cerberus_wifi_channel_survey_start_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi channel survey start request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_channel_survey_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi channel survey status payload size changed");
_Static_assert(sizeof(cerberus_wifi_channel_survey_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi channel survey result request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_channel_survey_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi channel survey result payload size changed");
_Static_assert(sizeof(cerberus_wifi_packet_monitor_start_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi packet monitor start request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_packet_monitor_stop_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi packet monitor stop request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_packet_monitor_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi packet monitor status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_packet_monitor_sample_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi packet monitor sample request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_packet_monitor_sample_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi packet monitor sample exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_defense_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi defensive survey status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_defense_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi defensive survey request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_wifi_defense_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi defensive survey result payload size changed");
_Static_assert(sizeof(cerberus_wifi_connect_password_request_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi password chunk size changed");
_Static_assert(sizeof(cerberus_wifi_connection_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi connection status size changed");
_Static_assert(sizeof(cerberus_wifi_dns_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Wi-Fi DNS status size changed");
_Static_assert(sizeof(cerberus_network_diagnostics_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "Network diagnostics status size changed");
_Static_assert(sizeof(cerberus_mdns_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "mDNS scan status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_mdns_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "mDNS result request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_mdns_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "mDNS result payload size changed");
_Static_assert(sizeof(cerberus_ble_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE scan status payload exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_scan_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE scan result request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_scan_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE scan result payload size changed");
_Static_assert(sizeof(cerberus_ble_scan_data_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE scan data request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_scan_data_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE scan data payload size changed");
_Static_assert(offsetof(cerberus_ble_scan_status_payload_t, dropped_reports) == 6U,
    "BLE dropped reports must occupy previously reserved status bytes");
_Static_assert(offsetof(cerberus_ble_scan_result_payload_t, advertisement_length) == 15U,
    "BLE advertisement metadata must occupy previously reserved result bytes");
_Static_assert(sizeof(cerberus_ble_connect_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE connection request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_connection_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE connection status payload size changed");
_Static_assert(sizeof(cerberus_ble_gatt_service_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT service status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_service_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT service request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_service_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT service result payload size changed");
_Static_assert(sizeof(cerberus_ble_gatt_characteristic_discover_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT characteristic discovery request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_characteristic_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT characteristic status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_characteristic_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT characteristic request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_characteristic_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT characteristic result payload size changed");
_Static_assert(sizeof(cerberus_ble_gatt_read_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT read request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_read_status_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT read status payload size changed");
_Static_assert(sizeof(cerberus_ble_gatt_read_data_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT read data request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ble_gatt_read_data_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "BLE GATT read data payload size changed");
_Static_assert(sizeof(cerberus_ieee802154_energy_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 energy scan status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_energy_scan_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 energy scan request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_energy_scan_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 energy scan result payload size changed");
_Static_assert(sizeof(cerberus_ieee802154_beacon_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 beacon scan status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_beacon_scan_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 beacon scan request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_beacon_scan_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 beacon scan result payload size changed");
_Static_assert(sizeof(cerberus_ieee802154_activity_scan_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 activity scan status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_activity_scan_result_request_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 activity scan request exceeds a V2 frame");
_Static_assert(sizeof(cerberus_ieee802154_activity_scan_result_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "802.15.4 activity scan result payload size changed");
_Static_assert(sizeof(cerberus_c5_event_queue_status_payload_t) <= CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "C5 event queue status exceeds a V2 frame");
_Static_assert(sizeof(cerberus_c5_event_queue_status_payload_t) == 18U,
    "C5 event queue status payload size changed");
_Static_assert(sizeof(cerberus_c5_event_flow_control_request_t) == 4U,
    "C5 event flow-control request size changed");
_Static_assert(sizeof(cerberus_c5_event_batch_request_t) == 1U,
    "C5 event batch request size changed");
_Static_assert(sizeof(cerberus_c5_event_payload_t) == CERBERUS_SPI_FRAME_PAYLOAD_SIZE,
    "C5 event payload size changed");

#ifdef __cplusplus
}
#endif
