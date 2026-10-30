#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cerberus_protocol.h"
static const uint8_t ap[]={2,0,0,0,0,1},sta[]={2,0,0,0,0,2},other[]={2,0,0,0,0,3};
static void frame(uint8_t *p,uint8_t control,uint8_t flags)
{
    memset(p,0,64);p[0]=control;p[1]=flags;
    memcpy(p+4,flags==2?sta:ap,6);memcpy(p+10,flags==2?ap:sta,6);memcpy(p+16,ap,6);
}
int main(void)
{
    cerberus_wifi_station_request_t request={.job_id=1,.bssid={2,0,0,0,0,1},.channel=36,.seconds=15};
    assert(cerberus_wifi_station_request_valid(&request));request.bssid[0]=3;assert(!cerberus_wifi_station_request_valid(&request));
    request.bssid[0]=2;request.seconds=61;assert(!cerberus_wifi_station_request_valid(&request));
    uint8_t raw[65],*p=raw+1;cerberus_wifi_station_observation_t o;
    frame(p,0x08,1);assert(cerberus_wifi_station_decode(p,24,ap,&o)&&!memcmp(o.mac,sta,6)&&o.flags==(CERBERUS_STATION_TX|CERBERUS_STATION_DATA));
    for(unsigned n=0;n<24;++n)assert(!cerberus_wifi_station_decode(p,n,ap,&o));
    assert(!cerberus_wifi_station_decode(p,24,other,&o));
    frame(p,0x08,2);assert(cerberus_wifi_station_decode(p,24,ap,&o)&&o.flags==(CERBERUS_STATION_AP_TX|CERBERUS_STATION_DATA));
    p[4]=0xff;assert(!cerberus_wifi_station_decode(p,24,ap,&o));
    frame(p,0x08,0);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    frame(p,0x08,3);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    frame(p,0x88,1);assert(!cerberus_wifi_station_decode(p,25,ap,&o));
    memcpy(p+26,"\xaa\xaa\x03\x00\x00\x00\x88\x8e",8);
    assert(cerberus_wifi_station_decode(p,34,ap,&o)&&(o.flags&CERBERUS_STATION_EAPOL));
    p[24]=0x80;assert(cerberus_wifi_station_decode(p,34,ap,&o)&&!(o.flags&CERBERUS_STATION_EAPOL));
    p[24]=0;p[1]|=0x40;assert(cerberus_wifi_station_decode(p,34,ap,&o)&&!(o.flags&CERBERUS_STATION_EAPOL));
    p[1]=0x81;assert(!cerberus_wifi_station_decode(p,29,ap,&o));
    memcpy(p+30,"\xaa\xaa\x03\x00\x00\x00\x88\x8e",8);
    assert(cerberus_wifi_station_decode(p,38,ap,&o)&&(o.flags&CERBERUS_STATION_EAPOL));
    frame(p,0xb0,0);p[24]=3;assert(cerberus_wifi_station_decode(p,30,ap,&o)&&(o.flags&CERBERUS_STATION_SAE));
    p[1]=0x40;assert(cerberus_wifi_station_decode(p,30,ap,&o)&&!(o.flags&CERBERUS_STATION_SAE));
    frame(p,0x00,0);assert(cerberus_wifi_station_decode(p,28,ap,&o)&&(o.flags&CERBERUS_STATION_MANAGEMENT));
    frame(p,0x80,0);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    frame(p,0x04,0);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    frame(p,0x09,1);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    frame(p,0x08,1);memcpy(p+10,ap,6);assert(!cerberus_wifi_station_decode(p,64,ap,&o));
    cerberus_wifi_station_table_t table={.status={.job_id=42,.state=CERBERUS_STATION_RUNNING}};
    frame(p,0x08,2);assert(cerberus_wifi_station_decode(p,24,ap,&o));
    cerberus_wifi_station_observe(&table,&o,-30,36,100);assert(table.status.count==1&&table.rows[0].rssi==-30&&table.rows[0].to_station==1);
    frame(p,0x08,1);assert(cerberus_wifi_station_decode(p,24,ap,&o));
    cerberus_wifi_station_observe(&table,&o,-70,36,200);
    o.flags=CERBERUS_STATION_AP_TX|CERBERUS_STATION_DATA;cerberus_wifi_station_observe(&table,&o,-20,36,300);
    assert(table.rows[0].rssi==-70&&table.rows[0].from_station==1&&table.rows[0].to_station==2&&table.rows[0].frames==3);
    for(unsigned i=1;i<32;++i){o.mac[5]=(uint8_t)(i+3);cerberus_wifi_station_observe(&table,&o,-60,36,i+300);}
    assert(table.status.count==32);o.mac[5]=99;cerberus_wifi_station_observe(&table,&o,-60,36,400);
    assert(table.status.overflow_frames==1);memcpy(o.mac,sta,6);cerberus_wifi_station_observe(&table,&o,-20,36,450);
    assert(table.rows[0].frames==4&&table.status.count==32&&table.status.overflow_frames==1);
    cerberus_wifi_station_observe(&table,NULL,-20,36,500);assert(table.status.observed_frames==table.status.matched_frames+1);
    assert(cerberus_wifi_station_result_valid(&table.rows[0],42,0));assert(!cerberus_wifi_station_result_valid(&table.rows[0],43,0));
    table.rows[0].frames=UINT32_MAX;table.rows[0].to_station=UINT16_MAX;table.status.observed_frames=table.status.matched_frames=UINT32_MAX;
    cerberus_wifi_station_observe(&table,&o,-20,36,550);assert(table.rows[0].frames==UINT32_MAX&&table.rows[0].to_station==UINT16_MAX&&table.status.matched_frames==UINT32_MAX);
    table.status.state=CERBERUS_STATION_CANCELLED;cerberus_wifi_station_observe(&table,&o,-20,36,600);assert(table.rows[0].last_seen_ms==550);
    puts("Station checks passed: address direction, AP scoping, malformed/QoS/protected frames, EAPOL/SAE flags, RSSI provenance, overflow and saturation.");
}
