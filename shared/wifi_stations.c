#include "wifi_stations.h"
#include <string.h>

bool cerberus_wifi_station_mac_valid(const uint8_t mac[6])
{
    static const uint8_t zero[6]={0};
    return mac&&!(mac[0]&1U)&&memcmp(mac,zero,6);
}
bool cerberus_wifi_station_request_valid(const cerberus_wifi_station_request_t *r)
{
    return r&&r->job_id&&r->channel&&r->seconds>=5&&r->seconds<=60&&cerberus_wifi_station_mac_valid(r->bssid);
}
bool cerberus_wifi_station_decode(const uint8_t *p,size_t n,const uint8_t ap[6],
    cerberus_wifi_station_observation_t *out)
{
    if(!p||!ap||!out||n<24||(p[0]&3U)||!cerberus_wifi_station_mac_valid(ap))return false;
    unsigned type=(p[0]>>2)&3U,subtype=p[0]>>4,ds=p[1]&3U;
    const uint8_t *station=NULL;uint8_t flags=0;size_t header=24;
    if(type==2){
        if(ds==1&&!memcmp(p+4,ap,6)){station=p+10;flags=CERBERUS_STATION_TX;}
        else if(ds==2&&!memcmp(p+10,ap,6)){station=p+4;flags=CERBERUS_STATION_AP_TX;}
        else return false;
        flags|=CERBERUS_STATION_DATA;
        bool qos=(subtype&8U)!=0;
        if(qos)header+=2+(p[1]&0x80U?4U:0U);
        if(n<header)return false;
        static const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
        if(!(p[1]&0x40U)&&(!qos||!(p[24]&0x80U))&&n>=header+sizeof(llc)&&
            !memcmp(p+header,llc,sizeof(llc)))flags|=CERBERUS_STATION_EAPOL;
    }else if(type==0){
        if(ds||memcmp(p+16,ap,6)||subtype==8||subtype==9)return false;
        if(!memcmp(p+4,ap,6)){station=p+10;flags=CERBERUS_STATION_TX;}
        else if(!memcmp(p+10,ap,6)){station=p+4;flags=CERBERUS_STATION_AP_TX;}
        else return false;
        flags|=CERBERUS_STATION_MANAGEMENT;
        if(subtype==11&&!(p[1]&0x40U)&&n>=30&&p[24]==3&&p[25]==0)flags|=CERBERUS_STATION_SAE;
    }else return false;
    if(!cerberus_wifi_station_mac_valid(station)||!memcmp(station,ap,6))return false;
    memcpy(out->mac,station,6);out->flags=flags;return true;
}
static void increment32(uint32_t *n){if(*n!=UINT32_MAX)++*n;}
void cerberus_wifi_station_observe(cerberus_wifi_station_table_t *t,
    const cerberus_wifi_station_observation_t *o,int8_t rssi,uint8_t channel,uint32_t elapsed)
{
    if(!t||t->status.state!=CERBERUS_STATION_RUNNING)return;
    /* Packed fields are copied before incrementing to avoid unaligned pointers. */
    uint32_t count=t->status.observed_frames;increment32(&count);t->status.observed_frames=count;
    if(!o)return;
    count=t->status.matched_frames;increment32(&count);t->status.matched_frames=count;
    unsigned index=0;
    while(index<t->status.count&&memcmp(t->rows[index].mac,o->mac,6))++index;
    if(index==CERBERUS_WIFI_STATION_LIMIT){
        count=t->status.overflow_frames;increment32(&count);t->status.overflow_frames=count;return;
    }
    cerberus_wifi_station_result_t *r=&t->rows[index];
    if(index==t->status.count){
        ++t->status.count;*r=(cerberus_wifi_station_result_t){.job_id=t->status.job_id,.index=(uint8_t)index};
        memcpy(r->mac,o->mac,6);
    }
    r->flags|=o->flags;r->channel=channel;r->last_seen_ms=elapsed>UINT16_MAX?UINT16_MAX:(uint16_t)elapsed;
    /* A downlink's RSSI belongs to the AP. Keep the last station-transmitted
     * signal once available; AP-only rows are labelled separately by the UI. */
    if((o->flags&CERBERUS_STATION_TX)||!r->from_station)r->rssi=rssi;
    count=r->frames;increment32(&count);r->frames=count;
    if((o->flags&CERBERUS_STATION_TX)&&r->from_station!=UINT16_MAX)++r->from_station;
    if((o->flags&CERBERUS_STATION_AP_TX)&&r->to_station!=UINT16_MAX)++r->to_station;
}
bool cerberus_wifi_station_result_valid(const cerberus_wifi_station_result_t *r,uint16_t id,uint8_t index)
{
    return r&&id&&r->job_id==id&&r->index==index&&index<CERBERUS_WIFI_STATION_LIMIT&&
        cerberus_wifi_station_mac_valid(r->mac)&&r->channel&&r->frames&&!(r->flags&~63U)&&
        (r->flags&(CERBERUS_STATION_TX|CERBERUS_STATION_AP_TX))&&
        (r->flags&(CERBERUS_STATION_DATA|CERBERUS_STATION_MANAGEMENT));
}
