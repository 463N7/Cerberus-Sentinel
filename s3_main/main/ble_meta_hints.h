#pragma once

/* Bounded evidence from one complete BLE report. These values are inherited
 * discovery heuristics, not authenticated vendor/model identities. Keep the
 * identifier AND field context together; unrelated fields cannot be combined
 * into an invented pair. Source identity/time is owned by the scan snapshot. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CERBERUS_BLE_META_ID_COUNT 11U
#define CERBERUS_BLE_META_ALLOWED_ID_COUNT 6U
#define CERBERUS_BLE_META_CONTEXT_COUNT 7U
#define CERBERUS_BLE_META_PAIR_COUNT 77U

typedef enum {
    CERBERUS_BLE_META_MFG16 = 0,
    CERBERUS_BLE_META_LIST16,
    CERBERUS_BLE_META_LIST32,
    CERBERUS_BLE_META_LIST128,
    CERBERUS_BLE_META_DATA16,
    CERBERUS_BLE_META_DATA32,
    CERBERUS_BLE_META_DATA128,
} cerberus_ble_meta_context_t;

typedef struct {
    /* Bit = id_index * 7 + context; low bit first. Upper three bits are zero.
     * A pair occurs at most once even if an AD repeats the same identifier. */
    uint8_t pairs[10];
} cerberus_ble_meta_evidence_t;

static inline uint16_t cerberus_ble_meta_id_value(size_t index)
{
    static const uint16_t ids[CERBERUS_BLE_META_ID_COUNT] = {
        0xFD5FU, 0xFEB7U, 0xFEB8U, 0x01ABU, 0x058EU, 0x0D53U,
        0xFD5AU, 0xFD69U, 0x004CU, 0x0006U, 0xFEF3U,
    };
    return index < CERBERUS_BLE_META_ID_COUNT ? ids[index] : 0U;
}

static inline bool cerberus_ble_meta_evidence_has(
    const cerberus_ble_meta_evidence_t *evidence, size_t id_index,
    cerberus_ble_meta_context_t context)
{
    if (!evidence || id_index >= CERBERUS_BLE_META_ID_COUNT ||
        (unsigned)context >= CERBERUS_BLE_META_CONTEXT_COUNT) return false;
    size_t bit = id_index * CERBERUS_BLE_META_CONTEXT_COUNT + (unsigned)context;
    return (evidence->pairs[bit / 8U] & (uint8_t)(1U << (bit % 8U))) != 0U;
}

static inline bool cerberus_ble_meta_evidence_add(
    cerberus_ble_meta_evidence_t *evidence, uint16_t identifier,
    cerberus_ble_meta_context_t context)
{
    if (!evidence || (unsigned)context >= CERBERUS_BLE_META_CONTEXT_COUNT) return false;
    for (size_t id = 0U; id < CERBERUS_BLE_META_ID_COUNT; ++id) {
        if (cerberus_ble_meta_id_value(id) != identifier) continue;
        size_t bit = id * CERBERUS_BLE_META_CONTEXT_COUNT + (unsigned)context;
        evidence->pairs[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
        return true;
    }
    return false;
}

static inline uint8_t cerberus_ble_meta_evidence_count(
    const cerberus_ble_meta_evidence_t *evidence)
{
    if (!evidence) return 0U;
    uint8_t count = 0U;
    for (size_t bit = 0U; bit < CERBERUS_BLE_META_PAIR_COUNT; ++bit)
        if (evidence->pairs[bit / 8U] & (uint8_t)(1U << (bit % 8U))) ++count;
    return count;
}

static inline bool cerberus_ble_meta_evidence_allowed(
    const cerberus_ble_meta_evidence_t *evidence)
{
    if (!evidence) return false;
    /* The first 6*7 = 42 bits are the allowed identifier rows. */
    for (size_t byte = 0U; byte < 5U; ++byte) if (evidence->pairs[byte]) return true;
    return (evidence->pairs[5] & 0x03U) != 0U;
}

static inline bool cerberus_ble_meta_evidence_blocked(
    const cerberus_ble_meta_evidence_t *evidence)
{
    if (!evidence) return false;
    if (evidence->pairs[5] & 0xFCU) return true;
    for (size_t byte = 6U; byte < 9U; ++byte) if (evidence->pairs[byte]) return true;
    return (evidence->pairs[9] & 0x1FU) != 0U;
}

/* Enumerate exact pairs in stable ID/context order. Failed reads leave the
 * caller's outputs unchanged. No list allocation or received-byte pointers. */
static inline bool cerberus_ble_meta_evidence_at(
    const cerberus_ble_meta_evidence_t *evidence, size_t ordinal,
    uint16_t *identifier, cerberus_ble_meta_context_t *context)
{
    if (!evidence || !identifier || !context || ordinal >= CERBERUS_BLE_META_PAIR_COUNT)
        return false;
    for (size_t id = 0U; id < CERBERUS_BLE_META_ID_COUNT; ++id)
        for (unsigned field = 0U; field < CERBERUS_BLE_META_CONTEXT_COUNT; ++field) {
            if (!cerberus_ble_meta_evidence_has(evidence, id,
                    (cerberus_ble_meta_context_t)field)) continue;
            if (ordinal) { --ordinal; continue; }
            *identifier = cerberus_ble_meta_id_value(id);
            *context = (cerberus_ble_meta_context_t)field;
            return true;
        }
    return false;
}
