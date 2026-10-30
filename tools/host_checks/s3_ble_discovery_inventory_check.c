#include "s3_ble_discovery_inventory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static cerberus_s3_ble_discovery_inventory_t inventory, saved;
static cerberus_s3_ble_discovery_bank_t bank, damaged;

static void begin(uint64_t epoch, uint64_t revision, uint64_t start, uint64_t now,
    cerberus_s3_ble_filter_t family, uint8_t count)
{
    memset(&bank, 0, sizeof(bank));
    bank.meta.scan_epoch = epoch;
    bank.meta.revision = revision;
    bank.meta.started_ms = start;
    bank.meta.source_now_ms = now;
    bank.meta.family = family;
    bank.meta.attached = true;
    bank.meta.owned = true;
    bank.meta.status.state = CERBERUS_BLE_SCAN_RUNNING;
    bank.meta.status.generation = (uint8_t)epoch;
    bank.meta.status.stored_count = count;
    bank.meta.status.total_count = count;
}

static void row(unsigned index, unsigned identity, uint8_t type, uint64_t time, uint8_t bits)
{
    cerberus_s3_ble_discovery_record_t *r = &bank.records[index];
    memset(r, 0, sizeof(*r));
    r->index = (uint8_t)index;
    r->generation = bank.meta.status.generation;
    r->observed_ms = (uint32_t)time;
    r->peer.address[0] = (uint8_t)identity;
    r->peer.address[1] = (uint8_t)(identity >> 8U);
    r->peer.address[5] = 0xc0;
    r->peer.address_type = type;
    r->peer.rssi = -40;
    snprintf(r->peer.name, sizeof(r->peer.name), "Peer %04u", identity);
    r->advertisement.address_type = type;
    memcpy(r->advertisement.address, r->peer.address, 6);
    r->advertisement.length = 3;
    r->advertisement.data[0] = 2;
    r->advertisement.data[1] = 1;
    r->advertisement.data[2] = 6;
    r->advertisement.reports_seen = 1;
    r->focus.category_bits = bits;
    cerberus_s3_ble_discovery_evidence_t *d = &r->focus.discovery;
    memcpy(d->address, r->peer.address, 6);
    d->address_type = type;
    d->generation = r->generation;
    d->advertisement_sequence = (uint32_t)(index + 1U);
    d->advertisement_observed_ms = (uint32_t)time;
    d->flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID |
        CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    if (bits & CERBERUS_S3_BLE_FOCUS_FIND_MY) d->findmy_advertisement.bits = CERBERUS_BLE_FINDMY_HINT_FMNA;
    if (bits & CERBERUS_S3_BLE_FOCUS_IBEACON) {
        r->focus.ibeacon_valid = true;
        r->focus.ibeacon_sequence = d->advertisement_sequence;
        r->focus.ibeacon_observed_ms = (uint32_t)time;
    }
}

static int find(unsigned identity, uint8_t type)
{
    for (unsigned i = 0; i < inventory.count; ++i) {
        const cerberus_c5_ble_result_t *r = &inventory.entries[i].record.peer;
        if (r->address[0] == (uint8_t)identity && r->address[1] == (uint8_t)(identity >> 8U) &&
            r->address_type == type) return (int)i;
    }
    return -1;
}

static void apply(void)
{
    CHECK(cerberus_s3_ble_discovery_bank_valid(&bank));
    CHECK(cerberus_s3_ble_discovery_inventory_apply(&inventory, &bank) == CERBERUS_BLE_INVENTORY_APPLIED);
}

static void reject(void)
{
    saved = inventory;
    CHECK(cerberus_s3_ble_discovery_inventory_apply(&inventory, &damaged) == CERBERUS_BLE_INVENTORY_INVALID);
    CHECK(!memcmp(&saved, &inventory, sizeof(saved)));
}

