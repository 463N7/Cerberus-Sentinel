#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_EXPANSION_PROTOCOL_VERSION 3
#define CERBERUS_MAX_SSID_LEN 32
#define CERBERUS_EXPANSION_C5_FIRMWARE_VERSION 1

#define CERBERUS_2G_CHANNEL_BIT(channel) (1u << (channel))
#define CERBERUS_2G_CHANNEL_MASK_US 0x0FFEu
#define CERBERUS_2G_CHANNEL_MASK_ETSI 0x3FFEu
#define CERBERUS_2G_CHANNEL_MASK_JP 0x7FFEu
#define CERBERUS_5G_CHANNEL_MASK_ALL 0x1FFFFFFEu

static inline uint8_t cerberus_5g_channel_to_bit(uint8_t channel)
{
    if (channel >= 36 && channel <= 64 && (channel - 36) % 4 == 0) {
        return (uint8_t)(((channel - 36) / 4) + 1);
    }
    if (channel >= 100 && channel <= 144 && (channel - 100) % 4 == 0) {
        return (uint8_t)(((channel - 100) / 4) + 9);
    }
    if (channel >= 149 && channel <= 177 && (channel - 149) % 4 == 0) {
        return (uint8_t)(((channel - 149) / 4) + 21);
    }
    return 0;
}

static inline uint8_t cerberus_5g_bit_to_channel(uint8_t bit)
{
    if (bit >= 1 && bit <= 8) {
        return (uint8_t)(((bit - 1) * 4) + 36);
    }
    if (bit >= 9 && bit <= 20) {
        return (uint8_t)(((bit - 9) * 4) + 100);
    }
    if (bit >= 21 && bit <= 28) {
        return (uint8_t)(((bit - 21) * 4) + 149);
    }
    return 0;
}

static inline uint32_t cerberus_5g_channel_bit(uint8_t channel)
{
    uint8_t bit = cerberus_5g_channel_to_bit(channel);
    return bit == 0 ? 0 : (1u << bit);
}

typedef enum {
    CERBERUS_RECORD_WIFI = 0,
    CERBERUS_RECORD_BLE = 1,
} cerberus_record_type_t;

typedef enum {
    CERBERUS_RADIO_MODE_WIFI = 0,
    CERBERUS_RADIO_MODE_BLE = 1,
} cerberus_radio_mode_t;

typedef enum {
    CERBERUS_REGION_US = 0,
    CERBERUS_REGION_ETSI = 1,
    CERBERUS_REGION_JP = 2,
} cerberus_wifi_region_t;

typedef enum {
    CERBERUS_AUTH_UNKNOWN = 0,
    CERBERUS_AUTH_OPEN,
    CERBERUS_AUTH_WEP,
    CERBERUS_AUTH_WPA,
    CERBERUS_AUTH_WPA2,
    CERBERUS_AUTH_WPA3,
    CERBERUS_AUTH_WPA_WPA2,
    CERBERUS_AUTH_WPA2_WPA3,
} cerberus_auth_mode_t;

typedef struct {
    uint8_t node_id;
    uint8_t record_type;
    char ssid[CERBERUS_MAX_SSID_LEN + 1];
    uint8_t bssid[6];
    uint8_t channel;
    int8_t rssi;
    cerberus_auth_mode_t auth_mode;
} cerberus_scan_record_t;

typedef struct {
    uint8_t protocol_version;
    uint8_t node_id;
    uint32_t firmware_version;
    uint32_t scan_interval_ms;
    uint16_t dwell_time_ms;
    uint16_t passive_dwell_time_ms;
    uint16_t channels_2g_mask;
    uint32_t channels_5g_mask;
    uint16_t ble_scan_interval_ms;
    uint16_t ble_scan_window_ms;
    int8_t min_rssi;
    uint8_t channels[14];
    uint8_t channel_count;
    uint8_t radio_mode;
    uint8_t region;
    uint8_t scan_enabled;
} cerberus_scan_config_t;

#ifdef __cplusplus
}
#endif
