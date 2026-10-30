#undef NDEBUG
#include "ble_analyzer.h"

#include <assert.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

static cerberus_ble_analyzer_report_t report(const uint8_t *ad, size_t length)
{
    cerberus_ble_analyzer_report_t result = {
        .address = {1, 2, 3, 4, 5, 6},
        .address_type = 1,
        .rssi_dbm = -62,
        .advertisement = ad,
        .advertisement_length = length,
        .complete = true,
    };
    return result;
}

static void check_latest_hc(void)
{
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    const uint8_t ordinary[]={2,1,6}, malformed[]={6,9,'H','C','-','0','5',2,3,1};
    uint8_t hc[]={6,9,'H','C','-','0','3'},oversized[256]={0};
    const uint8_t codes[]={CERBERUS_BLE_HC_03,CERBERUS_BLE_HC_05,CERBERUS_BLE_HC_06};
    cerberus_ble_analyzer_begin(&analyzer,1000U);
    for(unsigned i=0U;i<3U;++i){
        hc[6]=(uint8_t)('0'+codes[i]);
        cerberus_ble_analyzer_report_t r=report(hc,sizeof(hc));r.scan_response=i!=0U;
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1010U+i*10U));
        cerberus_ble_analyzer_snapshot(&analyzer,1013U+i*10U,&view);
        assert(view.last_report_valid && view.last_hc_name==codes[i] &&
            view.last_scan_response==(i!=0U) && view.last_seen_age_ms==3U &&
            view.hc_name_hint_events==i+1U && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT);
    }
    cerberus_ble_analyzer_view_t selected=view;
    /* Every accepted ordinary/invalid event replaces latest-event HC data,
     * including source kind, while the cumulative counter stays cumulative. */
    for(unsigned kind=0U;kind<8U;++kind){
        cerberus_ble_analyzer_report_t r=report(hc,sizeof(hc));
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1100U+kind*10U));
        uint32_t count=analyzer.hc_name_hint_events;
        r.scan_response=true;
        if(kind==0U){r.advertisement=ordinary;r.advertisement_length=sizeof(ordinary);}
        else if(kind==1U){r.advertisement=malformed;r.advertisement_length=sizeof(malformed);}
        else if(kind==2U)r.complete=false;
        else if(kind==3U)r.advertisement_flags=1U;
        else if(kind==4U){r.advertisement=oversized;r.advertisement_length=sizeof(oversized);}
        else if(kind==5U)hc[1]=8U;
        else if(kind==6U)hc[6]=0U;
        else hc[6]='4';
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1101U+kind*10U));
        cerberus_ble_analyzer_snapshot(&analyzer,1102U+kind*10U,&view);
        assert(!view.last_hc_name && view.last_scan_response && view.last_seen_age_ms==1U &&
            view.hc_name_hint_events==count && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_NONE);
        hc[1]=9U;hc[6]='6';
    }
    assert(selected.last_hc_name==CERBERUS_BLE_HC_06 && selected.hc_name_hint_events==3U);
    uint8_t conflict[14];memcpy(conflict,hc,sizeof(hc));memcpy(conflict+7U,hc,sizeof(hc));conflict[13]='3';
    cerberus_ble_analyzer_report_t r=report(conflict,sizeof(conflict));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1200U));
    cerberus_ble_analyzer_snapshot(&analyzer,1200U,&view);
    assert(!view.last_report_valid && !view.last_hc_name);
    uint8_t extended[255]={247U,0x30U};memcpy(extended+248U,hc,sizeof(hc));
    r=report(extended,sizeof(extended));r.scan_response=true;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1210U));
    cerberus_ble_analyzer_snapshot(&analyzer,1210U,&view);
    assert(view.last_report_valid && view.last_hc_name==CERBERUS_BLE_HC_06 && view.last_scan_response);
    /* A rejected API argument is not an accepted event and cannot erase it. */
    r.advertisement=NULL;
    assert(!cerberus_ble_analyzer_observe(&analyzer,&r,1220U));
    cerberus_ble_analyzer_snapshot(&analyzer,1220U,&view);
    assert(view.last_hc_name==CERBERUS_BLE_HC_06 && view.last_seen_age_ms==10U);
    analyzer.hc_name_hint_events=UINT32_MAX;
    r=report(hc,sizeof(hc));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1230U));
    cerberus_ble_analyzer_snapshot(&analyzer,1230U,&view);
    assert(view.hc_name_hint_events==UINT32_MAX && view.last_hc_name==CERBERUS_BLE_HC_06);
}