static void lifecycle(void)
{
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_FIND_MY));
    begin(1, 1, 0, 10, CERBERUS_S3_BLE_FILTER_FIND_MY, 2);
    row(0, 10, 0, 0, CERBERUS_S3_BLE_FOCUS_FIND_MY);
    row(1, 10, 1, 5, CERBERUS_S3_BLE_FOCUS_FIND_MY);
    apply();
    CHECK(inventory.count == 2 && inventory.current_mask == 3);
    CHECK(find(10, 0) == 0 && find(10, 1) == 1);
    uint64_t age = 99;
    CHECK(cerberus_s3_ble_discovery_entry_age_ms(&inventory.entries[0], 10, &age) && age == 10);
    CHECK(!cerberus_s3_ble_discovery_entry_age_ms(&inventory.entries[0], 9, &age) && age == 0);
    CHECK(cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_FIND_MY, 30000));
    CHECK(!cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_FIND_MY, 30001));
    cerberus_s3_ble_discovery_entry_t painted, selected;
    CHECK(cerberus_s3_ble_discovery_inventory_copy(&inventory, 0, &painted));
    selected = painted;
    saved = inventory;
    bank.meta.source_now_ms = 20;
    CHECK(cerberus_s3_ble_discovery_inventory_apply(&inventory, &bank) == CERBERUS_BLE_INVENTORY_DUPLICATE);
    CHECK(!memcmp(&saved, &inventory, sizeof(saved)));
    /* A new current response cannot refresh the original raw observation. */
    bank.meta.revision = 2;
    bank.records[0].focus.discovery.findmy_advertisement.bits = 0;
    bank.records[0].focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    bank.records[0].focus.discovery.scan_response_sequence = 3;
    bank.records[0].focus.discovery.scan_response_observed_ms = 20;
    bank.records[0].focus.discovery.findmy_scan_response.bits = CERBERUS_BLE_FINDMY_HINT_DULT;
    apply();
    CHECK(inventory.entries[0].original_source_ms == 0 && inventory.entries[0].record.observed_ms == 0);
    CHECK(cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_FIND_MY, 30020));
    CHECK(!cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_FIND_MY, 30021));
    CHECK(!memcmp(&selected, &painted, sizeof(selected)));
    CHECK(painted.record.focus.discovery.findmy_advertisement.bits == CERBERUS_BLE_FINDMY_HINT_FMNA);
    /* Explicit dequalification deletes only the exact typed key. */
    bank.meta.revision = 3;
    bank.meta.status.stored_count = 1;
    bank.records[0].focus.category_bits = 0;
    bank.records[0].focus.discovery.flags = 0;
    apply();
    CHECK(inventory.count == 1 && find(10, 0) < 0 && find(10, 1) == 0);
    CHECK(!inventory.current_mask);
    CHECK(inventory.entries[0].source_now_ms == 20);
    begin(2, 1, 30, 40, CERBERUS_S3_BLE_FILTER_FIND_MY, 0);
    apply();
    CHECK(inventory.count == 1 && !inventory.current_mask);
    CHECK(inventory.entries[0].epoch == 1 && inventory.entries[0].original_source_ms == 5);
    CHECK(!memcmp(&selected, &painted, sizeof(selected)));
    /* Family changes are explicit. A malformed new-family bank cannot clear history. */
    damaged = bank;
    damaged.meta.family = CERBERUS_S3_BLE_FILTER_FLIPPER;
    reject();
    damaged.meta.status.stored_count = 33;
    CHECK(!cerberus_s3_ble_discovery_bank_valid(&damaged));
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_FLIPPER));
    CHECK(!inventory.count && inventory.family == CERBERUS_S3_BLE_FILTER_FLIPPER);
}

static void atomic_validation(void)
{
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_ALL));
    begin(300, 9, 1000, 1100, CERBERUS_S3_BLE_FILTER_ALL, 2);
    row(0, 1, 0, 1000, 0);
    row(1, 2, 3, 1099, 0);
    apply();
