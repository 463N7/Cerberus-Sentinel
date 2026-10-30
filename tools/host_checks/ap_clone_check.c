#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_ap_clone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t ap[6]={0x10,0x22,0x33,0x44,0x55,0x66};
static const uint8_t clone[6]={0x02,0xaa,0xbb,0xcc,0xdd,0xee};
static const uint8_t caps[2]={0x31,0x04};
static cerberus_wifi_ap_job_t job(void)
{
    cerberus_wifi_ap_job_t r={.job_id=27,.generation=3,.index=2,.channel=6,
        .seconds=10,.scope=CERBERUS_AP_BROADCAST,.mode=CERBERUS_WIFI_AP_CLONE_MODE};
    memcpy(r.bssid,ap,6);memcpy(r.station,clone,6);return r;
}
static void check_frame(const cerberus_wifi_ap_job_t *r,const uint8_t *ssid,
    unsigned name_len,unsigned padded_len)
{
    uint8_t guarded[112];memset(guarded,0xa5,sizeof(guarded));uint8_t *p=guarded+1;
    size_t n=cerberus_wifi_ap_clone_beacon(r,clone,ssid,name_len,caps,padded_len,
        UINT64_C(0x0102030405060708),p,sizeof(guarded)-2);
    assert(n==77U+padded_len&&guarded[0]==0xa5&&p[n]==0xa5);
    assert(p[0]==0x80&&p[1]==0&&p[22]==0&&p[23]==0);
    for(unsigned i=0;i<6;++i)assert(p[4+i]==255);
    assert(!memcmp(p+10,clone,6)&&!memcmp(p+16,clone,6));
    for(unsigned i=0;i<8;++i)assert(p[24+i]==8-i);
    assert(p[32]==100&&p[33]==0&&!memcmp(p+34,caps,2));
    assert(p[36]==0&&p[37]==padded_len);
    if(name_len)assert(!memcmp(p+38,ssid,name_len));
    for(unsigned i=name_len;i<padded_len;++i)assert(p[38+i]==' ');
    assert(p[38+padded_len]==1&&p[39+padded_len]==8);
    assert(p[48+padded_len]==3&&p[49+padded_len]==1&&p[50+padded_len]==r->channel);
    assert(p[51+padded_len]==0x30&&p[52+padded_len]==0x18);
    if(r->channel<=11){
        assert(cerberus_wifi_ap_clone_rx_classify(r,clone,ssid,name_len,caps,padded_len,p,n));
        assert(cerberus_wifi_ap_clone_rx_classify(r,clone,ssid,name_len,caps,UINT8_MAX,p,n));
    }else assert(!cerberus_wifi_ap_clone_rx_classify(r,clone,ssid,name_len,caps,UINT8_MAX,p,n));
    assert(!cerberus_wifi_ap_clone_beacon(r,clone,ssid,name_len,caps,padded_len,0,p,n-1));
}
int main(void)
{
    cerberus_wifi_ap_job_t r=job();uint8_t ssid[32];
    for(unsigned i=0;i<sizeof(ssid);++i)ssid[i]=(uint8_t)('A'+i%26);
    for(unsigned name=0;name<=32;++name)
        for(unsigned padded=name;padded<=32;++padded)
            check_frame(&r,ssid,name,padded);
    /* A hidden source remains unknown. Zero bytes or spaces are explicit; the
     * builder does not substitute a guessed network name. */
    uint8_t p[112]={0};
    assert(cerberus_wifi_ap_clone_beacon(&r,clone,NULL,0,caps,0,0,p,sizeof(p))==77);
    assert(p[37]==0&&cerberus_wifi_ap_clone_rx_classify(&r,clone,NULL,0,caps,UINT8_MAX,p,77));
    assert(cerberus_wifi_ap_clone_beacon(&r,clone,NULL,0,caps,32,0,p,sizeof(p))==109);
    for(unsigned i=0;i<32;++i)assert(p[38+i]==' ');
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,33,caps,33,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,32,caps,31,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,0,caps,33,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,NULL,1,caps,1,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,NULL,1,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,NULL,ssid,1,caps,1,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(NULL,clone,ssid,1,caps,1,0,p,sizeof(p)));
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,NULL,109));
    uint8_t different[6];memcpy(different,clone,6);different[5]^=1;
    assert(!cerberus_wifi_ap_clone_beacon(&r,different,ssid,1,caps,1,0,p,sizeof(p)));
    r.station[0]=0x01;assert(!cerberus_wifi_ap_clone_beacon(&r,r.station,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.mode=CERBERUS_AP_DEAUTH;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.scope=CERBERUS_AP_ONE_STATION;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.channel=12;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.seconds=31;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.seconds=0;
    assert(cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p))==78);
    r.seconds=30;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.job_id=0;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();memcpy(r.bssid,clone,6);
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.bssid[0]|=1;
    assert(!cerberus_wifi_ap_clone_beacon(&r,clone,ssid,1,caps,1,0,p,sizeof(p)));
    r=job();r.channel=36;check_frame(&r,ssid,9,17);
    r=job();const size_t n=cerberus_wifi_ap_clone_beacon(&r,clone,ssid,9,caps,15,0,p,sizeof(p));
    assert(n==92);
    for(size_t i=0;i<n;++i)assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,UINT8_MAX,p,i));
    assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,UINT8_MAX,p,n+1));
    uint8_t bad[112];memcpy(bad,p,n);memcpy(bad+10,ap,6);memcpy(bad+16,ap,6);
    assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,UINT8_MAX,bad,n)); /* real AP */
    const unsigned offsets[]={0,1,4,10,16,22,32,34,36,37,38,47,50+15,51+15,52+15,91};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i){
        memcpy(bad,p,n);bad[offsets[i]]^=1;
        assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,UINT8_MAX,bad,n));
    }
    memcpy(bad,p,n);bad[22]=0x10; /* Sequence number may change, fragment may not. */
    assert(cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,UINT8_MAX,bad,n));
    assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,9,caps,14,p,n));
    assert(!cerberus_wifi_ap_clone_rx_classify(&r,clone,ssid,10,caps,UINT8_MAX,p,n));
    puts("AP Clone beacon passed: source identity, exact SSID/padding/IE/channel, hidden and 5 GHz policy, wildcard 2.4 GHz RX, malformed/real-AP rejection and bounds.");
}
