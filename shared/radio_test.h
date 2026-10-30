#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum { CERBERUS_TEST_WIFI_BEACON=1, CERBERUS_TEST_BLE_ADVERT=2, CERBERUS_TEST_WIFI_AP=3 };
enum { CERBERUS_TEST_IDLE, CERBERUS_TEST_STAGED, CERBERUS_TEST_RUNNING,
       CERBERUS_TEST_STOPPING, CERBERUS_TEST_COMPLETE, CERBERUS_TEST_CANCELLED, CERBERUS_TEST_ERROR };
/* For Wi-Fi AP jobs only, status.name_length carries the failing worker stage.
 * Beacon jobs retain its original name-length meaning. This uses an otherwise
 * unused AP status byte without changing the fixed 22-byte SPI payload. */
enum { CERBERUS_AP_STAGE_NONE, CERBERUS_AP_STAGE_GET_BAND,
       CERBERUS_AP_STAGE_GET_CHANNEL, CERBERUS_AP_STAGE_GET_MAC,
       CERBERUS_AP_STAGE_SET_BAND, CERBERUS_AP_STAGE_SET_CHANNEL,
       CERBERUS_AP_STAGE_SET_RATE, CERBERUS_AP_STAGE_PROMISC_FILTER,
       CERBERUS_AP_STAGE_PROMISC_ENABLE, CERBERUS_AP_STAGE_PROMISC_DISABLE,
       CERBERUS_AP_STAGE_BEACON_WAIT, CERBERUS_AP_STAGE_BUILD_FRAME,
       CERBERUS_AP_STAGE_TRANSMIT, CERBERUS_AP_STAGE_RESTORE_BAND,
       CERBERUS_AP_STAGE_RESTORE_CHANNEL, CERBERUS_AP_STAGE_RESTORE_MODE,
       CERBERUS_AP_STAGE_SAE_MATERIAL };
static inline const char *cerberus_ap_stage_name(uint8_t stage)
{
    static const char *const names[]={"NONE","GET BAND","GET CHANNEL","GET MAC",
        "SET BAND","SET CHANNEL","SET RATE","BEACON FILTER","BEACON RX",
        "STOP BEACON RX","WAIT BEACON","BUILD FRAME","TRANSMIT",
        "RESTORE BAND","RESTORE CHANNEL","RESTORE MODE","SAE MATERIAL"};
    return stage<sizeof(names)/sizeof(names[0])?names[stage]:"UNKNOWN";
}
typedef struct {
    uint16_t job_id;
    uint8_t mode, channel, seconds, name_length;
    char name[32];
} cerberus_radio_test_config_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t mode, channel, seconds, name_length;
    char name[16];
} cerberus_radio_test_begin_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t offset, length;
    char data[16];
} cerberus_radio_test_chunk_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t state, mode, channel, seconds, name_length, counts_valid;
    uint32_t elapsed_ms, accepted_frames;
    int32_t error_code;
    uint16_t minimum_heap_kib;
} cerberus_radio_test_status_t;
bool cerberus_radio_test_config_valid(const cerberus_radio_test_config_t *config);
/* Standard discovery advertisements only. These builders accept no copied
 * identities, malformed frames, credentials or arbitrary raw payload. */
size_t cerberus_radio_test_beacon(const cerberus_radio_test_config_t *config,
    const uint8_t mac[6], uint64_t timestamp_us, uint8_t *out, size_t capacity);
size_t cerberus_radio_test_ble(const cerberus_radio_test_config_t *config,
    uint8_t *out, size_t capacity);