#define BAD(change) do { damaged = bank; change; reject(); } while (0)
    BAD(damaged.meta.scan_epoch = 0);
    BAD(damaged.meta.scan_epoch--);
    BAD(damaged.meta.revision--);
    BAD(damaged.meta.attached = false);
    BAD(damaged.meta.source_now_ms = 1099);
    BAD(damaged.meta.started_ms++);
    BAD(damaged.meta.status.generation++);
    BAD(damaged.meta.status.state = CERBERUS_BLE_SCAN_ERROR);
    BAD(damaged.meta.status.unnamed_count = 3);
    BAD(damaged.meta.status.stored_count = 33);
    BAD(damaged.records[1].index = 0);
    BAD(damaged.records[1].generation++);
    BAD(damaged.records[1].peer.address_type = 4);
    BAD(damaged.records[1].advertisement.address[0]++);
    BAD(damaged.records[1].advertisement.address_type = 0);
    BAD(memset(damaged.records[1].peer.name, 'A', sizeof(damaged.records[1].peer.name)));
    BAD(damaged.records[1].peer.uuid_length = 17);
    BAD(damaged.records[1].peer.uuid_length = 3);
    BAD(damaged.records[1].peer.flags = 128);
    BAD(damaged.records[1].advertisement.length = 32);
    BAD(damaged.records[1].advertisement.flags = 128);
    BAD(damaged.records[1].focus.category_bits = 128);
    BAD(damaged.records[1].focus.discovery.flags = 128);
    BAD(damaged.records[1].focus.discovery.address[0]++);
    BAD(damaged.records[1].focus.discovery.generation++);
    BAD(damaged.records[1].observed_ms = 1101);
    BAD(damaged.records[1].observed_ms = 999);
    BAD(damaged.records[1].focus.discovery.advertisement_observed_ms = 1101);
    BAD(damaged.records[1].focus.discovery.advertisement_sequence = 0);
    BAD(damaged.records[1].focus.discovery.flags &= ~CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE);
    BAD(damaged.records[1].focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_SID_VALID; damaged.records[1].focus.discovery.sid = 16);
    BAD(damaged.records[1].focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_SID_VALID | CERBERUS_S3_BLE_EVIDENCE_SID_INVALID);
    BAD(damaged.records[1].focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID);
    BAD(damaged.records[1].focus.ibeacon_valid = true);
    BAD(damaged.records[1] = damaged.records[0]; damaged.records[1].index = 1);
    BAD(damaged.meta.revision++; damaged.records[1].observed_ms = 1098);
    /* Validation checks all rows before accepting an earlier valid update. */
    BAD(damaged.meta.revision++; damaged.records[0].peer.rssi = -9; damaged.records[1].advertisement.length = 255);
    saved = inventory;
    CHECK(!cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_COUNT));
    CHECK(!memcmp(&saved, &inventory, sizeof(saved)));
    /* Raw malformed reports remain useful ALL rows, never active eligibility. */
    bank.meta.revision++;
    bank.records[0].advertisement.flags = CERBERUS_BLE_ADVERTISEMENT_MALFORMED;
    bank.records[0].advertisement.event_type = 255;
    bank.records[0].focus.discovery.flags = 0;
    apply();
    CHECK(inventory.entries[0].record.advertisement.flags == CERBERUS_BLE_ADVERTISEMENT_MALFORMED);
#undef BAD
}

