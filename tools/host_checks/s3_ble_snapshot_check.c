#ifdef NDEBUG
#undef NDEBUG
#endif
#include "s3_ble_snapshot.h"
#include "s3_ble_wardrive.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static cerberus_s3_ble_snapshot_t snapshot;

static void check_wardrive_evidence_codec(void)
{
    cerberus_s3_ble_wardrive_evidence_t e={0};
    assert(sizeof(e)==28&&cerberus_s3_ble_wardrive_evidence_valid(&e));
    assert(!cerberus_s3_ble_wardrive_evidence_key(&e));
    assert(!cerberus_s3_ble_wardrive_evidence_valid(NULL));
    assert(cerberus_s3_ble_wardrive_evidence_level(NULL)==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    e.flags=CERBERUS_S3_BLE_WARDRIVE_OUI_CLUE;
    assert(cerberus_s3_ble_wardrive_evidence_key(&e)==(UINT64_C(1)<<40));
    e.flags=3;e.advertisement_sequence=1;e.scan_response_sequence=2;
    e.advertisement.facts=CERBERUS_BLE_FLOCK_FS_BATTERY;
    e.scan_response.facts=CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    assert(cerberus_s3_ble_wardrive_evidence_valid(&e));
    assert(cerberus_s3_ble_wardrive_evidence_level(&e)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    assert(cerberus_s3_ble_wardrive_evidence_key(&e)==(1U|(UINT64_C(48)<<20)));
    cerberus_s3_ble_wardrive_evidence_t same=e;
    same.advertisement_sequence=7;same.scan_response_sequence=8;
    same.advertisement_observed_ms=UINT32_MAX-1000U;same.scan_response_observed_ms=999;
    same.flags|=CERBERUS_S3_BLE_WARDRIVE_SID_VALID;same.sid=15;
    assert(cerberus_s3_ble_wardrive_evidence_valid(&same));
    assert(cerberus_s3_ble_wardrive_evidence_key(&e)==cerberus_s3_ble_wardrive_evidence_key(&same));
    same.scan_response_observed_ms=1000;assert(!cerberus_s3_ble_wardrive_evidence_valid(&same));
    same=e;same.advertisement=e.scan_response;same.scan_response=e.advertisement;
    assert(cerberus_s3_ble_wardrive_evidence_key(&e)!=cerberus_s3_ble_wardrive_evidence_key(&same));
    e.advertisement=(cerberus_ble_flock_hints_t){.raven_3100_contexts=1};
    e.scan_response=(cerberus_ble_flock_hints_t){.raven_3200_contexts=1};
    assert(cerberus_s3_ble_wardrive_evidence_level(&e)==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    assert(!cerberus_s3_ble_wardrive_evidence_key(&e));
    /* Every context bit has its own collision-free position; no union context
     * can imply a source/UUID combination that was not actually present. */
    for(unsigned accessory=0;accessory<4;++accessory)for(unsigned a=0;a<64;++a)for(unsigned b=0;b<64;++b){
        e.advertisement=(cerberus_ble_flock_hints_t){.facts=CERBERUS_BLE_FLOCK_FS_BATTERY,
            .accessory_contexts=(accessory&1?4:0)|(accessory&2?32:0),
            .raven_3100_contexts=a,.raven_3200_contexts=b};
        e.scan_response=(cerberus_ble_flock_hints_t){0};
        uint64_t expected=1U|((uint64_t)accessory<<6)|((uint64_t)a<<8)|((uint64_t)b<<14);
        assert(cerberus_s3_ble_wardrive_evidence_valid(&e));
        assert(cerberus_s3_ble_wardrive_evidence_key(&e)==expected);
        same=e;same.scan_response=e.advertisement;same.advertisement=(cerberus_ble_flock_hints_t){0};
        assert(cerberus_s3_ble_wardrive_evidence_key(&same)==(expected<<20));
    }
    /* Canonicality rejects unknown bits, impossible name/payload facts, and
     * stale bytes in any absent source. Valid zero time is never rejected. */
    e=(cerberus_s3_ble_wardrive_evidence_t){.flags=1,.advertisement_sequence=1};
    for(unsigned byte=0;byte<256;++byte){
        same=e;same.advertisement.facts=byte;
        unsigned names=byte&7U;
        bool valid=!(byte&~63U)&&!(names&&(names&(names-1U)))&&!(names&&(byte&8U))&&(!(byte&32U)||(byte&16U));
        assert(cerberus_s3_ble_wardrive_evidence_valid(&same)==valid);
        same=e;same.advertisement.accessory_contexts=byte;
        assert(cerberus_s3_ble_wardrive_evidence_valid(&same)==!(byte&~36U));
        same=e;same.advertisement.raven_3100_contexts=byte;
        assert(cerberus_s3_ble_wardrive_evidence_valid(&same)==(byte<64));
        same=e;same.advertisement.raven_3200_contexts=byte;
        assert(cerberus_s3_ble_wardrive_evidence_valid(&same)==(byte<64));
    }
    for(unsigned bad=0;bad<10;++bad){
        same=e;
        if(bad==0)same.flags|=16;
        if(bad==1)same.reserved[0]=1;
        if(bad==2)same.reserved[1]=1;
        if(bad==3)same.advertisement_sequence=0;
        if(bad==4)same.scan_response_sequence=1;
        if(bad==5)same.scan_response_observed_ms=1;
        if(bad==6)same.scan_response.facts=8;
        if(bad==7)same.sid=1;
        if(bad==8){same.flags|=8;same.sid=16;}
        if(bad==9){same.flags=0;same.advertisement_sequence=0;same.advertisement_observed_ms=1;}
        assert(!cerberus_s3_ble_wardrive_evidence_valid(&same));
        assert(!cerberus_s3_ble_wardrive_evidence_key(&same));
    }
    puts("Wardrive pure evidence codec: source separation, exact41-bit keys, complete context/fact domains and canonical absence passed.");
}

static cerberus_watch_scan_timing_payload_t completed_watch_timing(uint64_t now_us)
{
    cerberus_watch_capture_t copy = snapshot.watch;
    cerberus_watch_capture_finish(&copy, CERBERUS_WATCH_COMPLETE, 9000U);
    cerberus_watch_scan_timing_payload_t timing;
    assert(cerberus_watch_capture_get_timing(&copy, CERBERUS_WATCH_RADIO_BLE,
        copy.status.generation, 0U, now_us, &timing));
    return timing;
}

static void check_watch_callback_timing(void)
{
    uint8_t rid[32] = {30U,0x16U,0xfaU,0xffU,0x0dU,7U,0x02U,0x10U,'T','I','M','E'};
    const uint8_t ordinary[] = {2U,1U,6U};
    cerberus_s3_ble_report_t report = {.address={1,2,3,4,5,6},.address_type=1U,
        .rssi=-35,.complete=true,.data=rid,.length=31U};
    const uint64_t base = (UINT64_C(1)<<32)*1000U+17U, epoch = (UINT64_C(1)<<32)+7U;
    cerberus_s3_ble_snapshot_begin_with_epoch(&snapshot,7U,0U,true,epoch);
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,10U,base);
    assert(snapshot.watch.status.stored_count==1U && snapshot.records[0].last_advertisement_ms==10U);
    cerberus_watch_scan_timing_payload_t timing=completed_watch_timing(base+1001U);
    assert(timing.flags==CERBERUS_WATCH_TIMING_VALID && timing.source_age_ms==2U && timing.capture_epoch==epoch);
    /* Later ordinary, malformed, incomplete and response traffic cannot date
     * an earlier qualifying payload. Response bytes never become Watch data. */
    report.data=ordinary;report.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,20U,base+2000U);
    report.data=rid;report.length=31U;report.scan_response=true;
    const uint32_t before_responses=snapshot.watch.status.reports;
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,30U,base+3000U);
    assert(snapshot.watch.status.reports==before_responses);
    report.scan_response=false;report.complete=false;
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,40U,base+4000U);
    report.complete=true;rid[31]=1U;report.length=32U;
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,50U,base+5000U);
    report.length=31U;report.address_type=255U;
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,60U,base+6000U);
    timing=completed_watch_timing(base+7000U);
    assert(timing.flags==CERBERUS_WATCH_TIMING_VALID && timing.source_age_ms==7U);
    report.address_type=1U;report.rssi=-90;
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,70U,base+7000U);
    timing=completed_watch_timing(base+7001U);
    assert(timing.flags==CERBERUS_WATCH_TIMING_VALID && timing.source_age_ms==1U &&
        snapshot.watch.records[0].rssi==-90);
    timing=completed_watch_timing(base+6999U);
    assert(!timing.flags && !timing.source_age_ms && timing.capture_epoch==epoch);
    /* Qualifying untimed replacement clears provenance; it cannot borrow the
     * last timed callback. A backward qualifying time also loses validity. */
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,80U);
    timing=completed_watch_timing(base+8000U);assert(!timing.flags && !timing.source_age_ms);
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,90U,base+9000U);
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,100U,base+8999U);
    timing=completed_watch_timing(base+10000U);assert(!timing.flags);
    cerberus_s3_ble_snapshot_begin_with_epoch(&snapshot,7U,0U,true,epoch+1U);
    cerberus_s3_ble_snapshot_observe_timed(&snapshot,&report,0U,0U);
    timing=completed_watch_timing(0U);
    assert(timing.flags==CERBERUS_WATCH_TIMING_VALID && !timing.source_age_ms && timing.capture_epoch==epoch+1U);
    cerberus_s3_ble_snapshot_finish(&snapshot,true,0,1U);
    assert(!cerberus_watch_capture_get_timing(&snapshot.watch,CERBERUS_WATCH_RADIO_BLE,7U,0U,1000U,&timing));
}

static void make_ibeacon(uint8_t out[27],uint8_t seed,uint16_t major,uint16_t minor,int8_t power)
{
    memset(out,0,27U);out[0]=26;out[1]=0xff;out[2]=0x4c;out[4]=2;out[5]=0x15;
    for(unsigned i=0;i<16U;++i)out[6U+i]=(uint8_t)(seed+i);
    out[22]=(uint8_t)(major>>8);out[23]=(uint8_t)major;
    out[24]=(uint8_t)(minor>>8);out[25]=(uint8_t)minor;out[26]=(uint8_t)power;
}

