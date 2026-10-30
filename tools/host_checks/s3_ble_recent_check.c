#undef NDEBUG
#include "s3_ble_recent.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)

static cerberus_s3_ble_focus_t original(uint32_t observed)
{
    cerberus_s3_ble_focus_t focus = {0};
    focus.discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
    focus.discovery.advertisement_sequence = 3U;
    focus.discovery.advertisement_observed_ms = observed;
    focus.discovery.findmy_advertisement.bits = CERBERUS_BLE_FINDMY_HINT_APPLE;
    return focus;
}

static void check_support_and_boundaries(void)
{
    CHECK(CERBERUS_S3_BLE_RECENT_WINDOW_MS == 30000U);
    for (int value = -1; value <= CERBERUS_S3_BLE_FILTER_COUNT; ++value) {
        bool supported = value == CERBERUS_S3_BLE_FILTER_FIND_MY || value == CERBERUS_S3_BLE_FILTER_IBEACON;
        CHECK(cerberus_s3_ble_recent_supported((cerberus_s3_ble_filter_t)value) == supported);
        CHECK(!cerberus_s3_ble_focus_recent(NULL, (cerberus_s3_ble_filter_t)value, 0U));
        cerberus_s3_ble_focus_t f = original(0U);
        if (!supported) CHECK(!cerberus_s3_ble_focus_recent(&f, (cerberus_s3_ble_filter_t)value, 1U));
    }
    const uint32_t sources[] = {0U, 1000U, UINT32_MAX - 1000U, UINT32_MAX};
    const uint32_t deltas[] = {0U, 1U, 29999U, 30000U, 30001U, 60000U, UINT32_MAX / 2U, UINT32_MAX};
    for (size_t source = 0U; source < sizeof(sources)/sizeof(sources[0]); ++source) {
        cerberus_s3_ble_focus_t f = original(sources[source]);
        f.ibeacon_valid = true; f.ibeacon_sequence = 2U; f.ibeacon_observed_ms = sources[source];
        cerberus_s3_ble_focus_t frozen = f;
        for (size_t delta = 0U; delta < sizeof(deltas)/sizeof(deltas[0]); ++delta) {
            uint32_t now = sources[source] + deltas[delta];
            bool expected = delta < 4U;
            CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, now) == expected);
            CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, now) == expected);
            CHECK(!memcmp(&f, &frozen, sizeof(f)));
        }
    }
}

static void check_findmy_validity(void)
{
    /* Every flag combination: only completeness+validity admits the original;
     * RAW_COMPLETE is deliberately not needed for a complete extended source. */
    for (unsigned flags = 0U; flags <= UINT8_MAX; ++flags) {
        cerberus_s3_ble_focus_t f = original(1000U);
        f.discovery.flags = (uint8_t)flags;
        bool valid = (flags & 3U) == 3U && !(flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID);
        CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 31000U) == valid);
        f.discovery.advertisement_sequence = 0U;
        CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 1000U));
    }
    for (unsigned bits = 0U; bits <= UINT8_MAX; ++bits) {
        cerberus_s3_ble_focus_t f = original(1000U);
        f.discovery.findmy_advertisement.bits = (uint8_t)bits;
        CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 1000U) == ((bits & 7U) != 0U));
        f.discovery.findmy_advertisement.bits = 0U;
        f.discovery.findmy_advertisement.fmna_uuid_widths = 7U;
        f.category_bits = CERBERUS_S3_BLE_FOCUS_FIND_MY;
        f.findmy_evidence = "old cached label";
        CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 1000U));
    }
    cerberus_s3_ble_focus_t f = original(1000U);
    f.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_SID_VALID;
    for (unsigned sid = 0U; sid <= UINT8_MAX; ++sid) {
        f.discovery.sid = (uint8_t)sid;
        CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 1000U) == (sid <= 15U));
    }
}

static void check_response_sources(void)
{
    cerberus_s3_ble_focus_t f = original(1000U);
    f.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    f.discovery.scan_response_sequence = 4U;
    f.discovery.scan_response_observed_ms = 2000U;
    f.discovery.findmy_scan_response.bits = CERBERUS_BLE_FINDMY_HINT_DULT;
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 31001U)); /* ADV expired; RSP fresh. */
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 32000U));
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 32001U));
    f.discovery.findmy_advertisement.bits = 0U;
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 31001U)); /* Response-only hint. */
    for (unsigned flag = 0U; flag < 8U; ++flag) {
        cerberus_s3_ble_focus_t invalid = f;
        if (flag == 0U) invalid.discovery.flags &= (uint8_t)~CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
        if (flag == 1U) invalid.discovery.flags &= (uint8_t)~CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE;
        if (flag == 2U) invalid.discovery.flags &= (uint8_t)~CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
        if (flag == 3U) invalid.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_SID_INVALID;
        if (flag == 4U) invalid.discovery.advertisement_sequence = 0U;
        if (flag == 5U) invalid.discovery.scan_response_sequence = 0U;
        if (flag == 6U) invalid.discovery.scan_response_sequence = invalid.discovery.advertisement_sequence;
        if (flag == 7U) invalid.discovery.scan_response_sequence = invalid.discovery.advertisement_sequence - 1U;
        CHECK(!cerberus_s3_ble_focus_recent(&invalid, CERBERUS_S3_BLE_FILTER_FIND_MY, 31001U));
    }
    f.discovery.findmy_scan_response.bits = 0x80U;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 2000U));
    f.discovery.findmy_scan_response.bits = CERBERUS_BLE_FINDMY_HINT_FMNA;
    f.discovery.scan_response_observed_ms = 5001U;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 5000U));
    /* An expired qualifying ADV cannot borrow a newer ordinary response time. */
    f = original(1000U);
    f.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    f.discovery.scan_response_sequence = 4U; f.discovery.scan_response_observed_ms = 2000U;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 31001U));
    /* Coherent response across uint32 wrap, including genuine response time0. */
    f = original(UINT32_MAX - 10U); f.discovery.findmy_advertisement.bits = 0U;
    f.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    f.discovery.scan_response_sequence = 4U; f.discovery.scan_response_observed_ms = 0U;
    f.discovery.findmy_scan_response.bits = CERBERUS_BLE_FINDMY_HINT_APPLE;
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 30000U));
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 30001U));
}

