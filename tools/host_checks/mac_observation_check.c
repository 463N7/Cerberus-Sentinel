#include "mac_observation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);} }while(0)
static bool zero(const void *p,size_t n)
{const uint8_t *b=p;for(size_t i=0;i<n;++i)if(b[i])return false;return true;}
static void address(unsigned key,uint8_t out[6])
{memcpy(out,(uint8_t[]){2,3,4,5,(uint8_t)(key>>8),(uint8_t)key},6);}
static void domains(void)
{
    cerberus_mac_observation_t c;cerberus_mac_snapshot_t s;uint8_t mac[6];address(1,mac);
    CHECK(!cerberus_mac_observation_init(NULL,0,0));
    CHECK(!cerberus_mac_observation_init(&c,2,0));CHECK(zero(&c,sizeof(c)));
    for(unsigned radio=0;radio<2;++radio)for(unsigned type=0;type<256;++type)
        for(unsigned mask=0;mask<16;++mask){
            CHECK(cerberus_mac_observation_init(&c,radio,0));
            bool expected=radio==0?type==0&&(mask==1||mask==2||mask==4):
                type<=3 && (mask==1||mask==2||mask==5||mask==6);
            CHECK(cerberus_mac_observation_observe(&c,mac,type,-40,radio?0:6,mask,0)==expected);
            CHECK(c.observations==(unsigned)expected && c.invalid==(unsigned)!expected);
            CHECK(cerberus_mac_observation_snapshot(&c,0,&s));CHECK(cerberus_mac_snapshot_valid(&s));
        }
    for(int signal=-256;signal<=256;++signal){
        CHECK(cerberus_mac_observation_init(&c,1,0));
        CHECK(cerberus_mac_observation_observe(&c,mac,0,signal,0,1,0));
        bool valid=signal>=-127&&signal<=20;
        CHECK(c.rows[0].flags==(valid?1:0));CHECK(c.rows[0].rssi==(valid?signal:127));
        CHECK(cerberus_mac_observation_snapshot(&c,0,&s));
    }
    for(unsigned ch=0;ch<256;++ch){
        CHECK(cerberus_mac_observation_init(&c,0,0));
        bool valid=(ch>=1&&ch<=14)||(ch>=36&&ch<=64&&!(ch%4))||
            (ch>=100&&ch<=144&&!(ch%4))||(ch>=149&&ch<=177&&!((ch-149)%4));
        CHECK(cerberus_mac_observation_observe(&c,mac,0,-40,ch,1,0)==valid);
    }
    CHECK(cerberus_mac_observation_init(&c,0,0));
    CHECK(!cerberus_mac_observation_observe(&c,(uint8_t[6]){0},0,-50,6,1,0));
    mac[0]|=1;CHECK(!cerberus_mac_observation_observe(&c,mac,0,-50,6,1,0));
    CHECK(cerberus_mac_observation_init(&c,1,0));
    CHECK(cerberus_mac_observation_observe(&c,(uint8_t[6]){0},0,-50,0,1,0));
    CHECK(cerberus_mac_observation_observe(&c,mac,3,-50,0,1,0));
    CHECK(c.count==2); /* BLE byte order/type is not an Ethernet multicast test. */
}

/* Independent bounded LRU oracle: victim is smallest last callback, then
 * earliest incarnation. No implementation slot assumptions enter selection. */