static void check_ibeacon_provenance(void)
{
    uint8_t a[27],b[27],c[27];
    make_ibeacon(a,1U,1U,2U,-59);make_ibeacon(b,2U,3U,4U,-128);make_ibeacon(c,3U,5U,6U,127);
    const uint8_t ordinary[]={2,1,6};
    const uint8_t malformed[]={3,9,'x'};
    cerberus_s3_ble_report_t r={.address={1,2,3,4,5,6},.address_type=1U,.rssi=-50,
        .event_type=0U,.complete=true,.sid_valid=true,.sid=2U,.data=a,.length=sizeof(a)};
    cerberus_s3_ble_snapshot_begin(&snapshot,110U,1000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_focus_t *f=&record->focus;
    assert(f->ibeacon_valid && !f->ibeacon_scan_response && f->ibeacon_observed_ms==1010U &&
        f->ibeacon_sequence==1U && cerberus_s3_ble_ibeacon_is_current(f));
    r.data=b;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(f->ibeacon.uuid[0]==2U && f->ibeacon.major==3U && f->ibeacon.minor==4U &&
        f->ibeacon.measured_power_dbm==-128 && f->ibeacon_observed_ms==1020U && f->ibeacon_sequence==2U);
    cerberus_s3_ble_focus_t frozen=*f;
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1030U);
    assert(f->ibeacon_valid && !cerberus_s3_ble_ibeacon_is_current(f) && f->ibeacon_observed_ms==1020U);
    assert(record->last_advertisement_ms==1030U && f->category_bits&CERBERUS_S3_BLE_FOCUS_IBEACON);
    assert(frozen.ibeacon.major==3U && frozen.ibeacon_observed_ms==1020U &&
        cerberus_s3_ble_ibeacon_is_current(&frozen));
    uint32_t watch=snapshot.watch.status.reports;uint16_t reports=record->reports_seen;
    r.scan_response=true;r.data=c;r.length=sizeof(c);r.rssi=-10;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1040U);
    assert(f->ibeacon_scan_response && f->ibeacon.major==5U && f->ibeacon_observed_ms==1040U &&
        cerberus_s3_ble_ibeacon_is_current(f));
    assert(record->last_advertisement_ms==1030U && record->advertisement_length==sizeof(ordinary) &&
        !memcmp(record->advertisement,ordinary,sizeof(ordinary)) && record->reports_seen==reports &&
        record->rssi==-50 && snapshot.watch.status.reports==watch);
    uint32_t response_sequence=f->ibeacon_sequence;
    for(unsigned kind=0U;kind<11U;++kind){
        cerberus_s3_ble_report_t bad=r;bad.data=a;uint32_t time=1045U;
        if(kind==0U)bad.address[0]++;
        else if(kind==1U)bad.address_type=0U;
        else if(kind==2U)bad.sid=3U;
        else if(kind==3U)bad.sid_invalid=true;
        else if(kind==4U)bad.event_type=4U;
        else if(kind==5U)bad.complete=false;
        else if(kind==6U){bad.data=malformed;bad.length=sizeof(malformed);}
        else if(kind==7U)bad.data=NULL;
        else if(kind==8U)time=3031U;
        else if(kind==9U)time=1039U;
        else bad.length=256U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&bad,time);
        assert(f->ibeacon_sequence==response_sequence && f->ibeacon_observed_ms==1040U &&
            f->ibeacon.major==5U && cerberus_s3_ble_ibeacon_is_current(f));
    }
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1050U);
    assert(!cerberus_s3_ble_ibeacon_is_current(f) && f->ibeacon_sequence==response_sequence);
    /* Different qualifying ADV/RSP values stay unambiguous: latest qualifying
     * sequence wins, its source/time belongs to that payload alone. */
    r.scan_response=false;r.data=a;r.length=sizeof(a);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1060U);
    r.scan_response=true;r.data=b;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1070U);
    assert(f->ibeacon_scan_response && f->ibeacon.major==3U && f->ibeacon_observed_ms==1070U &&
        f->discovery.advertisement_observed_ms==1060U && cerberus_s3_ble_ibeacon_is_current(f));
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1080U);
    assert(f->ibeacon.major==3U && f->ibeacon_observed_ms==1070U && !cerberus_s3_ble_ibeacon_is_current(f));

    uint8_t oversized[256]={0};
    for(unsigned kind=0U;kind<8U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot,(uint8_t)(120U+kind),4000U,true);
        r.scan_response=false;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=a;r.length=sizeof(a);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4010U);
        r.scan_response=true;r.data=b;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4020U);
        r.scan_response=false;r.data=c;
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U)r.data=NULL;
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else if(kind==6U)r.event_type=4U;
        else snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4030U);
        assert(f->ibeacon_valid && f->ibeacon_scan_response && f->ibeacon.major==3U &&
            f->ibeacon_observed_ms==4020U && !cerberus_s3_ble_ibeacon_is_current(f));
        r.scan_response=true;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=c;r.length=sizeof(c);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4040U);
        assert(f->ibeacon.major==3U && f->ibeacon_observed_ms==4020U && !cerberus_s3_ble_ibeacon_is_current(f));
    }

    /* Complete long reports can qualify despite the bounded raw copy. A
     * conflicting iBeacon interpretation does not poison other families. */
    uint8_t duplicate[54];memcpy(duplicate,a,sizeof(a));memcpy(duplicate+27U,a,sizeof(a));
    cerberus_s3_ble_snapshot_begin(&snapshot,130U,5000U,true);
    r.scan_response=false;r.data=duplicate;r.length=sizeof(duplicate);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5010U);
    assert(f->ibeacon.major==1U && cerberus_s3_ble_ibeacon_is_current(f) &&
        !(f->discovery.flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
    assert(record->advertisement_flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED);
    uint8_t conflict[62];memcpy(conflict,a,sizeof(a));memcpy(conflict+27U,b,sizeof(b));
    const uint8_t other_hints[]={3,3,0x44,0xfd,3,3,0x81,0x30};
    memcpy(conflict+54U,other_hints,sizeof(other_hints));
    r.scan_response=true;r.data=conflict;r.length=sizeof(conflict);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5020U);
    assert(f->ibeacon.major==1U && f->ibeacon_observed_ms==5010U &&
        !f->ibeacon_scan_response && cerberus_s3_ble_ibeacon_is_current(f));
    assert(f->discovery.findmy_scan_response.bits && f->discovery.flipper_scan_response.bits &&
        cerberus_ble_meta_evidence_blocked(&f->discovery.meta_scan_response));
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5030U);
    assert(f->ibeacon.major==1U && !cerberus_s3_ble_ibeacon_is_current(f) &&
        f->discovery.findmy_advertisement.bits && f->discovery.flipper_advertisement.bits);
    cerberus_s3_ble_snapshot_begin(&snapshot,131U,6000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6010U);
    assert(!f->ibeacon_valid && !(f->category_bits&CERBERUS_S3_BLE_FOCUS_IBEACON));

    /* Own-match time is wrap-safe and never moves backwards even if a new
     * original callback has a larger sequence but an older supplied time. */
    cerberus_s3_ble_snapshot_begin(&snapshot,132U,UINT32_MAX-10U,true);
    r.data=a;r.length=sizeof(a);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,UINT32_MAX-10U);
    r.scan_response=true;r.data=b;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5U);
    assert(f->ibeacon.major==3U && f->ibeacon_observed_ms==5U);
    r.scan_response=false;r.data=c;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,4U);
    assert(f->ibeacon.major==3U && f->ibeacon_observed_ms==5U && !cerberus_s3_ble_ibeacon_is_current(f));
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6U);
    assert(f->ibeacon.major==5U && f->ibeacon_observed_ms==6U && cerberus_s3_ble_ibeacon_is_current(f));
    snapshot.report_sequence=UINT32_MAX-1U;r.scan_response=true;r.data=a;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7U);
    assert(f->ibeacon_sequence==UINT32_MAX && f->ibeacon_observed_ms==7U);
    r.scan_response=false;r.data=b;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,8U);
    assert(f->ibeacon_sequence==UINT32_MAX && f->ibeacon.major==1U && !cerberus_s3_ble_ibeacon_is_current(f));
    cerberus_s3_ble_snapshot_begin(&snapshot,133U,7000U,false);
    r.scan_response=true;r.sid_valid=false;r.event_type=4U;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7010U);
    assert(!snapshot.status.stored_count);
    r.scan_response=false;r.event_type=0U;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7020U);
    r.scan_response=true;r.event_type=4U;r.data=b;r.length=sizeof(b);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7030U);
    assert(f->ibeacon_valid && f->ibeacon_scan_response && f->ibeacon_sequence==3U &&
        f->ibeacon_observed_ms==7030U && cerberus_s3_ble_ibeacon_is_current(f));
    assert(!cerberus_s3_ble_ibeacon_is_current(NULL));
    printf("iBeacon latest-source checks passed: payload refresh, own ages, current/history, exact response binding, long/ambiguous reports and wrap; host focus %zu bytes.\n",sizeof(*f));
}

