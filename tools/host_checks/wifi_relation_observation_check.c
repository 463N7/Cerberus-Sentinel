#undef NDEBUG
#include "wifi_relation_observation.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t packet[128];
static const uint8_t ap[6]={2,1,2,3,4,5},sta[6]={4,6,7,8,9,10};
static size_t frame(unsigned direction,unsigned subtype,bool order)
{
    memset(packet,0,sizeof(packet));packet[0]=(uint8_t)(8U|(subtype<<4U));
    packet[1]=(uint8_t)(direction|(order?128U:0U));
    memcpy(packet+4,direction==1?ap:sta,6);memcpy(packet+10,direction==1?sta:ap,6);
    memset(packet+16,255,6); /* addr3 is opaque, not the inferred relation */
    return 24U+((subtype&8U)?2U+(order?4U:0U):0U);
}
static void zero(const void *data,size_t size)
{const uint8_t *p=data;for(size_t i=0;i<size;++i)CHECK(!p[i]);}
static void reject(size_t n,uint8_t channel,uint64_t time)
{cerberus_wifi_relation_row_t r;memset(&r,0xa5,sizeof(r));CHECK(!cerberus_wifi_relation_decode(packet,n,channel,-55,time,&r));zero(&r,sizeof(r));}
static cerberus_wifi_relation_row_t decode(unsigned ds,uint64_t time,int rssi)
{
    size_t n=frame(ds,8,false);cerberus_wifi_relation_row_t r;
    CHECK(cerberus_wifi_relation_decode(packet,n,149,rssi,time,&r));CHECK(cerberus_wifi_relation_row_valid(&r));return r;
}
static void parser(void)
{
    cerberus_wifi_relation_row_t r;
    for(unsigned ds=1;ds<=2;++ds)for(unsigned sub=0;sub<16;++sub)for(unsigned order=0;order<2;++order){
        size_t n=frame(ds,sub,order!=0);bool allowed=sub!=13&&(!order||(sub&8));
        CHECK(cerberus_wifi_relation_decode(packet,n,6,-55,0,&r)==allowed);
        if(!allowed){zero(&r,sizeof(r));continue;}
        CHECK(!memcmp(r.bssid,ap,6)&&!memcmp(r.station,sta,6)&&r.channel==6&&r.latest_direction==ds&&r.directions==ds);
        CHECK(r.to_ap_frames==(ds==1)&&r.from_ap_frames==(ds==2)&&!r.to_ap_callback_us&&!r.from_ap_callback_us);
        CHECK(r.to_ap_rssi==(ds==1?-55:127)&&r.from_ap_rssi==(ds==2?-55:127)&&r.rssi_valid==ds);
        CHECK(cerberus_wifi_relation_row_valid(&r));
        for(size_t cut=0;cut<n;++cut)reject(cut,6,0);
        memset(packet+n,0xa5,8);CHECK(cerberus_wifi_relation_decode(packet,n+8,6,-55,0,&r));
        packet[1]|=0x78;CHECK(cerberus_wifi_relation_decode(packet,n,6,-55,0,&r)); /* protected/retry/PM/moredata */
        packet[1]|=4;reject(n,6,0);packet[1]&=(uint8_t)~4U;
        packet[22]=1;reject(n,6,0);packet[22]=0xf0;CHECK(cerberus_wifi_relation_decode(packet,n,6,-55,0,&r));
    }
    size_t n=frame(1,8,false);
    for(unsigned qos=0;qos<=UINT16_MAX;++qos){packet[24]=(uint8_t)qos;packet[25]=(uint8_t)(qos>>8);
        CHECK(cerberus_wifi_relation_decode(packet,n,36,-90,UINT64_C(4294967296000),&r));}
    for(unsigned fc=0;fc<256;++fc){n=frame(1,0,false);packet[0]=(uint8_t)fc;
        bool expected=(fc&15U)==8U&&(fc>>4U)<8U;
        CHECK(cerberus_wifi_relation_decode(packet,n,1,-1,1,&r)==expected);}
    n=frame(1,0,false);for(unsigned ds=0;ds<4;++ds){packet[1]=(uint8_t)ds;CHECK(cerberus_wifi_relation_decode(packet,n,1,-1,1,&r)==(ds==1||ds==2));}
    n=frame(1,0,false);for(unsigned channel=0;channel<256;++channel){
        bool valid=(channel>=1&&channel<=14)||(channel>=36&&channel<=64&&channel%4==0)||
            (channel>=100&&channel<=144&&channel%4==0)||(channel>=149&&channel<=177&&(channel-149)%4==0);
        CHECK(cerberus_wifi_relation_decode(packet,n,(uint8_t)channel,-127,1,&r)==valid);}
    for(int signal=-300;signal<=300;++signal){r=decode(1,10,signal);CHECK(r.rssi_valid==(signal>=-127&&signal<=0?1:0));CHECK(r.to_ap_rssi==(signal>=-127&&signal<=0?signal:127));}
    for(unsigned address=4;address<=10;address+=6){n=frame(1,0,false);packet[address]|=1;reject(n,6,0);memset(packet+address,0,6);reject(n,6,0);}
    n=frame(1,0,false);memcpy(packet+4,packet+10,6);reject(n,6,0);
    n=frame(2,12,true);reject(n,6,(uint64_t)INT64_MAX+1U);
    CHECK(cerberus_wifi_relation_decode(packet,n,177,0,INT64_MAX,&r));
    CHECK(!cerberus_wifi_relation_decode(NULL,n,6,-55,0,&r));zero(&r,sizeof(r));
    CHECK(!cerberus_wifi_relation_decode(packet,n,6,-55,0,NULL));
}
static void valid_rows(void)
{
    cerberus_wifi_relation_row_t r=decode(1,0,-77),x;
    for(unsigned b=0;b<256;++b){x=r;x.directions=(uint8_t)b;CHECK(cerberus_wifi_relation_row_valid(&x)==(b==1));
        x=r;x.latest_direction=(uint8_t)b;CHECK(cerberus_wifi_relation_row_valid(&x)==(b==1));
        x=r;x.rssi_valid=(uint8_t)b;CHECK(cerberus_wifi_relation_row_valid(&x)==(b==1));}
    x=r;x.to_ap_frames=0;CHECK(!cerberus_wifi_relation_row_valid(&x));
    x=r;x.from_ap_callback_us=1;CHECK(!cerberus_wifi_relation_row_valid(&x));
    x=r;x.from_ap_rssi=0;CHECK(!cerberus_wifi_relation_row_valid(&x));
    x=r;x.reserved[1]=1;CHECK(!cerberus_wifi_relation_row_valid(&x));
    x=r;x.to_ap_callback_us=(uint64_t)INT64_MAX+1;CHECK(!cerberus_wifi_relation_row_valid(&x));
    CHECK(!cerberus_wifi_relation_row_valid(NULL));
}
static void table(void)
{
    cerberus_wifi_relation_table_t t={0};cerberus_wifi_relation_row_t r=decode(1,0,-55);
    CHECK(!cerberus_wifi_relation_commit(&t,1,&r,0));cerberus_wifi_relation_begin(&t,255);
    CHECK(t.status.epoch==1&&t.status.generation==255&&cerberus_wifi_relation_status_valid(&t.status));
    CHECK(!cerberus_wifi_relation_commit(&t,1,&r,0));cerberus_wifi_relation_arm(&t,true,0);
    CHECK(cerberus_wifi_relation_commit(&t,1,&r,0));r=decode(2,0,-90);CHECK(cerberus_wifi_relation_commit(&t,1,&r,0));
    CHECK(t.status.count==1&&t.rows[0].directions==3&&t.rows[0].latest_direction==2&&t.rows[0].to_ap_rssi==-55&&t.rows[0].from_ap_rssi==-90);
    r=decode(1,UINT64_C(4294967296000),127);CHECK(cerberus_wifi_relation_commit(&t,1,&r,UINT64_C(4294967296000)));
    CHECK(t.rows[0].rssi_valid==2&&t.rows[0].to_ap_rssi==127&&t.rows[0].to_ap_frames==2&&t.rows[0].from_ap_frames==1);
    cerberus_wifi_relation_table_t prior=t;
    r=decode(2,99,-1);CHECK(!cerberus_wifi_relation_commit(&t,1,&r,99));CHECK(!memcmp(&prior,&t,sizeof(t)));
    r=decode(2,UINT64_C(4294967296001),-88);CHECK(!cerberus_wifi_relation_commit(&t,2,&r,UINT64_C(4294967296001)));CHECK(!memcmp(&prior,&t,sizeof(t)));
    for(unsigned i=1;i<33;++i){r=decode(i%2+1,UINT64_C(4294967296001)+i,-80);r.station[5]=(uint8_t)(10+i);
        CHECK(cerberus_wifi_relation_commit(&t,1,&r,UINT64_C(4294967296001)+i)==(i<32));}
    CHECK(t.status.count==32&&t.status.matched_frames==35&&t.status.dropped_frames==1&&t.status.flags==3);
    CHECK(cerberus_wifi_relation_status_valid(&t.status));
    uint32_t sum=t.status.dropped_frames;for(unsigned i=0;i<32;++i){CHECK(cerberus_wifi_relation_row_valid(&t.rows[i]));sum+=t.rows[i].to_ap_frames+t.rows[i].from_ap_frames;}
    CHECK(sum==t.status.matched_frames);
    r=decode(2,t.latest_us+1,-100);CHECK(cerberus_wifi_relation_commit(&t,1,&r,t.latest_us+1));CHECK(t.status.count==32&&t.rows[0].from_ap_frames==2);
    cerberus_wifi_relation_finish(&t,2);prior=t;
    r=decode(2,t.latest_us+1,-1);CHECK(!cerberus_wifi_relation_commit(&t,1,&r,t.latest_us+1));CHECK(!memcmp(&prior,&t,sizeof(t)));
    for(unsigned i=0;i<32;++i){cerberus_wifi_relation_row_t assembled={0};for(unsigned chunk=0;chunk<5;++chunk){
        cerberus_wifi_relation_query_t q={1,255,(uint8_t)i,(uint8_t)chunk};cerberus_wifi_relation_chunk_t out;
        CHECK(cerberus_wifi_relation_get_chunk(&t,&q,&out));CHECK(out.epoch==1&&out.generation==255&&out.index==i&&out.chunk==chunk&&out.length==(chunk==4?4:10));
        memcpy((uint8_t *)&assembled+chunk*10,out.data,out.length);zero(out.data+out.length,10-out.length);}
        CHECK(!memcmp(&assembled,&t.rows[i],44));}
    cerberus_wifi_relation_clock_query_t cq={1,255};cerberus_wifi_relation_clock_t clock;
    CHECK(cerberus_wifi_relation_get_clock(&t,&cq,t.latest_us,&clock));CHECK(clock.epoch==1&&clock.count==32&&clock.source_now_us==t.latest_us);
    CHECK(!cerberus_wifi_relation_get_clock(&t,&cq,t.latest_us-1,&clock));zero(&clock,sizeof(clock));
    CHECK(!cerberus_wifi_relation_get_clock(&t,&cq,UINT64_MAX,&clock));zero(&clock,sizeof(clock));
    cerberus_wifi_relation_query_t q={1,255,0,0};cerberus_wifi_relation_chunk_t out;
    cerberus_wifi_relation_begin(&t,0);CHECK(!cerberus_wifi_relation_get_chunk(&t,&q,&out));zero(&out,sizeof(out));
    CHECK(t.status.epoch==2&&!t.status.count&&!t.status.flags);
    cerberus_wifi_relation_arm(&t,true,100);r=decode(1,99,-50);CHECK(!cerberus_wifi_relation_commit(&t,2,&r,99));
    r=decode(1,100,-50);CHECK(cerberus_wifi_relation_commit(&t,2,&r,100));
    cerberus_wifi_relation_finish(&t,0);CHECK(!t.status.count&&!t.status.flags&&t.status.epoch==2&&!t.accepting);zero(t.rows,sizeof(t.rows));
    cerberus_wifi_relation_begin(&t,1);cerberus_wifi_relation_arm(&t,false,0);cerberus_wifi_relation_finish(&t,2);CHECK(cerberus_wifi_relation_status_valid(&t.status)&&!t.status.flags);
    t.epoch_counter=UINT64_MAX;cerberus_wifi_relation_begin(&t,2);cerberus_wifi_relation_arm(&t,true,0);CHECK(!t.status.epoch&&!t.accepting);
    /* Saturation remains stable, and an existing key remains updateable when full. */
    memset(&t,0,sizeof(t));cerberus_wifi_relation_begin(&t,0);cerberus_wifi_relation_arm(&t,true,0);
    for(unsigned i=0;i<32;++i){r=decode(1,i,-55);r.station[5]=(uint8_t)i;CHECK(cerberus_wifi_relation_commit(&t,1,&r,i));}
    t.status.matched_frames=UINT32_MAX;t.status.dropped_frames=UINT32_MAX;t.status.flags=3;t.rows[0].to_ap_frames=UINT32_MAX;
    r=decode(1,33,-90);r.station[5]=0;CHECK(cerberus_wifi_relation_commit(&t,1,&r,33));
    CHECK(t.rows[0].to_ap_frames==UINT32_MAX&&t.rows[0].to_ap_rssi==-90&&t.status.matched_frames==UINT32_MAX);
    r.station[5]=99;r.to_ap_callback_us=34;CHECK(!cerberus_wifi_relation_commit(&t,1,&r,34));CHECK(t.status.dropped_frames==UINT32_MAX);
    for(unsigned i=0;i<32;++i){cerberus_wifi_relation_status_t bad=t.status;bad.count=(uint8_t)i;CHECK(!cerberus_wifi_relation_status_valid(&bad));}
}
int main(void)
{parser();valid_rows();table();printf("Wi-Fi relation parser/table: %u checks; table %zu bytes.\n",checks,sizeof(cerberus_wifi_relation_table_t));return 0;}
