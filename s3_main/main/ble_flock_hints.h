#pragma once

/* Receive-only evidence from ONE complete AD report. Specificity levels are
 * broadcast clues, not authenticated device identity, serials or active profiles.
 * Caller supplies completeness and binds separate ADV/response provenance. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_BLE_FLOCK_HINT_MAX_LENGTH 255U

enum {
    CERBERUS_BLE_FLOCK_FS_BATTERY = 1U << 0,
    CERBERUS_BLE_FLOCK_PENGUIN = 1U << 1,
    CERBERUS_BLE_FLOCK_SERIAL10 = 1U << 2, /* Exact decimal name, not a verified serial. */
    CERBERUS_BLE_FLOCK_NO_LOCAL_NAME = 1U << 3, /* No NONEMPTY AD08/09 field. */
    CERBERUS_BLE_FLOCK_MFG_09C8 = 1U << 4,
    CERBERUS_BLE_FLOCK_MFG_PAYLOAD = 1U << 5, /* Payload in an actual 09C8 field. */
};
enum {
    CERBERUS_BLE_FLOCK_LIST16 = 1U << 0,
    CERBERUS_BLE_FLOCK_LIST32 = 1U << 1,
    CERBERUS_BLE_FLOCK_LIST128 = 1U << 2,
    CERBERUS_BLE_FLOCK_DATA16 = 1U << 3,
    CERBERUS_BLE_FLOCK_DATA32 = 1U << 4,
    CERBERUS_BLE_FLOCK_DATA128 = 1U << 5,
};
typedef struct {
    uint8_t facts;
    uint8_t accessory_contexts; /* Exact custom UUID: LIST128 and/or DATA128. */
    uint8_t raven_3100_contexts; /* Per-service contexts, never a cross-product. */
    uint8_t raven_3200_contexts;
} cerberus_ble_flock_hints_t;

typedef enum {
    CERBERUS_BLE_FLOCK_LEVEL_NONE = 0,
    CERBERUS_BLE_FLOCK_LEVEL_CLUE,
    CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE,
    CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED,
} cerberus_ble_flock_level_t;

static inline cerberus_ble_flock_level_t cerberus_ble_flock_hints_level(
    const cerberus_ble_flock_hints_t *hints)
{
    if (!hints) return CERBERUS_BLE_FLOCK_LEVEL_NONE;
    bool battery_name = (hints->facts &
        (CERBERUS_BLE_FLOCK_FS_BATTERY | CERBERUS_BLE_FLOCK_PENGUIN)) != 0U;
    const uint8_t manufacturer = CERBERUS_BLE_FLOCK_MFG_09C8 | CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    if (battery_name && (hints->facts & manufacturer) == manufacturer)
        return CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED;
    if (battery_name || (hints->accessory_contexts &
        (CERBERUS_BLE_FLOCK_LIST128 | CERBERUS_BLE_FLOCK_DATA128)) ||
        ((hints->raven_3100_contexts & 63U) && (hints->raven_3200_contexts & 63U)))
        return CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE;
    if ((hints->facts & CERBERUS_BLE_FLOCK_MFG_09C8) && (hints->facts &
        (CERBERUS_BLE_FLOCK_SERIAL10 | CERBERUS_BLE_FLOCK_NO_LOCAL_NAME)))
        return CERBERUS_BLE_FLOCK_LEVEL_CLUE;
    return CERBERUS_BLE_FLOCK_LEVEL_NONE;
}

/* The parser validates complete field widths before this helper is called. */
static inline void cerberus_ble_flock_service(cerberus_ble_flock_hints_t *hints,
    const uint8_t *value, size_t width, uint8_t context)
{
    static const uint8_t accessory_le[16] = {
        0x6FU,0x2EU,0x17U,0xDFU,0x14U,0x18U,0xE5U,0x9FU,
        0xA8U,0x46U,0x32U,0x95U,0x38U,0xBBU,0xCCU,0xE8U,
    };
    static const uint8_t base_le[12] = {
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,0x00U,0x10U,0x00U,0x00U,
    };
    size_t position = 0U;
    if (width == 16U) {
        if (memcmp(value, accessory_le, sizeof(accessory_le)) == 0)
            hints->accessory_contexts |= context;
        if (memcmp(value, base_le, sizeof(base_le)) != 0 || value[14] || value[15]) return;
        position = 12U;
    } else if (width == 4U) {
        if (value[2] || value[3]) return;
    } else if (width != 2U) return;
    uint16_t service = (uint16_t)value[position] | ((uint16_t)value[position + 1U] << 8U);
    if (service == 0x3100U) hints->raven_3100_contexts |= context;
    if (service == 0x3200U) hints->raven_3200_contexts |= context;
}