static void check_extended_meta(void)
{
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    const uint8_t meta[]={3,0xff,0x5f,0xfd};
    const uint8_t hc[]={6,9,'H','C','-','0','5'};
    const uint8_t blocked[]={3,0xff,0x4c,0};
    uint8_t extended[255]={239,0x30};
    memcpy(extended+240U,meta,sizeof(meta));
    memcpy(extended+244U,hc,sizeof(hc));
    cerberus_ble_analyzer_report_t r=report(extended,sizeof(extended));
    r.scan_response=true;
    cerberus_ble_analyzer_begin(&analyzer,1000U);
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1010U));
    cerberus_ble_analyzer_snapshot(&analyzer,1020U,&view);
    assert(view.last_report_valid && view.last_scan_response && view.last_seen_age_ms==10U);
    assert(view.meta_hint_events==1U && view.hc_name_hint_events==1U);
    assert(view.watch_match_events==0U && view.ibeacon_format_events==0U);
    assert(cerberus_ble_meta_evidence_count(&view.last_meta)==1U &&
        cerberus_ble_meta_evidence_has(&view.last_meta,0U,CERBERUS_BLE_META_MFG16));
    assert(view.last_name_length==5U && !memcmp(view.last_name,"HC-05",5U) &&
        view.last_hc_name==CERBERUS_BLE_HC_05 && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT);
    memcpy(extended+251U,blocked,sizeof(blocked));r.scan_response=false;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1030U));
    cerberus_ble_analyzer_snapshot(&analyzer,1030U,&view);
    assert(view.last_report_valid && !view.last_scan_response &&
        cerberus_ble_meta_evidence_count(&view.last_meta)==2U && cerberus_ble_meta_evidence_blocked(&view.last_meta));
    assert(view.meta_hint_events==1U && view.hc_name_hint_events==2U);
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT);
    r=report(meta,sizeof(meta));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1040U));
    cerberus_ble_analyzer_snapshot(&analyzer,1040U,&view);
    assert(view.meta_hint_events==2U && !cerberus_ble_meta_evidence_blocked(&view.last_meta) && !view.last_hc_name);

    /* No old latest-event matrix/name/hint survives a malformed suffix,
     * incomplete source, oversize source or source truncation flag. */
    uint8_t oversized[256]={0};
    for(unsigned kind=0;kind<4U;++kind){
        r=report(extended,sizeof(extended));r.scan_response=true;
        if(kind==0U)extended[251]=4U;
        else if(kind==1U)r.complete=false;
        else if(kind==2U){r.advertisement=oversized;r.advertisement_length=sizeof(oversized);}
        else r.advertisement_flags=1U;
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1050U+kind));
        cerberus_ble_analyzer_snapshot(&analyzer,1050U+kind,&view);
        assert(!view.last_report_valid && view.last_scan_response && !view.last_name_length && !view.last_hc_name &&
            !cerberus_ble_meta_evidence_count(&view.last_meta));
        assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_NONE &&
            view.meta_hint_events==2U && view.hc_name_hint_events==2U);
        extended[251]=3U;
    }
    assert(view.malformed_reports==1U && view.unclassified_reports==3U);
    const uint8_t ordinary[]={2,1,6};
    r=report(ordinary,sizeof(ordinary));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1060U));
    cerberus_ble_analyzer_snapshot(&analyzer,1060U,&view);
    assert(view.last_report_valid && !view.last_scan_response && !cerberus_ble_meta_evidence_count(&view.last_meta));

    /* Watch keeps its whole-report bound while independent iBeacon parsing
     * now accepts the complete extended report and preserves its payload. */
    uint8_t long_beacon[34]={26,0xff,0x4c,0,2,0x15};
    memcpy(long_beacon+27U,hc,sizeof(hc));
    r=report(long_beacon,sizeof(long_beacon));
    cerberus_ble_analyzer_begin(&analyzer,2000U);
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,2010U));
    cerberus_ble_analyzer_snapshot(&analyzer,2010U,&view);
    assert(view.last_report_valid && view.ibeacon_format_events==1U && view.watch_match_events==0U &&
        view.last_ibeacon_valid);
    assert(view.hc_name_hint_events==1U && cerberus_ble_meta_evidence_blocked(&view.last_meta) &&
        view.last_hc_name==CERBERUS_BLE_HC_05 && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT);
    r.advertisement_length=27U;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,2020U));
    cerberus_ble_analyzer_snapshot(&analyzer,2020U,&view);
    assert(view.ibeacon_format_events==2U);
}

