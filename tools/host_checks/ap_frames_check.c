#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_ap_job.h"
#include "wifi_ap_rx.h"
#include "wifi_sae_commit.h"
#include "sae_fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    cerberus_wifi_ap_job_t r={.job_id=1,.mode=CERBERUS_AP_BAD_MESSAGE,.seconds=1,.channel=6,.scope=1,
        .bssid={2,1,2,3,4,5},.station={2,9,8,7,6,5}};
    uint8_t buffer[164],*p=buffer+1,nonce[32],ssid[32],cap[]={0x31,4},observed[2];
    for(unsigned i=0;i<32;++i){nonce[i]=i;ssid[i]='A'+i%26;}
    const uint8_t llc[]={0xaa,0xaa,3,0,0,0,0x88,0x8e,2,3,0,0x75,2,0,0xca,0,0x10};
    for(unsigned short_frame=0;short_frame<2;++short_frame){
        memset(buffer,0xa5,sizeof(buffer));size_t n=cerberus_wifi_ap_bad_message(&r,short_frame,0x0102030405060708ULL,nonce,p,160);
        assert(n==(short_frame?131:153)&&buffer[0]==0xa5&&p[n]==0xa5);
        assert(!memcmp(p+4,r.station,6)&&!memcmp(p+10,r.bssid,6)&&!memcmp(p+16,r.bssid,6));
        if(!short_frame)assert(!memcmp(p+24,llc,sizeof(llc))&&p[131]==0xdd&&p[152]==0x11);
        else assert(p[35]==95&&p[38]==0xcb&&p[40]==0);
        for(unsigned i=0;i<8;++i)assert(p[41+i]==i+1);
        assert(!memcmp(p+49,nonce,32)&&p[130]==22);
        for(unsigned i=81;i<130;++i)assert(p[i]==0);
        assert(cerberus_wifi_ap_rx_classify(&r,p,n)==CERBERUS_AP_RX_MATCH);
        for(unsigned i=0;i<n;++i)assert(!cerberus_wifi_ap_rx_classify(&r,p,i));
        p[38]^=1;assert(!cerberus_wifi_ap_rx_classify(&r,p,n));p[38]^=1;
        p[4]^=2;assert(!cerberus_wifi_ap_rx_classify(&r,p,n));p[4]^=2;
        assert(!cerberus_wifi_ap_bad_message(&r,short_frame,1,nonce,p,n-1));
    }
    /* Calling the long variant after the short one must reset its header. */
    assert(cerberus_wifi_ap_bad_message(&r,false,1,nonce,p,160)==153&&p[35]==117&&p[38]==0xca&&p[40]==16);
    r.scope=0;assert(!cerberus_wifi_ap_job_valid(&r));r.scope=1;r.mode=CERBERUS_AP_ASSOC_SLEEP;
    for(unsigned len=0;len<=32;++len){
        memset(buffer,0xa5,sizeof(buffer));size_t n=cerberus_wifi_ap_association(&r,ssid,len,p,160);
        assert(n==100+len&&buffer[0]==0xa5&&p[n]==0xa5&&p[0]==0&&p[1]==16&&p[24]==0x31&&p[26]==10&&p[29]==len);
        assert(!memcmp(p+30,ssid,len)&&!memcmp(p+4,r.bssid,6)&&!memcmp(p+10,r.station,6));
        unsigned tags[]={1,0x21,0x24,0x30,0x3b,0xdd},sizes[]={4,2,2,20,20,10};size_t at=30+len;
        for(unsigned i=0;i<6;++i){assert(p[at]==tags[i]&&p[at+1]==sizes[i]);at+=2+sizes[i];}assert(at==n);
        assert(cerberus_wifi_ap_rx_classify(&r,p,n)==CERBERUS_AP_RX_MATCH);
        assert(!cerberus_wifi_ap_association(&r,ssid,len,p,n-1));
        p[1]=0;assert(!cerberus_wifi_ap_rx_classify(&r,p,n));
    }
    r.scope=0;memset(r.station,0,6);
    for(unsigned mode=5;mode<=6;++mode){r.mode=mode;
        for(unsigned len=0;len<=32;++len){
            memset(buffer,0xa5,sizeof(buffer));size_t n=cerberus_wifi_ap_announcement(&r,ssid,len,cap,11,0x0102030405060708ULL,p,160);
            assert(n==(mode==5?56:82)+len&&buffer[0]==0xa5&&p[n]==0xa5&&p[37]==len&&p[50+len]==6);
            assert(p[24]==8&&p[31]==1&&p[32]==100&&!memcmp(p+34,cap,2));
            const uint8_t csa[]={37,3,1,11,255},quiet[]={40,6,1,5,255,255,0,100};
            assert(!memcmp(p+(mode==5?51+len:n-8),mode==5?csa:quiet,mode==5?5:8));
            assert(cerberus_wifi_ap_beacon_capabilities(&r,ssid,len,p,n,observed)&&!memcmp(cap,observed,2));
            assert(cerberus_wifi_ap_rx_classify(&r,p,n)==(CERBERUS_AP_RX_MATCH|CERBERUS_AP_RX_BEACON));
            assert(!cerberus_wifi_ap_announcement(&r,ssid,len,cap,11,0,p,n-1));
            p[n-1]^=1;assert(cerberus_wifi_ap_rx_classify(&r,p,n)==CERBERUS_AP_RX_BEACON);p[n-1]^=1;
            if(len){p[38]^=1;assert(!cerberus_wifi_ap_beacon_capabilities(&r,ssid,len,p,n,observed));p[38]^=1;}
            p[50+len]=1;assert(!cerberus_wifi_ap_beacon_capabilities(&r,ssid,len,p,n,observed));p[50+len]=6;
            p[1]=0x40;assert(!cerberus_wifi_ap_rx_classify(&r,p,n));
        }
    }
    r.mode=5;assert(!cerberus_wifi_ap_announcement(&r,ssid,32,cap,6,0,p,160));
    assert(!cerberus_wifi_ap_announcement(&r,ssid,32,cap,52,0,p,160));
    r.mode=CERBERUS_AP_SAE_COMMIT;
    uint8_t source[6]={2,0xaa,0xbb,0xcc,0xdd,0xee},scalar[32]={0};scalar[31]=2;
    size_t sae_bytes=cerberus_wifi_sae_commit_frame(&r,source,scalar,cerberus_test_sae_2g,
        NULL,0,p,160);
    assert(sae_bytes==128&&cerberus_wifi_ap_rx_classify(&r,p,sae_bytes)==CERBERUS_AP_RX_MATCH);
    for(unsigned i=0;i<sae_bytes;++i)assert(!cerberus_wifi_ap_rx_classify(&r,p,i));
    assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes+1));
    p[1]=8;p[22]=0x10;assert(cerberus_wifi_ap_rx_classify(&r,p,sae_bytes)==CERBERUS_AP_RX_MATCH);
    p[1]=0;p[22]=0;
    p[24]=2;assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes));p[24]=3;
    p[30]=20;assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes));p[30]=19;
    p[4]^=2;assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes));p[4]^=2;
    p[10]=3;assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes));p[10]=2;
    uint8_t original_x[32];memcpy(original_x,p+64,32);memset(p+64,0xff,32);
    assert(!cerberus_wifi_ap_rx_classify(&r,p,sae_bytes));memcpy(p+64,original_x,32);
    puts("AP frames passed: bounded layouts and S3 classification, including SAE commit and malformed authentication frames.");
}
