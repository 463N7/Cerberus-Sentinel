#undef NDEBUG
#include "cerberus_watch_capture.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static cerberus_watch_capture_t capture;
static const uint8_t address[6]={2,3,4,5,6,7};
static bool zero(const void *p,size_t n)
{const uint8_t *b=p;for(size_t i=0;i<n;++i)if(b[i])return false;return true;}
static sentinel_watch_protocol_record_t claim={.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
    .transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN,.flags=SENTINEL_WATCH_PROTOCOL_HAS_ID,.id="TEST-A"};
static void begin(uint64_t epoch)
{cerberus_watch_capture_begin_with_epoch(&capture,CERBERUS_WATCH_RADIO_WIFI,255,123,epoch);}
static void observe(uint64_t time,int8_t signal)
{cerberus_watch_capture_observe_timed(&capture,&claim,address,0,signal,149,time);}
static cerberus_watch_scan_timing_payload_t timing(uint64_t now)
{
    cerberus_watch_scan_timing_payload_t out;memset(&out,0xa5,sizeof(out));
    CHECK(cerberus_watch_capture_get_timing(&capture,CERBERUS_WATCH_RADIO_WIFI,255,0,now,&out));
    CHECK(out.radio==CERBERUS_WATCH_RADIO_WIFI&&out.generation==255&&!out.index);
    CHECK(out.capture_epoch==capture.capture_epoch&&zero(out.reserved,sizeof(out.reserved)));
    return out;
}
static void rejection(void)
{
    cerberus_watch_scan_timing_payload_t out;
    for(unsigned defect=0;defect<8;++defect){
        begin(5);observe(0,-60);cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
        memset(&out,0xa5,sizeof(out));
        if(defect==4)capture.status.state=CERBERUS_WATCH_RUNNING;
        if(defect==5)capture.capture_epoch=0;
        if(defect==6)capture.status.stored_count=0;
        CHECK(!cerberus_watch_capture_get_timing(defect==0?NULL:&capture,
            defect==1?0:CERBERUS_WATCH_RADIO_WIFI,defect==2?0:255,
            defect==3?12:0,0,defect==7?NULL:&out));
        if(defect!=7)CHECK(zero(&out,sizeof(out)));
    }
}
static void boundaries(void)
{
    const uint64_t times[]={0,1,999,1000,1001,UINT64_C(4294967295000),UINT64_MAX};
    const uint64_t ages[]={0,1,1,1,2,UINT64_C(4294967295),UINT64_C(18446744073709552)};
    for(unsigned i=0;i<sizeof(times)/sizeof(times[0]);++i){
        begin(UINT64_MAX);observe(0,-30);cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
        cerberus_watch_scan_timing_payload_t out=timing(times[i]);
        CHECK(out.flags==CERBERUS_WATCH_TIMING_VALID&&out.source_age_ms==ages[i]);
    }
    begin(9);observe(UINT64_C(4294967296001),-60);cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
    cerberus_watch_scan_timing_payload_t out=timing(UINT64_C(4294967296000));CHECK(!out.flags&&!out.source_age_ms);
    out=timing(UINT64_C(4294967296001));CHECK(out.flags==1&&!out.source_age_ms);
    out=timing(UINT64_C(4294967297002));CHECK(out.flags==1&&out.source_age_ms==2);
    /* Query time never changes the frozen callback timestamp or record bytes. */
    cerberus_watch_capture_t saved=capture;(void)timing(UINT64_MAX);CHECK(!memcmp(&saved,&capture,sizeof(saved)));
}
static void qualified_updates(void)
{
    begin(10);observe(1000,-20);observe(9000,-100);
    CHECK(capture.records[0].rssi==-100&&capture.qualifying_us[0]==9000&&capture.qualifying_valid==1);
    uint32_t reports=capture.status.reports;
    cerberus_watch_capture_observe_timed(&capture,NULL,address,0,-1,149,10000);
    sentinel_watch_protocol_record_t ordinary={0};
    cerberus_watch_capture_observe_timed(&capture,&ordinary,address,0,-1,149,20000);
    cerberus_watch_capture_observe_timed(&capture,&claim,NULL,0,-1,149,30000);
    CHECK(capture.status.reports==reports+3&&capture.qualifying_us[0]==9000&&capture.qualifying_valid==1);
    for(unsigned i=1;i<12;++i){uint8_t unique[6]={2,3,4,5,6,(uint8_t)(7+i)};
        cerberus_watch_capture_observe_timed(&capture,&claim,unique,0,-60,6,i*1000);}
    uint64_t saved[12];memcpy(saved,capture.qualifying_us,sizeof(saved));
    uint8_t extra[6]={2,3,4,5,6,250};cerberus_watch_capture_observe_timed(&capture,&claim,extra,0,-1,6,99999);
    CHECK(capture.status.stored_count==12&&capture.status.dropped_records==1);
    CHECK(!memcmp(saved,capture.qualifying_us,sizeof(saved))&&capture.qualifying_valid==0x0fff);
    observe(9000,-80);CHECK(capture.qualifying_us[0]==9000&&(capture.qualifying_valid&1));
    observe(8999,-75);CHECK(!(capture.qualifying_valid&1)&&capture.qualifying_us[0]==0);
    observe(12000,-90);CHECK((capture.qualifying_valid&1)&&capture.qualifying_us[0]==12000);
    cerberus_watch_capture_observe(&capture,&claim,address,0,-80,149);
    CHECK(!(capture.qualifying_valid&1)&&capture.qualifying_us[0]==0);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
    cerberus_watch_scan_timing_payload_t out=timing(15000);CHECK(!out.flags&&!out.source_age_ms);
    observe(20000,-1);CHECK(!(capture.qualifying_valid&1));
    begin(11);CHECK(!capture.qualifying_valid&&zero(capture.qualifying_us,sizeof(capture.qualifying_us)));
    observe(0,-50);cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_ERROR,600);
    CHECK(!capture.qualifying_valid&&zero(capture.qualifying_us,sizeof(capture.qualifying_us)));
    for(unsigned state=0;state<=3;++state)if(state!=CERBERUS_WATCH_COMPLETE){
        begin(12);observe(3,-50);cerberus_watch_capture_finish(&capture,(uint8_t)state,500);
        CHECK(!capture.qualifying_valid&&zero(capture.qualifying_us,sizeof(capture.qualifying_us)));
    }
    /* Old API remains usable but cannot manufacture timing. */
    memset(&capture,0xa5,sizeof(capture));cerberus_watch_capture_begin(&capture,2,255,0);
    observe(0,-40);CHECK(!capture.capture_epoch&&!capture.qualifying_valid);
    cerberus_watch_capture_finish(&capture,2,1);memset(&out,0xa5,sizeof(out));
    CHECK(!cerberus_watch_capture_get_timing(&capture,2,255,0,1,&out)&&zero(&out,sizeof(out)));
    cerberus_watch_scan_data_payload_t chunk;
    CHECK(cerberus_watch_capture_get_chunk(&capture,2,255,0,0,&chunk)&&chunk.byte_count==17);
    CHECK(sizeof(capture.records[0])==46&&sizeof(capture.status)==16&&sizeof(out)==22);
}