static void check_latest_ibeacon(void)
{
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    uint8_t a[27]={26,0xff,0x4c,0,2,0x15},b[27];
    a[6]=1U;a[22]=1U;a[23]=2U;a[24]=3U;a[25]=4U;a[26]=0x80U;
    memcpy(b,a,sizeof(b));b[6]=2U;b[23]=9U;b[26]=127U;
    cerberus_ble_analyzer_report_t r=report(a,sizeof(a));
    cerberus_ble_analyzer_begin(&analyzer,1000U);
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1010U));
    cerberus_ble_analyzer_snapshot(&analyzer,1011U,&view);
    assert(view.last_ibeacon_valid && view.last_ibeacon.uuid[0]==1U &&
        view.last_ibeacon.major==258U && view.last_ibeacon.minor==772U &&
        view.last_ibeacon.measured_power_dbm==-128 && !view.last_scan_response && view.last_seen_age_ms==1U);
    r.advertisement=b;r.scan_response=true;r.address[0]=9U;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1020U));
    cerberus_ble_analyzer_snapshot(&analyzer,1021U,&view);
    assert(view.last_ibeacon_valid && view.last_ibeacon.uuid[0]==2U &&
        view.last_ibeacon.major==265U && view.last_ibeacon.measured_power_dbm==127 &&
        view.last_scan_response && view.last_address[0]==9U && view.ibeacon_format_events==2U);
    const uint8_t ordinary[]={2,1,6}, malformed[]={3,9,'x'};
    uint8_t oversized[256]={0};
    cerberus_ibeacon_t zero={0};
    for(unsigned kind=0U;kind<5U;++kind){
        r=report(a,sizeof(a));assert(cerberus_ble_analyzer_observe(&analyzer,&r,1030U+kind*2U));
        r.scan_response=true;
        if(kind==0U){r.advertisement=ordinary;r.advertisement_length=sizeof(ordinary);}
        else if(kind==1U){r.advertisement=malformed;r.advertisement_length=sizeof(malformed);}
        else if(kind==2U)r.complete=false;
        else if(kind==3U)r.advertisement_flags=1U;
        else {r.advertisement=oversized;r.advertisement_length=sizeof(oversized);}
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1031U+kind*2U));
        cerberus_ble_analyzer_snapshot(&analyzer,1031U+kind*2U,&view);
        assert(!view.last_ibeacon_valid && !memcmp(&view.last_ibeacon,&zero,sizeof(zero)) &&
            view.last_scan_response && view.last_report_valid==(kind==0U));
    }
    /* Full255-byte evidence is accepted without truncation. */
    uint8_t extended[255]={227U,0x30U};memcpy(extended+228U,b,sizeof(b));
    r=report(extended,sizeof(extended));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1100U));
    cerberus_ble_analyzer_snapshot(&analyzer,1100U,&view);
    assert(view.last_report_valid && view.last_ibeacon_valid && view.last_ibeacon.uuid[0]==2U);
    uint8_t duplicates[61];memcpy(duplicates,a,sizeof(a));memcpy(duplicates+27U,a,sizeof(a));
    const uint8_t hc[]={6,9,'H','C','-','0','5'};memcpy(duplicates+54U,hc,sizeof(hc));
    r=report(duplicates,sizeof(duplicates));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1110U));
    cerberus_ble_analyzer_snapshot(&analyzer,1110U,&view);
    assert(view.last_ibeacon_valid && view.last_ibeacon.uuid[0]==1U);
    uint32_t matches=view.ibeacon_format_events;
    memcpy(duplicates+27U,b,sizeof(b));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1120U));
    cerberus_ble_analyzer_snapshot(&analyzer,1120U,&view);
    assert(view.last_report_valid && !view.last_ibeacon_valid && view.ibeacon_format_events==matches);
    assert(!memcmp(&view.last_ibeacon,&zero,sizeof(zero)) && view.hc_name_hint_events==2U &&
        cerberus_ble_meta_evidence_blocked(&view.last_meta));
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT);
    /* Independent payload publication does not override Watch's existing
     * equal-confidence tie priority. */
    uint8_t watch_and_beacon[31];memcpy(watch_and_beacon,a,sizeof(a));
    const uint8_t axon[]={3,3,0x81,0xfc};memcpy(watch_and_beacon+27U,axon,sizeof(axon));
    r=report(watch_and_beacon,sizeof(watch_and_beacon));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1130U));
    cerberus_ble_analyzer_snapshot(&analyzer,1130U,&view);
    assert(view.last_ibeacon_valid && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH);
}