static void check_flock_provenance(void)
{
    const uint8_t ordinary[]={2,1,6};
    const uint8_t fs[]={15,9,'F','S',' ','E','x','t',' ','B','a','t','t','e','r','y'};
    const uint8_t company[]={3,0xff,0xc8,9},payload[]={4,0xff,0xc8,9,0x42};
    const uint8_t serial[]={11,9,'0','1','2','3','4','5','6','7','8','9'};
    const uint8_t raven_a[]={3,3,0,0x31},raven_b[]={3,0x16,0,0x32};
    const uint8_t malformed[]={3,9,'x'};
    uint8_t corroborated[sizeof(fs)+sizeof(payload)],pair[sizeof(raven_a)+sizeof(raven_b)];
    memcpy(corroborated,fs,sizeof(fs));memcpy(corroborated+sizeof(fs),payload,sizeof(payload));
    memcpy(pair,raven_a,sizeof(raven_a));memcpy(pair+sizeof(raven_a),raven_b,sizeof(raven_b));
    cerberus_s3_ble_report_t r={.address={1,2,3,0x81,0x8e,0x58},.address_type=1U,.rssi=-50,
        .event_type=0U,.complete=true,.sid_valid=true,.sid=2U,.data=ordinary,.length=sizeof(ordinary)};
    cerberus_s3_ble_snapshot_begin(&snapshot,140U,1000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_focus_t *f=&record->focus;
    cerberus_s3_ble_discovery_evidence_t *e=&f->discovery;
    assert(e->flock_advertisement.facts==CERBERUS_BLE_FLOCK_NO_LOCAL_NAME &&
        cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    assert(!f->flock_oui_clue && !(f->category_bits&CERBERUS_S3_BLE_FOCUS_FLOCK));
    uint32_t watch=snapshot.watch.status.reports;
    r.scan_response=true;r.data=company;r.length=sizeof(company);r.rssi=-10;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(cerberus_ble_flock_hints_level(&e->flock_scan_response)==CERBERUS_BLE_FLOCK_LEVEL_CLUE);
    assert(f->category_bits&CERBERUS_S3_BLE_FOCUS_FLOCK && strstr(f->flock_evidence,"RESPONSE"));
    assert(!f->flock_oui_clue && record->last_advertisement_ms==1010U && record->rssi==-50 &&
        record->reports_seen==1U && snapshot.watch.status.reports==watch &&
        !memcmp(record->advertisement,ordinary,sizeof(ordinary)));
    r.data=fs;r.length=sizeof(fs);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1030U);
    assert(cerberus_ble_flock_hints_level(&e->flock_scan_response)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    assert(!(e->flock_scan_response.facts&CERBERUS_BLE_FLOCK_MFG_09C8));
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1040U);
    assert(!f->flock_evidence && !(f->category_bits&CERBERUS_S3_BLE_FOCUS_FLOCK));
    /* Cached names and opposite-source manufacturer fields cannot upgrade
     * either independent report into corroborated evidence. */
    r.scan_response=false;r.data=fs;r.length=sizeof(fs);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1050U);
    r.scan_response=true;r.data=payload;r.length=sizeof(payload);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1060U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE &&
        cerberus_ble_flock_hints_level(&e->flock_scan_response)==CERBERUS_BLE_FLOCK_LEVEL_CLUE);
    assert(!(e->flock_scan_response.facts&CERBERUS_BLE_FLOCK_FS_BATTERY));
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1070U);
    r.scan_response=true;r.data=fs;r.length=sizeof(fs);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1080U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_CLUE &&
        cerberus_ble_flock_hints_level(&e->flock_scan_response)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE);
    r.scan_response=false;r.data=raven_a;r.length=sizeof(raven_a);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1090U);
    r.scan_response=true;r.data=raven_b;r.length=sizeof(raven_b);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1100U);
    assert(e->flock_advertisement.raven_3100_contexts==CERBERUS_BLE_FLOCK_LIST16 &&
        e->flock_scan_response.raven_3200_contexts==CERBERUS_BLE_FLOCK_DATA16);
    assert(!cerberus_ble_flock_hints_level(&e->flock_advertisement) &&
        !cerberus_ble_flock_hints_level(&e->flock_scan_response) && !f->flock_evidence);
    r.scan_response=false;r.data=pair;r.length=sizeof(pair);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1110U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE &&
        e->flock_advertisement.raven_3100_contexts==CERBERUS_BLE_FLOCK_LIST16 &&
        e->flock_advertisement.raven_3200_contexts==CERBERUS_BLE_FLOCK_DATA16);
    uint8_t weak[sizeof(serial)+sizeof(company)];memcpy(weak,serial,sizeof(serial));
    memcpy(weak+sizeof(serial),company,sizeof(company));
    r.data=weak;r.length=sizeof(weak);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1120U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_CLUE &&
        (e->flock_advertisement.facts&CERBERUS_BLE_FLOCK_SERIAL10));
    r.data=corroborated;r.length=sizeof(corroborated);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1130U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED);
    cerberus_s3_ble_discovery_evidence_t frozen=*e;
    for(unsigned kind=0U;kind<11U;++kind){
        cerberus_s3_ble_report_t bad=r;bad.scan_response=true;uint32_t time=1140U;
        if(kind==0U)bad.address[0]++;
        else if(kind==1U)bad.address_type=0U;
        else if(kind==2U)bad.sid=3U;
        else if(kind==3U)bad.sid_invalid=true;
        else if(kind==4U)bad.event_type=4U;
        else if(kind==5U)bad.complete=false;
        else if(kind==6U){bad.data=malformed;bad.length=sizeof(malformed);}
        else if(kind==7U)bad.data=NULL;
        else if(kind==8U)time=3131U;
        else if(kind==9U)time=1129U;
        else bad.length=256U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&bad,time);
        assert(!memcmp(e,&frozen,sizeof(frozen)) && !f->flock_oui_clue);
    }
    uint8_t oversized[256]={0};cerberus_ble_flock_hints_t zero={0};
    for(unsigned kind=0U;kind<8U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot,(uint8_t)(150U+kind),4000U,true);
        r.scan_response=false;r.complete=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=corroborated;r.length=sizeof(corroborated);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4010U);
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U)r.data=NULL;
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else if(kind==6U)r.event_type=4U;
        else snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4020U);
        assert(!memcmp(&e->flock_advertisement,&zero,sizeof(zero)) &&
            !memcmp(&e->flock_scan_response,&zero,sizeof(zero)) && !f->flock_oui_clue && !f->flock_evidence);
        r.scan_response=true;r.complete=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=corroborated;r.length=sizeof(corroborated);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4030U);
        assert(!e->scan_response_sequence && !f->flock_evidence);
    }

    /* A stronger Watch winner cannot hide another family's Flock signature. */
    const uint8_t both[31]={3,3,0x80,0x30,8,9,'F','l','i','p','p','e','r',
        17,7,0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8};
    cerberus_s3_ble_snapshot_begin(&snapshot,160U,5000U,true);
    r.scan_response=false;r.data=both;r.length=sizeof(both);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5010U);
    assert((f->category_bits&(CERBERUS_S3_BLE_FOCUS_FLIPPER|CERBERUS_S3_BLE_FOCUS_FLOCK))==
        (CERBERUS_S3_BLE_FOCUS_FLIPPER|CERBERUS_S3_BLE_FOCUS_FLOCK));
    assert(e->flock_advertisement.accessory_contexts==CERBERUS_BLE_FLOCK_LIST128);
    uint8_t extended[255]={233U,0x30U};memcpy(extended+234U,corroborated,sizeof(corroborated));
    r.data=extended;r.length=sizeof(extended);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5020U);
    assert(cerberus_ble_flock_hints_level(&e->flock_advertisement)==CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED &&
        !(e->flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
    assert(frozen.flock_advertisement.facts==(CERBERUS_BLE_FLOCK_FS_BATTERY|
        CERBERUS_BLE_FLOCK_MFG_09C8|CERBERUS_BLE_FLOCK_MFG_PAYLOAD));

    /* Address-only clues begin after a valid original, remain separate from
     * later invalid source data, and never attach to random address types. */
    cerberus_s3_ble_snapshot_begin(&snapshot,161U,6000U,true);
    r.address_type=0U;r.data=malformed;r.length=sizeof(malformed);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6010U);
    assert(!f->flock_oui_clue && !f->flock_evidence);
    r.scan_response=true;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6020U);
    assert(!f->flock_oui_clue);
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6030U);
    assert(f->flock_oui_clue && f->category_bits==CERBERUS_S3_BLE_FOCUS_FLOCK &&
        !cerberus_ble_flock_hints_level(&e->flock_advertisement) && strstr(f->flock_evidence,"ADDRESS"));
    r.complete=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6040U);
    assert(f->flock_oui_clue && !memcmp(&e->flock_advertisement,&zero,sizeof(zero)) &&
        !(e->flags&CERBERUS_S3_BLE_EVIDENCE_ADV_VALID) && strstr(f->flock_evidence,"ADDRESS"));
    r.complete=true;
    for(unsigned type=1U;type<=3U;++type){
        r.address_type=(uint8_t)type;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,6050U+type);
        assert(snapshot.records[type].focus.flock_oui_clue==(type==2U));
    }
    r.address_type=0U;r.address[5]|=2U;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6060U);
    assert(!snapshot.records[4].focus.flock_oui_clue);
    r.address[5]=0x59U;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6070U);
    assert(!snapshot.records[5].focus.flock_oui_clue);
    cerberus_s3_ble_snapshot_begin(&snapshot,162U,7000U,true);
    r.address[5]=0x58U;r.scan_response=true;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7010U);
    assert(!snapshot.status.stored_count && !snapshot.records[0].focus.flock_oui_clue);
    puts("Flock source checks passed: independent levels/context, no cross-report corroboration, invalidation, Watch independence and address-only OUI lifecycle.");
}

static void check_findmy_provenance(void)
{
    const uint8_t ordinary[] = {2,1,6};
    const uint8_t fmna16[] = {3,3,0x44,0xfd};
    const uint8_t fmna32[] = {5,5,0x44,0xfd,0,0};
    const uint8_t fmna128[] = {17,7,0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,
        0,0x10,0,0,0x44,0xfd,0,0};
    const uint8_t dult[] = {17,7,0x85,0x2a,0x9f,0x57,0xc5,0x2a,0xed,0x88,
        0x26,0xc2,0xf4,0x12,1,0,0x19,0x15};
    uint8_t apple[31] = {30,0xff,0x4c,0,0x12,0x19};
    cerberus_s3_ble_report_t r = {.address={1,2,3,4,5,6},.address_type=1U,
        .rssi=-50,.event_type=0U,.connectable=true,.complete=true,
        .sid_valid=true,.sid=2U,.data=ordinary,.length=sizeof(ordinary)};
    cerberus_s3_ble_snapshot_begin(&snapshot, 20U, 1000U, true);
    r.scan_response=true;r.data=fmna16;r.length=sizeof(fmna16);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1001U);
    assert(!snapshot.status.stored_count); /* No response-only identity. */
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_discovery_evidence_t *e=&record->focus.discovery;
    const uint8_t valid_original = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_ADV_VALID | CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_SID_VALID;
    assert(e->advertisement_sequence==2U && e->advertisement_observed_ms==1010U);
    assert(e->generation==20U && e->address_type==1U && !memcmp(e->address,r.address,6U));
    assert(e->flags==valid_original && !e->findmy_advertisement.bits && !e->findmy_scan_response.bits);
    uint32_t original_sequence=e->advertisement_sequence;
    uint32_t watch_reports=snapshot.watch.status.reports;
    uint16_t activity_reports=snapshot.activity.status.reports;
    r.scan_response=true;r.data=fmna128;r.length=sizeof(fmna128);r.rssi=-10;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(e->findmy_scan_response.bits==CERBERUS_BLE_FINDMY_HINT_FMNA);
    assert(e->findmy_scan_response.fmna_uuid_widths==CERBERUS_BLE_FINDMY_UUID128);
    assert(e->scan_response_observed_ms==1020U && e->scan_response_sequence>original_sequence);
    assert(e->flags==(valid_original|CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID));
    assert(record->focus.category_bits&CERBERUS_S3_BLE_FOCUS_FIND_MY);
    assert(strstr(record->focus.findmy_evidence,"RESPONSE"));
    assert(record->last_advertisement_ms==1010U && record->advertisement_length==sizeof(ordinary));
    assert(!memcmp(record->advertisement,ordinary,sizeof(ordinary)) && record->rssi==-50);
    assert(record->reports_seen==1U && snapshot.watch.status.reports==watch_reports &&
        snapshot.activity.status.reports==activity_reports);
    assert(e->advertisement_sequence==original_sequence && e->advertisement_observed_ms==1010U);
    cerberus_s3_ble_discovery_evidence_t frozen=*e;

    /* A new ordinary original clears current response hints; no historical
     * hint is relabeled with the new raw packet's timestamp. */
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1030U);
    assert(!e->findmy_advertisement.bits && !e->findmy_scan_response.bits && !e->scan_response_sequence);
    assert(!(record->focus.category_bits&CERBERUS_S3_BLE_FOCUS_FIND_MY));
    assert(e->advertisement_observed_ms==1030U && frozen.scan_response_observed_ms==1020U);

    /* Mismatched identities/types/SIDs and stale or malformed responses
     * cannot publish a hint against the current original. */
    for(unsigned kind=0U;kind<11U;++kind){
        cerberus_s3_ble_report_t response=r;
        response.scan_response=true;response.data=fmna16;response.length=sizeof(fmna16);
        uint32_t time=1040U;
        if(kind==0U)response.address[0]++;
        else if(kind==1U)response.address_type=0U;
        else if(kind==2U)response.sid=3U;
        else if(kind==3U)response.sid_valid=false;
        else if(kind==4U)response.sid_invalid=true;
        else if(kind==5U)time=3031U;
        else if(kind==6U)response.complete=false;
        else if(kind==7U)response.length=3U;
        else if(kind==8U)response.event_type=1U;
        else if(kind==9U)response.event_type=4U;
        else response.event_type=5U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&response,time);
        assert(!e->scan_response_sequence && !e->findmy_scan_response.bits);
    }

    /* Incomplete/oversize/null originals keep the old raw capture exactly,
     * yet invalidate response binding before their early returns. */
    uint8_t oversized[256]={0};const uint8_t malformed[]={5,9,'x'};
    for(unsigned kind=0U;kind<7U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot, (uint8_t)(30U+kind), 4000U, true);
        r.scan_response=false;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=ordinary;r.length=sizeof(ordinary);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4010U);
        r.scan_response=true;r.data=fmna16;r.length=sizeof(fmna16);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4020U);
        assert(e->findmy_scan_response.bits);
        r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U){r.data=NULL;r.length=1U;}
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else r.event_type=4U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4030U);
        assert(!e->findmy_advertisement.bits && !e->findmy_scan_response.bits && !e->scan_response_sequence);
        assert(!(e->flags&CERBERUS_S3_BLE_EVIDENCE_ADV_VALID));
        if(kind<=2U)assert(record->last_advertisement_ms==4010U &&
            record->advertisement_length==sizeof(ordinary) &&
            !memcmp(record->advertisement,ordinary,sizeof(ordinary)));
        assert(e->advertisement_observed_ms==4030U); /* Truthful rejected-source provenance. */
        r.scan_response=true;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=fmna16;r.length=sizeof(fmna16);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4040U);
        assert(!e->scan_response_sequence && !(record->focus.category_bits&CERBERUS_S3_BLE_FOCUS_FIND_MY));
    }

    /* Full extended reports are independently complete even when the stored
     * 31-byte raw copy is truncated. They may bind a new passive response. */
    uint8_t mixed[sizeof(apple)+sizeof(fmna16)+sizeof(dult)];
    memcpy(mixed,apple,sizeof(apple));memcpy(mixed+sizeof(apple),fmna16,sizeof(fmna16));
    memcpy(mixed+sizeof(apple)+sizeof(fmna16),dult,sizeof(dult));
    cerberus_s3_ble_snapshot_begin(&snapshot, 40U, 5000U, true);
    r.scan_response=false;r.data=mixed;r.length=sizeof(mixed);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5010U);
    assert(e->findmy_advertisement.bits==7U && e->findmy_advertisement.fmna_uuid_widths==CERBERUS_BLE_FINDMY_UUID16);
    assert((e->flags&(CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_ADV_VALID))==
        (CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE|CERBERUS_S3_BLE_EVIDENCE_ADV_VALID));
    assert(!(e->flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
    assert(record->advertisement_flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED && record->advertisement_length==31U);
    r.scan_response=true;r.data=fmna32;r.length=sizeof(fmna32);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5020U);
    assert(e->findmy_scan_response.bits==CERBERUS_BLE_FINDMY_HINT_FMNA &&
        e->findmy_scan_response.fmna_uuid_widths==CERBERUS_BLE_FINDMY_UUID32);
    assert(record->last_advertisement_ms==5010U && !memcmp(record->advertisement,apple,sizeof(apple)));

    cerberus_s3_ble_snapshot_begin(&snapshot, 41U, UINT32_MAX-1000U, true);
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,UINT32_MAX-1000U);
    r.scan_response=true;r.data=fmna16;r.length=sizeof(fmna16);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,500U);
    assert(e->findmy_scan_response.bits && e->scan_response_observed_ms==500U);
    uint32_t response_sequence=e->scan_response_sequence;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,400U);
    assert(e->scan_response_sequence==response_sequence); /* No backwards response replacement. */
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1001U);
    assert(e->scan_response_sequence==response_sequence); /* Expired across timer wrap. */

    /* Sequence exhaustion never reuses a provenance token within a sweep. */
    cerberus_s3_ble_snapshot_begin(&snapshot, 42U, 6000U, true);
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6010U);
    snapshot.report_sequence=UINT32_MAX-1U;
    r.scan_response=true;r.data=fmna16;r.length=sizeof(fmna16);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6020U);
    assert(e->scan_response_sequence==UINT32_MAX && e->findmy_scan_response.bits);
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6030U);
    assert(!e->advertisement_sequence && !e->findmy_scan_response.bits &&
        !(e->flags&CERBERUS_S3_BLE_EVIDENCE_ADV_VALID));
    r.scan_response=true;r.data=fmna16;r.length=sizeof(fmna16);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,6040U);
    assert(snapshot.report_sequence==UINT32_MAX && !e->scan_response_sequence);
    cerberus_s3_ble_snapshot_begin(&snapshot, 43U, 7000U, true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7010U);
    assert(!snapshot.status.stored_count); /* New sweep cannot inherit old binding. */
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,7020U);
    assert(e->generation==43U && e->advertisement_sequence==2U && e->findmy_advertisement.bits);

    /* The separate legacy response publication path uses event4/no SID. */
    cerberus_s3_ble_snapshot_begin(&snapshot, 44U, 8000U, false);
    r.scan_response=false;r.sid_valid=false;r.sid=0U;r.event_type=0U;
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,8010U);
    r.scan_response=true;r.event_type=4U;r.data=fmna16;r.length=sizeof(fmna16);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,8020U);
    assert(e->findmy_scan_response.bits && (e->flags&CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID));
    assert(!(e->flags&CERBERUS_S3_BLE_EVIDENCE_SID_VALID) && record->last_advertisement_ms==8010U);
    assert(sizeof(*e)<=64U);
    printf("Shared discovery provenance: %zu bytes per record, %zu-byte host snapshot; original/response identity, sequence, SID, time, invalidation and truncation checks passed.\n",
        sizeof(*e),sizeof(snapshot));
}

