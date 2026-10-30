#pragma once

/* Receive-only format hints from ONE complete AD report. These bits are not
 * physical-device identity, connectability, a GATT profile, or Sound eligibility.
 * Caller owns report completeness and separate ADV/scan-response provenance. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_BLE_FINDMY_HINT_MAX_LENGTH 255U

enum {
    CERBERUS_BLE_FINDMY_HINT_APPLE = 1U << 0,
    CERBERUS_BLE_FINDMY_HINT_FMNA = 1U << 1,
    CERBERUS_BLE_FINDMY_HINT_DULT = 1U << 2,
};
enum {
    CERBERUS_BLE_FINDMY_UUID16 = 1U << 0,
    CERBERUS_BLE_FINDMY_UUID32 = 1U << 1,
    CERBERUS_BLE_FINDMY_UUID128 = 1U << 2,
};

typedef struct {
    uint8_t bits; /* Independent hints; multiple bits are valid. */
    uint8_t fmna_uuid_widths; /* Exact FD44 UUID-list representations observed. */
} cerberus_ble_findmy_hints_t;

/* Validate the entire length-delimited AD stream, including the suffix after a
 * matching field. True + bits=0 means valid ordinary/empty AD. Invalid input
 * clears output. Zero AD terminators may be followed only by zero padding.
 * Unknown complete AD types are allowed; this is structural, not cryptographic
 * validation. Never join cached bytes or pass an incomplete report here. */
static inline bool cerberus_ble_findmy_hints_parse(const uint8_t *data,
    size_t length, cerberus_ble_findmy_hints_t *out)
{
    static const uint8_t fmna_uuid_le[16] = {
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0x00U,0x00U,0x80U,
        0x00U,0x10U,0x00U,0x00U,0x44U,0xFDU,0x00U,0x00U,
    };
    static const uint8_t dult_uuid_le[16] = {
        0x85U,0x2AU,0x9FU,0x57U,0xC5U,0x2AU,0xEDU,0x88U,
        0x26U,0xC2U,0xF4U,0x12U,0x01U,0x00U,0x19U,0x15U,
    };
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if ((!data && length) || length > CERBERUS_BLE_FINDMY_HINT_MAX_LENGTH) return false;
    cerberus_ble_findmy_hints_t pending = {0};
    const uint8_t *complete_name = NULL;
    size_t complete_name_length = 0U;
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
            for (size_t i = 0U; i < value_length; i += width) {
                bool fmna = width == 16U ? memcmp(value + i, fmna_uuid_le, 16U) == 0 :
                    value[i] == 0x44U && value[i + 1U] == 0xFDU &&
                    (width == 2U || (value[i + 2U] == 0U && value[i + 3U] == 0U));
                if (fmna) {
                    pending.bits |= CERBERUS_BLE_FINDMY_HINT_FMNA;
                    pending.fmna_uuid_widths |= width == 2U ? CERBERUS_BLE_FINDMY_UUID16 :
                        width == 4U ? CERBERUS_BLE_FINDMY_UUID32 : CERBERUS_BLE_FINDMY_UUID128;
                }
                if (width == 16U && memcmp(value + i, dult_uuid_le, 16U) == 0)
                    pending.bits |= CERBERUS_BLE_FINDMY_HINT_DULT;
            }
        } else if (type == 0xFFU) {
            if (value_length < 2U) return false;
            /* Exact manufacturer field: Apple 004C, type 12, length 19,
             * followed by all 25 frame bytes. No substring or name matching. */
            if (value_length == 29U && value[0] == 0x4CU && value[1] == 0U &&
                value[2] == 0x12U && value[3] == 0x19U)
                pending.bits |= CERBERUS_BLE_FINDMY_HINT_APPLE;
        } else if ((type == 0x16U && value_length < 2U) ||
                   (type == 0x20U && value_length < 4U) ||
                   (type == 0x21U && value_length < 16U) ||
                   (type == 0x01U && value_length != 1U)) {
            return false;
        } else if (type == 0x09U && value_length) {
            if (complete_name && (complete_name_length != value_length ||
                memcmp(complete_name, value, value_length) != 0)) return false;
            complete_name = value;
            complete_name_length = value_length;
        }
        offset += field_length;
    }
    *out = pending;
    return true;
}