static void check_latest_flock(void)
{
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    const uint8_t ordinary[]={2,1,6},company[]={3,0xff,0xc8,9};
    const uint8_t fs[]={15,9,'F','S',' ','E','x','t',' ','B','a','t','t','e','r','y'};
    const uint8_t payload[]={4,0xff,0xc8,9,0x42},meta[]={3,0x16,0x5f,0xfd};
    const uint8_t hc[]={6,9,'H','C','-','0','5'};
    const uint8_t raven_a[]={3,3,0,0x31},raven_b[]={3,0x16,0,0x32};
    const uint8_t malformed[]={3,9,'x'};
    uint8_t corroborated[sizeof(fs)+sizeof(payload)];
    memcpy(corroborated,fs,sizeof(fs));memcpy(corroborated+sizeof(fs),payload,sizeof(payload));
    cerberus_ble_analyzer_begin(&analyzer,1000U);
    cerberus_ble_analyzer_report_t r=report(company,sizeof(company));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1010U));
    cerberus_ble_analyzer_snapshot(&analyzer,1010U,&view);
    assert(cerberus_ble_flock_hints_level(&view.last_flock)==CERBERUS_BLE_FLOCK_LEVEL_CLUE &&
        !view.last_flock_oui_clue && view.flock_hint_events==1U &&
        view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT && view.evidence_confidence==SENTINEL_WATCH_CONFIDENCE_LOW);
    r=report(fs,sizeof(fs));r.scan_response=true;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1020U));
    cerberus_ble_analyzer_snapshot(&analyzer,1020U,&view);
    assert(view.last_scan_response && view.flock_hint_events==2U &&
        cerberus_ble_flock_hints_level(&view.last_flock)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE &&
        !(view.last_flock.facts&CERBERUS_BLE_FLOCK_MFG_09C8));
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH);
    r=report(raven_a,sizeof(raven_a));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1030U));
    r=report(raven_b,sizeof(raven_b));r.scan_response=true;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1040U));
    cerberus_ble_analyzer_snapshot(&analyzer,1040U,&view);
    assert(view.flock_hint_events==2U && !cerberus_ble_flock_hints_level(&view.last_flock) &&
        !view.last_flock.raven_3100_contexts && view.last_flock.raven_3200_contexts==CERBERUS_BLE_FLOCK_DATA16);

    /* Specific packet signatures can outrank low Meta/HC fallback; weak
     * address/company clues cannot mask their equal-confidence display. */
    uint8_t long_sig[40]={19U,0x30U};memcpy(long_sig+20U,fs,sizeof(fs));memcpy(long_sig+36U,meta,sizeof(meta));
    r=report(long_sig,sizeof(long_sig));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1050U));
    cerberus_ble_analyzer_snapshot(&analyzer,1050U,&view);
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT &&
        view.evidence_confidence==SENTINEL_WATCH_CONFIDENCE_MEDIUM && cerberus_ble_meta_evidence_allowed(&view.last_meta));
    uint8_t weak_meta[sizeof(company)+sizeof(meta)];memcpy(weak_meta,company,sizeof(company));memcpy(weak_meta+sizeof(company),meta,sizeof(meta));
    r=report(weak_meta,sizeof(weak_meta));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1060U));
    cerberus_ble_analyzer_snapshot(&analyzer,1060U,&view);
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT &&
        cerberus_ble_flock_hints_level(&view.last_flock)==CERBERUS_BLE_FLOCK_LEVEL_CLUE);
    r=report(hc,sizeof(hc));memcpy(r.address,(uint8_t[]){1,2,3,0x81,0x8e,0x58},6U);r.address_type=0U;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1070U));
    cerberus_ble_analyzer_snapshot(&analyzer,1070U,&view);
    assert(view.last_flock_oui_clue && !cerberus_ble_flock_hints_level(&view.last_flock) &&
        view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT);
    r.advertisement=ordinary;r.advertisement_length=sizeof(ordinary);r.scan_response=true;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1080U));
    cerberus_ble_analyzer_snapshot(&analyzer,1080U,&view);
    assert(view.last_flock_oui_clue && view.last_scan_response && !cerberus_ble_flock_hints_level(&view.last_flock) &&
        view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT && strstr(view.evidence_reason,"address"));
    uint32_t clue_events=view.flock_hint_events;
    r.address_type=1U;
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1090U));
    cerberus_ble_analyzer_snapshot(&analyzer,1090U,&view);
    assert(!view.last_flock_oui_clue && view.flock_hint_events==clue_events &&
        !cerberus_ble_flock_hints_level(&view.last_flock));
    const uint8_t watch_flock[31]={3,3,0x80,0x30,8,9,'F','l','i','p','p','e','r',
        17,7,0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8};
    r=report(watch_flock,sizeof(watch_flock));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1100U));
    cerberus_ble_analyzer_snapshot(&analyzer,1100U,&view);
    assert(view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH && strstr(view.evidence_label,"Flipper") &&
        cerberus_ble_flock_hints_level(&view.last_flock)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    uint8_t beacon_and_flock[48]={26,0xff,0x4c,0,2,0x15};memcpy(beacon_and_flock+27U,corroborated,sizeof(corroborated));
    r=report(beacon_and_flock,sizeof(beacon_and_flock));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1110U));
    cerberus_ble_analyzer_snapshot(&analyzer,1110U,&view);
    assert(view.last_ibeacon_valid && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT &&
        cerberus_ble_flock_hints_level(&view.last_flock)==CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED);
    uint8_t extended[255]={233U,0x30U};memcpy(extended+234U,corroborated,sizeof(corroborated));
    r=report(extended,sizeof(extended));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1120U));
    cerberus_ble_analyzer_snapshot(&analyzer,1120U,&view);
    assert(view.last_report_valid && view.evidence_kind==CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT &&
        view.evidence_confidence==SENTINEL_WATCH_CONFIDENCE_HIGH);
    uint8_t oversized[256]={0};cerberus_ble_flock_hints_t zero={0};
    for(unsigned kind=0U;kind<4U;++kind){
        r=report(corroborated,sizeof(corroborated));r.address_type=0U;
        memcpy(r.address,(uint8_t[]){1,2,3,0x81,0x8e,0x58},6U);
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1130U+kind*2U));
        uint32_t before=analyzer.flock_hint_events;r.scan_response=true;
        if(kind==0U){r.advertisement=malformed;r.advertisement_length=sizeof(malformed);}
        else if(kind==1U)r.complete=false;
        else if(kind==2U)r.advertisement_flags=1U;
        else {r.advertisement=oversized;r.advertisement_length=sizeof(oversized);}
        assert(cerberus_ble_analyzer_observe(&analyzer,&r,1131U+kind*2U));
        cerberus_ble_analyzer_snapshot(&analyzer,1131U+kind*2U,&view);
        assert(!view.last_report_valid && !view.last_flock_oui_clue && view.last_scan_response &&
            !memcmp(&view.last_flock,&zero,sizeof(zero)) && view.flock_hint_events==before);
    }
    analyzer.flock_hint_events=UINT32_MAX;r=report(company,sizeof(company));
    assert(cerberus_ble_analyzer_observe(&analyzer,&r,1200U) && analyzer.flock_hint_events==UINT32_MAX);
}