static void clocks_and_generations(void)
{
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_IBEACON));
    const uint64_t wrap = UINT64_C(1) << 32;
    begin(255, 1, wrap - 50, wrap + 50, CERBERUS_S3_BLE_FILTER_IBEACON, 1);
    row(0, 1, 0, wrap - 1, CERBERUS_S3_BLE_FOCUS_IBEACON);
    apply();
    CHECK(inventory.entries[0].original_source_ms == wrap - 1);
    CHECK(cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_IBEACON, wrap + 29999));
    CHECK(!cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_IBEACON, wrap + 30000));
    CHECK(!cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_IBEACON, 2 * wrap - 1));
    /* A later ordinary original preserves the older iBeacon tuple and own age. */
    bank.meta.revision++;
    bank.records[0].observed_ms = 40;
    bank.records[0].focus.discovery.advertisement_sequence = 5;
    bank.records[0].focus.discovery.advertisement_observed_ms = 40;
    apply();
    CHECK(!cerberus_s3_ble_ibeacon_is_current(&inventory.entries[0].record.focus));
    CHECK(inventory.entries[0].original_source_ms == wrap + 40);
    CHECK(cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_IBEACON, wrap + 29999));
    for (unsigned i = 256; i < 800; ++i) {
        begin(i, 1, wrap + i, wrap + i + 1U, CERBERUS_S3_BLE_FILTER_IBEACON, 1);
        row(0, 1, 0, wrap + i, CERBERUS_S3_BLE_FOCUS_IBEACON);
        apply();
        CHECK(inventory.generation == (uint8_t)i && inventory.epoch == i && inventory.count == 1);
    }
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_IBEACON));
    begin(1, 1, 0, wrap - 1, CERBERUS_S3_BLE_FILTER_IBEACON, 1);
    row(0, 1, 0, 0, CERBERUS_S3_BLE_FOCUS_IBEACON);
    apply();
    CHECK(!cerberus_s3_ble_discovery_entry_recent(&inventory.entries[0], CERBERUS_S3_BLE_FILTER_IBEACON, wrap));
    damaged = bank;
    damaged.meta.source_now_ms = wrap;
    reject();
    /* Near uint64 maximum uses subtraction only and zero is never a sentinel. */
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_ALL));
    begin(UINT64_MAX, UINT64_MAX, UINT64_MAX - 100, UINT64_MAX, CERBERUS_S3_BLE_FILTER_ALL, 1);
    row(0, 1, 0, UINT64_MAX - 50, 0);
    apply();
    uint64_t age;
    CHECK(cerberus_s3_ble_discovery_entry_age_ms(&inventory.entries[0], UINT64_MAX, &age) && age == 50);
}

static void capacity_and_order(void)
{
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_ALL));
    begin(1, 1, 0, 100, CERBERUS_S3_BLE_FILTER_ALL, 32);
    for (unsigned i = 0; i < 32; ++i) row(i, i, 0, i, 0);
    apply();
    CHECK(inventory.current_mask == UINT32_MAX && inventory.count == 32);
    cerberus_s3_ble_discovery_entry_t painted[4];
    for (unsigned i = 0; i < 4; ++i) CHECK(cerberus_s3_ble_discovery_inventory_copy(&inventory, (uint8_t)i, &painted[i]));
    begin(2, 1, 100, 101, CERBERUS_S3_BLE_FILTER_ALL, 2);
    row(0, 100, 0, 100, 0);
    row(1, 0, 0, 100, 0); /* Existing oldest is current, cannot be a victim. */
    apply();
    CHECK(find(0, 0) == 0 && find(100, 0) == 1 && find(1, 0) < 0);
    CHECK(inventory.replacements == 1 && inventory.current_mask == 3);
    for (unsigned i = 0; i < 4; ++i) CHECK(painted[i].record.peer.address[0] == i && painted[i].epoch == 1);
    /* Many rotations replace deterministic oldest history; all current survive. */
    for (unsigned epoch = 3; epoch < 100; ++epoch) {
        begin(epoch, 1, epoch * 100U, epoch * 100U + 99U, CERBERUS_S3_BLE_FILTER_ALL, 32);
        for (unsigned i = 0; i < 32; ++i) row(i, epoch * 32U + (31U - i), 0, epoch * 100U + i, 0);
        apply();
        CHECK(inventory.count == 32 && inventory.current_mask == UINT32_MAX);
        for (unsigned i = 0; i < 32; ++i) CHECK(find(epoch * 32U + i, 0) >= 0);
    }
    inventory.replacements = UINT32_MAX;
    begin(100, 1, 10000, 10010, CERBERUS_S3_BLE_FILTER_ALL, 1);
    row(0, 60000, 0, 10000, 0);
    apply();
    CHECK(inventory.replacements == UINT32_MAX);
    /* Equal original times select lowest noncurrent slot consistently. */
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_ALL));
    begin(1, 1, 0, 0, CERBERUS_S3_BLE_FILTER_ALL, 32);
    for (unsigned i = 0; i < 32; ++i) row(i, i, 0, 0, 0);
    apply();
    begin(2, 1, 1, 1, CERBERUS_S3_BLE_FILTER_ALL, 1);
    row(0, 500, 0, 1, 0);
    apply();
    CHECK(find(500, 0) == 0 && find(0, 0) < 0);
}

