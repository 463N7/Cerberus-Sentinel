#ifdef NDEBUG
#undef NDEBUG
#endif
#include "s3_findmy_monitor.h"
#include <assert.h>
#include <stdio.h>

static cerberus_findmy_monitor_t cache;
static cerberus_s3_ble_focus_t focus;
static cerberus_c5_ble_result_t peer;
static cerberus_c5_ble_advertisement_t raw;
static bool automatic_provenance = true;

static void bind(uint32_t observed)
{
    memset(&focus.discovery, 0, sizeof(focus.discovery));
    focus.discovery.advertisement_sequence = 1U;
    focus.discovery.advertisement_observed_ms = observed;
    memcpy(focus.discovery.address, peer.address, 6U);
    focus.discovery.address_type = peer.address_type;
    focus.discovery.generation = 7U;
    focus.discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    if (cerberus_ble_findmy_hints_parse(raw.data, raw.length, &focus.discovery.findmy_advertisement))
        focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
}

static void observation(uint8_t address, uint8_t type)
{
    memset(&peer, 0, sizeof(peer));
    memset(&raw, 0, sizeof(raw));
    memcpy(peer.name, "TAG", 4U);
    peer.address[0] = address;
    peer.address_type = type;
    peer.rssi = -61;
    raw.address[0] = address;
    raw.address_type = type;
    raw.event_type = 3U; /* nonconnectable is still a monitor observation */
    const uint8_t fmna[] = {3U, 3U, 0x44U, 0xFDU};
    memcpy(raw.data, fmna, sizeof(fmna));
    raw.length = sizeof(fmna);
    raw.reports_seen = 1U;
}

static bool ingest(uint32_t seen, uint32_t now)
{
    if (automatic_provenance) bind(seen);
    return cerberus_findmy_monitor_ingest(&cache, &peer, &raw, &focus,
        7U, 3U, seen, now);
}