static void check_retained_and_replacement(void)
{
    cerberus_s3_ble_focus_t f = original(1000U);
    f.ibeacon_valid = true; f.ibeacon_sequence = 2U; f.ibeacon_observed_ms = 1000U;
    /* The current original is ordinary. Retained iBeacon is explicitly EARLIER
     * and still eligible until its own age expires. It borrows no raw age. */
    f.discovery.advertisement_observed_ms = 20000U;
    f.discovery.findmy_advertisement.bits = 0U;
    CHECK(!cerberus_s3_ble_ibeacon_is_current(&f));
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 31000U));
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 31001U));
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 20000U));
    f.discovery.flags = CERBERUS_S3_BLE_EVIDENCE_SID_INVALID;
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 31000U));
    f.ibeacon_scan_response = true;
    CHECK(cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 31000U));
    f.ibeacon_sequence = 0U;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 1000U));
    f.ibeacon_sequence = UINT32_MAX; f.ibeacon_valid = false;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 1000U));
    /* New sweep/ordinary replacement cannot inherit a category string or bit. */
    memset(&f, 0, sizeof(f)); f.category_bits = CERBERUS_S3_BLE_FOCUS_FIND_MY | CERBERUS_S3_BLE_FOCUS_IBEACON;
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_FIND_MY, 0U));
    CHECK(!cerberus_s3_ble_focus_recent(&f, CERBERUS_S3_BLE_FILTER_IBEACON, 0U));
}

static void check_production_replacements(void)
{
    static cerberus_s3_ble_snapshot_t snapshot;
    const uint8_t fmna[] = {3U,3U,0x44U,0xfdU}, ordinary[] = {2U,1U,6U};
    const uint8_t malformed[] = {3U,3U,0x44U,0xfdU,2U,3U,1U};
    cerberus_s3_ble_report_t report = {.address={1,2,3,4,5,6}, .address_type=1U,
        .rssi=-50, .complete=true, .data=fmna, .length=sizeof(fmna)};
    cerberus_s3_ble_snapshot_begin(&snapshot,1U,0U,false);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,0U);
    cerberus_s3_ble_record_t *row = &snapshot.records[0];
    CHECK(snapshot.status.stored_count==1U && row->focus.discovery.advertisement_sequence==1U);
    CHECK(cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,30000U));
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,30001U));
    cerberus_s3_ble_focus_t frozen = row->focus;
    report.data=ordinary; report.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,10000U);
    CHECK(row->last_advertisement_ms==10000U);
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,10000U));
    CHECK(cerberus_s3_ble_focus_recent(&frozen,CERBERUS_S3_BLE_FILTER_FIND_MY,10000U));
    report.scan_response=true; report.event_type=4U; report.data=fmna; report.length=sizeof(fmna);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,10010U);
    CHECK(!row->focus.discovery.findmy_advertisement.bits && row->focus.discovery.findmy_scan_response.bits);
    CHECK(cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,40010U));
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,40011U));
    /* An incomplete original retains the raw capture time but invalidates the
     * source used for freshness and prevents a later response rebinding it. */
    report.scan_response=false; report.event_type=0U; report.complete=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,20000U);
    CHECK(row->last_advertisement_ms==10000U);
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,20000U));
    report.scan_response=true; report.event_type=4U; report.complete=true;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,20010U);
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,20010U));
    report.scan_response=false; report.event_type=0U;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,21000U);
    CHECK(cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,21000U));
    report.data=malformed; report.length=sizeof(malformed);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,22000U);
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_FIND_MY,22000U));

    uint8_t ibeacon[27] = {26U,0xffU,0x4cU,0U,2U,0x15U};
    ibeacon[26]=0xc5U;
    cerberus_s3_ble_snapshot_begin(&snapshot,2U,0U,false);
    report.data=ibeacon; report.length=sizeof(ibeacon);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,1000U);
    CHECK(row->focus.ibeacon_valid && cerberus_s3_ble_ibeacon_is_current(&row->focus));
    report.data=ordinary; report.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,10000U);
    CHECK(!cerberus_s3_ble_ibeacon_is_current(&row->focus));
    CHECK(row->focus.ibeacon_observed_ms==1000U && row->last_advertisement_ms==10000U);
    CHECK(cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_IBEACON,31000U));
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_IBEACON,31001U));
    report.complete=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,15000U);
    CHECK(cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_IBEACON,15000U));
    CHECK(!cerberus_s3_ble_ibeacon_is_current(&row->focus));
    cerberus_s3_ble_snapshot_begin(&snapshot,3U,20000U,false);
    CHECK(!snapshot.status.stored_count);
    CHECK(!cerberus_s3_ble_focus_recent(&row->focus,CERBERUS_S3_BLE_FILTER_IBEACON,20000U));
}

int main(void)
{
    check_support_and_boundaries(); check_findmy_validity();
    check_response_sources(); check_retained_and_replacement();
    check_production_replacements();
    printf("BLE recent source predicates: %u checks passed (boundaries/wrap/flags/own times/retained evidence).\n", checks);
    return 0;
}
