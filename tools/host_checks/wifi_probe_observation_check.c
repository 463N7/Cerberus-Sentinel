#undef NDEBUG
#include "wifi_probe_observation.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t frame[2400];
static const uint8_t ordinary[6]={0x02,3,4,5,6,7};
static const uint8_t prefixes[][3]={
    {0x58,0x8e,0x81},{0xcc,0xcc,0xcc},{0xec,0x1b,0xbd},{0x90,0x35,0xea},
    {0x04,0x0d,0x84},{0xf0,0x82,0xc0},{0x1c,0x34,0xf1},{0x38,0x5b,0x44},
    {0x94,0x34,0x69},{0xb4,0xe3,0xf9},{0x70,0xc9,0x4e},{0x3c,0x91,0x80},
    {0xd8,0xf3,0xbc},{0x80,0x30,0x49},{0x14,0x5a,0xfc},{0x74,0x4c,0xa1},
    {0x08,0x3a,0x88},{0x9c,0x2f,0x9d},{0x94,0x08,0x53},{0xe4,0xaa,0xea},
    {0xf4,0x6a,0xdd},{0xf8,0xa2,0xd6},{0xe0,0x0a,0xf6},{0x00,0xf4,0x8d},
    {0xd0,0x39,0x57},{0xe8,0xd0,0xfc},{0xb4,0x1e,0x52}};