static void check_shared_discovery(void)
{
    const uint8_t ordinary[] = {2,1,6};
    const uint8_t mixed[] = {5,3,0x44,0xfd,0x81,0x30};
    const uint8_t response_mixed[] = {5,5,0x44,0xfd,0,0,3,3,0x82,0x30};
    const uint8_t transparent[] = {3,3,0x83,0x30};
    const uint8_t base[] = {3,3,0x80,0x30};
    const uint8_t name[] = {8,9,'F','l','i','p','p','e','r'};
    const uint8_t corroborated[] = {3,3,0x80,0x30,8,9,'F','l','i','p','p','e','r'};
    const uint8_t service_data[] = {3,0x16,0x80,0x30};
    const uint8_t named_data[] = {3,0x16,0x81,0x30,8,9,'F','l','i','p','p','e','r'};
    const uint8_t malformed[] = {5,3,0x44,0xfd,0x81,0x30,3,9,'x'};
    cerberus_s3_ble_report_t r = {.address={1,2,3,4,5,6},.address_type=1U,
        .rssi=-50,.event_type=0U,.complete=true,.sid_valid=true,.sid=2U,
        .data=mixed,.length=sizeof(mixed)};
    cerberus_s3_ble_snapshot_begin(&snapshot,50U,1000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_discovery_evidence_t *e=&record->focus.discovery;
    const uint8_t both=CERBERUS_S3_BLE_FOCUS_FIND_MY|CERBERUS_S3_BLE_FOCUS_FLIPPER;
    assert(record->focus.category_bits==both);
    assert(e->findmy_advertisement.bits==CERBERUS_BLE_FINDMY_HINT_FMNA);
    assert(e->flipper_advertisement.bits==CERBERUS_BLE_FLIPPER_HINT_BLACK);
    assert(e->flipper_advertisement.flags==CERBERUS_BLE_FLIPPER_UUID16);
    const uint32_t original_sequence=e->advertisement_sequence;
    const uint32_t watch_reports=snapshot.watch.status.reports;
    const uint16_t activity_reports=snapshot.activity.status.reports;
    r.scan_response=true;r.data=response_mixed;r.length=sizeof(response_mixed);r.rssi=-10;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(e->findmy_scan_response.bits==CERBERUS_BLE_FINDMY_HINT_FMNA);
    assert(e->findmy_scan_response.fmna_uuid_widths==CERBERUS_BLE_FINDMY_UUID32);
    assert(e->flipper_scan_response.bits==CERBERUS_BLE_FLIPPER_HINT_WHITE);
    assert(e->scan_response_observed_ms==1020U && e->scan_response_sequence>original_sequence);
    assert(e->advertisement_sequence==original_sequence && e->advertisement_observed_ms==1010U);
    assert(strstr(record->focus.flipper_evidence,"ADV + MATCHED RESPONSE"));
    assert(record->advertisement_length==sizeof(mixed) && !memcmp(record->advertisement,mixed,sizeof(mixed)));
    assert(record->last_advertisement_ms==1010U && record->reports_seen==1U && record->rssi==-50);
    assert(snapshot.watch.status.reports==watch_reports && snapshot.activity.status.reports==activity_reports);
    cerberus_s3_ble_discovery_evidence_t frozen=*e;

    /* Both parsers share binding rejection, and invalid responses cannot
     * mutate either family's last successfully bound evidence. */
    for(unsigned kind=0U;kind<13U;++kind){
        cerberus_s3_ble_report_t bad=r;
        uint32_t time=1030U;
        if(kind==0U)bad.address[0]++;
        else if(kind==1U)bad.address_type=0U;
        else if(kind==2U)bad.sid=3U;
        else if(kind==3U)bad.sid_valid=false;
        else if(kind==4U)bad.sid_invalid=true;
        else if(kind==5U)bad.complete=false;
        else if(kind==6U){bad.data=malformed;bad.length=sizeof(malformed);}
        else if(kind==7U)bad.data=NULL;
        else if(kind==8U)bad.event_type=4U;
        else if(kind==9U)time=3011U;
        else if(kind==10U)time=1019U;
        else if(kind==11U)bad.length=0U;
        else bad.length=256U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&bad,time);
        assert(!memcmp(e,&frozen,sizeof(frozen)));
    }
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1040U);
    assert(!e->findmy_scan_response.bits && !e->flipper_scan_response.bits);
    assert(e->scan_response_observed_ms==1040U && (e->flags&CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID));
    assert(record->focus.category_bits==both); /* Current originals still qualify. */
    assert(!strstr(record->focus.flipper_evidence,"RESPONSE"));
    assert(frozen.flipper_scan_response.bits==CERBERUS_BLE_FLIPPER_HINT_WHITE &&
        frozen.scan_response_observed_ms==1020U); /* Frozen copy is immutable. */
    r.scan_response=false;r.data=transparent;r.length=sizeof(transparent);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1050U);
    assert(record->focus.category_bits==CERBERUS_S3_BLE_FOCUS_FLIPPER);
    assert(e->flipper_advertisement.bits==CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT);
    assert(!e->findmy_advertisement.bits && !e->scan_response_sequence);
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1060U);
    assert(!record->focus.category_bits && !record->focus.flipper_evidence && !record->focus.findmy_evidence);

    /* Preserve the existing 3080+name compatibility only within one source.
     * Cached display names and adjacent ADV/response packets are not proof. */
    r.data=corroborated;r.length=sizeof(corroborated);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1070U);
    assert(e->flipper_advertisement.bits==CERBERUS_BLE_FLIPPER_HINT_BASE);
    assert(e->flipper_advertisement.flags==(CERBERUS_BLE_FLIPPER_UUID16|CERBERUS_BLE_FLIPPER_NAME));
    assert(record->focus.category_bits==CERBERUS_S3_BLE_FOCUS_FLIPPER);
    r.data=base;r.length=sizeof(base);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1080U);
    assert(record->name_length==7U && !record->focus.category_bits && !e->flipper_advertisement.bits);
    r.scan_response=true;r.data=name;r.length=sizeof(name);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1090U);
    assert(!e->flipper_scan_response.bits && e->flipper_scan_response.flags==CERBERUS_BLE_FLIPPER_NAME);
    assert(!record->focus.category_bits);
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1100U);
    r.scan_response=true;r.data=base;r.length=sizeof(base);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1110U);
    assert(!e->flipper_scan_response.bits && !record->focus.category_bits);
    r.data=corroborated;r.length=sizeof(corroborated);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1120U);
    assert(e->flipper_scan_response.bits==CERBERUS_BLE_FLIPPER_HINT_BASE);
    assert(strstr(record->focus.flipper_evidence,"MATCHED RESPONSE"));
    assert(e->advertisement_observed_ms==1100U && record->last_advertisement_ms==1100U);

    /* Keep Watch's stronger named service-data compatibility within each
     * complete report, without promoting unnamed prefixes or cached names. */
    r.scan_response=false;r.data=named_data;r.length=sizeof(named_data);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1130U);
    assert(e->flipper_advertisement.bits==CERBERUS_BLE_FLIPPER_HINT_BLACK);
    assert(e->flipper_advertisement.flags==(CERBERUS_BLE_FLIPPER_UUID16|
        CERBERUS_BLE_FLIPPER_NAME|CERBERUS_BLE_FLIPPER_SERVICE_DATA));
    assert(record->focus.category_bits==CERBERUS_S3_BLE_FOCUS_FLIPPER);
    r.data=service_data;r.length=sizeof(service_data);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1140U);
    assert(!record->focus.category_bits && !e->flipper_advertisement.bits);
    r.scan_response=true;r.data=name;r.length=sizeof(name);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1150U);
    assert(!e->flipper_scan_response.bits && !record->focus.category_bits);
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1160U);
    r.scan_response=true;r.data=service_data;r.length=sizeof(service_data);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1170U);
    assert(!e->flipper_scan_response.bits && !record->focus.category_bits);
    r.data=named_data;r.length=sizeof(named_data);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1180U);
    assert(e->flipper_scan_response.bits==CERBERUS_BLE_FLIPPER_HINT_BLACK);
    assert(e->flipper_scan_response.flags==(CERBERUS_BLE_FLIPPER_UUID16|
        CERBERUS_BLE_FLIPPER_NAME|CERBERUS_BLE_FLIPPER_SERVICE_DATA));
    assert(record->focus.category_bits==CERBERUS_S3_BLE_FOCUS_FLIPPER);
    assert(e->advertisement_observed_ms==1160U && record->last_advertisement_ms==1160U);

    /* Every rejected replacement invalidates both families before the raw
     * path's early returns, and later responses cannot revive the old source. */
    uint8_t oversized[256]={0};
    for(unsigned kind=0U;kind<8U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot,(uint8_t)(60U+kind),2000U,true);
        r.scan_response=false;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=mixed;r.length=sizeof(mixed);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2010U);
        r.scan_response=true;r.data=response_mixed;r.length=sizeof(response_mixed);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2020U);
        assert(record->focus.category_bits==both);
        r.scan_response=false;r.data=mixed;r.length=sizeof(mixed);
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U)r.data=NULL;
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else if(kind==6U)r.event_type=4U;
        else snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2030U);
        assert(!(e->flags&CERBERUS_S3_BLE_EVIDENCE_ADV_VALID));
        assert(!e->findmy_advertisement.bits && !e->findmy_scan_response.bits &&
            !e->flipper_advertisement.bits && !e->flipper_scan_response.bits);
        assert(!record->focus.category_bits && !e->scan_response_sequence);
        if(kind<=2U)assert(record->last_advertisement_ms==2010U &&
            record->advertisement_length==sizeof(mixed) && !memcmp(record->advertisement,mixed,sizeof(mixed)));
        r.scan_response=true;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=response_mixed;r.length=sizeof(response_mixed);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2040U);
        assert(!e->scan_response_sequence && !record->focus.category_bits);
    }

    /* Source validation uses all extended bytes; truncated raw storage is an
     * independent property and does not discard complete passive evidence. */
    uint8_t long_ad[42]={0};
    long_ad[0]=35U;long_ad[1]=0x30U; /* Valid unrelated field before the hints. */
    memcpy(long_ad+36U,mixed,sizeof(mixed));
    cerberus_s3_ble_snapshot_begin(&snapshot,70U,3000U,true);
    r.scan_response=false;r.data=long_ad;r.length=sizeof(long_ad);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3010U);
    assert(record->focus.category_bits==both && !(e->flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
    assert(record->advertisement_length==31U && record->advertisement_flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED);
    r.scan_response=true;r.data=response_mixed;r.length=sizeof(response_mixed);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3020U);
    assert(e->findmy_scan_response.bits && e->flipper_scan_response.bits);
    assert(record->last_advertisement_ms==3010U && e->advertisement_observed_ms==3010U);
    puts("Shared Find My/Flipper discovery checks passed: independent current sources, exact response binding, no cross-packet corroboration, invalidation and extended raw truncation.");
}

