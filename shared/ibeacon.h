#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* One complete AD container, independent of the bounded legacy raw copy.
 * Normal legacy advertising fits 31 bytes; accepting a complete extended
 * container does not assert that any particular beacon emits that format.
 * No scan responses or separate reports are concatenated here. */
enum { CERBERUS_IBEACON_AD_MAX = 255U };

typedef struct {
    uint8_t uuid[16];
    uint16_t major,minor; /* decoded network byte order */
    int8_t measured_power_dbm;
} cerberus_ibeacon_t;

typedef enum {
    CERBERUS_IBEACON_NONE = 0,
    CERBERUS_IBEACON_FOUND = 1,
    CERBERUS_IBEACON_MALFORMED = 2,
} cerberus_ibeacon_parse_result_t;

/* Apple manufacturer data: company 0x004c, type 0x02, length 0x15,
 * 16-byte UUID, major/minor in network byte order, measured power signed.
 * A valid iBeacon manufacturer AD structure has exactly 25 value bytes.
 * Every non-FOUND result clears `out`. The complete AD stream must be valid
 * before output commits. Identical exact fields coalesce; conflicting exact
 * fields (including power) reject this iBeacon interpretation. Such ambiguity
 * is separate from the structural validity used by other passive classifiers.
 * Advertised UUID/major/minor/power are not authenticated device identity. */
static inline cerberus_ibeacon_parse_result_t cerberus_ibeacon_parse_ad(
    const uint8_t *advertisement, size_t length, cerberus_ibeacon_t *out)
{
    if (!out) return CERBERUS_IBEACON_MALFORMED;
    memset(out, 0, sizeof(*out));
    if ((!advertisement && length) || length > CERBERUS_IBEACON_AD_MAX)
        return CERBERUS_IBEACON_MALFORMED;
    const uint8_t *beacon_value = NULL, *complete_name = NULL;
    size_t complete_name_length = 0U;
    for (size_t offset = 0U; offset < length;) {
        uint8_t field_length = advertisement[offset++];
        if (!field_length) {
            for (; offset < length; ++offset) if (advertisement[offset] != 0U)
                return CERBERUS_IBEACON_MALFORMED;
            break;
        }
        if (field_length > length - offset) return CERBERUS_IBEACON_MALFORMED;
        uint8_t type = advertisement[offset];
        const uint8_t *value = advertisement + offset + 1U;
        size_t value_length = (size_t)field_length - 1U;
        if (type >= 0x02U && type <= 0x07U) {
            size_t width = type <= 0x03U ? 2U : type <= 0x05U ? 4U : 16U;
            if (value_length % width) return CERBERUS_IBEACON_MALFORMED;
        } else if (type == 0xFFU) {
            if (value_length < 2U) return CERBERUS_IBEACON_MALFORMED;
            if (value_length == 25U && value[0] == 0x4CU && value[1] == 0U &&
                value[2] == 0x02U && value[3] == 0x15U) {
                if (beacon_value && memcmp(beacon_value, value, 25U) != 0)
                    return CERBERUS_IBEACON_MALFORMED;
                beacon_value = value;
            }
        } else if ((type == 0x16U && value_length < 2U) ||
                   (type == 0x20U && value_length < 4U) ||
                   (type == 0x21U && value_length < 16U) ||
                   (type == 0x01U && value_length != 1U)) {
            return CERBERUS_IBEACON_MALFORMED;
        } else if (type == 0x09U && value_length) {
            if (complete_name && (complete_name_length != value_length ||
                memcmp(complete_name, value, value_length) != 0))
                return CERBERUS_IBEACON_MALFORMED;
            complete_name = value;
            complete_name_length = value_length;
        }
        offset += (size_t)field_length;
    }
    if (!beacon_value) return CERBERUS_IBEACON_NONE;
    memcpy(out->uuid, beacon_value + 4U, sizeof(out->uuid));
    out->major = (uint16_t)((uint16_t)beacon_value[20] << 8U) | beacon_value[21];
    out->minor = (uint16_t)((uint16_t)beacon_value[22] << 8U) | beacon_value[23];
    /* Decode the signed wire byte without an out-of-range uint8->int8 cast. */
    int power = beacon_value[24];
    out->measured_power_dbm = (int8_t)(power < 128 ? power : power - 256);
    return CERBERUS_IBEACON_FOUND;
}
