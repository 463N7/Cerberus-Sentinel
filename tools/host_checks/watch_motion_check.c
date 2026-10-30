#undef NDEBUG
#include "cerberus_watch_capture.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static bool zeros(const void *data,size_t n)
{const uint8_t *p=data;for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,int32_t n)
{uint32_t u=(uint32_t)n;for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(u>>(8*i));}
static void location(uint8_t m[25])
{
    memset(m,0,25);m[0]=0x12;m[2]=90;m[3]=40;
    put32(m+5,123456789);put32(m+9,-987654321);
    put16(m+15,2030);put16(m+17,2011);put16(m+21,1234);
}
static void basic(uint8_t m[25],char id)
{memset(m,0,25);m[0]=0x02;m[1]=0x10;m[2]=(uint8_t)id;}
/* Independent small literal framing: BLE single and both beacon OUIs/NAN packs. */
static size_t frame(unsigned form,const uint8_t *messages,unsigned count,uint8_t out[256])
{
    memset(out,0,256);
    if(!form&&count==1){out[0]=30;out[1]=0x16;out[2]=0xfa;out[3]=0xff;out[4]=0x0d;out[5]=7;
        memcpy(out+6,messages,25);return 31;}
    size_t pos;
    if(form==1||form==2){
        out[0]=0x80;memset(out+4,255,6);out[10]=2;out[16]=2;
        out[36]=221;out[37]=(uint8_t)(8U+25U*count);
        if(form==1){out[38]=0xfa;out[39]=0x0b;out[40]=0xbc;}
        else{out[38]=0x90;out[39]=0x3a;out[40]=0xe6;}
        out[41]=0x0d;out[42]=7;pos=43;
    }else if(form==3){
        static const uint8_t dest[]={0x51,0x6f,0x9a,1,0,0};
        static const uint8_t nan[]={4,9,0x50,0x6f,0x9a,0x13,3};
        static const uint8_t service[]={0x88,0x69,0x19,0x9d,0x92,9,1,0,0x12};
        out[0]=0xd0;memcpy(out+4,dest,6);out[10]=2;out[16]=2;
        memcpy(out+24,nan,sizeof(nan));put16(out+31,(uint16_t)(14U+25U*count));
        memcpy(out+33,service,sizeof(service));out[42]=(uint8_t)(4U+25U*count);out[43]=7;pos=44;
    }else{
        out[0]=(uint8_t)(8U+25U*count);out[1]=0x16;out[2]=0xfa;out[3]=0xff;out[4]=0x0d;out[5]=7;pos=6;
    }
    out[pos++]=0xf2;out[pos++]=25;out[pos++]=(uint8_t)count;
    memcpy(out+pos,messages,25U*count);return pos+25U*count;
}
static bool decode(unsigned form,const uint8_t *bytes,size_t n,sentinel_watch_protocol_record_t *r)
{return form?sentinel_watch_protocol_decode_wifi(bytes,n,r):sentinel_watch_protocol_decode_ble(bytes,n,r);}
static sentinel_watch_protocol_record_t parse(unsigned form,const uint8_t m[25])
{
    uint8_t wire[256];sentinel_watch_protocol_record_t r;
    CHECK(decode(form,wire,frame(form,m,1,wire),&r));CHECK(sentinel_watch_motion_valid(&r.motion));return r;
}
static void rejects(unsigned form,const uint8_t *wire,size_t n)
{
    sentinel_watch_protocol_record_t r;memset(&r,0xa5,sizeof(r));
    CHECK(!decode(form,wire,n,&r));CHECK(zeros(&r,sizeof(r)));
}
static void parser_values_and_boundaries(void)
{
    uint8_t m[25];location(m);
    for(unsigned form=0;form<4;++form){
        sentinel_watch_protocol_record_t r=parse(form,m);
        CHECK(r.motion.flags==15&&r.motion.speed_qms==40&&r.motion.direction_deg==90&&r.motion.height_encoded==2011);
        CHECK(r.latitude_e7==123456789&&r.longitude_e7==-987654321&&r.altitude_dm==150);
        for(unsigned high=0;high<2;++high)for(unsigned raw=0;raw<256;++raw){
            m[1]=(uint8_t)high;m[3]=(uint8_t)raw;r=parse(form,m);
            unsigned q=high?255U+3U*raw:raw;
            CHECK(!!(r.motion.flags&SENTINEL_WATCH_MOTION_SPEED)==(q<=1017U));
            CHECK(r.motion.speed_qms==(q<=1017U?q:0));
        }
        location(m);
        for(unsigned west=0;west<2;++west)for(unsigned raw=0;raw<256;++raw){
            m[1]=(uint8_t)(west*2U);m[2]=(uint8_t)raw;
            if(raw>181){uint8_t wire[256];size_t n=frame(form,m,1,wire);rejects(form,wire,n);continue;}
            r=parse(form,m);unsigned degrees=west*180U+raw;
            CHECK(!!(r.motion.flags&SENTINEL_WATCH_MOTION_DIRECTION)==(degrees<=360U));
            CHECK(r.motion.direction_deg==(degrees<=360U?degrees:0));
        }
        location(m);
    }
    /* Exhaust all height encodings and both reference bits. Zero is absent;
     * valid negative/zero metre heights keep their flag, unlike an absent field. */
    for(unsigned agl=0;agl<2;++agl)for(unsigned raw=0;raw<=UINT16_MAX;++raw){
        m[1]=(uint8_t)(agl*4U);put16(m+17,(uint16_t)raw);
        sentinel_watch_protocol_record_t r=parse(0,m);
        CHECK(r.motion.height_encoded==raw);
        CHECK(!!(r.motion.flags&SENTINEL_WATCH_MOTION_HEIGHT)==(raw!=0));
        CHECK(!!(r.motion.flags&SENTINEL_WATCH_MOTION_HEIGHT_AGL)==(agl&&raw));
    }
    location(m);m[1]=7;m[2]=181;m[3]=255;put16(m+17,0);
    for(unsigned form=0;form<4;++form){sentinel_watch_protocol_record_t r=parse(form,m);
        CHECK(r.motion.flags==SENTINEL_WATCH_MOTION_LOCATION_PRESENT);
        CHECK(!r.motion.speed_qms&&!r.motion.direction_deg&&!r.motion.height_encoded);}
    /* Explicit primary-reference distinctions: the same raw byte is not a sentinel in both ranges. */
    m[1]=0;sentinel_watch_protocol_record_t r=parse(0,m);
    CHECK(r.motion.speed_qms==255&&r.motion.direction_deg==181&&r.motion.flags==7);
    m[1]=2;m[2]=180;r=parse(0,m);CHECK(r.motion.direction_deg==360&&(r.motion.flags&4));
    m[1]=0;m[2]=0;m[3]=0;put16(m+17,2000);r=parse(0,m);
    CHECK(r.motion.flags==15&&!r.motion.speed_qms&&!r.motion.direction_deg&&r.motion.height_encoded==2000);
}
static void codec_canonicality(void)
{
    sentinel_watch_motion_t m={0};CHECK(sizeof(m)==8);CHECK(sentinel_watch_motion_valid(&m));
    CHECK(!sentinel_watch_motion_valid(NULL));
    for(unsigned flags=0;flags<256;++flags){m=(sentinel_watch_motion_t){.flags=(uint8_t)flags};
        bool expected=flags==0||flags==1||flags==3||flags==5||flags==7;
        CHECK(sentinel_watch_motion_valid(&m)==expected);}
    for(unsigned raw=0;raw<=UINT16_MAX;++raw){
        m=(sentinel_watch_motion_t){.speed_qms=(uint16_t)raw,.flags=3};
        CHECK(sentinel_watch_motion_valid(&m)==(raw<=255||(raw<=1017&&raw%3==0)));
        m.flags=1;CHECK(sentinel_watch_motion_valid(&m)==(raw==0));
        m=(sentinel_watch_motion_t){.direction_deg=(uint16_t)raw,.flags=5};
        CHECK(sentinel_watch_motion_valid(&m)==(raw<=360));
        m.flags=1;CHECK(sentinel_watch_motion_valid(&m)==(raw==0));
        m=(sentinel_watch_motion_t){.height_encoded=(uint16_t)raw,.flags=25};
        CHECK(sentinel_watch_motion_valid(&m)==(raw!=0));
        m.flags=1;CHECK(sentinel_watch_motion_valid(&m)==(raw==0));
    }
    m=(sentinel_watch_motion_t){.flags=1,.reserved=1};CHECK(!sentinel_watch_motion_valid(&m));
    m=(sentinel_watch_motion_t){.speed_qms=1};CHECK(!sentinel_watch_motion_valid(&m));
}
static void transactional_frames(void)
{
    uint8_t m[75],wire[256];basic(m,'A');location(m+25);basic(m+50,'B');
    for(unsigned form=0;form<4;++form){
        size_t n=frame(form,m,2,wire);sentinel_watch_protocol_record_t r;
        CHECK(decode(form,wire,n,&r));CHECK(r.motion.flags==15&&r.id[0]=='A');
        for(size_t cut=0;cut<n;++cut)rejects(form,wire,cut);
        /* A later malformed complete AD/IE/attribute invalidates the earlier motion too. */
        wire[n]=9;rejects(form,wire,n+1);
        location(m+50);n=frame(form,m,3,wire);rejects(form,wire,n); /* duplicate Location */
        basic(m+50,'B');m[49]=1;n=frame(form,m,2,wire);rejects(form,wire,n);m[49]=0;
        m[25]=0x11;n=frame(form,m,2,wire);rejects(form,wire,n);m[25]=0x12;
    }
}
static const uint8_t source[6]={2,3,4,5,6,7};
static sentinel_watch_protocol_record_t report(bool loc,char id)
{uint8_t m[25];if(loc)location(m);else basic(m,id);return parse(0,m);}
static void observe(cerberus_watch_capture_t *c,const sentinel_watch_protocol_record_t *r,uint64_t us)
{cerberus_watch_capture_observe_timed(c,r,source,1,-45,0,us);}
static void read_motion(const cerberus_watch_capture_t *c,uint8_t index,uint64_t now,
    sentinel_watch_motion_t *m,cerberus_watch_scan_timing_payload_t *t)
{
    cerberus_watch_capture_t finished=*c;cerberus_watch_capture_finish(&finished,CERBERUS_WATCH_COMPLETE,10);
    CHECK(cerberus_watch_capture_get_motion(&finished,finished.status.radio,finished.status.generation,index,now,m,t));
    CHECK(t->capture_epoch==c->capture_epoch&&t->index==index&&sentinel_watch_motion_valid(m));
}
static void collector_group_lifetime(void)
{
    cerberus_watch_capture_t c;sentinel_watch_motion_t m;cerberus_watch_scan_timing_payload_t t;
    sentinel_watch_protocol_record_t loc=report(true,0),id=report(false,'A');
    cerberus_watch_capture_begin_with_epoch(&c,CERBERUS_WATCH_RADIO_BLE,255,0,42);
    observe(&c,&id,0);read_motion(&c,0,100,&m,&t);CHECK(zeros(&m,sizeof(m))&&!t.flags&&!t.source_age_ms);
    observe(&c,&loc,0);read_motion(&c,0,1,&m,&t);CHECK(m.flags==15&&t.flags==1&&t.source_age_ms==1);
    observe(&c,&id,9000000);read_motion(&c,0,9000001,&m,&t);CHECK(t.source_age_ms==9001&&c.motion_us[0]==0);
    cerberus_watch_capture_observe(&c,&id,source,1,-60,0);
    read_motion(&c,0,10000000,&m,&t);CHECK(t.source_age_ms==10000&&t.flags==1);
    sentinel_watch_motion_t retained=m;
    observe(&c,NULL,20000000);read_motion(&c,0,20000000,&m,&t);
    CHECK(!memcmp(&retained,&m,sizeof(m))&&t.source_age_ms==20000);
    /* New Location has unavailable coordinates/altitude/motion; clear the whole
     * previous group while retaining its independently advertised Basic ID. */
    uint8_t empty[25]={0x12,7,181,255};loc=parse(0,empty);observe(&c,&loc,21000000);
    CHECK(c.records[0].flags==SENTINEL_WATCH_PROTOCOL_HAS_ID&&c.records[0].id[0]=='A');
    CHECK(!c.records[0].latitude_e7&&!c.records[0].longitude_e7&&!c.records[0].altitude_dm&&!c.records[0].altitude_kind);
    read_motion(&c,0,21000001,&m,&t);CHECK(m.flags==1&&t.flags==1&&t.source_age_ms==1);
    loc=report(true,0);observe(&c,&loc,22000000);id=report(false,'B');observe(&c,&id,23000000);
    read_motion(&c,0,24000000,&m,&t);CHECK(zeros(&m,sizeof(m))&&!t.flags&&!t.source_age_ms);
    CHECK(c.records[0].flags==1&&c.records[0].id[0]=='B'&&!c.records[0].latitude_e7);
    observe(&c,&loc,25000000);cerberus_watch_capture_observe(&c,&loc,source,1,-45,0);
    read_motion(&c,0,26000000,&m,&t);CHECK(m.flags==15&&!t.flags&&!t.source_age_ms);
    observe(&c,&loc,27000000);observe(&c,&loc,26999999);
    read_motion(&c,0,28000000,&m,&t);CHECK(m.flags==15&&!t.flags);
    observe(&c,&loc,29000000);read_motion(&c,0,28999999,&m,&t);CHECK(m.flags==15&&!t.flags&&!t.source_age_ms);
    read_motion(&c,0,29000000,&m,&t);CHECK(t.flags==1&&!t.source_age_ms);
    observe(&c,&loc,UINT64_MAX-1);read_motion(&c,0,UINT64_MAX,&m,&t);CHECK(t.flags==1&&t.source_age_ms==1);
    cerberus_watch_capture_begin_with_epoch(&c,CERBERUS_WATCH_RADIO_BLE,0,0,43);
    observe(&c,&loc,0);read_motion(&c,0,UINT64_MAX,&m,&t);
    CHECK(t.flags==1&&t.source_age_ms==UINT64_MAX/1000U+1U);
    cerberus_watch_capture_t before=c;sentinel_watch_protocol_record_t invalid=loc;invalid.motion.reserved=1;
    observe(&c,&invalid,100);CHECK(c.status.reports==before.status.reports+1);
    c.status.reports=before.status.reports;CHECK(!memcmp(&c,&before,sizeof(c)));
    /* Exact address type/transport identity stays independent. */
    cerberus_watch_capture_observe_timed(&c,&id,source,0,-45,0,2);CHECK(c.status.stored_count==2);
    read_motion(&c,1,10,&m,&t);CHECK(zeros(&m,sizeof(m))&&!t.flags);
    /* A later generic source report establishes a clock floor for Location.
     * Clearing a changed claimed ID must not erase that known regression. */
    for(unsigned changed_id=0;changed_id<2;++changed_id){
        cerberus_watch_capture_begin_with_epoch(&c,CERBERUS_WATCH_RADIO_BLE,5,0,45);
        id=report(false,'A');observe(&c,&id,90);observe(&c,&loc,100);observe(&c,&id,200);
        sentinel_watch_protocol_record_t replacement=loc;
        if(changed_id){uint8_t messages[50],wire[256];basic(messages,'B');location(messages+25);
            size_t size=frame(0,messages,2,wire);CHECK(decode(0,wire,size,&replacement));}
        observe(&c,&replacement,150);read_motion(&c,0,300,&m,&t);
        CHECK(m.flags==15&&!t.flags&&!t.source_age_ms&&!c.qualifying_valid&&!c.motion_valid);
        if(changed_id)CHECK(c.records[0].id[0]=='B');
        cerberus_watch_capture_finish(&c,CERBERUS_WATCH_COMPLETE,1);
        CHECK(cerberus_watch_capture_get_timing(&c,CERBERUS_WATCH_RADIO_BLE,5,0,300,&t));
        CHECK(!t.flags&&!t.source_age_ms);
    }
}
static void collector_gates_eviction_and_reset(void)
{
    cerberus_watch_capture_t c;sentinel_watch_motion_t m;cerberus_watch_scan_timing_payload_t t;
    sentinel_watch_protocol_record_t loc=report(true,0),id=report(false,'A');
    loc.transport=id.transport=SENTINEL_WATCH_TRANSPORT_WIFI_BEACON;
    uint8_t target[6]={2,9,9,9,9,9};
    cerberus_watch_capture_begin_with_epoch(&c,CERBERUS_WATCH_RADIO_WIFI,2,0,UINT64_MAX);
    CHECK(cerberus_watch_capture_protect_wifi_source(&c,SENTINEL_WATCH_TRANSPORT_WIFI_NAN,target));
    for(unsigned i=0;i<12;++i){uint8_t addr[6]={2,0,0,0,0,(uint8_t)i};
        cerberus_watch_capture_observe_timed(&c,&loc,addr,0,-50,6,i);}
    sentinel_watch_motion_t first[11];memcpy(first,c.motion,sizeof(first));
    id.transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    cerberus_watch_capture_observe_timed(&c,&id,target,0,-40,149,100);
    CHECK(c.status.stored_count==12&&c.status.dropped_records==1&&(c.status.flags&CERBERUS_WATCH_CAPTURE_OVERFLOW));
    CHECK(!memcmp(first,c.motion,sizeof(first))&&zeros(&c.motion[11],sizeof(m))&&!c.motion_us[11]&&!(c.motion_valid&(1U<<11)));
    loc.transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    cerberus_watch_capture_observe_timed(&c,&loc,target,0,-40,149,101);
    read_motion(&c,11,102,&m,&t);CHECK(m.flags==15&&t.flags==1&&t.source_age_ms==1);
    memset(&m,0xa5,sizeof(m));memset(&t,0xa5,sizeof(t));
    CHECK(!cerberus_watch_capture_get_motion(&c,1,2,0,100,&m,&t));CHECK(zeros(&m,sizeof(m))&&zeros(&t,sizeof(t)));
    cerberus_watch_capture_finish(&c,CERBERUS_WATCH_COMPLETE,10);
    for(unsigned failure=0;failure<6;++failure){
        memset(&m,0xa5,sizeof(m));memset(&t,0xa5,sizeof(t));
        CHECK(!cerberus_watch_capture_get_motion(failure==0?NULL:&c,
            failure==1?CERBERUS_WATCH_RADIO_BLE:CERBERUS_WATCH_RADIO_WIFI,
            failure==2?3:2,failure==3?12:0,100,failure==4?NULL:&m,failure==5?NULL:&t));
        if(failure!=4)CHECK(zeros(&m,sizeof(m)));if(failure!=5)CHECK(zeros(&t,sizeof(t)));
    }
    c.motion[0].reserved=1;CHECK(!cerberus_watch_capture_get_motion(&c,CERBERUS_WATCH_RADIO_WIFI,2,0,100,&m,&t));
    CHECK(zeros(&m,sizeof(m))&&zeros(&t,sizeof(t)));c.motion[0].reserved=0;
    sentinel_watch_motion_t retained=c.motion[0];
    cerberus_watch_capture_finish(&c,CERBERUS_WATCH_IDLE,11);
    CHECK(!memcmp(&c.motion[0],&retained,sizeof(retained))&&zeros(c.motion_us,sizeof(c.motion_us))&&!c.motion_valid);
    cerberus_watch_capture_begin_with_epoch(&c,CERBERUS_WATCH_RADIO_BLE,3,0,44);
    CHECK(zeros(c.motion,sizeof(c.motion))&&zeros(c.motion_us,sizeof(c.motion_us))&&!c.motion_valid);
    loc=report(true,0);observe(&c,&loc,10);cerberus_watch_capture_finish(&c,CERBERUS_WATCH_ERROR,11);
    CHECK(!c.status.stored_count&&zeros(c.motion,sizeof(c.motion))&&zeros(c.motion_us,sizeof(c.motion_us))&&!c.motion_valid);
    cerberus_watch_capture_begin(&c,CERBERUS_WATCH_RADIO_BLE,4,0);loc=report(true,0);observe(&c,&loc,0);
    cerberus_watch_capture_finish(&c,CERBERUS_WATCH_COMPLETE,10);
    CHECK(!cerberus_watch_capture_get_motion(&c,CERBERUS_WATCH_RADIO_BLE,4,0,100,&m,&t));
    CHECK(zeros(&m,sizeof(m))&&zeros(&t,sizeof(t)));
    CHECK(sizeof(cerberus_watch_observation_t)==46);
}
int main(void)
{
    parser_values_and_boundaries();codec_canonicality();transactional_frames();
    collector_group_lifetime();collector_gates_eviction_and_reset();
    printf("Watch motion decoder/collector: %u checks; motion=%zu collector=%zu bytes\n",
        checks,sizeof(sentinel_watch_motion_t),sizeof(cerberus_watch_capture_t));return 0;
}