static void check_meta_provenance(void)
{
    const uint8_t ordinary[]={2,1,6};
    const uint8_t adv_ids[]={3,0xff,0x53,0x0d,3,0x16,0x5f,0xfd};
    const uint8_t rsp_ids[]={3,0xff,0xb7,0xfe,3,3,0xb8,0xfe};
    const uint8_t blocked[]={3,0xff,0x4c,0};
    const uint8_t malformed[]={3,0xff,0x4c,0,3,9,'x'};
    cerberus_s3_ble_report_t r={.address={1,2,3,4,5,6},.address_type=1U,.rssi=-50,
        .event_type=0U,.complete=true,.sid_valid=true,.sid=2U,.data=adv_ids,.length=sizeof(adv_ids)};
    cerberus_s3_ble_snapshot_begin(&snapshot,80U,1000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_discovery_evidence_t *e=&record->focus.discovery;
    assert(cerberus_ble_meta_evidence_count(&e->meta_advertisement)==2U);
    assert(cerberus_ble_meta_evidence_has(&e->meta_advertisement,5U,CERBERUS_BLE_META_MFG16));
    assert(cerberus_ble_meta_evidence_has(&e->meta_advertisement,0U,CERBERUS_BLE_META_DATA16));
    assert(!cerberus_ble_meta_evidence_has(&e->meta_advertisement,5U,CERBERUS_BLE_META_DATA16));
    assert(!cerberus_ble_meta_evidence_has(&e->meta_advertisement,0U,CERBERUS_BLE_META_MFG16));
    assert(record->focus.category_bits==CERBERUS_S3_BLE_FOCUS_META_HINT && !record->focus.meta_suppressed);
    const uint32_t watch_reports=snapshot.watch.status.reports;
    r.scan_response=true;r.data=rsp_ids;r.length=sizeof(rsp_ids);r.rssi=-10;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(cerberus_ble_meta_evidence_count(&e->meta_scan_response)==2U);
    assert(cerberus_ble_meta_evidence_has(&e->meta_scan_response,1U,CERBERUS_BLE_META_MFG16));
    assert(cerberus_ble_meta_evidence_has(&e->meta_scan_response,2U,CERBERUS_BLE_META_LIST16));
    assert(!cerberus_ble_meta_evidence_has(&e->meta_scan_response,0U,CERBERUS_BLE_META_DATA16));
    assert(e->advertisement_observed_ms==1010U && e->scan_response_observed_ms==1020U);
    assert(record->last_advertisement_ms==1010U && record->reports_seen==1U && record->rssi==-50 &&
        !memcmp(record->advertisement,adv_ids,sizeof(adv_ids)) && snapshot.watch.status.reports==watch_reports);
    cerberus_s3_ble_discovery_evidence_t frozen=*e;
    for(unsigned kind=0;kind<11U;++kind){
        cerberus_s3_ble_report_t bad=r;bad.data=blocked;bad.length=sizeof(blocked);
        uint32_t time=1030U;
        if(kind==0U)bad.address[0]++;
        else if(kind==1U)bad.address_type=0U;
        else if(kind==2U)bad.sid=3U;
        else if(kind==3U)bad.sid_invalid=true;
        else if(kind==4U)bad.event_type=4U;
        else if(kind==5U)bad.complete=false;
        else if(kind==6U){bad.data=malformed;bad.length=sizeof(malformed);}
        else if(kind==7U)bad.data=NULL;
        else if(kind==8U)time=3011U;
        else if(kind==9U)time=1019U;
        else bad.length=256U;
        cerberus_s3_ble_snapshot_observe(&snapshot,&bad,time);
        assert(!memcmp(e,&frozen,sizeof(frozen)) && !record->focus.meta_suppressed);
    }
    r.data=blocked;r.length=sizeof(blocked);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1040U);
    assert(record->focus.meta_suppressed && !(record->focus.category_bits&CERBERUS_S3_BLE_FOCUS_META_HINT));
    assert(cerberus_ble_meta_evidence_blocked(&e->meta_scan_response));
    r.scan_response=false;r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1050U);
    assert(!cerberus_ble_meta_evidence_count(&e->meta_advertisement) &&
        !cerberus_ble_meta_evidence_count(&e->meta_scan_response));
    assert(record->focus.meta_suppressed && !record->focus.meta_evidence); /* Historical suppression, no invented current block. */
    r.data=adv_ids;r.length=sizeof(adv_ids);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1060U);
    assert(cerberus_ble_meta_evidence_allowed(&e->meta_advertisement) &&
        !cerberus_ble_meta_evidence_blocked(&e->meta_advertisement));
    assert(record->focus.meta_suppressed && !record->focus.meta_evidence);

    /* Blocked-before-positive has the same sweep precedence; a new sweep
     * resets suppression and ordinary replacement clears positive routing. */
    cerberus_s3_ble_snapshot_begin(&snapshot,81U,2000U,true);
    r.data=blocked;r.length=sizeof(blocked);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,2010U);
    r.data=adv_ids;r.length=sizeof(adv_ids);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,2020U);
    assert(record->focus.meta_suppressed && !record->focus.meta_evidence);
    cerberus_s3_ble_snapshot_begin(&snapshot,82U,3000U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3010U);
    assert(!record->focus.meta_suppressed && record->focus.meta_evidence);
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3020U);
    assert(!record->focus.meta_evidence && !(record->focus.category_bits&CERBERUS_S3_BLE_FOCUS_META_HINT));

    /* Rejected originals retire both current matrices without allowing an
     * invalid blocked prefix to suppress future valid evidence. */
    uint8_t oversized[256]={0};
    for(unsigned kind=0;kind<8U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot,(uint8_t)(90U+kind),4000U,true);
        r.scan_response=false;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=adv_ids;r.length=sizeof(adv_ids);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4010U);
        r.scan_response=true;r.data=rsp_ids;r.length=sizeof(rsp_ids);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4020U);
        r.scan_response=false;r.data=blocked;r.length=sizeof(blocked);
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U)r.data=NULL;
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else if(kind==6U)r.event_type=4U;
        else snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4030U);
        assert(!cerberus_ble_meta_evidence_count(&e->meta_advertisement) &&
            !cerberus_ble_meta_evidence_count(&e->meta_scan_response));
        assert(!record->focus.meta_suppressed && !record->focus.meta_evidence);
        if(kind<=2U)assert(record->last_advertisement_ms==4010U &&
            !memcmp(record->advertisement,adv_ids,sizeof(adv_ids)));
        r.scan_response=true;r.complete=true;r.sid_valid=true;r.sid_invalid=false;r.sid=2U;r.event_type=0U;
        r.data=blocked;r.length=sizeof(blocked);
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,4040U);
        assert(!record->focus.meta_suppressed && !e->scan_response_sequence);
    }
    uint8_t extended[44]={35U,0x30U};
    memcpy(extended+36U,adv_ids,sizeof(adv_ids));
    cerberus_s3_ble_snapshot_begin(&snapshot,100U,5000U,true);
    r.scan_response=false;r.data=extended;r.length=sizeof(extended);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,5010U);
    assert(cerberus_ble_meta_evidence_count(&e->meta_advertisement)==2U && record->focus.meta_evidence);
    assert(!(e->flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE) &&
        record->advertisement_flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED);
    puts("Meta source checks passed: exact ID/context pairs, source binding, historical suppression, invalidation and full extended evidence.");
}

