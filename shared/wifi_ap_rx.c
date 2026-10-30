#include "wifi_ap_rx.h"
#include "wifi_sae_commit.h"
#include <string.h>
static bool matches(const uint8_t *p,const uint8_t *expected,size_t n)
{
    /* The driver assigns sequence/duration and can set Retry. */
    return p[0]==expected[0]&&(p[1]&~8U)==expected[1]&&!memcmp(p+4,expected+4,18)&&!memcmp(p+24,expected+24,n-24);
}
unsigned cerberus_wifi_ap_rx_classify(const cerberus_wifi_ap_job_t *job,const uint8_t *p,size_t n)
{
    if(!job||!p||n<24||(p[1]&0x45)||(p[22]&0x0f)||memcmp(p+16,job->bssid,6))return 0;
    if(job->mode==CERBERUS_AP_BAD_MESSAGE&&p[0]==8&&p[1]==2&&(n==131||n==153)){
        uint8_t expected[153];uint64_t replay=0;
        for(unsigned i=0;i<8;++i)replay=(replay<<8)|p[41+i];
        size_t length=cerberus_wifi_ap_bad_message(job,n==131,replay,p+49,expected,sizeof(expected));
        return length==n&&matches(p,expected,n)?CERBERUS_AP_RX_MATCH:0;
    }
    if((p[0]&15)||(p[1]&3))return 0;
    if(p[0]==0x80&&n>=36&&!memcmp(p+10,job->bssid,6)){
        unsigned result=CERBERUS_AP_RX_BEACON;
        bool csa=job->mode==CERBERUS_AP_CHANNEL_SWITCH;
        if((csa||job->mode==CERBERUS_AP_QUIET)&&n>=56&&p[36]==0&&p[37]<=32&&n>=56U+p[37]){
            uint8_t expected[160];uint64_t timestamp=0;
            for(unsigned i=0;i<8;++i)timestamp|=(uint64_t)p[24+i]<<(8*i);
            size_t length=cerberus_wifi_ap_announcement(job,p+38,p[37],p+34,csa?p[54+p[37]]:0,
                timestamp,expected,sizeof(expected));
            if(length==n&&matches(p,expected,n))result|=CERBERUS_AP_RX_MATCH;
        }
        return result;
    }
    if(job->mode==CERBERUS_AP_ASSOC_SLEEP&&p[0]==0&&n>=100&&p[28]==0&&p[29]<=32){
        uint8_t expected[132];
        size_t length=cerberus_wifi_ap_association(job,p+30,p[29],expected,sizeof(expected));
        return length==n&&matches(p,expected,n)?CERBERUS_AP_RX_MATCH:0;
    }
    if(job->mode==CERBERUS_AP_SAE_COMMIT){
        if(p[0]!=0xb0||n!=CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES)return 0;
        uint8_t expected[CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES];
        size_t length=cerberus_wifi_sae_commit_frame(job,p+10,p+32,p+64,
            NULL,0,expected,sizeof(expected));
        return length==n&&matches(p,expected,n)?CERBERUS_AP_RX_MATCH:0;
    }
    if(job->mode==CERBERUS_AP_PROBE_REQUEST){
        if(p[0]!=0x40||n<36||p[24]||p[25]>32||n!=36U+p[25]||p[26+p[25]]!=1||p[27+p[25]]!=8||
           (p[10]&3)!=2||memcmp(p+4,job->bssid,6)||!memcmp(p+10,job->bssid,6))return 0;
        return CERBERUS_AP_RX_MATCH;
    }
    if(job->mode!=CERBERUS_AP_DEAUTH)return 0;
    if(p[0]!=0xc0||n!=26||p[24]!=7||p[25])return 0;
    if(job->scope==CERBERUS_AP_BROADCAST){
        static const uint8_t broadcast[6]={255,255,255,255,255,255};
        return !memcmp(p+4,broadcast,6)&&!memcmp(p+10,job->bssid,6)?CERBERUS_AP_RX_MATCH:0;
    }
    if(job->scope!=CERBERUS_AP_ONE_STATION)return 0;
    return ((!memcmp(p+4,job->station,6)&&!memcmp(p+10,job->bssid,6))||
            (!memcmp(p+4,job->bssid,6)&&!memcmp(p+10,job->station,6)))?CERBERUS_AP_RX_MATCH:0;
}
unsigned cerberus_wifi_ap_rx_classify_stations(const cerberus_wifi_ap_job_t *job,
    const cerberus_wifi_ap_station_set_t *stations,const uint8_t *frame,size_t length)
{
    if(!job||!stations||!frame||length<24||job->scope!=CERBERUS_AP_STATION_LIST||
       stations->count<2||stations->count>CERBERUS_WIFI_STATION_LIMIT||
       !cerberus_wifi_ap_job_valid(job))return 0;
    if(frame[0]==0x80)return cerberus_wifi_ap_rx_classify(job,frame,length)&CERBERUS_AP_RX_BEACON;
    /* The AP must occupy one address of every supported client-specific
     * packet. Find the other address in the selected set before performing
     * the more expensive exact packet check. */
    const uint8_t *client=NULL;
    if(!memcmp(frame+10,job->bssid,6))client=frame+4;
    else if(!memcmp(frame+4,job->bssid,6))client=frame+10;
    if(!client)return 0;
    cerberus_wifi_ap_job_t one=*job;one.scope=CERBERUS_AP_ONE_STATION;
    for(unsigned i=0;i<stations->count;++i){
        if(memcmp(client,stations->mac[i],6))continue;
        memcpy(one.station,client,6);
        return cerberus_wifi_ap_rx_classify(&one,frame,length)&CERBERUS_AP_RX_MATCH;
    }
    return 0;
}