static void protection_configuration(void)
{
    uint8_t zero_address[6]={0},multicast[6]={1,2,3,4,5,6};
    for(unsigned defect=0;defect<9;++defect){
        begin(3);
        if(defect==4)capture.status.radio=CERBERUS_WATCH_RADIO_BLE;
        if(defect==5)capture.status.state=CERBERUS_WATCH_COMPLETE;
        if(defect==6)capture.status.reports=1;
        if(defect==7)capture.status.stored_count=1;
        cerberus_watch_capture_t saved=capture;
        CHECK(!cerberus_watch_capture_protect_wifi_source(defect==0?NULL:&capture,
            defect==8?SENTINEL_WATCH_TRANSPORT_BLE:SENTINEL_WATCH_TRANSPORT_WIFI_NAN,
            defect==1?NULL:defect==2?zero_address:defect==3?multicast:address));
        CHECK(!memcmp(&saved,&capture,sizeof(saved)));
    }
    begin(4);uint8_t copied[6];memcpy(copied,address,6);
    CHECK(cerberus_watch_capture_protect_wifi_source(&capture,SENTINEL_WATCH_TRANSPORT_WIFI_NAN,copied));
    memset(copied,0,sizeof(copied));CHECK(!memcmp(capture.protected_address,address,6));
    CHECK(capture.status.stored_count==0&&capture.status.reports==0&&!capture.qualifying_valid);
    CHECK(zero(capture.records,sizeof(capture.records))&&zero(capture.qualifying_us,sizeof(capture.qualifying_us)));
    cerberus_watch_capture_observe_timed(&capture,NULL,address,0,-30,6,3);
    CHECK(capture.status.reports==1&&!capture.status.stored_count);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,300);
    cerberus_watch_scan_data_payload_t chunk;
    CHECK(!cerberus_watch_capture_get_chunk(&capture,2,255,0,0,&chunk));
    begin(5);CHECK(!capture.protected_transport&&zero(capture.protected_address,6));
}