static void oracle(void)
{
    struct expected {unsigned key,type,inc,count,rsp;uint64_t first,last;} rows[32]={0};
    unsigned n=0,inc=0,replacements=0;cerberus_mac_observation_t c;
    cerberus_mac_snapshot_t s;CHECK(cerberus_mac_observation_init(&c,1,0));
    for(unsigned step=0;step<2048;++step){
        unsigned key=(step*37+step/11)%91,type=(step/13)%4;
        uint64_t now=((uint64_t)1<<43)+(step/3); /* Ties and beyond 32-bit uptime. */
        bool response=(step%5)==0;uint8_t mac[6];address(key,mac);
        unsigned pos=n;
        for(unsigned i=0;i<n;++i)if(rows[i].key==key&&rows[i].type==type){pos=i;break;}
        if(pos==n){
            if(n<32)++n;
            else{pos=0;for(unsigned i=1;i<32;++i)
                if(rows[i].last<rows[pos].last || (rows[i].last==rows[pos].last&&rows[i].inc<rows[pos].inc))pos=i;
                ++replacements;}
            rows[pos]=(struct expected){.key=key,.type=type,.inc=++inc,.first=now};
        }
        rows[pos].last=now;++rows[pos].count;rows[pos].rsp+=response;
        CHECK(cerberus_mac_observation_observe(&c,mac,type,-30-(step%50),0,response?6:1,now));
        CHECK(cerberus_mac_observation_snapshot(&c,now,&s));
        CHECK(s.header.count==n && s.header.observations==step+1 && s.header.replacements==replacements);
        for(unsigned i=0;i<n;++i){
            unsigned j=0;for(;j<n;++j)if(s.rows[j].incarnation==rows[i].inc)break;
            CHECK(j<n);const cerberus_mac_row_t *r=&s.rows[j];address(rows[i].key,mac);
            CHECK(!memcmp(r->address,mac,6)&&r->address_type==rows[i].type);
            CHECK(r->observations==rows[i].count&&r->responses==rows[i].rsp);
            CHECK(r->first_us==rows[i].first&&r->last_us==rows[i].last);
        }
    }
}
static void boundaries(void)
{
    cerberus_mac_observation_t c;cerberus_mac_snapshot_t s,old;
    uint8_t mac[6];address(1,mac);
    CHECK(cerberus_mac_observation_init(&c,1,100));
    CHECK(!cerberus_mac_observation_observe(&c,mac,0,-30,0,1,99));
    CHECK(c.invalid==1&&!c.count);
    CHECK(cerberus_mac_observation_observe(&c,mac,0,-30,0,1,100));
    CHECK(cerberus_mac_observation_observe(&c,mac,0,127,0,6,200));
    CHECK(c.rows[0].first_us==100&&c.rows[0].last_us==200&&c.rows[0].rssi==127&&c.rows[0].flags==0);
    CHECK(c.rows[0].observations==2&&c.rows[0].responses==1&&c.rows[0].type_mask==7);
    CHECK(cerberus_mac_observation_snapshot(&c,200,&old));
    CHECK(!cerberus_mac_observation_observe(&c,mac,0,-10,0,1,199));
    address(2,mac);CHECK(!cerberus_mac_observation_observe(&c,mac,0,-10,0,1,199));
    CHECK(c.count==1&&c.next_incarnation==1&&c.invalid==3);
    memset(&s,0xa5,sizeof(s));CHECK(!cerberus_mac_observation_snapshot(&c,199,&s));
    CHECK(zero(&s,sizeof(s))&&c.next_snapshot_id==1);
    CHECK(cerberus_mac_observation_snapshot(&c,201,&s));CHECK(s.header.snapshot_id==2);
    CHECK(old.header.snapshot_id==1&&old.header.invalid==1); /* Frozen independent copy. */
    c.next_incarnation=UINT32_MAX;
    CHECK(!cerberus_mac_observation_observe(&c,mac,0,-10,0,1,201));
    address(1,mac);CHECK(cerberus_mac_observation_observe(&c,mac,0,-10,0,1,201));
    c.next_snapshot_id=UINT32_MAX;CHECK(!cerberus_mac_observation_snapshot(&c,201,&s));CHECK(zero(&s,sizeof(s)));
    c.next_snapshot_id=UINT32_MAX-1;CHECK(cerberus_mac_observation_snapshot(&c,201,&s));
    CHECK(s.header.snapshot_id==UINT32_MAX);CHECK(!cerberus_mac_observation_snapshot(&c,201,&s));
    c.invalid=c.observations=UINT32_MAX;c.replacements=0;c.rows[0].observations=c.rows[0].responses=UINT32_MAX;
    CHECK(cerberus_mac_observation_observe(&c,mac,0,-127,0,2,UINT64_MAX));
    CHECK(c.observations==UINT32_MAX&&c.rows[0].observations==UINT32_MAX&&c.rows[0].responses==UINT32_MAX);
    cerberus_mac_observation_reject(&c);CHECK(c.invalid==UINT32_MAX);
    c.next_snapshot_id=0;CHECK(cerberus_mac_observation_snapshot(&c,UINT64_MAX,&s));
    CHECK(cerberus_mac_snapshot_valid(&s));
}
static void validation(void)
{
    cerberus_mac_observation_t c;cerberus_mac_snapshot_t base,s;
    uint8_t mac[6];address(5,mac);CHECK(cerberus_mac_observation_init(&c,1,0));
    CHECK(cerberus_mac_observation_observe(&c,mac,0,-40,0,1,0));
    CHECK(cerberus_mac_observation_snapshot(&c,1,&base));
#define BAD(field,value) do{s=base;s.field=(value);CHECK(!cerberus_mac_snapshot_valid(&s));}while(0)
    BAD(header.version,2);BAD(header.radio,2);BAD(header.snapshot_id,0);BAD(header.count,33);
    BAD(header.reserved,1);BAD(header.observations,0);BAD(header.observations,2);BAD(header.replacements,1);BAD(rows[0].incarnation,0);
    BAD(rows[0].radio,0);BAD(rows[0].address_type,4);BAD(rows[0].first_us,1);
    BAD(rows[0].last_us,2);BAD(rows[0].channel,1);BAD(rows[0].type_mask,0);
    BAD(rows[0].type_mask,4);BAD(rows[0].type_mask,9);BAD(rows[0].flags,2);
    BAD(rows[0].flags,0);BAD(rows[0].rssi,21);BAD(rows[0].rssi,-128);
    BAD(rows[0].responses,1);BAD(rows[1].address[0],1);
    s=base;s.header.count=2;s.header.observations=2;s.rows[1]=s.rows[0];
    CHECK(!cerberus_mac_snapshot_valid(&s));s.rows[1].incarnation=2;
    CHECK(!cerberus_mac_snapshot_valid(&s));s.rows[1].address_type=2;
    CHECK(cerberus_mac_snapshot_valid(&s));s.rows[1].incarnation=1;
    CHECK(!cerberus_mac_snapshot_valid(&s));
    /* A malformed internal row cannot partially publish or consume an ID. */
    c.rows[0].flags=2;uint32_t id=c.next_snapshot_id;
    CHECK(!cerberus_mac_observation_snapshot(&c,1,&s));CHECK(zero(&s,sizeof(s))&&c.next_snapshot_id==id);
#undef BAD
}
int main(void)
{
    CHECK(sizeof(cerberus_mac_row_t)==40);CHECK(sizeof(cerberus_mac_snapshot_t)==1308);
    domains();oracle();boundaries();validation();
    printf("MAC observation: %u checks; collector %zu bytes; exact callback counts, typed LRU and canonical snapshots passed.\n",checks,sizeof(cerberus_mac_observation_t));
    return 0;
}