static void pages(void)
{
    CHECK(cerberus_s3_ble_discovery_inventory_init(&inventory, CERBERUS_S3_BLE_FILTER_ALL));
    begin(1, 1, 0, 0, CERBERUS_S3_BLE_FILTER_ALL, 6);
    const char *names[] = {"z", "b", "A", "a", "c", "d"};
    const int signals[] = {-80, -40, -40, -40, 127, -128};
    for (unsigned i = 0; i < 6; ++i) {
        row(i, i, 0, 0, 0);
        strcpy(bank.records[i].peer.name, names[i]);
        bank.records[i].peer.rssi = (int8_t)signals[i];
    }
    apply();
    const uint8_t expected[3][6] = {{2,3,1,0,4,5},{2,3,1,4,5,0},{0,1,2,3,4,5}};
    saved = inventory;
    for (unsigned sort = 0; sort < 3; ++sort) for (uint32_t mask = 0; mask < 64; ++mask) {
        uint8_t want[6]; size_t count = 0;
        for (unsigned i = 0; i < 6; ++i) if (mask & (1U << expected[sort][i])) want[count++] = expected[sort][i];
        for (size_t offset = 0; offset < 8; ++offset) for (size_t capacity = 0; capacity < 8; ++capacity) {
            uint8_t indexes[10]; memset(indexes, 0xa5, sizeof(indexes));
            CHECK(cerberus_s3_ble_discovery_inventory_page_sorted(&inventory, (cerberus_s3_ble_sort_t)sort,
                mask, offset, indexes + 1, capacity) == count);
            size_t copied = offset < count ? count - offset : 0;
            if (copied > capacity) copied = capacity;
            for (size_t i = 0; i < copied; ++i) CHECK(indexes[i + 1] == want[offset + i]);
            CHECK(indexes[0] == 0xa5 && indexes[copied + 1] == 0xa5);
        }
    }
    CHECK(!memcmp(&saved, &inventory, sizeof(saved)));
    uint8_t untouched = 99;
    CHECK(!cerberus_s3_ble_discovery_inventory_page_sorted(&inventory, CERBERUS_S3_BLE_SORT_COUNT, UINT32_MAX, 0, &untouched, 1));
    CHECK(untouched == 99);
    cerberus_s3_ble_discovery_entry_t output;
    memset(&output, 0xff, sizeof(output));
    CHECK(!cerberus_s3_ble_discovery_inventory_copy(&inventory, 32, &output));
    for (size_t i = 0; i < sizeof(output); ++i) CHECK(((uint8_t *)&output)[i] == 0);
}

int main(void)
{
    lifecycle();
    atomic_validation();
    clocks_and_generations();
    capacity_and_order();
    pages();
    printf("BLE discovery inventory: %u checks passed; inventory=%zu entry=%zu bytes\n", checks,
        sizeof(inventory), sizeof(cerberus_s3_ble_discovery_entry_t));
    return 0;
}
