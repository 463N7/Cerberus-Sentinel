#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wifi_stations.h"
enum { CERBERUS_AP_DEAUTH=1, CERBERUS_AP_PROBE_REQUEST=2,
    CERBERUS_AP_BAD_MESSAGE=3, CERBERUS_AP_ASSOC_SLEEP=4,
    CERBERUS_AP_CHANNEL_SWITCH=5, CERBERUS_AP_QUIET=6,
    CERBERUS_AP_SAE_COMMIT=7, CERBERUS_AP_CLONE_SPAM=8 };
enum { CERBERUS_AP_BROADCAST=0, CERBERUS_AP_ONE_STATION=1, CERBERUS_AP_STATION_LIST=2 };
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t generation,index,bssid[6],station[6],channel,seconds,scope,mode;
} cerberus_wifi_ap_job_t;
/* In AP_CLONE_SPAM only, station[] carries the generated local clone BSSID;
 * no observed client is involved. Both chips use the same frozen identity. */
/* A selected-AP job still fits in one SPI frame. seconds=0 runs until Stop,
 * lease expiry, page exit, or an error; finite jobs are bounded to 20 seconds.
 * Its immutable client list is staged in 3-address chunks before AP_START. */
typedef struct {
    uint8_t count;
    uint8_t mac[CERBERUS_WIFI_STATION_LIMIT][6];
} cerberus_wifi_ap_station_set_t;
typedef struct __attribute__((packed)) {
    cerberus_wifi_ap_job_t job;
    uint8_t count;
} cerberus_wifi_ap_station_begin_t;
typedef struct __attribute__((packed)) {
    uint16_t job_id;
    uint8_t offset,count;
    uint8_t mac[3][6];
} cerberus_wifi_ap_station_chunk_t;
bool cerberus_wifi_ap_station_set_valid(const cerberus_wifi_ap_job_t *job,
    const cerberus_wifi_ap_station_set_t *set);
bool cerberus_wifi_ap_job_valid(const cerberus_wifi_ap_job_t *job);
size_t cerberus_wifi_ap_frame(const cerberus_wifi_ap_job_t *job,bool from_station,
    uint8_t *out,size_t capacity);
size_t cerberus_wifi_ap_probe(const cerberus_wifi_ap_job_t *job,const uint8_t source[6],
    const uint8_t *ssid,size_t ssid_length,uint8_t *out,size_t capacity);
bool cerberus_wifi_ap_channel_valid(uint8_t channel);
bool cerberus_wifi_ap_needs_station(uint8_t mode);
/* Scan metadata is private to C5; it is never accepted from a stale UI copy. */
typedef struct { uint8_t ssid[32],ssid_length; bool short_eapol; } cerberus_wifi_ap_metadata_t;
size_t cerberus_wifi_ap_bad_message(const cerberus_wifi_ap_job_t *job,bool short_variant,
    uint64_t replay,const uint8_t nonce[32],uint8_t *out,size_t capacity);
size_t cerberus_wifi_ap_association(const cerberus_wifi_ap_job_t *job,
    const uint8_t *ssid,size_t ssid_length,uint8_t *out,size_t capacity);
size_t cerberus_wifi_ap_announcement(const cerberus_wifi_ap_job_t *job,
    const uint8_t *ssid,size_t ssid_length,const uint8_t capabilities[2],
    uint8_t destination_channel,uint64_t timestamp,uint8_t *out,size_t capacity);
bool cerberus_wifi_ap_beacon_capabilities(const cerberus_wifi_ap_job_t *job,
    const uint8_t *ssid,size_t ssid_length,const uint8_t *frame,size_t length,uint8_t out[2]);