static unsigned window_checks;
#define WINDOW(x) do { ++window_checks; assert(x); } while (0)
static cerberus_ble_analyzer_window_t window(const cerberus_ble_analyzer_view_t *view)
{
    cerberus_ble_analyzer_window_t result;
    WINDOW(cerberus_ble_analyzer_window(view,&result));return result;
}
static void check_completed_window(void)
{
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    cerberus_ble_analyzer_report_t event=report(NULL,0);
    event.complete=false; /* Every accepted callback counts, not just classified ADs. */
    cerberus_ble_analyzer_begin(&analyzer,1000);
    cerberus_ble_analyzer_snapshot(&analyzer,1199,&view);
    cerberus_ble_analyzer_window_t stats=window(&view);
    WINDOW(!stats.buckets&&!stats.events&&!stats.peak&&!stats.capped);
    WINDOW(cerberus_ble_analyzer_observe(&analyzer,&event,1199));
    cerberus_ble_analyzer_snapshot(&analyzer,1199,&view);stats=window(&view);
    WINDOW(view.current_events==1&&!stats.buckets&&!stats.events&&!stats.peak);
    cerberus_ble_analyzer_snapshot(&analyzer,1200,&view);stats=window(&view);
    WINDOW(stats.buckets==1&&stats.events==1&&stats.peak==1&&!stats.capped);
    cerberus_ble_analyzer_snapshot(&analyzer,1800,&view);stats=window(&view);
    WINDOW(stats.buckets==4&&stats.events==1&&stats.peak==1); /* silence is in denominator */
    WINDOW(stats.events*10U/stats.buckets==2); /* displayed mean0.2, not integer0 or mean1 */
    cerberus_ble_analyzer_t saved=analyzer;
    cerberus_ble_analyzer_snapshot(&analyzer,1799,&view);stats=window(&view);
    WINDOW(stats.buckets==4&&stats.events==1&&!memcmp(&saved,&analyzer,sizeof(saved)));

    /* More than two rotations, using an independently computed event sequence.
     * Run again across uint32 time rollover; report counts are identical. */
    for(unsigned wrapped=0;wrapped<2;++wrapped){
        uint32_t base=wrapped?UINT32_MAX-350U:20000U;
        cerberus_ble_analyzer_begin(&analyzer,base);
        for(unsigned bucket=0;bucket<150;++bucket){
            unsigned count=(bucket*7U)%11U;
            for(unsigned n=0;n<count;++n)
                WINDOW(cerberus_ble_analyzer_observe(&analyzer,&event,base+bucket*200U+1U));
            cerberus_ble_analyzer_snapshot(&analyzer,base+(bucket+1U)*200U,&view);
            stats=window(&view);unsigned n=bucket<64U?bucket+1U:64U,sum=0,peak=0;
            for(unsigned age=0;age<n;++age){
                unsigned expected=((bucket-age)*7U)%11U;
                WINDOW(view.history[age]==expected);sum+=expected;if(expected>peak)peak=expected;
            }
            WINDOW(stats.buckets==n&&stats.events==sum&&stats.peak==peak&&!stats.capped);
            cerberus_ble_analyzer_view_t partial=view;partial.current_events=UINT16_MAX;
            cerberus_ble_analyzer_window_t unchanged=window(&partial);
            WINDOW(!memcmp(&stats,&unchanged,sizeof(stats))); /* current never scales/statistically dominates */
        }
    }
    /* Exactly64 elapsed buckets retains the current one as the oldest visible
     * bucket. At65 it has aged out. Test a nonzero prior ring cursor as well. */
    for(unsigned cursor=0;cursor<2;++cursor)for(unsigned steps=64;steps<=65;++steps){
        cerberus_ble_analyzer_begin(&analyzer,5000);
        if(cursor)cerberus_ble_analyzer_advance(&analyzer,5200);
        uint32_t start=5000+cursor*200;
        for(unsigned n=0;n<3;++n)WINDOW(cerberus_ble_analyzer_observe(&analyzer,&event,start+1));
        cerberus_ble_analyzer_snapshot(&analyzer,start+steps*200,&view);stats=window(&view);
        WINDOW(stats.buckets==64&&stats.events==(steps==64?3U:0U)&&stats.peak==(steps==64?3U:0U));
        WINDOW(view.history[63]==(steps==64?3U:0U)&&!view.current_events);
    }
    cerberus_ble_analyzer_begin(&analyzer,0);analyzer.current_events=UINT16_MAX-1U;analyzer.total_events=UINT32_MAX-1U;
    for(unsigned n=0;n<3;++n)WINDOW(cerberus_ble_analyzer_observe(&analyzer,&event,1));
    cerberus_ble_analyzer_snapshot(&analyzer,200,&view);stats=window(&view);
    WINDOW(view.total_events==UINT32_MAX&&stats.events==UINT16_MAX&&stats.peak==UINT16_MAX&&stats.capped);
    view.history_count=64;for(unsigned i=0;i<64;++i)view.history[i]=UINT16_MAX;stats=window(&view);
    WINDOW(stats.events==4194240U&&stats.events*10U/stats.buckets==655350U&&stats.capped);
    /* A large valid forward jump removes all old samples in bounded work. */
    cerberus_ble_analyzer_snapshot(&analyzer,INT32_MAX,&view);stats=window(&view);
    WINDOW(stats.buckets==64&&!stats.events&&!stats.peak&&!stats.capped);
    cerberus_ble_analyzer_begin(&analyzer,0);WINDOW(cerberus_ble_analyzer_observe(&analyzer,&event,1));
    cerberus_ble_analyzer_stop(&analyzer);cerberus_ble_analyzer_snapshot(&analyzer,1000,&view);stats=window(&view);
    WINDOW(!stats.buckets&&!stats.events&&view.current_events==1&&!view.active);
    memset(&stats,0xa5,sizeof(stats));view.history_count=65;
    WINDOW(!cerberus_ble_analyzer_window(&view,&stats));
    cerberus_ble_analyzer_window_t zero={0};WINDOW(!memcmp(&stats,&zero,sizeof(stats)));
    memset(&stats,0xa5,sizeof(stats));WINDOW(!cerberus_ble_analyzer_window(NULL,&stats));WINDOW(!memcmp(&stats,&zero,sizeof(stats)));
    WINDOW(!cerberus_ble_analyzer_window(&view,NULL));
    printf("BLE Analyzer completed-window statistics: %u checks passed.\n",window_checks);
}

