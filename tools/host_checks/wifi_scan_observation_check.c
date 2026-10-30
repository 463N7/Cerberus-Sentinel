#undef NDEBUG
#include "wifi_scan_observation.h"
#include <assert.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static uint8_t packet[2400];
static size_t make_packet(bool probe, const uint8_t *ssid, uint8_t length)
{
    memset(packet, 0, sizeof(packet));
    packet[0] = probe ? 0x50U : 0x80U;
    memset(packet+4, 255, 6);
    const uint8_t identity[6] = {2, 7, 8, 9, 10, 11};
    memcpy(packet+10, identity, 6); memcpy(packet+16, identity, 6);
    packet[32] = 100; packet[34] = 1; /* ordinary fixed beacon fields */
    packet[36] = 0; packet[37] = length;
    if (length) memcpy(packet+38, ssid, length);
    return 38U+length;
}
static void reject(size_t length, uint8_t channel)
{
    cerberus_wifi_observation_frame_t out;
    memset(&out, 0xa5, sizeof(out));
    CHECK(!cerberus_wifi_observation_decode(packet, length, channel, &out));
    const cerberus_wifi_observation_frame_t zero = {0};
    CHECK(memcmp(&out, &zero, sizeof(out)) == 0);
}
static void frames(void)
{
    const uint8_t name[] = {'t', 0, 'e', 's', 't'};
    cerberus_wifi_observation_frame_t out;
    for (unsigned probe=0; probe<2; ++probe) {
        size_t size=make_packet(probe, name, sizeof(name));
        CHECK(cerberus_wifi_observation_decode(packet,size,36,&out));
        CHECK(out.key.ssid_length == 5 && memcmp(out.key.ssid,name,5) == 0);
        CHECK(out.key.channel == 36 && out.flags == (probe ? 5 : 3));
        CHECK(memcmp(out.key.bssid,packet+16,6) == 0);
        for (size_t cut=0; cut<size; ++cut) reject(cut,36);
        packet[size++]=3; packet[size++]=1; packet[size++]=1;
        CHECK(cerberus_wifi_observation_decode(packet,size,36,&out));
        CHECK(out.key.channel == 36); /* advertised channel never replaces RX */
        packet[size++]=221; packet[size++]=4;
        memset(packet+size,0x42,4); size+=4;
        CHECK(cerberus_wifi_observation_decode(packet,size,36,&out));
        reject(size-1,36); packet[size++]=1; reject(size,36); /* malformed suffix */
    }
    size_t size=make_packet(false,name,5);
    for (unsigned channel=0; channel<256; ++channel) {
        bool expected=(channel>=1 && channel<=14) || (channel>=36 && channel<=64 && channel%4==0) ||
            (channel>=100 && channel<=144 && channel%4==0) ||
            (channel>=149 && channel<=177 && (channel-149)%4==0);
        CHECK(cerberus_wifi_observation_decode(packet,size,(uint8_t)channel,&out) == expected);
    }
    const uint8_t bad_fc[] = {0,0x40,0x81,0x84,0x88,0x90,0x08};
    for (unsigned i=0;i<sizeof(bad_fc);++i) { packet[0]=bad_fc[i]; reject(size,1); }
    packet[0]=0x80;
    for (unsigned bit=0;bit<8;++bit) {
        packet[1]=(uint8_t)(1U<<bit);
        if (packet[1]&0xc7U) reject(size,1);
        else CHECK(cerberus_wifi_observation_decode(packet,size,1,&out));
    }
    packet[1]=0; packet[22]=1; reject(size,1); packet[22]=0x10;
    CHECK(cerberus_wifi_observation_decode(packet,size,1,&out));
    packet[4]=2; reject(size,1); packet[0]=0x50;
    CHECK(cerberus_wifi_observation_decode(packet,size,1,&out));
    packet[4]=3; reject(size,1); memset(packet+4,0,6); reject(size,1);
    for(unsigned address=10;address<=16;address+=6) {
        size=make_packet(false,name,5); packet[address]|=1; reject(size,1);
        memset(packet+address,0,6); reject(size,1);
    }
    size=make_packet(false,name,5); packet[size++]=0; packet[size++]=0; reject(size,1);
    size=make_packet(false,name,5); packet[size++]=3; packet[size++]=0; reject(size,1);
    packet[size-1]=1; packet[size++]=0; reject(size,1);
    packet[size-1]=6;
    CHECK(cerberus_wifi_observation_decode(packet,size,1,&out));
    packet[size++]=3; packet[size++]=1; packet[size++]=6; reject(size,1);
    uint8_t full_name[33]; memset(full_name,0xff,sizeof(full_name));
    size=make_packet(false,full_name,32);
    CHECK(cerberus_wifi_observation_decode(packet,size,165,&out));
    CHECK(out.key.ssid_length==32 && memcmp(out.key.ssid,full_name,32)==0);
    size=make_packet(false,full_name,33); reject(size,1);
    size=make_packet(false,NULL,0);
    CHECK(cerberus_wifi_observation_decode(packet,size,1,&out) && out.key.ssid_length==0);
    packet[36]=1; reject(size,1); /* no SSID */
    size=make_packet(false,NULL,0);
    while (size+257<=2304) { packet[size]=221; packet[size+1]=255; size+=257; }
    size_t remainder=2304-size;
    packet[size]=221; packet[size+1]=(uint8_t)(remainder-2); size+=remainder;
    CHECK(cerberus_wifi_observation_decode(packet,size,1,&out));
    reject(2305,1);
    memset(&out,0xa5,sizeof(out)); CHECK(!cerberus_wifi_observation_decode(NULL,38,1,&out));
    const cerberus_wifi_observation_frame_t zero={0}; CHECK(memcmp(&out,&zero,sizeof(out))==0);
    CHECK(!cerberus_wifi_observation_decode(packet,size,1,NULL));
}
static bool result(cerberus_wifi_observation_table_t *table,
    const cerberus_wifi_observation_frame_t *frame, uint64_t now, cerberus_wifi_scan_result_payload_t *out)
{
    memset(out,0xa5,sizeof(*out));
    bool matched=cerberus_wifi_observation_result(table,frame->key.bssid,frame->key.ssid,
        frame->key.ssid_length,frame->key.channel,now,out);
    for (unsigned i=0;i<12;++i) CHECK(((const uint8_t *)out)[i]==0xa5); /* prior wire fields */
    if (!matched) CHECK(out->observation_age_ms==0 && out->observation_source_ms==0 &&
        out->observation_flags==0 && out->observation_channel==0);
    return matched;
}
static void table_cases(void)
{
    cerberus_wifi_observation_table_t table={0};
    cerberus_wifi_observation_frame_t frame;
    cerberus_wifi_scan_result_payload_t out;
    size_t size=make_packet(false,(const uint8_t *)"test",4);
    CHECK(cerberus_wifi_observation_decode(packet,size,6,&frame));
    uint64_t epoch=cerberus_wifi_observation_begin(&table); CHECK(epoch==1);
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,0,-70));
    CHECK(!cerberus_wifi_observation_arm(&table,0));
    CHECK(cerberus_wifi_observation_arm(&table,epoch));
    CHECK(!cerberus_wifi_observation_arm(&table,epoch));
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,0,-70)); /* genuine uptime0 */
    CHECK(!result(&table,&frame,0,&out));
    cerberus_wifi_observation_finish(&table,true);
    for(unsigned us=0;us<=2001;++us) {
        CHECK(result(&table,&frame,us,&out));
        CHECK(out.observation_age_ms==(us+999)/1000 && out.observation_flags==3 && out.observation_channel==6);
    }
    CHECK(result(&table,&frame,UINT64_MAX,&out) && out.observation_age_ms==UINT32_MAX);
    uint64_t old=epoch; epoch=cerberus_wifi_observation_begin(&table);
    CHECK(cerberus_wifi_observation_arm(&table,epoch));
    CHECK(!cerberus_wifi_observation_commit(&table,old,&frame,100,-70));
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,UINT64_C(4294967300)*1000,-70));
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,1,-70) && table.stale_updates==1);
    frame.flags=5; CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,UINT64_C(4294967300)*1000,-70));
    cerberus_wifi_observation_finish(&table,true);
    CHECK(!result(&table,&frame,1,&out));
    CHECK(result(&table,&frame,UINT64_C(4294967300)*1000+1,&out));
    CHECK(out.observation_source_ms==4 && out.observation_age_ms==1 && out.observation_flags==5);
    frame.key.channel=11; CHECK(!result(&table,&frame,UINT64_MAX,&out)); frame.key.channel=6;
    frame.key.ssid[1]^=1; CHECK(!result(&table,&frame,UINT64_MAX,&out)); frame.key.ssid[1]^=1;
    frame.key.ssid_length=3; CHECK(!result(&table,&frame,UINT64_MAX,&out)); frame.key.ssid_length=4;
    frame.key.bssid[5]^=1; CHECK(!result(&table,&frame,UINT64_MAX,&out)); frame.key.bssid[5]^=1;
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,UINT64_MAX,-70));
    cerberus_wifi_observation_finish(&table,false); CHECK(!result(&table,&frame,UINT64_MAX,&out));
    epoch=cerberus_wifi_observation_begin(&table); CHECK(cerberus_wifi_observation_arm(&table,epoch));
    for(unsigned i=0;i<64;++i) { frame.key.bssid[5]=(uint8_t)i; CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,i,-70)); }
    frame.key.bssid[5]=64; CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,100,-70));
    CHECK(table.count==64 && table.capacity_drops==1);
    frame.key.bssid[5]=0; CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,100,-70));
    CHECK(table.entries[0].observed_us==100 && table.count==64);
    table.capacity_drops=UINT32_MAX; frame.key.bssid[5]=65;
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,100,-70) && table.capacity_drops==UINT32_MAX);
    table.count=65; CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,100,-70));
    cerberus_wifi_observation_finish(&table,true); CHECK(!result(&table,&frame,UINT64_MAX,&out));
    table.epoch=UINT64_MAX; CHECK(cerberus_wifi_observation_begin(&table)==0);
    CHECK(!cerberus_wifi_observation_arm(&table,UINT64_MAX) && table.count==0);
    table.epoch=0;
    for(unsigned i=0;i<513;++i) {
        uint64_t next=cerberus_wifi_observation_begin(&table); CHECK(next==(uint64_t)i+1);
        CHECK(cerberus_wifi_observation_arm(&table,next));
        CHECK(!cerberus_wifi_observation_commit(&table,old,&frame,100,-70) || old==next);
    }
    CHECK(sizeof(cerberus_wifi_scan_result_payload_t)==22);
    CHECK(offsetof(cerberus_wifi_scan_result_payload_t,observation_age_ms)==12);
}
static void rank_cases(void)
{
    cerberus_wifi_observation_table_t table={0};
    cerberus_wifi_observation_frame_t frame;
    cerberus_wifi_scan_result_payload_t out;
    size_t bytes=make_packet(false,(const uint8_t *)"dense",5);
    CHECK(cerberus_wifi_observation_decode(packet,bytes,1,&frame));
    uint64_t epoch=cerberus_wifi_observation_begin(&table);
    CHECK(cerberus_wifi_observation_arm(&table,epoch));
    for(unsigned i=0;i<64;++i) {
        frame.key.bssid[5]=(uint8_t)i;
        CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,10+i,-80));
    }
    /* Later strong5GHz evidence displaces a weak first-band slot. */
    frame.key.channel=177;frame.key.bssid[5]=80;
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,100,-35));
    CHECK(table.count==64 && table.capacity_replacements==1 && table.capacity_drops==0);
    CHECK(table.entries[0].key.channel==177 && table.entries[0].key.bssid[5]==80);
    cerberus_wifi_observation_frame_t five=frame;
    frame.key.channel=1;frame.key.bssid[5]=0;
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,101,-80));
    CHECK(table.capacity_drops==1); /* ties don't churn retained slots */
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,102,-100));
    CHECK(table.capacity_drops==2);
    frame=five;frame.flags=5;
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,200,-90));
    CHECK(table.entries[0].observed_us==200 && table.entries[0].strongest_rssi==-35 && table.entries[0].flags==5);
    CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,199,-10));
    CHECK(table.entries[0].strongest_rssi==-35); /* stale time cannot improve retention */
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,201,-20));
    CHECK(table.entries[0].strongest_rssi==-20);
    const int invalid[]={-128,127,21,INT_MIN,INT_MAX};
    frame.key.bssid[5]=81;
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i)
        CHECK(!cerberus_wifi_observation_commit(&table,epoch,&frame,202,invalid[i]));
    table.capacity_replacements=UINT32_MAX;
    CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,203,-10));
    CHECK(table.capacity_replacements==UINT32_MAX && table.entries[1].key.bssid[5]==81);
    cerberus_wifi_observation_finish(&table,true);
    CHECK(result(&table,&five,1201,&out));
    CHECK(out.observation_age_ms==1 && out.observation_flags==5 && out.observation_channel==177);
    frame.key.channel=1;frame.key.bssid[5]=0;
    CHECK(!result(&table,&frame,1201,&out)); /* eviction never fabricates a time */
    epoch=cerberus_wifi_observation_begin(&table);
    CHECK(cerberus_wifi_observation_arm(&table,epoch));
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        frame.key.bssid[5]=(uint8_t)i;
        CHECK(cerberus_wifi_observation_commit(&table,epoch,&frame,1000,invalid[i]));
        CHECK(table.entries[i].strongest_rssi==INT8_MIN);
    }
    cerberus_wifi_observation_finish(&table,true);
    CHECK(result(&table,&frame,1000,&out)); /* invalid RSSI doesn't erase valid frame presence */
    CHECK(out.observation_age_ms==0);
}
int main(void)
{
    frames(); table_cases(); rank_cases();
    printf("Wi-Fi observation parser/table: %u checks, %zu-byte bounded table; exact tuple/atomic parser/age/epochs/capacity passed.\n",checks,sizeof(cerberus_wifi_observation_table_t));
    return 0;
}
