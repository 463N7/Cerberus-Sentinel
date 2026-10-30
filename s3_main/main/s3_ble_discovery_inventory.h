#pragma once

/* UI-owned bounded history; no allocation, radio calls or report parsing.
 * One proof per typed address is retained. Missing rows become EARLIER;
 * an explicitly present dequalified row retires that address's old proof. */
#include "s3_ble_discovery.h"
#include "s3_ble_list.h"
#include "s3_ble_recent.h"

typedef struct {
    cerberus_s3_ble_discovery_record_t record;
    uint64_t epoch;
    uint64_t source_now_ms; /* Proof-copy ceiling, never a source timestamp. */
    uint64_t original_source_ms; /* Unambiguous lift of record.observed_ms. */
} cerberus_s3_ble_discovery_entry_t;

typedef struct {
    cerberus_s3_ble_discovery_entry_t entries[CERBERUS_BLE_SCAN_MAX_RESULTS];
    uint64_t epoch, revision, started_ms, source_now_ms;
    uint32_t current_mask, replacements;
    cerberus_s3_ble_filter_t family;
    uint8_t count, generation;
    bool initialized;
} cerberus_s3_ble_discovery_inventory_t;

typedef enum {
    CERBERUS_BLE_INVENTORY_INVALID = -1,
    CERBERUS_BLE_INVENTORY_DUPLICATE = 0,
    CERBERUS_BLE_INVENTORY_APPLIED = 1,
} cerberus_s3_ble_discovery_apply_t;

bool cerberus_s3_ble_discovery_inventory_init(
    cerberus_s3_ble_discovery_inventory_t *, cerberus_s3_ble_filter_t);
/* Validate before replacing a different family's inventory. Every source
 * time must fit one <2^32-ms epoch interval; malformed banks never mutate it. */
bool cerberus_s3_ble_discovery_bank_valid(const cerberus_s3_ble_discovery_bank_t *);
cerberus_s3_ble_discovery_apply_t cerberus_s3_ble_discovery_inventory_apply(
    cerberus_s3_ble_discovery_inventory_t *, const cerberus_s3_ble_discovery_bank_t *);

/* False on future/rewound local time. Zero is a genuine original source time.
 * These helpers never authorize an action or replace its raw-packet gate. */
bool cerberus_s3_ble_discovery_entry_age_ms(const cerberus_s3_ble_discovery_entry_t *,
    uint64_t now_ms, uint64_t *age_ms);
bool cerberus_s3_ble_discovery_entry_recent(const cerberus_s3_ble_discovery_entry_t *,
    cerberus_s3_ble_filter_t, uint64_t now_ms);
bool cerberus_s3_ble_discovery_inventory_current(
    const cerberus_s3_ble_discovery_inventory_t *, uint8_t index);
bool cerberus_s3_ble_discovery_inventory_copy(
    const cerberus_s3_ble_discovery_inventory_t *, uint8_t index,
    cerberus_s3_ble_discovery_entry_t *out);
/* Stable sort of indexes using the existing BLE comparator; no payload copy.
 * The caller owns painted entry copies, so a newer bank cannot retarget a tap. */
size_t cerberus_s3_ble_discovery_inventory_page_sorted(
    const cerberus_s3_ble_discovery_inventory_t *, cerberus_s3_ble_sort_t,
    uint32_t visible_mask, size_t offset, uint8_t *indexes, size_t capacity);