static void protected_source_capacity(void)
{
    for(unsigned transport=2;transport<=3;++transport)for(unsigned timed=0;timed<2;++timed){
        begin(20);
        CHECK(cerberus_watch_capture_protect_wifi_source(&capture,(uint8_t)transport,address));
        sentinel_watch_protocol_record_t victim={.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
            .transport=SENTINEL_WATCH_TRANSPORT_WIFI_BEACON,
            .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID|SENTINEL_WATCH_PROTOCOL_HAS_POSITION|SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE,
            .altitude_kind=SENTINEL_WATCH_ALTITUDE_PRESSURE,.latitude_e7=123456,.longitude_e7=-654321,
            .altitude_dm=500,.id="OTHER-SOURCE"};
        for(unsigned i=0;i<12;++i){uint8_t mac[6]={2,9,8,7,6,(uint8_t)i};
            cerberus_watch_capture_observe_timed(&capture,&victim,mac,0,-40,6,10000+i);}
        CHECK(capture.status.stored_count==12&&capture.status.reports==12&&!capture.status.dropped_records);
        cerberus_watch_observation_t saved[12];memcpy(saved,capture.records,sizeof(saved));
        uint64_t timestamps[12];memcpy(timestamps,capture.qualifying_us,sizeof(timestamps));
        sentinel_watch_protocol_record_t target={.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
            .transport=(uint8_t)transport,.flags=SENTINEL_WATCH_PROTOCOL_HAS_ID,.id="SELECTED"};
        for(unsigned mismatch=0;mismatch<4;++mismatch){
            sentinel_watch_protocol_record_t wrong=target;uint8_t other_mac[6];memcpy(other_mac,address,6);
            if(mismatch==0)wrong.transport=transport==2?3:2;
            if(mismatch==1)wrong.kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
            if(mismatch==3)++other_mac[5];
            cerberus_watch_capture_observe_timed(&capture,&wrong,other_mac,mismatch==2?1:0,-1,177,99999);
            CHECK(!memcmp(saved,capture.records,sizeof(saved))&&!memcmp(timestamps,capture.qualifying_us,sizeof(timestamps)));
            CHECK(capture.status.dropped_records==mismatch+1U);
        }
        if(timed)cerberus_watch_capture_observe_timed(&capture,&target,address,0,-100,177,0);
        else cerberus_watch_capture_observe(&capture,&target,address,0,-100,177);
        CHECK(capture.status.reports==17&&capture.status.stored_count==12&&capture.status.dropped_records==5);
        CHECK(capture.status.flags&CERBERUS_WATCH_CAPTURE_OVERFLOW);
        CHECK(capture.status.reports>=capture.status.stored_count+capture.status.dropped_records);
        CHECK(!memcmp(saved,capture.records,11*sizeof(saved[0])));
        CHECK(!memcmp(timestamps,capture.qualifying_us,11*sizeof(timestamps[0])));
        cerberus_watch_observation_t *kept=&capture.records[11];
        CHECK(!memcmp(kept->address,address,6)&&kept->transport==transport&&kept->kind==SENTINEL_WATCH_PROTOCOL_DRONE&&kept->address_type==0);
        CHECK(kept->flags==SENTINEL_WATCH_PROTOCOL_HAS_ID&&!strcmp(kept->id,"SELECTED"));
        CHECK(!kept->latitude_e7&&!kept->longitude_e7&&!kept->altitude_dm&&!kept->altitude_kind);
        CHECK(kept->rssi==-100&&kept->channel==177&&capture.qualifying_us[11]==0);
        CHECK(((capture.qualifying_valid>>11)&1U)==timed);
        cerberus_watch_capture_observe_timed(&capture,&target,address,0,-90,149,100);
        CHECK(capture.status.stored_count==12&&capture.status.dropped_records==5&&capture.qualifying_us[11]==100);
        CHECK(kept->channel==149&&kept->rssi==-90);
        uint8_t overflow_mac[6]={2,9,8,7,6,77};cerberus_watch_observation_t selected=*kept;
        cerberus_watch_capture_observe_timed(&capture,&victim,overflow_mac,0,-1,6,100000);
        CHECK(capture.status.dropped_records==6&&!memcmp(kept,&selected,sizeof(selected))&&capture.qualifying_us[11]==100);
        cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,500);
        cerberus_watch_scan_timing_payload_t out;
        CHECK(cerberus_watch_capture_get_timing(&capture,2,255,11,1100,&out)&&out.flags==CERBERUS_WATCH_TIMING_VALID&&out.source_age_ms==1);
        CHECK(capture.status.reports>=capture.status.stored_count+capture.status.dropped_records);
    }
    /* Saturating counters still admit the protected exact source. */
    begin(21);CHECK(cerberus_watch_capture_protect_wifi_source(&capture,3,address));
    for(unsigned i=0;i<12;++i){uint8_t other[6]={2,7,6,5,4,(uint8_t)i};
        cerberus_watch_capture_observe_timed(&capture,&claim,other,0,-20,6,i);}
    capture.status.reports=UINT32_MAX;capture.status.dropped_records=UINT16_MAX;
    observe(10,-60);CHECK(capture.status.reports==UINT32_MAX&&capture.status.dropped_records==UINT16_MAX);
    CHECK(!memcmp(capture.records[11].address,address,6));
}
int main(void)
{
    rejection();boundaries();qualified_updates();protection_configuration();protected_source_capacity();
    printf("Watch collector timing: %u checks; source protection, full-width timing, capacity and cleanup passed; capture=%zu bytes.\n",checks,sizeof(capture));
    return 0;
}