static inline bool cerberus_ble_flock_decimal10(const uint8_t *value, size_t length)
{
    if (!value || length != 10U) return false;
    for (size_t i = 0U; i < length; ++i)
        if (value[i] < '0' || value[i] > '9') return false;
    return true;
}

/* Transactional full-stream validation, compatible with other passive parsers.
 * Unknown complete fields and empty lists/names are valid. Identical nonempty
 * complete names coalesce; conflicting nonempty complete names reject the AD.
 * Empty/absent names permit only the weak 09C8 clue. Nonempty shortened/binary
 * names prevent that absence clue and cannot satisfy exact complete-name rules.
 * No byte substring, cached name, adjacent field or other report can corroborate. */
static inline bool cerberus_ble_flock_hints_parse(const uint8_t *data, size_t length,
    cerberus_ble_flock_hints_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if ((!data && length) || length > CERBERUS_BLE_FLOCK_HINT_MAX_LENGTH) return false;
    cerberus_ble_flock_hints_t pending = { .facts = CERBERUS_BLE_FLOCK_NO_LOCAL_NAME };
    const uint8_t *complete_name = NULL;
    size_t complete_length = 0U;
    for (size_t offset = 0U; offset < length;) {
        size_t field_length = data[offset++];
        if (!field_length) {
            while (offset < length) if (data[offset++] != 0U) return false;
            break;
        }
        if (field_length > length - offset) return false;
        uint8_t type = data[offset];
        const uint8_t *value = data + offset + 1U;
        size_t value_length = field_length - 1U;
        if (type >= 0x02U && type <= 0x07U) {
            size_t kind = (type - 2U) / 2U;
            size_t width = kind == 0U ? 2U : kind == 1U ? 4U : 16U;
            if (value_length % width) return false;
            for (size_t i = 0U; i < value_length; i += width)
                cerberus_ble_flock_service(&pending, value + i, width, (uint8_t)(1U << kind));
        } else if (type == 0x16U || type == 0x20U || type == 0x21U) {
            size_t kind = type == 0x16U ? 0U : type == 0x20U ? 1U : 2U;
            size_t width = kind == 0U ? 2U : kind == 1U ? 4U : 16U;
            if (value_length < width) return false;
            cerberus_ble_flock_service(&pending, value, width, (uint8_t)(1U << (kind + 3U)));
        } else if (type == 0xFFU) {
            if (value_length < 2U) return false;
            if (value[0] == 0xC8U && value[1] == 0x09U) {
                pending.facts |= CERBERUS_BLE_FLOCK_MFG_09C8;
                if (value_length > 2U) pending.facts |= CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
            }
        } else if (type == 0x01U && value_length != 1U) {
            return false;
        } else if ((type == 0x08U || type == 0x09U) && value_length) {
            pending.facts &= (uint8_t)~CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
            if (type == 0x09U) {
                if (complete_name && (complete_length != value_length ||
                    memcmp(complete_name, value, value_length) != 0)) return false;
                complete_name = value;
                complete_length = value_length;
            }
        }
        offset += field_length;
    }
    if (complete_name) {
        if (complete_length == 14U && memcmp(complete_name, "FS Ext Battery", 14U) == 0)
            pending.facts |= CERBERUS_BLE_FLOCK_FS_BATTERY;
        if (complete_length == 18U && memcmp(complete_name, "Penguin-", 8U) == 0 &&
            cerberus_ble_flock_decimal10(complete_name + 8U, complete_length - 8U))
            pending.facts |= CERBERUS_BLE_FLOCK_PENGUIN;
        if (cerberus_ble_flock_decimal10(complete_name, complete_length))
            pending.facts |= CERBERUS_BLE_FLOCK_SERIAL10;
    }
    *out = pending;
    return true;
}
