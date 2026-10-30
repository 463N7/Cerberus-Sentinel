#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../../expansion/shared/cerberus_protocol.h"
#define EXP_PACKET_SIZE 64
#define EXP_BATCH_MAX 3
enum { EXP_STATUS = 1, EXP_CONFIG = 2, EXP_READ = 4, EXP_ARM = 5 };
typedef struct {
    uint8_t bssid[6], channel, security, radio, ssid_length;
    int8_t rssi;
    char ssid[33];
    char vendor[48];
} expansion_record_t;
typedef struct {
    bool enabled;
    uint8_t node_id, channel, queued;
    uint32_t firmware, dropped;
    bool config_proof_supported;
    uint8_t config_result, last_rejected_channel;
    uint32_t config_fingerprint, scan_rejections, last_scan_error;
} expansion_status_t;
void expansion_request(uint8_t packet[64], uint8_t command, uint8_t sequence);
void expansion_config(uint8_t packet[64], uint8_t sequence, unsigned slot, bool enabled);
void expansion_config_profile(uint8_t packet[64], uint8_t sequence,
    const cerberus_scan_config_t *profile);
bool expansion_parse_status(const uint8_t packet[64], uint8_t sequence, expansion_status_t *status);
bool expansion_status_matches_profile(const expansion_status_t *status,
    const cerberus_scan_config_t *profile);
bool expansion_parse_records(const uint8_t packet[64], uint8_t sequence, unsigned slot,
    expansion_record_t records[EXP_BATCH_MAX], uint8_t *count);
