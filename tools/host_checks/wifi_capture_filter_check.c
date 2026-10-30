#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_capture_filter.h"
#include "cerberus_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void check_sae_commit(void)
{
    const uint8_t ap[6]={2,3,4,5,6,7};
    const uint8_t client[6]={4,5,6,7,8,9};
    uint8_t frame[128]={0xb0};
    memcpy(frame+4,ap,6);memcpy(frame+10,client,6);memcpy(frame+16,ap,6);
    frame[24]=3U;frame[26]=1U;frame[30]=19U;
    for(size_t i=32U;i<sizeof(frame);++i)frame[i]=(uint8_t)i;
    const uint8_t mode=CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT;
    assert(cerberus_wifi_capture_filter_match(mode,true,ap,frame,sizeof(frame)));
    assert(!cerberus_wifi_capture_filter_match(mode,true,client,frame,sizeof(frame)));
    assert(!cerberus_wifi_capture_filter_match(mode,true,NULL,frame,sizeof(frame)));
    assert(cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    /* AP replies and client commits use addr3 as the selected BSSID. */
    memcpy(frame+4,client,6);memcpy(frame+10,ap,6);
    frame[28]=76U; /* anti-clogging-token required: retain the received reply */
    assert(cerberus_wifi_capture_filter_match(mode,true,ap,frame,sizeof(frame)));
    for(size_t n=0;n<32U;++n)
        assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,n));
    /* Complete group field is the parser boundary, not cryptographic proof. */
    assert(cerberus_wifi_capture_filter_match(mode,false,NULL,frame,32U));
    frame[25]=1U;
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[25]=0U;frame[27]=1U;
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[27]=0U;frame[26]=2U; /* SAE confirm */
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[26]=1U;frame[24]=0U; /* open authentication */
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[24]=3U;frame[0]=0xb1U; /* unsupported protocol version */
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[0]=0x08U; /* same bytes inside a data frame */
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[0]=0xb0U;
    const uint8_t invalid_flags[]={1U,2U,4U,0x40U,0x80U};
    for(size_t i=0;i<sizeof(invalid_flags);++i){
        frame[1]=invalid_flags[i];
        assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    }
    frame[1]=8U; /* retry is valid captured evidence */
    assert(cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[22]=1U;
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[22]=0U;frame[0]=0x40U;
    assert(cerberus_wifi_capture_filter_match(mode|CERBERUS_WIFI_CAPTURE_MODE_PROBE,
        false,NULL,frame,sizeof(frame)));
    assert(!cerberus_wifi_capture_filter_match(0x40U,false,NULL,frame,sizeof(frame)));
}

static void check_beacon(void)
{
    const uint8_t ap[6]={2,3,4,5,6,7};
    uint8_t frame[48]={0x80};memcpy(frame+16,ap,6);
    const uint8_t mode=CERBERUS_WIFI_CAPTURE_MODE_BEACON;
    assert(cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    assert(cerberus_wifi_capture_filter_match(mode,true,ap,frame,sizeof(frame)));
    frame[16]^=2U;
    assert(!cerberus_wifi_capture_filter_match(mode,true,ap,frame,sizeof(frame)));
    frame[16]^=2U;
    for(size_t n=0;n<36U;++n)
        assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,n));
    const uint8_t bad_types[]={0x81,0x50,0x40,0xc0,0x08,0x84};
    for(size_t i=0;i<sizeof(bad_types);++i){
        frame[0]=bad_types[i];
        assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    }
    frame[0]=0x80;
    const uint8_t bad_flags[]={1,2,4,0x40,0x80};
    for(size_t i=0;i<sizeof(bad_flags);++i){
        frame[1]=bad_flags[i];
        assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    }
    frame[1]=8; /* retry is retained */
    assert(cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[22]=1;
    assert(!cerberus_wifi_capture_filter_match(mode,false,NULL,frame,sizeof(frame)));
    frame[22]=0;frame[0]=0x40;
    assert(cerberus_wifi_capture_filter_match(mode|CERBERUS_WIFI_CAPTURE_MODE_PROBE,
        false,NULL,frame,sizeof(frame)));
}

int main(void)
{
    check_sae_commit();
    check_beacon();
    const uint8_t ap[6]={1,2,3,4,5,6};
    uint8_t frame[64]={0};
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_RAW,false,ap,frame,1));
    assert(!cerberus_wifi_capture_filter_match(0,false,ap,frame,sizeof(frame)));
    assert(!cerberus_wifi_capture_filter_match(0x80,false,ap,frame,sizeof(frame)));
    frame[0]=0x40; /* probe request, normally broadcast BSSID */
    memset(frame+16,0xff,6);
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_PROBE,false,ap,frame,24));
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_PROBE,true,ap,frame,24));
    frame[0]=0x50;memcpy(frame+16,ap,6); /* probe response */
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_PROBE,true,ap,frame,24));
    frame[0]=0xc0;
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_DEAUTH,true,ap,frame,25));
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_DEAUTH,true,ap,frame,26));
    frame[0]=0xa0;
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_DEAUTH,true,ap,frame,26));
    frame[0]=0x08;frame[1]=1;memcpy(frame+4,ap,6); /* to-DS BSSID at addr1 */
    static const uint8_t llc[8]={0xaa,0xaa,3,0,0,0,0x88,0x8e};
    memcpy(frame+24,llc,8);
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,true,ap,frame,32));
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,true,ap,frame,31));
    frame[0]=0x88;memcpy(frame+26,llc,8); /* QoS data */
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,true,ap,frame,34));
    frame[1]=0x83;memcpy(frame+36,llc,8); /* four-address QoS plus HT control */
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,true,ap,frame,44));
    assert(cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,false,ap,frame,44));
    frame[36]=0;
    assert(!cerberus_wifi_capture_filter_match(CERBERUS_WIFI_CAPTURE_MODE_EAPOL,false,ap,frame,44));
    puts("Wi-Fi capture filter passed: beacon, probe, deauth, EAPOL, SAE Commit, BSSID, QoS/WDS and malformed bounds.");
    return 0;
}
