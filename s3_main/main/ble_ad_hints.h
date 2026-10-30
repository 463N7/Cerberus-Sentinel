#pragma once

/* Shared, allocation-free parsing for scan and analyzer hints. A hint is a
 * statement about one complete AD payload, never authenticated device identity. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ble_meta_hints.h"
#include "ble_hc_hints.h"

#define CERBERUS_BLE_AD_HINT_NAME_MAX 32U
#define CERBERUS_BLE_AD_HINT_MAX_LENGTH 255U

typedef struct {
    uint8_t name[CERBERUS_BLE_AD_HINT_NAME_MAX];
    uint8_t name_length;
    bool complete_name, name_truncated, meta_id, blocked_id;
    cerberus_ble_meta_evidence_t meta;
} cerberus_ble_ad_hints_t;

/* The caller has already validated this field's complete width. Only a
 * zero-extended UUID32 or exact Bluetooth-base UUID128 is a UUID16 equivalent;
 * arbitrary custom128 substrings and payload bytes are not identifiers. */
static inline bool cerberus_ble_ad_hint_uuid16(
    const uint8_t *value, size_t width, uint16_t *identifier)
{
    static const uint8_t base_le[12] = {
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,0x00U,0x10U,0x00U,0x00U,
    };
    if (!value || !identifier) return false;
    size_t position = 0U;
    if (width == 16U) {
        if (memcmp(value, base_le, sizeof(base_le)) != 0 || value[14] || value[15]) return false;
        position = 12U;
    } else if (width == 4U) {
        if (value[2] || value[3]) return false;
    } else if (width != 2U) return false;
    *identifier = (uint16_t)value[position] | ((uint16_t)value[position + 1U] << 8U);
    return true;
}

/* Transactional: every invalid input clears all names and evidence. Complete
 * unknown AD fields are allowed, but known structural widths, Flags length,
 * conflicting complete names and zero padding are validated before commit. */
static inline bool cerberus_ble_ad_hints_parse(const uint8_t *data,
    size_t length, cerberus_ble_ad_hints_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if ((!data && length) || length > CERBERUS_BLE_AD_HINT_MAX_LENGTH) return false;
    cerberus_ble_ad_hints_t pending = {0};
    cerberus_ble_ad_hints_t *parsed = &pending;
    const uint8_t *complete_name = NULL;
    size_t complete_length = 0U;
    for (size_t offset = 0U; offset < length;) {
        const uint8_t field_length = data[offset++];
        if (!field_length) {
            while (offset < length) if (data[offset++] != 0U) return false;
            break;
        }
        if (field_length > length - offset) return false;
        const uint8_t type = data[offset];
        const uint8_t *value = data + offset + 1U;
        const size_t value_length = (size_t)field_length - 1U;
        if ((type == 0x08U || type == 0x09U) && value_length) {
            if (type == 0x09U) {
                if (complete_name && (complete_length != value_length ||
                    memcmp(complete_name, value, value_length) != 0)) return false;
                complete_name = value;
                complete_length = value_length;
            }
            if (type == 0x09U || !parsed->name_length) {
                size_t count = value_length < sizeof(parsed->name) ?
                    value_length : sizeof(parsed->name);
                memcpy(parsed->name, value, count);
                parsed->name_length = (uint8_t)count;
                parsed->name_truncated = value_length > count;
                if (type == 0x09U) parsed->complete_name = true;
            }
        } else if (type >= 0x02U && type <= 0x07U) {
            size_t width = type <= 0x03U ? 2U : type <= 0x05U ? 4U : 16U;
            if (value_length % width) return false;
            cerberus_ble_meta_context_t context = width == 2U ? CERBERUS_BLE_META_LIST16 :
                width == 4U ? CERBERUS_BLE_META_LIST32 : CERBERUS_BLE_META_LIST128;
            for (size_t i = 0U; i < value_length; i += width) {
                uint16_t identifier;
                if (cerberus_ble_ad_hint_uuid16(value + i, width, &identifier))
                    cerberus_ble_meta_evidence_add(&parsed->meta, identifier, context);
            }
        } else if (type == 0xFFU) {
            if (value_length < 2U) return false;
            cerberus_ble_meta_evidence_add(&parsed->meta, (uint16_t)value[0] |
                ((uint16_t)value[1] << 8U), CERBERUS_BLE_META_MFG16);
        } else if (type == 0x16U || type == 0x20U || type == 0x21U) {
            size_t width = type == 0x16U ? 2U : type == 0x20U ? 4U : 16U;
            if (value_length < width) return false;
            cerberus_ble_meta_context_t context = width == 2U ? CERBERUS_BLE_META_DATA16 :
                width == 4U ? CERBERUS_BLE_META_DATA32 : CERBERUS_BLE_META_DATA128;
            uint16_t identifier;
            if (cerberus_ble_ad_hint_uuid16(value, width, &identifier))
                cerberus_ble_meta_evidence_add(&parsed->meta, identifier, context);
        } else if (type == 0x01U && value_length != 1U) {
            return false;
        }
        offset += field_length;
    }
    parsed->meta_id = cerberus_ble_meta_evidence_allowed(&parsed->meta);
    parsed->blocked_id = cerberus_ble_meta_evidence_blocked(&parsed->meta);
    *out = pending;
    return true;
}

static inline bool cerberus_ble_ad_meta_hint(const cerberus_ble_ad_hints_t *parsed)
{
    return parsed && parsed->meta_id && !parsed->blocked_id;
}

/* Use only a successfully parsed complete report. The typed code preserves
 * which exact name matched without adding another AD parser or cached bytes. */
static inline uint8_t cerberus_ble_ad_hc_name_code(const cerberus_ble_ad_hints_t *parsed)
{
    if (!parsed || !parsed->complete_name || parsed->name_truncated ||
        parsed->name_length != 5U) return CERBERUS_BLE_HC_NONE;
    if (memcmp(parsed->name, "HC-03", 5U) == 0) return CERBERUS_BLE_HC_03;
    if (memcmp(parsed->name, "HC-05", 5U) == 0) return CERBERUS_BLE_HC_05;
    if (memcmp(parsed->name, "HC-06", 5U) == 0) return CERBERUS_BLE_HC_06;
    return CERBERUS_BLE_HC_NONE;
}

static inline bool cerberus_ble_ad_hc_name_hint(const cerberus_ble_ad_hints_t *parsed)
{
    return cerberus_ble_ad_hc_name_code(parsed) != CERBERUS_BLE_HC_NONE;
}
