#pragma once
#include "mac_observation.h"

#define CERBERUS_MAC_MONITOR_PAGE_ROWS 4U
enum { CERBERUS_MAC_SORT_COUNT, CERBERUS_MAC_SORT_SIGNAL, CERBERUS_MAC_SORT_RECENT };

/* Exact radio/type/address source; latest row time and RSSI include responses
 * and partial BLE reports. Totals are known retained callback observations,
 * not packet/device/person counts. All storage is caller owned. */
typedef struct {
    cerberus_mac_row_t row;
    uint64_t epoch, source_now_us, request_started_us;
    uint32_t snapshot_id, observations, responses;
    uint8_t producer_index;
    bool valid, present, timing_valid;
} cerberus_mac_monitor_record_t;
typedef struct {
    cerberus_mac_monitor_record_t frozen, latest;
    bool valid, present, unavailable;
} cerberus_mac_monitor_selection_t;
typedef struct {
    cerberus_mac_monitor_record_t records[CERBERUS_MAC_CAPACITY];
    cerberus_mac_snapshot_header_t header;
    uint64_t epoch;
    uint32_t publications;
    bool published, unavailable;
} cerberus_mac_monitor_bank_t;
typedef struct {
    cerberus_mac_monitor_bank_t banks[2];
    cerberus_mac_monitor_selection_t selected;
} cerberus_mac_monitor_inventory_t;
typedef struct {
    cerberus_mac_monitor_record_t rows[CERBERUS_MAC_MONITOR_PAGE_ROWS];
    size_t offset;
    uint8_t total, copied, sort;
} cerberus_mac_monitor_page_t;
enum { CERBERUS_MAC_PUBLISHED, CERBERUS_MAC_DUPLICATE, CERBERUS_MAC_INVALID, CERBERUS_MAC_STALE };

void cerberus_mac_monitor_inventory_init(cerberus_mac_monitor_inventory_t *inventory);
/* Every row is validated before commit. Duplicate epoch/snapshot never refreshes
 * time or counters; same incarnation uses cumulative deltas. New epoch permits
 * new incarnation counters. Absent retained identities keep their own ages. */
unsigned cerberus_mac_monitor_inventory_publish(cerberus_mac_monitor_inventory_t *inventory,
    uint8_t radio, uint64_t epoch, const cerberus_mac_snapshot_t *snapshot,
    uint64_t request_started_us, uint64_t received_us, bool timing_valid);
void cerberus_mac_monitor_inventory_unavailable(cerberus_mac_monitor_inventory_t *inventory,
    uint8_t radio, bool unavailable);
bool cerberus_mac_monitor_source_equal(const cerberus_mac_monitor_record_t *a,
    const cerberus_mac_monitor_record_t *b);
bool cerberus_mac_monitor_age(const cerberus_mac_monitor_record_t *record,
    uint64_t now_us, uint64_t *age_ms);
bool cerberus_mac_monitor_inventory_page(const cerberus_mac_monitor_inventory_t *inventory,
    uint8_t sort, size_t offset, uint64_t now_us, cerberus_mac_monitor_page_t *page);
bool cerberus_mac_monitor_inventory_select(cerberus_mac_monitor_inventory_t *inventory,
    const cerberus_mac_monitor_record_t *painted);
void cerberus_mac_monitor_inventory_clear_selection(cerberus_mac_monitor_inventory_t *inventory);