int main(void)
{
    check_completed_window();
    check_latest_hc();
    cerberus_ble_analyzer_t analyzer;
    cerberus_ble_analyzer_view_t view;
    uint8_t empty[] = {0};
    cerberus_ble_analyzer_report_t r = report(empty, sizeof(empty));
    cerberus_ble_analyzer_begin(&analyzer, 1000U);
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 1001U));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 1002U));
    cerberus_ble_analyzer_snapshot(&analyzer, 1199U, &view);
    assert(view.active && view.current_events == 2U && view.history_count == 0U);
    assert(view.total_events == 2U && view.last_fresh && view.last_seen_age_ms == 197U);
    cerberus_ble_analyzer_snapshot(&analyzer, 1200U, &view);
    assert(view.current_events == 0U && view.history_count == 1U && view.history[0] == 2U);
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 1201U));
    cerberus_ble_analyzer_snapshot(&analyzer, 1400U, &view);
    assert(view.history_count == 2U && view.history[0] == 1U && view.history[1] == 2U);
    cerberus_ble_analyzer_snapshot(&analyzer, 17000U, &view);
    assert(view.history_count == CERBERUS_BLE_ANALYZER_HISTORY);
    assert(view.history[0] == 0U && view.history[63] == 0U);
    assert(!view.last_fresh && view.total_events == 3U);

    uint8_t malformed[] = {3U, 0x09U, 'A'};
    r = report(malformed, sizeof(malformed));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17001U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17001U, &view);
    assert(view.malformed_reports == 1U && view.evidence_kind ==
        CERBERUS_BLE_ANALYZER_EVIDENCE_NONE);

    uint8_t hc_name[] = {6U, 0x09U, 'H', 'C', '-', '0', '5'};
    r = report(hc_name, sizeof(hc_name));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17002U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17002U, &view);
    assert(view.hc_name_hint_events == 1U && view.last_name_length == 5U);
    assert(view.evidence_kind == CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT);
    assert(view.evidence_confidence == SENTINEL_WATCH_CONFIDENCE_LOW);
    assert(strstr(view.evidence_reason, "purpose unknown") != NULL);

    uint8_t meta[] = {3U, 0xFFU, 0x5FU, 0xFDU};
    r = report(meta, sizeof(meta));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17003U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17003U, &view);
    assert(view.meta_hint_events == 1U &&
        view.evidence_kind == CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT &&
        view.evidence_confidence == SENTINEL_WATCH_CONFIDENCE_LOW);

    uint8_t blocked_meta[] = {3U, 0xFFU, 0x5FU, 0xFDU,
                              3U, 0xFFU, 0x4CU, 0x00U};
    r = report(blocked_meta, sizeof(blocked_meta));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17004U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17004U, &view);
    assert(view.meta_hint_events == 1U);
    assert(view.evidence_kind != CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT);

    uint8_t ibeacon[27] = {26U, 0xFFU, 0x4CU, 0U, 0x02U, 0x15U};
    r = report(ibeacon, sizeof(ibeacon));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17005U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17005U, &view);
    assert(view.ibeacon_format_events == 1U &&
        view.evidence_kind == CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT &&
        view.evidence_confidence == SENTINEL_WATCH_CONFIDENCE_MEDIUM);

    uint8_t findmy[31] = {30U, 0xFFU, 0x4CU, 0U, 0x12U, 0x19U};
    r = report(findmy, sizeof(findmy));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17006U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17006U, &view);
    assert(view.watch_match_events >= 1U &&
        view.evidence_kind == CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH &&
        view.evidence_confidence == SENTINEL_WATCH_CONFIDENCE_HIGH);

    r.complete = false;
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 17007U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17007U, &view);
    assert(view.unclassified_reports == 1U &&
        view.evidence_kind == CERBERUS_BLE_ANALYZER_EVIDENCE_NONE);
    r.advertisement = NULL;
    assert(!cerberus_ble_analyzer_observe(&analyzer, &r, 17008U));
    cerberus_ble_analyzer_snapshot(&analyzer, 17008U, &view);
    assert(view.total_events == 10U);

    cerberus_ble_analyzer_begin(&analyzer, UINT32_MAX - 99U);
    r = report(empty, sizeof(empty));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, UINT32_MAX - 98U));
    cerberus_ble_analyzer_snapshot(&analyzer, 101U, &view);
    assert(view.history_count == 1U && view.history[0] == 1U);
    assert(view.last_seen_age_ms == 200U && view.last_fresh);

    cerberus_ble_analyzer_stop(&analyzer);
    assert(!cerberus_ble_analyzer_observe(&analyzer, &r, 102U));
    cerberus_ble_analyzer_snapshot(&analyzer, 102U, &view);
    assert(!view.active && view.total_events == 1U);
    /* Analyzer and inventory must reject the same malformed/short-name hints. */
    cerberus_ble_analyzer_begin(&analyzer, 20000U);
    uint8_t semantic_bad[] = {3,0xFF,0x5F,0xFD,2,0x03,0x01};
    r = report(semantic_bad, sizeof(semantic_bad));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 20001U));
    hc_name[1] = 0x08U;
    r = report(hc_name, sizeof(hc_name));
    assert(cerberus_ble_analyzer_observe(&analyzer, &r, 20002U));
    cerberus_ble_analyzer_snapshot(&analyzer, 20002U, &view);
    assert(view.malformed_reports == 1U && view.meta_hint_events == 0U &&
        view.hc_name_hint_events == 0U);
    check_extended_meta();
    check_latest_ibeacon();
    check_latest_flock();
    return 0;
}
