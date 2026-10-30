#pragma once

/* Observational service hints from ONE complete AD report. Variant names
 * label advertised codes, not authenticated hardware/color or an active action.
 * Caller binds report completeness, identity and ADV/response provenance. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_BLE_FLIPPER_HINT_MAX_LENGTH 255U
#define CERBERUS_BLE_FLIPPER_HINT_NAME_MAX 32U

enum {
    CERBERUS_BLE_FLIPPER_HINT_BASE = 1U << 0, /* 3080 + same-report complete name. */
    CERBERUS_BLE_FLIPPER_HINT_BLACK = 1U << 1, /* 3081 advertised variant code. */
    CERBERUS_BLE_FLIPPER_HINT_WHITE = 1U << 2, /* 3082 advertised variant code. */
    CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT = 1U << 3, /* 3083 variant code. */
};
enum {
    CERBERUS_BLE_FLIPPER_UUID16 = 1U << 0,
    CERBERUS_BLE_FLIPPER_UUID32 = 1U << 1,
    CERBERUS_BLE_FLIPPER_UUID128 = 1U << 2,
    CERBERUS_BLE_FLIPPER_NAME = 1U << 3,
    CERBERUS_BLE_FLIPPER_SERVICE_DATA = 1U << 4, /* Named exact UUID-prefix compatibility. */
};

typedef struct {
    uint8_t bits; /* Qualifying service hints; may contain several codes. */
    uint8_t flags; /* Qualifying widths + name + named service-data evidence. */
} cerberus_ble_flipper_hints_t;

static inline bool cerberus_ble_flipper_name_matches(const uint8_t *name, size_t length)
{
    if (!name || length > CERBERUS_BLE_FLIPPER_HINT_NAME_MAX ||
        !((length == 7U && memcmp(name, "Flipper", 7U) == 0) ||
          (length > 8U && memcmp(name, "Flipper ", 8U) == 0))) return false;
    for (size_t i = 0U; i < length; ++i)
        if (name[i] < 32U || name[i] > 126U) return false;
    return true;
}

/* Return 3080..3083 only for exact UUID values. Official Flipper serial
 * advertising uses 16-bit 3080|color. UUID32/base128 normalization is an
 * explicitly supported equivalent encoding, not an observed official output.
 * The distinct custom 128-bit serial GATT service is NOT this advertising UUID. */
static inline uint16_t cerberus_ble_flipper_service_code(const uint8_t *value, size_t width)
{
    static const uint8_t base_le[12] = {
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,0x00U,0x10U,0x00U,0x00U,
    };
    size_t position = 0U;
    if (width == 16U) {
        if (memcmp(value, base_le, sizeof(base_le)) != 0 || value[14] || value[15]) return 0U;
        position = 12U;
    } else if (width == 4U) {
        if (value[2] || value[3]) return 0U;
    } else if (width != 2U) return 0U;
    uint16_t code = (uint16_t)value[position] | ((uint16_t)value[position + 1U] << 8U);
    return code >= 0x3080U && code <= 0x3083U ? code : 0U;
}

/* Validates the entire AD stream, even after a positive field. Invalid input
 * clears output; true + bits=0 means ordinary AD, even if NAME is set. No
 * substring matching or cross-report corroboration. Exact service-data UUID
 * prefixes qualify ONLY with the same-report complete name (legacy behavior).
 * Zero terminators permit zero padding only. Unknown complete fields are
 * allowed; structural validation is not authentication or full vendor decoding. */
static inline bool cerberus_ble_flipper_hints_parse(const uint8_t *data, size_t length,
    cerberus_ble_flipper_hints_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if ((!data && length) || length > CERBERUS_BLE_FLIPPER_HINT_MAX_LENGTH) return false;
    cerberus_ble_flipper_hints_t pending = {0};
    uint8_t base_widths = 0U;
    uint8_t named_data_bits = 0U, named_data_widths = 0U;
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
            size_t width = type <= 0x03U ? 2U : type <= 0x05U ? 4U : 16U;
            if (value_length % width) return false;
            uint8_t width_flag = width == 2U ? CERBERUS_BLE_FLIPPER_UUID16 :
                width == 4U ? CERBERUS_BLE_FLIPPER_UUID32 : CERBERUS_BLE_FLIPPER_UUID128;
            for (size_t i = 0U; i < value_length; i += width) {
                uint16_t code = cerberus_ble_flipper_service_code(value + i, width);
                if (code == 0x3080U) base_widths |= width_flag;
                else if (code) {
                    pending.bits |= (uint8_t)(1U << (code - 0x3080U));
                    pending.flags |= width_flag;
                }
            }
        } else if (type == 0x16U || type == 0x20U || type == 0x21U) {
            size_t width = type == 0x16U ? 2U : type == 0x20U ? 4U : 16U;
            if (value_length < width) return false;
            uint16_t code = cerberus_ble_flipper_service_code(value, width);
            if (code) {
                named_data_bits |= (uint8_t)(1U << (code - 0x3080U));
                named_data_widths |= width == 2U ? CERBERUS_BLE_FLIPPER_UUID16 :
                    width == 4U ? CERBERUS_BLE_FLIPPER_UUID32 : CERBERUS_BLE_FLIPPER_UUID128;
            }
        } else if ((type == 0xFFU && value_length < 2U) ||
                   (type == 0x01U && value_length != 1U)) {
            return false;
        } else if (type == 0x09U && value_length) {
            if (complete_name && (complete_length != value_length ||
                memcmp(complete_name, value, value_length) != 0)) return false;
            complete_name = value;
            complete_length = value_length;
        }
        offset += field_length;
    }
    if (cerberus_ble_flipper_name_matches(complete_name, complete_length)) {
        pending.flags |= CERBERUS_BLE_FLIPPER_NAME;
        if (base_widths) {
            pending.bits |= CERBERUS_BLE_FLIPPER_HINT_BASE;
            pending.flags |= base_widths;
        }
        if (named_data_bits) {
            pending.bits |= named_data_bits;
            pending.flags |= named_data_widths | CERBERUS_BLE_FLIPPER_SERVICE_DATA;
        }
    }
    *out = pending;
    return true;
}
