#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_WIFI_STATION_LIMIT 32U
enum {CERBERUS_STATION_IDLE, CERBERUS_STATION_RUNNING, CERBERUS_STATION_STOPPING,
      CERBERUS_STATION_COMPLETE, CERBERUS_STATION_CANCELLED, CERBERUS_STATION_ERROR};
enum {CERBERUS_STATION_TX=1U, CERBERUS_STATION_AP_TX=2U, CERBERUS_STATION_DATA=4U,
      CERBERUS_STATION_MANAGEMENT=8U, CERBERUS_STATION_EAPOL=16U, CERBERUS_STATION_SAE=32U};
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t generation,index,bssid[6],channel,seconds;
} cerberus_wifi_station_request_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t state,count;
    uint32_t observed_frames,matched_frames,overflow_frames;
    uint16_t elapsed_ms; /* Jobs are <=60 seconds; saturates at 65,535 ms. */
    int32_t error_code;
} cerberus_wifi_station_status_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t index,flags,mac[6];
    int8_t rssi;
    uint8_t channel;
    uint32_t frames;
    uint16_t last_seen_ms;
    uint16_t from_station,to_station;
} cerberus_wifi_station_result_t;
typedef struct __attribute__((packed)) {uint16_t job_id;uint8_t index;} cerberus_wifi_station_query_t;
typedef struct {uint8_t mac[6],flags;} cerberus_wifi_station_observation_t;
typedef struct {
    cerberus_wifi_station_status_t status;
    cerberus_wifi_station_result_t rows[CERBERUS_WIFI_STATION_LIMIT];
} cerberus_wifi_station_table_t;

bool cerberus_wifi_station_mac_valid(const uint8_t mac[6]);
bool cerberus_wifi_station_request_valid(const cerberus_wifi_station_request_t *request);
/* Input excludes FCS. This observes directed frame addresses, not authenticated
 * association state. WDS, IBSS, group addresses and control frames are excluded. */
bool cerberus_wifi_station_decode(const uint8_t *frame,size_t length,const uint8_t bssid[6],
    cerberus_wifi_station_observation_t *observation);
void cerberus_wifi_station_observe(cerberus_wifi_station_table_t *table,
    const cerberus_wifi_station_observation_t *observation,int8_t rssi,uint8_t channel,uint32_t elapsed_ms);
bool cerberus_wifi_station_result_valid(const cerberus_wifi_station_result_t *result,uint16_t job_id,uint8_t index);