static void check_hc_provenance(void)
{
    const uint8_t ordinary[]={2,1,6},malformed[]={6,9,'H','C','-','0','5',2,3,1};
    uint8_t hc[]={6,9,'H','C','-','0','3'},oversized[256]={0};
    cerberus_s3_ble_report_t r={.address={1,2,3,4,5,6},.address_type=1U,.rssi=-50,
        .event_type=0U,.complete=true,.sid_valid=true,.sid=2U,.data=hc,.length=sizeof(hc)};
    cerberus_s3_ble_snapshot_begin(&snapshot,31U,1000U,true);
    r.scan_response=true;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1001U);
    assert(!snapshot.status.stored_count);
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U);
    cerberus_s3_ble_record_t *record=&snapshot.records[0];
    cerberus_s3_ble_focus_t *f=&record->focus;
    cerberus_s3_ble_discovery_evidence_t *e=&f->discovery;
    assert(e->hc_advertisement==CERBERUS_BLE_HC_03 && !e->hc_scan_response &&
        (f->category_bits&CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
    hc[6]='5';r.scan_response=true;r.rssi=20;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1020U);
    assert(e->hc_advertisement==CERBERUS_BLE_HC_03 && e->hc_scan_response==CERBERUS_BLE_HC_05 &&
        e->advertisement_observed_ms==1010U && e->scan_response_observed_ms==1020U);
    assert(record->last_advertisement_ms==1010U && record->advertisement[6]=='3' &&
        record->reports_seen==1U && record->rssi==-50 && snapshot.watch.status.reports==1U);
    cerberus_s3_ble_focus_t selected=*f;
    hc[6]='6';
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1030U);
    assert(e->hc_scan_response==CERBERUS_BLE_HC_06 && e->scan_response_observed_ms==1030U &&
        selected.discovery.hc_scan_response==CERBERUS_BLE_HC_05 &&
        selected.discovery.scan_response_observed_ms==1020U);

    /* Rejected responses cannot refresh codes, source time, or raw capture. */
    cerberus_s3_ble_focus_t frozen=*f;
    for(unsigned kind=0U;kind<14U;++kind){
        cerberus_s3_ble_report_t bad=r;uint32_t at=1040U;
        if(kind==0U)bad.address[0]^=1U;
        else if(kind==1U)bad.address_type=0U;
        else if(kind==2U)bad.sid=3U;
        else if(kind==3U)bad.sid_valid=false;
        else if(kind==4U)bad.sid_invalid=true;
        else if(kind==5U)bad.event_type=4U;
        else if(kind==6U)bad.complete=false;
        else if(kind==7U){bad.data=malformed;bad.length=sizeof(malformed);}
        else if(kind==8U){bad.data=oversized;bad.length=sizeof(oversized);}
        else if(kind==9U)bad.data=NULL;
        else if(kind==10U)at=3011U;
        else if(kind==11U)at=1029U;
        else if(kind==12U)++snapshot.status.generation;
        else snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&bad,at);
        assert(!memcmp(f,&frozen,sizeof(frozen)) && record->reports_seen==1U && record->rssi==-50);
        snapshot.status.generation=31U;
    }
    snapshot.report_sequence=100U;
    r.data=ordinary;r.length=sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1040U);
    assert(e->hc_advertisement==CERBERUS_BLE_HC_03 && !e->hc_scan_response &&
        e->scan_response_observed_ms==1040U);
    r.scan_response=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1050U);
    assert(!e->hc_advertisement && !e->hc_scan_response &&
        !(f->category_bits&CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
    assert(record->name_complete && !memcmp(record->name,"HC-03",5U)); /* Cached name is not current evidence. */

    /* An invalid replacement clears both codes before all early returns; it
     * never lets a following response bind through the older complete ADV. */
    for(unsigned kind=0U;kind<10U;++kind){
        cerberus_s3_ble_snapshot_begin(&snapshot,32U,2000U,true);
        r=(cerberus_s3_ble_report_t){.address={1,2,3,4,5,6},.address_type=1U,.rssi=-50,
            .event_type=0U,.complete=true,.sid_valid=true,.sid=2U,.data=hc,.length=sizeof(hc)};
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2010U);
        r.scan_response=true;cerberus_s3_ble_snapshot_observe(&snapshot,&r,2020U);
        r.scan_response=false;
        if(kind==0U)r.complete=false;
        else if(kind==1U){r.data=oversized;r.length=sizeof(oversized);}
        else if(kind==2U)r.data=NULL;
        else if(kind==3U){r.data=malformed;r.length=sizeof(malformed);}
        else if(kind==4U)r.sid_invalid=true;
        else if(kind==5U)r.sid=16U;
        else if(kind==6U)r.event_type=4U;
        else if(kind==7U)r.event_type=255U;
        else if(kind==8U)snapshot.report_sequence=UINT32_MAX;
        else {hc[1]=8U;} /* Valid shortened name cannot publish exact HC evidence. */
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2030U);
        assert(!e->hc_advertisement && !e->hc_scan_response &&
            !(f->category_bits&CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
        if(kind<3U)assert(record->last_advertisement_ms==2010U && record->advertisement[6]=='6');
        hc[1]=9U;r.complete=true;r.data=hc;r.length=sizeof(hc);r.sid=2U;
        r.sid_invalid=false;r.event_type=0U;r.scan_response=true;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,2040U);
        if(kind<9U)assert(!e->hc_scan_response);
        else assert(e->hc_scan_response==CERBERUS_BLE_HC_06); /* Valid nonmatching original can bind. */
    }
    /* Full 255-byte source is independent of the 31-byte stored raw copy. */
    uint8_t extended[255]={247U,0x30U};memcpy(extended+248U,hc,sizeof(hc));
    cerberus_s3_ble_snapshot_begin(&snapshot,33U,3000U,true);
    r.scan_response=false;r.data=extended;r.length=sizeof(extended);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3010U);
    assert(e->hc_advertisement==CERBERUS_BLE_HC_06 &&
        !(e->flags&CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE) &&
        record->advertisement_flags==CERBERUS_BLE_ADVERTISEMENT_TRUNCATED);
    r.scan_response=true;r.data=hc;r.length=sizeof(hc);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,3020U);
    assert(e->hc_scan_response==CERBERUS_BLE_HC_06 && record->last_advertisement_ms==3010U);
    cerberus_s3_ble_snapshot_begin(&snapshot,34U,UINT32_MAX-20U,true);
    r.scan_response=false;cerberus_s3_ble_snapshot_observe(&snapshot,&r,UINT32_MAX-10U);
    r.scan_response=true;cerberus_s3_ble_snapshot_observe(&snapshot,&r,5U);
    assert(e->hc_scan_response==CERBERUS_BLE_HC_06 && e->scan_response_observed_ms==5U);
    cerberus_s3_ble_snapshot_begin(&snapshot,35U,100U,true);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,110U);
    assert(!snapshot.status.stored_count);
    assert(sizeof(*e)==64U);
    puts("HC source checks passed: exact codes, current replacement, admission, raw isolation, extended reports and wrap.");
}

static void check_rssi_peak(void)
{
    const uint8_t ordinary[]={2,1,6};
    const int8_t samples[]={127,-50,127,-40,-128,21,20,-127};
    const int8_t peaks[]={127,-50,-50,-40,-40,-40,20,20};
    cerberus_s3_ble_report_t r={.address={1,2,3,4,5,6},.address_type=1U,
        .complete=true,.event_type=0U,.data=ordinary,.length=sizeof(ordinary)};
    cerberus_s3_ble_snapshot_begin(&snapshot,40U,1000U,false);
    for(unsigned i=0U;i<sizeof(samples);++i){
        r.rssi=samples[i];cerberus_s3_ble_snapshot_observe(&snapshot,&r,1010U+i);
        const cerberus_s3_ble_record_t *record=&snapshot.records[0];
        assert(record->rssi==peaks[i] && record->reports_seen==i+1U &&
            record->last_advertisement_ms==1010U+i && record->advertisement_length==sizeof(ordinary) &&
            !record->advertisement_flags && record->advertisement_event_type==0U &&
            !memcmp(record->advertisement,ordinary,sizeof(ordinary)));
    }
    /* Same address, different type is a separate all-invalid row. A response
     * may carry a valid strong RSSI but cannot change that original peak. */
    r.address_type=0U;r.rssi=-128;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1100U);
    assert(snapshot.status.stored_count==2U && snapshot.records[1].rssi==127);
    r.scan_response=true;r.event_type=4U;r.rssi=20;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1110U);
    assert(snapshot.records[1].rssi==127 && snapshot.records[1].reports_seen==1U &&
        snapshot.records[1].last_advertisement_ms==1100U);
    r.scan_response=false;r.event_type=0U;r.rssi=126;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1120U);
    assert(snapshot.records[1].rssi==127);
    r.rssi=-127;cerberus_s3_ble_snapshot_observe(&snapshot,&r,1130U);
    assert(snapshot.records[1].rssi==-127 && snapshot.records[0].rssi==20);
    r.scan_response=true;r.event_type=4U;r.rssi=20;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1140U);
    assert(snapshot.records[1].rssi==-127 && snapshot.records[1].reports_seen==3U);
    r.scan_response=false;r.event_type=0U;r.complete=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1150U);
    assert(snapshot.records[1].rssi==-127 && snapshot.records[1].last_advertisement_ms==1130U);
    cerberus_s3_ble_snapshot_finish(&snapshot,false,0,1200U);
    assert(snapshot.records[0].rssi==20 && snapshot.records[1].rssi==-127);
    cerberus_s3_ble_snapshot_begin(&snapshot,41U,1300U,false);
    r.complete=true;r.rssi=21;cerberus_s3_ble_snapshot_observe(&snapshot,&r,1310U);
    cerberus_s3_ble_snapshot_finish(&snapshot,false,0,1320U);
    assert(snapshot.status.stored_count==1U && snapshot.records[0].rssi==127);
    puts("RSSI peak checks passed: unavailable normalization/recovery, valid endpoints, typed rows, original counts and response exclusion.");
}

