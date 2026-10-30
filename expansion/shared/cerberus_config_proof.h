#pragma once

#include "cerberus_protocol.h"
#include <stdbool.h>
#include <stdint.h>

/* Versioned extension in status.reserved. Older expansion images leave the
 * reserved bytes zero, so the S3 can keep their original fixed survey mode. */
#define CERBERUS_CONFIG_PROOF_TAG0 0x43U /* C */
#define CERBERUS_CONFIG_PROOF_TAG1 0x50U /* P */
#define CERBERUS_CONFIG_PROOF_VERSION 1U
enum {
    CERBERUS_CONFIG_RESULT_NONE = 0U,
    CERBERUS_CONFIG_RESULT_APPLIED = 1U,
    CERBERUS_CONFIG_RESULT_REJECTED = 2U,
    CERBERUS_CONFIG_RESULT_COUNTRY_FAILED = 3U,
};
enum {
    CERBERUS_CONFIG_PROOF_TAG0_AT = 0U,
    CERBERUS_CONFIG_PROOF_TAG1_AT = 1U,
    CERBERUS_CONFIG_PROOF_VERSION_AT = 2U,
    CERBERUS_CONFIG_PROOF_RESULT_AT = 3U,
    CERBERUS_CONFIG_PROOF_FINGERPRINT_AT = 4U,
    CERBERUS_CONFIG_PROOF_SCAN_REJECTIONS_AT = 8U,
    CERBERUS_CONFIG_PROOF_LAST_REJECTED_CHANNEL_AT = 12U,
    CERBERUS_CONFIG_PROOF_LAST_SCAN_ERROR_AT = 13U,
};

static inline uint16_t cerberus_config_region_2g_mask(uint8_t region)
{
    switch (region) {
    case CERBERUS_REGION_US: return CERBERUS_2G_CHANNEL_MASK_US;
    case CERBERUS_REGION_ETSI: return CERBERUS_2G_CHANNEL_MASK_ETSI;
    case CERBERUS_REGION_JP: return CERBERUS_2G_CHANNEL_MASK_JP;
    default: return 0U;
    }
}

static inline uint32_t cerberus_config_region_5g_mask(uint8_t region)
{
    if (region > CERBERUS_REGION_JP) return 0U;
    uint32_t mask = 0U;
    /* 36-64 and 100-144; ETSI omits 144; US alone includes 149-165. */
    for (uint8_t bit = 1U; bit <= 20U; ++bit)
        if (!(region == CERBERUS_REGION_ETSI && bit == 20U)) mask |= 1UL << bit;
    if (region == CERBERUS_REGION_US)
        for (uint8_t bit = 21U; bit <= 25U; ++bit) mask |= 1UL << bit;
    return mask;
}

static inline bool cerberus_config_region_masks_valid(
    const cerberus_scan_config_t *config)
{
    if (!config || config->region > CERBERUS_REGION_JP) return false;
    uint16_t allowed_2g = cerberus_config_region_2g_mask(config->region);
    uint32_t allowed_5g = cerberus_config_region_5g_mask(config->region);
    return (config->channels_2g_mask || config->channels_5g_mask) &&
        !(config->channels_2g_mask & (uint16_t)~allowed_2g) &&
        !(config->channels_5g_mask & ~allowed_5g);
}

static inline bool cerberus_config_payload_valid(
    const cerberus_scan_config_t *config, uint8_t expected_node)
{
    return config && config->protocol_version == CERBERUS_EXPANSION_PROTOCOL_VERSION &&
        config->firmware_version == CERBERUS_EXPANSION_C5_FIRMWARE_VERSION &&
        (expected_node == 0U ?
            (config->node_id >= 1U && config->node_id <= 6U) :
            config->node_id == expected_node) &&
        config->scan_interval_ms >= 50U && config->scan_interval_ms <= 60000U &&
        config->dwell_time_ms >= 20U && config->dwell_time_ms <= 1500U &&
        config->passive_dwell_time_ms >= 20U &&
        config->passive_dwell_time_ms <= 1500U &&
        config->radio_mode == CERBERUS_RADIO_MODE_WIFI &&
        config->scan_enabled <= 1U &&
        cerberus_config_region_masks_valid(config);
}

/* Canonical field hash deliberately excludes channels[14] and channel_count:
 * the C5 rebuilds those legacy preview fields from the masks after applying
 * CONFIG. Explicit byte order also avoids S3/C5 struct padding differences. */
static inline uint32_t cerberus_config_proof_byte(uint32_t hash, uint8_t value)
{
    return (hash ^ value) * 16777619UL;
}
static inline uint32_t cerberus_config_proof_u16(uint32_t hash, uint16_t value)
{
    hash = cerberus_config_proof_byte(hash, (uint8_t)value);
    return cerberus_config_proof_byte(hash, (uint8_t)(value >> 8U));
}
static inline uint32_t cerberus_config_proof_u32(uint32_t hash, uint32_t value)
{
    hash = cerberus_config_proof_u16(hash, (uint16_t)value);
    return cerberus_config_proof_u16(hash, (uint16_t)(value >> 16U));
}
static inline uint32_t cerberus_config_fingerprint(
    const cerberus_scan_config_t *config)
{
    if (!config) return 0U;
    uint32_t hash = 2166136261UL;
    hash = cerberus_config_proof_byte(hash, config->protocol_version);
    hash = cerberus_config_proof_byte(hash, config->node_id);
    hash = cerberus_config_proof_u32(hash, config->firmware_version);
    hash = cerberus_config_proof_u32(hash, config->scan_interval_ms);
    hash = cerberus_config_proof_u16(hash, config->dwell_time_ms);
    hash = cerberus_config_proof_u16(hash, config->passive_dwell_time_ms);
    hash = cerberus_config_proof_u16(hash, config->channels_2g_mask);
    hash = cerberus_config_proof_u32(hash, config->channels_5g_mask);
    hash = cerberus_config_proof_u16(hash, config->ble_scan_interval_ms);
    hash = cerberus_config_proof_u16(hash, config->ble_scan_window_ms);
    hash = cerberus_config_proof_byte(hash, (uint8_t)config->min_rssi);
    hash = cerberus_config_proof_byte(hash, config->radio_mode);
    hash = cerberus_config_proof_byte(hash, config->region);
    return cerberus_config_proof_byte(hash, config->scan_enabled ? 1U : 0U);
}

static inline void cerberus_config_proof_store_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8U);
    out[2] = (uint8_t)(value >> 16U);
    out[3] = (uint8_t)(value >> 24U);
}
static inline uint32_t cerberus_config_proof_load_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8U) |
        ((uint32_t)in[2] << 16U) | ((uint32_t)in[3] << 24U);
}
