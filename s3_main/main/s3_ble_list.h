#pragma once

/* Presentation-only ordering for one frozen BLE sweep. Return original scan
 * indexes; never move the peer records or change their captured evidence. */
#include "c5_spi_link.h"
#include "s3_ble_snapshot.h"
#include "ble_rssi.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    CERBERUS_S3_BLE_SORT_SIGNAL_DESC = 0,
    CERBERUS_S3_BLE_SORT_NAME_ASC,
    CERBERUS_S3_BLE_SORT_DISCOVERY,
    CERBERUS_S3_BLE_SORT_COUNT,
} cerberus_s3_ble_sort_t;

/* The screen font is ASCII. Keep original name bytes in the frozen record;
 * sanitize only this display label and bound all reads even for a damaged
 * nonterminated record. Unnamed peers use their exact displayed address. */
static inline size_t cerberus_s3_ble_list_label(
    const cerberus_c5_ble_result_t *peer, char *out, size_t capacity)
{
    if (!peer || (!out && capacity)) return 0U;
    size_t length = 0U;
    if (peer->name[0]) {
        while (length < CERBERUS_BLE_NAME_MAX_LENGTH && peer->name[length]) {
            unsigned char value = (unsigned char)peer->name[length];
            if (length + 1U < capacity)
                out[length] = value >= 0x20U && value <= 0x7eU ? (char)value : '?';
            ++length;
        }
    } else {
        static const char hex[] = "0123456789ABCDEF";
        for (size_t i = 0U; i < 6U; ++i) {
            uint8_t byte = peer->address[5U - i];
            if (length + 1U < capacity) out[length] = hex[byte >> 4U];
            ++length;
            if (length + 1U < capacity) out[length] = hex[byte & 0x0fU];
            ++length;
            if (i != 5U) {
                if (length + 1U < capacity) out[length] = ':';
                ++length;
            }
        }
    }
    if (capacity) out[length < capacity ? length : capacity - 1U] = '\0';
    return length;
}

static inline unsigned char cerberus_s3_ble_list_fold(unsigned char value)
{
    return value >= 'A' && value <= 'Z' ? (unsigned char)(value + ('a' - 'A')) : value;
}

static inline int cerberus_s3_ble_list_name_compare(
    const cerberus_c5_ble_result_t *left, const cerberus_c5_ble_result_t *right)
{
    char left_label[CERBERUS_BLE_NAME_MAX_LENGTH + 1U];
    char right_label[CERBERUS_BLE_NAME_MAX_LENGTH + 1U];
    size_t left_length = cerberus_s3_ble_list_label(left, left_label, sizeof(left_label));
    size_t right_length = cerberus_s3_ble_list_label(right, right_label, sizeof(right_label));
    size_t length = left_length < right_length ? left_length : right_length;
    for (size_t i = 0U; i < length; ++i) {
        unsigned char a = cerberus_s3_ble_list_fold((unsigned char)left_label[i]);
        unsigned char b = cerberus_s3_ble_list_fold((unsigned char)right_label[i]);
        if (a != b) return a < b ? -1 : 1;
    }
    return left_length < right_length ? -1 : left_length > right_length ? 1 : 0;
}

static inline bool cerberus_s3_ble_list_before(
    const cerberus_c5_ble_result_t *left, const cerberus_c5_ble_result_t *right,
    cerberus_s3_ble_sort_t sort)
{
    if (sort == CERBERUS_S3_BLE_SORT_DISCOVERY) return false;
    if (sort == CERBERUS_S3_BLE_SORT_SIGNAL_DESC) {
        /* HCI RSSI 127 means unavailable; -128 is outside the accepted range.
         * Such values must not rank above a real measured signal. */
        bool left_valid = cerberus_ble_rssi_valid(left->rssi);
        bool right_valid = cerberus_ble_rssi_valid(right->rssi);
        if (left_valid != right_valid) return left_valid;
        if (left_valid && left->rssi != right->rssi) return left->rssi > right->rssi;
    }
    return cerberus_s3_ble_list_name_compare(left, right) < 0;
}

/* Intersect the original-index visibility mask with the focus category, return
 * the total visible focused matches, and copy one sorted page of their original
 * indexes. Equal sort keys retain discovery order. Input is never mutated.
 * Zero-capacity calls count matches; invalid arguments return zero and leave
 * output untouched. Only 32 bytes of bounded index workspace are needed. */
static inline size_t cerberus_s3_ble_focus_page_sorted_masked(
    const cerberus_c5_ble_result_t *results, const uint8_t *category_bits,
    size_t count, cerberus_s3_ble_filter_t filter, cerberus_s3_ble_sort_t sort,
    uint32_t visible_mask, size_t offset, uint8_t *indexes, size_t capacity)
{
    if (count > 32U || count > CERBERUS_BLE_SCAN_MAX_RESULTS ||
        (count && (!results || !category_bits)) || (capacity && !indexes) ||
        (unsigned)filter >= CERBERUS_S3_BLE_FILTER_COUNT ||
        (unsigned)sort >= CERBERUS_S3_BLE_SORT_COUNT) return 0U;
    uint8_t wanted = filter == CERBERUS_S3_BLE_FILTER_ALL ? 0U :
        (uint8_t)(1U << ((unsigned)filter - 1U));
    uint8_t ordered[CERBERUS_BLE_SCAN_MAX_RESULTS];
    size_t matched = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (!(visible_mask & (UINT32_C(1) << i))) continue;
        if (wanted && !(category_bits[i] & wanted)) continue;
        size_t position = matched;
        while (position && cerberus_s3_ble_list_before(&results[i],
                &results[ordered[position - 1U]], sort)) {
            ordered[position] = ordered[position - 1U];
            --position;
        }
        ordered[position] = (uint8_t)i;
        ++matched;
    }
    if (offset < matched) {
        size_t remaining = matched - offset;
        size_t copied = capacity < remaining ? capacity : remaining;
        for (size_t i = 0U; i < copied; ++i) indexes[i] = ordered[offset + i];
    }
    return matched;
}

/* Existing callers retain their all-visible behavior. */
static inline size_t cerberus_s3_ble_focus_page_sorted(
    const cerberus_c5_ble_result_t *results, const uint8_t *category_bits,
    size_t count, cerberus_s3_ble_filter_t filter, cerberus_s3_ble_sort_t sort,
    size_t offset, uint8_t *indexes, size_t capacity)
{
    return cerberus_s3_ble_focus_page_sorted_masked(results, category_bits, count,
        filter, sort, UINT32_MAX, offset, indexes, capacity);
}