static void check_meta_hc_filters(void)
{
    const uint8_t ordinary[] = {2,1,6};
    const uint8_t meta[] = {3,0x16,0x5F,0xFD};
    const uint8_t hc[] = {6,0x09,'H','C','-','0','5'};
    const uint8_t blocked[] = {3,0xFF,0x4C,0x00};
    const uint8_t malformed[] = {3,0x16,0x5F,0xFD,2,0x03,0x01};
    cerberus_s3_ble_report_t r = {
        .address = {1,2,3,4,5,6}, .address_type = 1U, .rssi = -60,
        .complete = true, .scan_response = true,
        .sid_valid = true, .sid = 2U, .data = hc, .length = sizeof(hc),
    };
    cerberus_s3_ble_snapshot_begin(&snapshot, 11U, 30000U, true);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30010U);
    assert(snapshot.status.stored_count == 0U); /* response alone is not a device */
    r.scan_response = false; r.data = meta; r.length = sizeof(meta);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30100U);
    assert(snapshot.records[0].focus.category_bits == CERBERUS_S3_BLE_FOCUS_META_HINT);
    assert(snapshot.records[0].focus.meta_evidence);
    r.scan_response = true; r.data = hc; r.length = sizeof(hc);
    r.sid = 3U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30120U);
    r.sid = 2U; r.address_type = 0U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30130U);
    r.address_type = 1U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 32101U);
    assert(!(snapshot.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30140U);
    assert(snapshot.records[0].focus.category_bits ==
        (CERBERUS_S3_BLE_FOCUS_META_HINT | CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT));
    assert(snapshot.records[0].focus.discovery.hc_scan_response==CERBERUS_BLE_HC_05 &&
        snapshot.records[0].name_complete && snapshot.records[0].name_length == 5U);
    assert(snapshot.records[0].last_advertisement_ms == 30100U &&
        snapshot.records[0].advertisement_length == sizeof(meta) &&
        memcmp(snapshot.records[0].advertisement, meta, sizeof(meta)) == 0 &&
        snapshot.records[0].reports_seen == 1U && snapshot.watch.status.reports == 1U &&
        snapshot.activity.status.reports == 1U);
    r.data = blocked; r.length = sizeof(blocked);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30150U);
    assert(!(snapshot.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_META_HINT));
    assert(!snapshot.records[0].focus.meta_evidence);
    r.data = meta; r.length = sizeof(meta);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30160U);
    assert(!(snapshot.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_META_HINT));

    r.scan_response = false; r.address[0] = 2U; r.data = ordinary; r.length = sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30200U);
    r.scan_response = true; r.data = meta; r.length = sizeof(meta);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30210U);
    assert(snapshot.records[1].focus.category_bits == CERBERUS_S3_BLE_FOCUS_META_HINT);
    assert(strstr(snapshot.records[1].focus.meta_evidence, "RESPONSE"));
    r.scan_response = false; r.address[0] = 3U; r.data = malformed; r.length = sizeof(malformed);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 30300U);
    assert(snapshot.records[2].focus.category_bits == 0U &&
        (snapshot.records[2].advertisement_flags & CERBERUS_BLE_ADVERTISEMENT_MALFORMED));
    cerberus_s3_ble_snapshot_finish(&snapshot, false, 0, 31000U);
    uint8_t bits[] = {CERBERUS_S3_BLE_FOCUS_META_HINT, 0U,
        CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT, CERBERUS_S3_BLE_FOCUS_META_HINT,
        CERBERUS_S3_BLE_FOCUS_META_HINT | CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT};
    uint8_t page[2] = {255U,255U};
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_META_HINT,
        1U, page, 2U) == 3U && page[0] == 3U && page[1] == 4U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_HC_NAME_HINT,
        0U, page, 2U) == 2U && page[0] == 2U && page[1] == 4U);
    cerberus_s3_ble_snapshot_begin(&snapshot, 12U, 40000U, true);
    r.address[0] = 1U; r.scan_response = true; r.data = hc; r.length = sizeof(hc);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 40010U);
    assert(snapshot.status.stored_count == 0U); /* previous sweep cannot admit a response */
    r.scan_response = false; r.data = meta; r.length = sizeof(meta);
    cerberus_s3_ble_snapshot_observe(&snapshot, &r, 40020U);
    assert(snapshot.records[0].focus.category_bits == CERBERUS_S3_BLE_FOCUS_META_HINT);
}

static void fill_retention_bank(uint32_t start)
{
    const uint8_t ordinary[] = {2,1,6};
    cerberus_s3_ble_report_t r={.address={0,2,3,4,5,6},.address_type=1,
        .rssi=-40,.complete=true,.data=ordinary,.length=sizeof(ordinary)};
    cerberus_s3_ble_snapshot_begin(&snapshot,19,start,false);
    for(unsigned i=0;i<CERBERUS_BLE_SCAN_MAX_RESULTS;++i){
        r.address[0]=(uint8_t)i;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,start+i);
    }
    assert(snapshot.status.stored_count==32 && !snapshot.inventory_replacements);
}

static void check_recent_inventory_retention(void)
{
    const uint8_t ordinary[]={2,1,6},malformed[]={5,9,'x'};
    cerberus_s3_ble_report_t r={.address={99,2,3,4,5,6},.address_type=1,
        .rssi=-80,.complete=true,.data=ordinary,.length=sizeof(ordinary)};
    /* Independent LRU model: duplicates refresh only their original-report
     * position; no RSSI/family/name weighting and no shifting other indexes. */
    fill_retention_bank(1000);
    uint8_t identities[32];unsigned recency[32];
    for(unsigned i=0;i<32;++i){identities[i]=(uint8_t)i;recency[i]=i;}
    unsigned replacements=0;
    for(unsigned step=32;step<220;++step){
        const uint8_t id=step%3U==0U?identities[(step*7U)%32U]:(uint8_t)step;
        unsigned slot=32;
        for(unsigned i=0;i<32;++i)if(identities[i]==id){slot=i;break;}
        if(slot==32){slot=0;for(unsigned i=1;i<32;++i)if(recency[i]<recency[slot])slot=i;
            identities[slot]=id;++replacements;}
        recency[slot]=step;r.address[0]=id;
        cerberus_s3_ble_snapshot_observe(&snapshot,&r,1000+step);
        assert(snapshot.status.stored_count==32 && snapshot.inventory_replacements==replacements);
        assert(!snapshot.status.dropped_reports);
        for(unsigned i=0;i<32;++i)assert(snapshot.records[i].address[0]==identities[i]);
    }
    /* Equal time is legitimate; sequences provide stable original ordering.
     * A same-byte address with another address type is a distinct newcomer. */
    fill_retention_bank(0);r.address[0]=0;r.address_type=2;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,31);
    assert(snapshot.records[0].address_type==2 && snapshot.inventory_replacements==1);
    r.address_type=1;r.address[0]=99;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,31);
    assert(snapshot.records[1].address[0]==99 && snapshot.inventory_replacements==2);
    /* Ordinary 32-bit timer rollover remains valid. Backward time, including
     * one older than a later bound response, cannot evict another identity. */
    fill_retention_bank(UINT32_MAX-20U);r.address[0]=99;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,11);
    assert(snapshot.inventory_replacements==1 && snapshot.records[0].address[0]==99);
    for(unsigned bad=0;bad<12;++bad){
        fill_retention_bank(1000);
        cerberus_s3_ble_record_t frozen[32];memcpy(frozen,snapshot.records,sizeof(frozen));
        cerberus_s3_ble_report_t invalid=r;uint32_t now=1100;
        if(bad==0){invalid.data=malformed;invalid.length=sizeof(malformed);}
        if(bad==1)invalid.complete=false;
        if(bad==2)invalid.scan_response=true;
        if(bad==3)invalid.event_type=4;
        if(bad==4){invalid.sid_valid=true;invalid.sid=16;}
        if(bad==5)invalid.sid_invalid=true;
        if(bad==6)invalid.address_type=4;
        if(bad==7)invalid.data=NULL;
        if(bad==8)invalid.length=256;
        if(bad==9)now=1030;
        if(bad==10)now=999;
        if(bad==11)snapshot.report_sequence=UINT32_MAX;
        cerberus_s3_ble_snapshot_observe(&snapshot,&invalid,now);
        assert(!snapshot.inventory_replacements && !memcmp(frozen,snapshot.records,sizeof(frozen)));
    }
    fill_retention_bank(1000);
    cerberus_s3_ble_report_t response=r;response.address[0]=1;response.scan_response=true;response.event_type=4;
    cerberus_s3_ble_snapshot_observe(&snapshot,&response,1200);
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,1199);
    assert(!snapshot.inventory_replacements && snapshot.status.dropped_reports==1);
    /* Every field of the victim must belong to the newcomer. Seed the first
     * row with real name/UUID/company/iBeacon evidence and a matched response. */
    uint8_t rich[64];make_ibeacon(rich,1,2,3,-50);
    const uint8_t extras[]={4,9,'O','L','D',3,3,0x81,0x30};memcpy(rich+27,extras,sizeof(extras));
    cerberus_s3_ble_snapshot_begin(&snapshot,19,0,false);
    cerberus_s3_ble_report_t old=r;old.address[0]=0;old.connectable=true;old.rssi=-10;
    old.data=rich;old.length=27+sizeof(extras);
    cerberus_s3_ble_snapshot_observe(&snapshot,&old,0);
    old.scan_response=true;old.event_type=4;
    cerberus_s3_ble_snapshot_observe(&snapshot,&old,1);
    for(unsigned i=1;i<32;++i){old=r;old.address[0]=(uint8_t)i;
        cerberus_s3_ble_snapshot_observe(&snapshot,&old,i+1);}
    const cerberus_s3_ble_record_t selected=snapshot.records[0];
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,100);
    const cerberus_s3_ble_record_t *fresh=&snapshot.records[0];
    assert(fresh->address[0]==99 && fresh->rssi==-80 && !fresh->name_length && !fresh->name_complete &&
        !fresh->uuid_length && fresh->manufacturer_id==CERBERUS_BLE_MANUFACTURER_UNKNOWN &&
        !fresh->flags && fresh->reports_seen==1 && fresh->advertisement_length==sizeof(ordinary) &&
        !fresh->advertisement_flags && fresh->last_advertisement_ms==100 && !fresh->advertisement_sid_valid &&
        !fresh->focus.category_bits && !fresh->focus.ibeacon_valid && !fresh->focus.meta_suppressed &&
        !fresh->focus.findmy_evidence && !fresh->focus.flipper_evidence && !fresh->focus.flock_evidence &&
        !fresh->focus.meta_evidence && !fresh->focus.discovery.scan_response_sequence);
    assert(selected.address[0]==0 && selected.name_length==3 && selected.focus.ibeacon_valid);
    /* A late real iBeacon and a response-only Find My hint both survive
     * capacity. Responses for the evicted address cannot resurrect its row. */
    old=r;old.address[0]=100;old.data=rich;old.length=27;
    cerberus_s3_ble_snapshot_observe(&snapshot,&old,101);
    assert(snapshot.records[1].focus.ibeacon_valid && snapshot.records[1].address[0]==100);
    const uint8_t fmna[]={3,3,0x44,0xfd};
    response=r;response.scan_response=true;response.event_type=4;response.data=fmna;response.length=sizeof(fmna);
    cerberus_s3_ble_snapshot_observe(&snapshot,&response,102);
    assert(snapshot.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY);
    assert(snapshot.records[0].last_advertisement_ms==100 && snapshot.records[0].reports_seen==1);
    response.address[0]=0;cerberus_s3_ble_snapshot_observe(&snapshot,&response,103);
    assert(snapshot.inventory_replacements==2 && snapshot.records[0].address[0]==99);
    snapshot.inventory_replacements=UINT16_MAX-1U;
    for(unsigned i=0;i<3;++i){r.address[0]=(uint8_t)(200+i);cerberus_s3_ble_snapshot_observe(&snapshot,&r,104+i);}
    assert(snapshot.inventory_replacements==UINT16_MAX && !snapshot.status.dropped_reports);
    cerberus_s3_ble_snapshot_finish(&snapshot,false,0,8000);
    const cerberus_s3_ble_snapshot_t complete=snapshot;
    cerberus_s3_ble_snapshot_observe(&snapshot,&r,8001);
    assert(!memcmp(&complete,&snapshot,sizeof(snapshot)));
    cerberus_s3_ble_snapshot_begin(&snapshot,20,9000,false);
    assert(!snapshot.inventory_replacements && !snapshot.status.dropped_reports);
    puts("Bounded recent inventory passed: independent LRU model, late focused evidence, full victim reset, rejection/clock/sequence guards, saturation and frozen completed banks.");
}

#include "s3_ble_discovery_collect_check.h"