static bool zero(const void *p,size_t n)
{const uint8_t *b=p;while(n--)if(*b++)return false;return true;}
static size_t packet(const uint8_t *ssid,size_t n)
{
    memset(frame,0,sizeof(frame));frame[0]=0x40;
    memset(frame+4,255,6);memcpy(frame+10,ordinary,6);memset(frame+16,255,6);
    frame[24]=0;frame[25]=(uint8_t)n;if(n)memcpy(frame+26,ssid,n);return 26+n;
}
static cerberus_wifi_probe_row_t decode(size_t n,uint8_t ch,int rssi,uint64_t us,bool wanted)
{
    cerberus_wifi_probe_row_t r;memset(&r,0xa5,sizeof(r));
    CHECK(cerberus_wifi_probe_decode(frame,n,ch,rssi,us,&r)==wanted);
    if(wanted)CHECK(cerberus_wifi_probe_row_valid(&r));else CHECK(zero(&r,sizeof(r)));
    return r;
}
static void parser(void)
{
    static const char *const names[]={"flock","penguin","pigvision","fs ext battery"};
    for(unsigned kind=0;kind<4;++kind){
        size_t length=strlen(names[kind]);
        /* Independently generate every ASCII case variant, with bounded prefix/suffix. */
        for(unsigned mask=0;mask<(1U<<length);++mask){
            uint8_t name[32];name[0]='x';name[1]='-';
            for(size_t i=0;i<length;++i){uint8_t c=(uint8_t)names[kind][i];
                name[i+2]=(mask&(1U<<i))&&c>='a'&&c<='z'?(uint8_t)(c-32):c;}
            name[length+2]='!';size_t n=packet(name,length+3);
            cerberus_wifi_probe_row_t r=decode(n,149,-101,0,true);
            CHECK(r.name_bits==(1U<<kind)&&!r.oui_hint&&r.callback_ms==0&&r.frames==1);
            CHECK(r.ssid_length==length+3&&!memcmp(r.ssid,name,length+3));
        }
    }
    size_t n=packet((const uint8_t *)"FLOCKpenguinPIGVISION",21);
    CHECK(decode(n,6,-1,1999,true).name_bits==7);
    const uint8_t binary[]={0,0xff,'F','l','o','c','k',0};
    n=packet(binary,sizeof(binary));CHECK(decode(n,6,-99,2000,true).ssid_length==8);
    n=packet((const uint8_t *)"Flock",5);
    for(size_t i=0;i<n;++i)decode(i,6,-99,1,false);
    for(unsigned fc=0;fc<256;++fc){frame[0]=(uint8_t)fc;decode(n,6,-99,0,fc==0x40);}
    frame[0]=0x40;
    for(unsigned fc=0;fc<256;++fc){frame[1]=(uint8_t)fc;decode(n,6,-99,0,!(fc&0xc7));}
    frame[1]=0;
    for(unsigned frag=0;frag<256;++frag){frame[22]=(uint8_t)frag;decode(n,6,-99,0,!(frag&15));}
    frame[22]=0;
    for(unsigned ch=0;ch<256;++ch){bool valid=(ch>=1&&ch<=14)||(ch>=36&&ch<=64&&ch%4==0)||
        (ch>=100&&ch<=144&&ch%4==0)||(ch>=149&&ch<=177&&(ch-149)%4==0);
        decode(n,(uint8_t)ch,-99,0,valid);}
    for(int rssi=-256;rssi<=256;++rssi)decode(n,6,rssi,0,rssi>=-127&&rssi<=0);
    for(unsigned first=0;first<256;++first){frame[10]=(uint8_t)first;decode(n,6,-99,0,!(first&1));}
    memset(frame+10,0,6);decode(n,6,-99,0,false);memcpy(frame+10,ordinary,6);
    /* Full outer IE stream is atomic, including unknown IE bodies. */
    frame[n]=221;frame[n+1]=255;memset(frame+n+2,0x46,255);
    for(size_t i=1;i<257;++i)decode(n+i,6,-99,0,false);
    decode(n+257,6,-99,UINT64_MAX,true);frame[n+257]=17;decode(n+258,6,-99,0,false);
    n=packet((const uint8_t *)"Flock",5);frame[n]=0;frame[n+1]=0;decode(n+2,6,-99,0,false);
    frame[n]=1;frame[n+1]=0;decode(n+2,6,-99,0,true);
    frame[24]=1;decode(n,6,-99,0,false);
    uint8_t maxname[33];memset(maxname,'x',sizeof(maxname));memcpy(maxname+27,"flock",5);
    n=packet(maxname,32);decode(n,6,-99,0,true);n=packet(maxname,33);decode(n,6,-99,0,false);
    n=packet(NULL,0);decode(n,6,-99,0,false);
    for(unsigned i=0;i<27;++i)for(unsigned suffix=0;suffix<256;suffix+=17){
        memcpy(frame+10,prefixes[i],3);frame[13]=(uint8_t)suffix;frame[14]=0;frame[15]=255;
        cerberus_wifi_probe_row_t r=decode(n,6,-99,0,true);CHECK(!r.name_bits&&r.oui_hint==1);
        frame[10]|=2;decode(n,6,-99,0,false);memcpy(frame+10,prefixes[i],3);
        frame[10]|=1;decode(n,6,-99,0,false);
    }
    n=packet((const uint8_t *)"ordinary",8);frame[n]=221;frame[n+1]=5;memcpy(frame+n+2,"flock",5);
    decode(n+7,6,-99,0,false); /* payload strings are not SSID evidence */
    cerberus_wifi_probe_row_t out;memset(&out,0xa5,sizeof(out));
    CHECK(!cerberus_wifi_probe_decode(NULL,n,6,-99,0,&out)&&zero(&out,sizeof(out)));
    CHECK(!cerberus_wifi_probe_decode(frame,n,6,-99,0,NULL));
    n=packet((const uint8_t *)"Flock",5);out=decode(n,6,-99,UINT64_MAX,true);
    out.callback_ms=UINT64_MAX/1000U+1;CHECK(!cerberus_wifi_probe_row_valid(&out));
    out=decode(n,6,-99,0,true);
    for(unsigned i=0;i<32;++i){cerberus_wifi_probe_row_t bad=out;
        if(i>=5){bad.ssid[i]=1;CHECK(!cerberus_wifi_probe_row_valid(&bad));}}
    out.name_bits=2;CHECK(!cerberus_wifi_probe_row_valid(&out));out.name_bits=1;
    out.oui_hint=1;CHECK(!cerberus_wifi_probe_row_valid(&out));out.oui_hint=0;
    out.frames=0;CHECK(!cerberus_wifi_probe_row_valid(&out));
}
static void collector(void)
{
    cerberus_wifi_probe_table_t t={0};cerberus_wifi_probe_begin(&t,255);
    CHECK(t.status.epoch==1&&t.status.generation==255&&cerberus_wifi_probe_status_valid(&t.status));
    size_t n=packet((const uint8_t *)"Flock",5);cerberus_wifi_probe_row_t r=decode(n,6,-20,1001,true);
    CHECK(!cerberus_wifi_probe_commit(&t,1,&r,1001));cerberus_wifi_probe_arm(&t,true,0);
    CHECK(cerberus_wifi_probe_commit(&t,1,&r,1001));
    r.rssi=-100;r.callback_ms=1;CHECK(cerberus_wifi_probe_commit(&t,1,&r,1999));
    CHECK(t.rows[0].rssi==-100&&t.rows[0].frames==2&&t.callback_us[0]==1999);
    CHECK(!cerberus_wifi_probe_commit(&t,1,&r,1998));CHECK(t.status.matched_frames==2);
    r.channel=149;CHECK(cerberus_wifi_probe_commit(&t,1,&r,1999));CHECK(t.status.count==2);
    r.ssid[0]='f';CHECK(cerberus_wifi_probe_commit(&t,1,&r,1999));CHECK(t.status.count==3);
    r.ssid[0]='F';
    for(unsigned i=3;i<16;++i){r.transmitter[5]=(uint8_t)(i+20);CHECK(cerberus_wifi_probe_commit(&t,1,&r,1999));}
    CHECK(t.status.count==16&&t.status.matched_frames==17);
    r.transmitter[5]=99;CHECK(!cerberus_wifi_probe_commit(&t,1,&r,1999));
    CHECK(t.status.matched_frames==18&&t.status.dropped_frames==1&&t.status.flags==3);
    r=t.rows[0];r.rssi=-127;r.callback_ms=9;CHECK(cerberus_wifi_probe_commit(&t,1,&r,9999));
    CHECK(t.rows[0].frames==3&&t.rows[0].rssi==-127&&t.status.count==16);
    CHECK(cerberus_wifi_probe_status_valid(&t.status));
    cerberus_wifi_probe_table_t full=t;full.status.matched_frames=UINT32_MAX;
    full.status.dropped_frames=UINT32_MAX;r.transmitter[5]=199;
    CHECK(!cerberus_wifi_probe_commit(&full,1,&r,9999));
    CHECK(full.status.matched_frames==UINT32_MAX&&full.status.dropped_frames==UINT32_MAX&&full.status.count==16);
    cerberus_wifi_probe_query_t q={.epoch=1,.generation=255};cerberus_wifi_probe_chunk_t out;
    CHECK(!cerberus_wifi_probe_get_chunk(&t,&q,&out));cerberus_wifi_probe_finish(&t,2);
    uint8_t bytes[55];for(unsigned i=0;i<6;++i){q.chunk=(uint8_t)i;
        CHECK(cerberus_wifi_probe_get_chunk(&t,&q,&out));CHECK(out.epoch==1&&out.generation==255&&out.index==0&&out.chunk==i);
        CHECK(out.length==(i==5?5:10));memcpy(bytes+i*10,out.data,out.length);
        for(unsigned j=out.length;j<10;++j)CHECK(out.data[j]==0);}
    CHECK(!memcmp(bytes,&t.rows[0],55));
    CHECK(!cerberus_wifi_probe_commit(&t,1,&r,9999));
    for(unsigned fault=0;fault<5;++fault){q=(cerberus_wifi_probe_query_t){.epoch=1,.generation=255};
        if(fault==0)q.epoch=0;if(fault==1)q.epoch=2;if(fault==2)q.generation=0;if(fault==3)q.index=16;if(fault==4)q.chunk=6;
        memset(&out,0xa5,sizeof(out));CHECK(!cerberus_wifi_probe_get_chunk(&t,&q,&out)&&zero(&out,sizeof(out)));}
    cerberus_wifi_probe_begin(&t,0);cerberus_wifi_probe_arm(&t,true,0);
    CHECK(t.status.epoch==2&&!t.status.count&&t.status.generation==0);
    CHECK(!cerberus_wifi_probe_commit(&t,1,&r,9999));
    cerberus_wifi_probe_arm(&t,true,50);r.callback_ms=0;
    CHECK(!cerberus_wifi_probe_commit(&t,2,&r,49));CHECK(!t.status.count);
    CHECK(cerberus_wifi_probe_commit(&t,2,&r,50));CHECK(t.callback_us[0]==50);
    cerberus_wifi_probe_begin(&t,0);cerberus_wifi_probe_arm(&t,true,0);
    uint64_t current_epoch=t.status.epoch;
    r.callback_ms=0;CHECK(cerberus_wifi_probe_commit(&t,current_epoch,&r,0));
    CHECK(t.rows[0].callback_ms==0);t.rows[0].frames=UINT32_MAX;t.status.matched_frames=UINT32_MAX;
    CHECK(cerberus_wifi_probe_commit(&t,current_epoch,&r,0));CHECK(t.rows[0].frames==UINT32_MAX&&t.status.matched_frames==UINT32_MAX);
    for(unsigned state=0;state<4;++state)if(state!=2){cerberus_wifi_probe_finish(&t,(uint8_t)state);
        CHECK(!t.accepting&&zero(t.rows,sizeof(t.rows))&&zero(t.callback_us,sizeof(t.callback_us)));
        CHECK(cerberus_wifi_probe_status_valid(&t.status));}
    t.epoch_counter=UINT64_MAX-1;cerberus_wifi_probe_begin(&t,1);cerberus_wifi_probe_arm(&t,true,0);
    CHECK(t.status.epoch==UINT64_MAX&&t.accepting);cerberus_wifi_probe_begin(&t,2);cerberus_wifi_probe_arm(&t,true,0);
    CHECK(!t.status.epoch&&!t.accepting&&cerberus_wifi_probe_status_valid(&t.status));
    cerberus_wifi_probe_finish(&t,2);CHECK(cerberus_wifi_probe_status_valid(&t.status));
    for(unsigned i=0;i<256;++i){cerberus_wifi_probe_begin(&t,(uint8_t)i);CHECK(!t.status.epoch&&!t.accepting);}
    /* Canonical status combinations are tested independently from row parsing. */
    for(unsigned state=0;state<256;++state)for(unsigned flags=0;flags<256;++flags){
        cerberus_wifi_probe_status_t s={.epoch=3,.state=(uint8_t)state,.flags=(uint8_t)flags};
        bool valid=state<4&&(flags==0||(flags==1&&(state==1||state==2)));
        CHECK(cerberus_wifi_probe_status_valid(&s)==valid);
    }
    for(unsigned count=0;count<=17;++count){cerberus_wifi_probe_status_t s={.epoch=3,.state=2,.flags=3,
        .count=(uint8_t)count,.matched_frames=20,.dropped_frames=1};
        CHECK(cerberus_wifi_probe_status_valid(&s)==(count==16));}
}
int main(void)
{parser();collector();printf("Wi-Fi probe parser/collector: %u checks; table=%zuB,row=%zuB,status=%zuB.\n",checks,
 sizeof(cerberus_wifi_probe_table_t),sizeof(cerberus_wifi_probe_row_t),sizeof(cerberus_wifi_probe_status_t));return 0;}
