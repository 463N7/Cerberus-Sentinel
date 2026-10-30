#include "wifi_capture_filter.h"
#include "cerberus_protocol.h"
#include <string.h>

static bool sae_commit_frame(const uint8_t *frame,size_t length)
{
    /* Authentication has a 24-byte MAC header, three LE16 fixed fields,
     * then an SAE group field. Capture this frame family, including status
     * replies; this does not validate the scalar, element or token body.
     * Encrypted/fragmented frames cannot establish these fields here. */
    if(length<32U||frame[0]!=0xb0U||
       (frame[1]&0xc7U)!=0U||(frame[22]&0x0fU)!=0U)return false;
    return frame[24]==3U&&frame[25]==0U&&
        frame[26]==1U&&frame[27]==0U;
}

bool cerberus_wifi_capture_filter_match(uint8_t mode_mask,bool filter_bssid,
    const uint8_t bssid[6],const uint8_t *frame,size_t length)
{
    if(!frame||length<2U||!mode_mask||(mode_mask&~CERBERUS_WIFI_CAPTURE_MODE_ALL)||
       (filter_bssid&&!bssid))return false;
    unsigned type=(frame[0]>>2U)&3U,subtype=(frame[0]>>4U)&15U;
    const uint8_t *frame_bssid=NULL;
    if(type==0U){
        if(length<24U)return false;
        frame_bssid=frame+16U;
    }else if(type==2U){
        if(length<24U)return false;
        bool to_ds=(frame[1]&1U)!=0U,from_ds=(frame[1]&2U)!=0U;
        if(!to_ds&&!from_ds)frame_bssid=frame+16U;
        else if(to_ds&&!from_ds)frame_bssid=frame+4U;
        else if(!to_ds&&from_ds)frame_bssid=frame+10U;
        /* Four-address WDS frames have no BSSID. */
    }
    if(filter_bssid&&(!frame_bssid||memcmp(frame_bssid,bssid,6U)!=0))return false;
    if(mode_mask&CERBERUS_WIFI_CAPTURE_MODE_RAW)return true;
    if(type==0U){
        /* Keep complete beacon bytes for the S3's strict IE parser. This
         * cheap family gate does not claim that the SSID/IE body is valid. */
        if((mode_mask&CERBERUS_WIFI_CAPTURE_MODE_BEACON)&&
           frame[0]==0x80U&&length>=36U&&
           (frame[1]&0xc7U)==0U&&(frame[22]&0x0fU)==0U)return true;
        if((mode_mask&CERBERUS_WIFI_CAPTURE_MODE_PROBE)&&
           (subtype==4U||subtype==5U))return true;
        if((mode_mask&CERBERUS_WIFI_CAPTURE_MODE_DEAUTH)&&length>=26U&&
           (subtype==10U||subtype==12U))return true;
        if((mode_mask&CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT)&&
           sae_commit_frame(frame,length))return true;
    }
    if(type==2U&&(mode_mask&CERBERUS_WIFI_CAPTURE_MODE_EAPOL)){
        bool to_ds=(frame[1]&1U)!=0U,from_ds=(frame[1]&2U)!=0U;
        size_t header=24U+(to_ds&&from_ds?6U:0U)+(subtype&8U?2U:0U);
        if((subtype&8U)&&(frame[1]&0x80U))header+=4U;
        static const uint8_t eapol_llc[8]={0xaaU,0xaaU,0x03U,0,0,0,0x88U,0x8eU};
        return length>=header+sizeof(eapol_llc)&&
            memcmp(frame+header,eapol_llc,sizeof(eapol_llc))==0;
    }
    return false;
}