int main(void)
{
    cerberus_s3_ble_snapshot_begin(&snapshot, 7U, 1000U, true);
    assert(snapshot.status.state == CERBERUS_BLE_SCAN_RUNNING);
    assert(snapshot.watch.status.state == CERBERUS_WATCH_RUNNING);
    assert(snapshot.activity.status.flags & CERBERUS_BLE_ACTIVITY_EXTENDED_ENABLED);

    const uint8_t ordinary[] = {2, 1, 6, 5, 9, 'T', 'e', 's', 't', 3, 0xff, 0x34, 0x12};
    cerberus_s3_ble_report_t report = {
        .address = {1, 2, 3, 4, 5, 6}, .address_type = 0, .rssi = -61,
        .event_type = 0, .connectable = true, .complete = true,
        .data = ordinary, .length = sizeof(ordinary),
    };
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1100U);
    assert(snapshot.status.stored_count == 1U && snapshot.status.total_count == 1U);
    assert(snapshot.records[0].name_length == 4U);
    assert(memcmp(snapshot.records[0].name, "Test", 4U) == 0);
    assert(snapshot.records[0].manufacturer_id == 0x1234U);
    assert(snapshot.records[0].advertisement_length == sizeof(ordinary));
    assert(snapshot.records[0].reports_seen == 1U);
    assert(snapshot.records[0].flags & CERBERUS_BLE_RESULT_CONNECTABLE);

    report.rssi = -45;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1200U);
    assert(snapshot.status.stored_count == 1U && snapshot.records[0].rssi == -45);
    assert(snapshot.records[0].reports_seen == 2U);
    report.scan_response = true;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1300U);
    assert(snapshot.records[0].reports_seen == 2U && snapshot.activity.status.reports == 2U);

    report.scan_response = false;
    report.complete = false;
    report.address[0] = 7U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1400U);
    assert(snapshot.status.stored_count == 1U && snapshot.activity.status.reports == 3U);
    assert(snapshot.activity.status.valid_reports == 2U);

    uint8_t drone[31] = {30, 0x16, 0xfa, 0xff, 0x0d, 0x00, 0x00, 0x10,
        'T', 'E', 'S', 'T', '1'};
    report.complete = true;
    report.address[0] = 8U;
    report.data = drone;
    report.length = sizeof(drone);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1500U);
    assert(snapshot.watch.status.stored_count == 1U);
    assert(snapshot.watch.records[0].kind == SENTINEL_WATCH_PROTOCOL_DRONE);
    assert(strcmp(snapshot.watch.records[0].id, "TEST1") == 0);
    assert(snapshot.records[1].advertisement_length == 31U);

    const uint8_t malformed[] = {5, 9, 'x'};
    report.address[0] = 9U;
    report.data = malformed;
    report.length = sizeof(malformed);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1600U);
    assert(snapshot.records[2].advertisement_flags & CERBERUS_BLE_ADVERTISEMENT_MALFORMED);
    assert(snapshot.records[2].name_length == 0U);

    report.data = ordinary;
    report.length = sizeof(ordinary);
    for (uint8_t i = 10U; i < 45U; ++i) {
        report.address[0] = i;
        cerberus_s3_ble_snapshot_observe(&snapshot, &report, 1700U + i);
    }
    assert(snapshot.status.stored_count == CERBERUS_BLE_SCAN_MAX_RESULTS);
    assert(snapshot.status.dropped_reports == 0U && snapshot.inventory_replacements == 6U);
    assert(snapshot.status.total_count == 39U);
    assert(snapshot.activity.status.reports == 40U);

    cerberus_s3_ble_snapshot_finish(&snapshot, false, 0, 9000U);
    assert(snapshot.status.state == CERBERUS_BLE_SCAN_COMPLETE);
    assert(snapshot.watch.status.state == CERBERUS_WATCH_COMPLETE);
    assert(snapshot.activity.status.state == CERBERUS_WATCH_COMPLETE);
    assert(snapshot.status.unnamed_count == 0U);
    assert(snapshot.activity.status.peak_reports_per_second > 0U);

    cerberus_s3_ble_snapshot_begin(&snapshot, 8U, 10000U, false);
    assert(snapshot.status.stored_count == 0U && snapshot.watch.status.stored_count == 0U);
    cerberus_s3_ble_snapshot_finish(&snapshot, true, 0, 10100U);
    assert(snapshot.status.state == CERBERUS_BLE_SCAN_IDLE && snapshot.status.generation == 8U);
    assert(snapshot.watch.status.state == CERBERUS_WATCH_IDLE);

    cerberus_s3_ble_snapshot_begin(&snapshot, 9U, 11000U, false);
    uint8_t ibeacon[31] = {2, 1, 6, 0x1a, 0xff, 0x4c, 0, 2, 0x15,
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        0x12, 0x34, 0xab, 0xcd, 0xc5};
    uint8_t findmy[31] = {30, 0xff, 0x4c, 0, 0x12, 0x19};
    uint8_t flipper[] = {3, 0x03, 0x80, 0x30,
        8, 0x09, 'F', 'l', 'i', 'p', 'p', 'e', 'r'};
    const uint8_t flock_canonical[16] = {0xe8,0xcc,0xbb,0x38,0x95,0x32,
        0x46,0xa8,0x9f,0xe5,0x18,0x14,0xdf,0x17,0x2e,0x6f};
    uint8_t flock[18] = {17, 0x07};
    for (size_t i = 0U; i < 16U; ++i) flock[i + 2U] = flock_canonical[15U - i];
    report = (cerberus_s3_ble_report_t){
        .address = {1,2,3,4,5,6}, .address_type = 1, .rssi = -60,
        .complete = true, .data = ibeacon, .length = sizeof(ibeacon),
    };
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11100U);
    assert(snapshot.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_IBEACON);
    assert(snapshot.records[0].focus.ibeacon_valid);
    assert(snapshot.records[0].focus.ibeacon.major == 0x1234U &&
        snapshot.records[0].focus.ibeacon.minor == 0xabcdU &&
        snapshot.records[0].focus.ibeacon.measured_power_dbm == -59);
    report.address[0] = 2U;
    report.data = findmy; report.length = sizeof(findmy);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11200U);
    assert(snapshot.records[1].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY);
    assert(snapshot.records[1].focus.findmy_evidence != NULL);
    report.address[0] = 3U;
    report.data = flipper; report.length = sizeof(flipper);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11300U);
    assert(snapshot.records[2].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FLIPPER);
    assert(snapshot.records[2].focus.flipper_evidence != NULL);
    report.address[0] = 4U;
    report.data = flock; report.length = sizeof(flock);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11400U);
    assert(snapshot.records[3].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FLOCK);
    assert(snapshot.records[3].focus.flock_evidence != NULL);
    report.address[0] = 5U;
    report.data = ordinary; report.length = sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11500U);
    assert(snapshot.records[4].focus.category_bits == 0U);

    /* A later unrelated AD overwrites the raw capture, not the earlier
     * category evidence. Never join separate packets to form a signature. */
    report.address[0] = 1U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11600U);
    assert(snapshot.records[0].focus.category_bits == CERBERUS_S3_BLE_FOCUS_IBEACON);
    assert(snapshot.records[0].focus.ibeacon.major == 0x1234U);
    report.address[0] = 6U;
    report.data = ibeacon; report.length = sizeof(ibeacon);
    report.scan_response = true;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11700U);
    report.scan_response = false; report.complete = false;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 11800U);
    assert(snapshot.status.stored_count == 5U);
    cerberus_s3_ble_snapshot_finish(&snapshot, false, 0, 19000U);
    uint8_t bits[5] = {0};
    for (size_t i = 0U; i < 5U; ++i) bits[i] = snapshot.records[i].focus.category_bits;
    uint8_t page[2] = {255, 255};
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_ALL,
        2U, page, 2U) == 5U && page[0] == 2U && page[1] == 3U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_IBEACON,
        0U, page, 2U) == 1U && page[0] == 0U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_FIND_MY,
        0U, page, 2U) == 1U && page[0] == 1U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_FLIPPER,
        0U, page, 2U) == 1U && page[0] == 2U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_FLOCK,
        0U, page, 2U) == 1U && page[0] == 3U);
    assert(cerberus_s3_ble_focus_page(bits, 5U, CERBERUS_S3_BLE_FILTER_COUNT,
        0U, page, 2U) == 0U);

    /* A response can enrich a known display record without changing the
     * original advertisement, Watch evidence, or packet counts. */
    cerberus_s3_ble_snapshot_begin(&snapshot, 10U, 20000U, true);
    const uint8_t short_ad[] = {3U, 0x08U, 'A', 'B', 3U, 0xFFU, 0x34U, 0x12U};
    const uint8_t response_ad[] = {5U, 0x09U, 'A', 'B', 'C', 'D',
        3U, 0x03U, 0x44U, 0xFDU, 3U, 0xFFU, 0xCDU, 0xABU};
    report = (cerberus_s3_ble_report_t){
        .address = {9, 8, 7, 6, 5, 4}, .address_type = 1U,
        .rssi = -54, .event_type = 0U, .connectable = true,
        .scan_response = true, .complete = true,
        .sid = 3U, .sid_valid = true,
        .data = response_ad, .length = sizeof(response_ad),
    };
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20050U);
    assert(snapshot.status.stored_count == 0U);
    report.scan_response = false;
    report.data = short_ad; report.length = sizeof(short_ad);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20100U);
    assert(snapshot.records[0].name_length == 2U &&
        !snapshot.records[0].name_complete && snapshot.records[0].uuid_length == 0U);
    uint16_t activity_before = snapshot.activity.status.reports;
    uint32_t watch_before = snapshot.watch.status.reports;
    report.scan_response = true;
    report.data = response_ad; report.length = sizeof(response_ad);
    report.sid = 4U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20120U);
    report.sid = 3U;
    report.address_type = 0U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20130U);
    report.address_type = 1U;
    report.address[0] = 1U;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20140U);
    report.address[0] = 9U;
    const uint8_t bad_response[] = {5U, 0x09U, 'X'};
    report.data = bad_response; report.length = sizeof(bad_response);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20150U);
    report.data = response_ad; report.length = sizeof(response_ad);
    report.complete = false;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20160U);
    report.complete = true;
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 22601U);
    assert(snapshot.records[0].name_length == 2U &&
        snapshot.records[0].uuid_length == 0U);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20200U);
    assert(snapshot.status.stored_count == 1U &&
        snapshot.records[0].name_complete &&
        snapshot.records[0].name_length == 4U &&
        memcmp(snapshot.records[0].name, "ABCD", 4U) == 0 &&
        snapshot.records[0].uuid_length == 2U &&
        snapshot.records[0].uuid[0] == 0x44U &&
        snapshot.records[0].uuid[1] == 0xFDU);
    assert(snapshot.records[0].manufacturer_id == 0x1234U &&
        snapshot.records[0].advertisement_length == sizeof(short_ad) &&
        memcmp(snapshot.records[0].advertisement, short_ad, sizeof(short_ad)) == 0 &&
        snapshot.records[0].reports_seen == 1U &&
        snapshot.activity.status.reports == activity_before &&
        snapshot.watch.status.reports == watch_before);
    const uint8_t complete_ad[] = {6U, 0x09U, 'P', 'r', 'i', 'm', 'e'};
    report.scan_response = false;
    report.address[0] = 2U;
    report.data = complete_ad; report.length = sizeof(complete_ad);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20300U);
    report.scan_response = true;
    report.data = response_ad; report.length = sizeof(response_ad);
    cerberus_s3_ble_snapshot_observe(&snapshot, &report, 20320U);
    assert(snapshot.records[1].name_complete &&
        snapshot.records[1].name_length == 5U &&
        memcmp(snapshot.records[1].name, "Prime", 5U) == 0);
    cerberus_s3_ble_snapshot_finish(&snapshot, false, 0, 21000U);
    assert(snapshot.status.stored_count == 2U &&
        snapshot.status.unnamed_count == 0U);

    check_meta_hc_filters();
    check_findmy_provenance();
    check_shared_discovery();
    check_meta_provenance();
    check_ibeacon_provenance();
    check_flock_provenance();
    check_hc_provenance();
    check_rssi_peak();
    check_watch_callback_timing();
    check_wardrive_evidence_codec();
    check_recent_inventory_retention();
    check_focused_discovery_collect();
    puts("S3 BLE snapshot checks passed: dedupe, raw packets, focused evidence/filter mapping, malformed/incomplete reports, bounded cache, activity and finish.");
}
