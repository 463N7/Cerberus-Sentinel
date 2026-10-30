#include "wifi_ap_clone.h"
#include <string.h>

/* Layout and 39-byte post-SSID IE sequence derived from ESP32Marauder
 * 511bb3694d0c4dc064888e762bde44fcc9b7f48a,
 * esp32_marauder/WiFiScan.h:657-674 and WiFiScan.cpp:9445-9570.
 * Copyright (c) 2020 Just Call Me Koko; MIT notice in
 * LICENSES/ESP32Marauder.txt. Construction/bounds/parser are Cerberus code. */
static const uint8_t clone_tail[39]={
    0x01,0x08,0x82,0x84,0x8b,0x96,0x24,0x30,0x48,0x6c,
    0x03,0x01,0x00,
    0x30,0x18,0x01,0x00,0x00,0x0f,0xac,0x02,0x02,0x00,
    0x00,0x0f,0xac,0x04,0x00,0x0f,0xac,0x04,0x01,0x00,
    0x00,0x0f,0xac,0x02,0x00,0x00
};
static bool unicast(const uint8_t mac[6])
{return mac&&!(mac[0]&1)&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]);}
static bool target_valid(const cerberus_wifi_ap_job_t *job,
    const uint8_t clone[6],const uint8_t *ssid,size_t ssid_length,
    const uint8_t capabilities[2])
{
    return job&&job->job_id&&job->mode==CERBERUS_WIFI_AP_CLONE_MODE&&
        job->scope==CERBERUS_AP_BROADCAST&&clone&&
        !memcmp(job->station,clone,6)&&
        job->seconds<=20&&cerberus_wifi_ap_channel_valid(job->channel)&&
        unicast(job->bssid)&&unicast(clone)&&(clone[0]&2)&&
        memcmp(clone,job->bssid,6)&&capabilities&&
        ssid_length<=32&&(!ssid_length||ssid);
}
size_t cerberus_wifi_ap_clone_beacon(const cerberus_wifi_ap_job_t *job,
    const uint8_t clone[6],const uint8_t *ssid,size_t ssid_length,
    const uint8_t capabilities[2],uint8_t padded_length,uint64_t timestamp,
    uint8_t *out,size_t capacity)
{
    size_t length=77U+padded_length;
    if(!target_valid(job,clone,ssid,ssid_length,capabilities)||
       padded_length<ssid_length||padded_length>32||
       !out||capacity<length)return 0;
    memset(out,0,length);out[0]=0x80;
    memset(out+4,0xff,6);memcpy(out+10,clone,6);memcpy(out+16,clone,6);
    for(unsigned i=0;i<8;++i)out[24+i]=(uint8_t)(timestamp>>(i*8));
    out[32]=100;memcpy(out+34,capabilities,2);
    out[37]=padded_length;
    if(ssid_length)memcpy(out+38,ssid,ssid_length);
    memset(out+38+ssid_length,' ',padded_length-ssid_length);
    memcpy(out+38+padded_length,clone_tail,sizeof(clone_tail));
    out[50+padded_length]=job->channel;
    return length;
}
bool cerberus_wifi_ap_clone_rx_classify(const cerberus_wifi_ap_job_t *job,
    const uint8_t clone[6],const uint8_t *ssid,size_t ssid_length,
    const uint8_t capabilities[2],uint8_t padded_length,
    const uint8_t *frame,size_t length)
{
    if(!target_valid(job,clone,ssid,ssid_length,capabilities)||
       job->channel>11||!frame||length<77U||
       frame[36]||
       frame[0]!=0x80||(frame[1]&0x47)||(frame[22]&15)||
       memcmp(frame+4,(uint8_t[6]){255,255,255,255,255,255},6)||
       memcmp(frame+10,clone,6)||memcmp(frame+16,clone,6)||
       frame[32]!=100||frame[33]||memcmp(frame+34,capabilities,2)||
       (padded_length!=UINT8_MAX&&frame[37]!=padded_length))return false;
    padded_length=frame[37];
    if(padded_length<ssid_length||padded_length>32||length!=77U+padded_length||
       (ssid_length&&memcmp(frame+38,ssid,ssid_length)))return false;
    for(size_t i=ssid_length;i<padded_length;++i)if(frame[38+i]!=' ')return false;
    const uint8_t *tail=frame+38+padded_length;
    for(unsigned i=0;i<sizeof(clone_tail);++i)
        if(tail[i]!=(i==12?job->channel:clone_tail[i]))return false;
    return true;
}