int main(void)
{
    cerberus_findmy_monitor_init(&cache);
    observation(1U, 1U);
    assert(ingest(1000U, 1100U));
    assert(cache.count == 1U && cache.records[0].generation == 7U &&
        cache.records[0].index == 3U && cache.records[0].observed_ms == 1000U);
    assert(cache.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY);
    assert(cerberus_findmy_monitor_record_fresh(&cache.records[0], 31000U));
    assert(!cerberus_findmy_monitor_record_fresh(&cache.records[0], 31001U));
    assert(cerberus_findmy_monitor_record_age_ms(&cache.records[0], 1100U) == 100U);
    cerberus_findmy_monitor_record_t selected;
    assert(cerberus_findmy_monitor_copy(&cache, 0U, &selected));
    raw.data[1] = 2U; raw.reports_seen = 10U; peer.rssi = -41;
    assert(ingest(2000U, 2100U));
    assert(cache.count == 1U && cache.records[0].advertisement.data[1] == 2U &&
        cache.records[0].advertisement.reports_seen == 10U &&
        cache.records[0].peer.rssi == -41 && selected.advertisement.data[1] == 3U &&
        selected.observed_ms == 1000U && selected.peer.rssi == -61);
    assert(!ingest(1500U, 2200U));
    observation(1U, 0U); assert(ingest(2300U, 2400U));
    observation(2U, 1U); assert(ingest(2500U, 2600U));
    assert(cache.count == 3U); /* rotating address and address type are distinct */
    assert(cerberus_findmy_monitor_expire(&cache, 122000U) == 1U);
    assert(cache.count == 2U && selected.observed_ms == 1000U);
    assert(cerberus_findmy_monitor_expire(&cache, 122500U) == 2U);
    assert(!cerberus_findmy_monitor_copy(&cache, 0U, &selected));

    observation(3U, 1U);
    assert(ingest(UINT32_MAX - 100U, 100U));
    assert(cerberus_findmy_monitor_record_age_ms(&cache.records[0], 100U) == 201U);
    assert(cerberus_findmy_monitor_record_fresh(&cache.records[0], 100U));
    assert(ingest(150U, 200U));
    assert(!ingest(UINT32_MAX - 50U, 200U));
    assert(cerberus_findmy_monitor_expire(&cache, 120150U) == 1U);
    assert(!ingest(1U, 120001U));
    assert(!ingest(200U, 100U)); /* future/out-of-order clock input */

    cerberus_findmy_monitor_init(&cache);
    for (unsigned i = 0U; i < CERBERUS_FINDMY_MONITOR_CAPACITY; ++i) {
        observation((uint8_t)i, 0U); assert(ingest(1000U + i, 1100U));
    }
    assert(cache.count == CERBERUS_FINDMY_MONITOR_CAPACITY);
    observation(80U, 0U); assert(ingest(1200U, 1200U));
    assert(cache.count == CERBERUS_FINDMY_MONITOR_CAPACITY && cache.replacements == 1U);
    assert(cache.records[0].peer.address[0] == 80U); /* oldest replaced */
    observation(81U, 0U); assert(ingest(1201U, 1201U));
    assert(cache.records[1].peer.address[0] == 81U && cache.replacements == 2U);

    cerberus_findmy_monitor_init(&cache);
    observation(5U, 0U); raw.address[0] = 6U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.address_type = 1U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.flags = 1U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.event_type = 4U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.length = sizeof(raw.data) + 1U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.data[0] = 5U; assert(!ingest(10U, 10U));
    observation(5U, 0U); raw.data[2] = 0x45U;
    focus.category_bits = CERBERUS_S3_BLE_FOCUS_FIND_MY;
    assert(!ingest(10U, 10U)); /* stale focus cannot admit unrelated latest raw */
    assert(cache.count == 0U);
    observation(5U, 0U);
    const uint8_t dult[] = {17U,7U,0x85U,0x2AU,0x9FU,0x57U,0xC5U,0x2AU,
        0xEDU,0x88U,0x26U,0xC2U,0xF4U,0x12U,0x01U,0x00U,0x19U,0x15U};
    memcpy(raw.data, dult, sizeof(dult)); raw.length = sizeof(dult);
    assert(ingest(100U, 100U));
    const uint8_t fmna[] = {3U,3U,0x44U,0xFDU};
    memcpy(raw.data + sizeof(dult), fmna, sizeof(fmna));
    raw.length = sizeof(dult) + sizeof(fmna);
    assert(cerberus_findmy_classify_ad(raw.data, raw.length, true, false) ==
        CERBERUS_FINDMY_PROFILE_NONE); /* ambiguous for Sound, useful in Monitor */
    assert(ingest(101U, 101U));
    observation(6U, 1U); memset(raw.data, 0, sizeof(raw.data));
    raw.data[0] = 30U; raw.data[1] = 0xFFU; raw.data[2] = 0x4CU;
    raw.data[4] = 0x12U; raw.data[5] = 0x19U; raw.length = 31U;
    assert(ingest(110U, 110U));
    assert(cache.count == 2U);

    /* An ordinary original can have a matched response-only hint. Its source
     * time must not replace the original raw packet's active freshness time. */
    cerberus_findmy_monitor_init(&cache);
    observation(9U,1U);raw.data[2]=0x45U;bind(100U);automatic_provenance=false;
    focus.discovery.findmy_scan_response=(cerberus_ble_findmy_hints_t){CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID128};
    focus.discovery.scan_response_sequence=2U;focus.discovery.scan_response_observed_ms=1100U;
    focus.discovery.flags|=CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    assert(ingest(100U,1200U));
    assert(cerberus_findmy_monitor_record_age_ms(&cache.records[0],1200U)==1100U);
    assert(cerberus_findmy_monitor_hint_age_ms(&cache.records[0],1200U)==100U);
    assert(cerberus_findmy_monitor_copy(&cache,0,&selected));
    assert(selected.observed_ms==100U && selected.focus.discovery.scan_response_observed_ms==1100U);
    assert(ingest(100U,1200U)); /* exact idempotent copy */
    focus.discovery.scan_response_sequence=3U;focus.discovery.scan_response_observed_ms=1150U;
    assert(ingest(100U,1200U));
    focus.discovery.scan_response_sequence=2U;focus.discovery.scan_response_observed_ms=1100U;
    assert(!ingest(100U,1200U)); /* equal-ms source must not roll back response */
    focus.discovery.scan_response_sequence=4U;
    assert(!ingest(100U,1200U)); /* newer sequence cannot move response time back */
    focus.discovery.scan_response_observed_ms=1150U;
    assert(ingest(100U,1200U));
    focus.discovery.advertisement_sequence=3U;
    assert(ingest(100U,1200U));
    focus.discovery.advertisement_sequence=2U;assert(!ingest(100U,1200U));
    /* Restore a baseline record for independent corruption/expiry cases. */
    cerberus_findmy_monitor_init(&cache);
    focus.discovery.advertisement_sequence=1U;focus.discovery.scan_response_sequence=2U;
    focus.discovery.scan_response_observed_ms=1100U;assert(ingest(100U,1200U));
    assert(cerberus_findmy_ui_raw_profile(peer.address,peer.address_type,
        raw.address,raw.address_type,raw.event_type,raw.flags,raw.data,raw.length)==CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_monitor_expire(&cache,120100U)==0U);
    assert(cerberus_findmy_monitor_expire(&cache,121100U)==1U);
    assert(selected.focus.discovery.findmy_scan_response.bits==CERBERUS_BLE_FINDMY_HINT_FMNA);
    /* Token, typed identity, completeness, width consistency and ordering
     * checks reject stale/corrupt provenance even with a valid category bit. */
    cerberus_s3_ble_focus_t good=focus;
    ++focus.discovery.generation;assert(!ingest(100U,1200U));focus=good;
    ++focus.discovery.address[0];assert(!ingest(100U,1200U));focus=good;
    ++focus.discovery.address_type;assert(!ingest(100U,1200U));focus=good;
    ++focus.discovery.advertisement_observed_ms;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.advertisement_sequence=0;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.scan_response_sequence=1;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.scan_response_observed_ms=2101U;assert(!ingest(100U,2200U));focus=good;
    focus.discovery.scan_response_observed_ms=99U;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.flags&=(uint8_t)~CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.flags|=CERBERUS_S3_BLE_EVIDENCE_SID_INVALID;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.flags|=CERBERUS_S3_BLE_EVIDENCE_SID_VALID;focus.discovery.sid=16;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.findmy_scan_response.fmna_uuid_widths=0;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.findmy_scan_response.bits|=0x80U;assert(!ingest(100U,1200U));focus=good;
    focus.discovery.findmy_advertisement.bits=CERBERUS_BLE_FINDMY_HINT_APPLE;assert(!ingest(100U,1200U));focus=good;
    assert(!ingest(100U,1000U)); /* response timestamp is in the future */

    /* Passive full-source evidence remains useful when the raw copy is
     * truncated, while Sound continues to reject that exact raw copy. */
    raw.length=sizeof(raw.data);raw.flags=CERBERUS_BLE_ADVERTISEMENT_TRUNCATED;
    focus.discovery.flags&=(uint8_t)~CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    assert(ingest(100U,1200U));
    assert(cerberus_findmy_ui_raw_profile(peer.address,peer.address_type,
        raw.address,raw.address_type,raw.event_type,raw.flags,raw.data,raw.length)==CERBERUS_FINDMY_PROFILE_NONE);
    raw.flags|=CERBERUS_BLE_ADVERTISEMENT_MALFORMED;assert(!ingest(100U,1200U));
    raw.flags=CERBERUS_BLE_ADVERTISEMENT_TRUNCATED;--raw.length;assert(!ingest(100U,1200U));

    char line[64];
    cerberus_ble_findmy_hints_t all={7U,7U};
    cerberus_findmy_hint_line(&all,false,100U,2100U,line,sizeof(line));
    assert(!strcmp(line,"ADV HINT APPLE FMNA16+32+128 DULT / 2s"));
    cerberus_findmy_hint_line(&all,true,UINT32_MAX-100U,1100U,line,sizeof(line));
    assert(!strcmp(line,"RSP HINT APPLE FMNA16+32+128 DULT / 1s"));
    cerberus_findmy_hint_line(&all,true,2200U,2100U,line,sizeof(line));
    assert(strstr(line,"AGE UNKNOWN"));
    cerberus_findmy_hint_sources_line(&all,&all,line,sizeof(line));
    assert(!strcmp(line,"ADV MIXED / RSP MIXED / HINTS"));
    puts("Find My monitor: immutable raw selection, independent hint ages, bound ADV/response provenance, mixed profiles, truncated-copy isolation, expiry and wrap passed");
    return 0;
}
