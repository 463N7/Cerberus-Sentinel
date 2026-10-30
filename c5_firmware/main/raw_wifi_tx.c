#include "raw_wifi_tx.h"
#include "wifi_sae_commit.h"
#include "esp_wifi.h"
#include "mbedtls/platform_util.h"
#include <string.h>
/* The project-local archive preserves the vendor function under this alias.
 * Its four-argument ABI is checked against the pinned IDF 6.0.1 C5 blob.
 * Ordinary frames continue through the original validation. */
extern int cerberus_vendor_raw_frame_sanity_check(int,const void *,int,bool);
static const uint8_t *approved_frame;
static size_t approved_length;
int ieee80211_raw_frame_sanity_check(int interface,const void *buffer,int length,bool sequence)
{
    if(buffer&&buffer==approved_frame&&length>0&&(size_t)length==approved_length&&interface==WIFI_IF_STA&&sequence)
        return ESP_OK;
    return cerberus_vendor_raw_frame_sanity_check(interface,buffer,length,sequence);
}
esp_err_t cerberus_c5_raw_deauth(const uint8_t *frame,size_t length)
{
    if(!frame||length!=26||frame[0]!=0xc0||frame[1]||frame[24]!=7||frame[25]||
       (frame[10]&1)||(frame[16]&1)||
       (memcmp(frame+10,frame+16,6)&&memcmp(frame+4,frame+16,6)))return ESP_ERR_INVALID_ARG;
    /* Called only by the exclusive C5 TX worker. esp_wifi_80211_tx performs
     * its sanity check synchronously; this is never a general bypass. */
    approved_length=length;approved_frame=frame;
    esp_err_t error=esp_wifi_80211_tx(WIFI_IF_STA,frame,(int)length,true);
    approved_frame=NULL;approved_length=0;
    return error;
}
esp_err_t cerberus_c5_raw_association(const cerberus_wifi_ap_job_t *job,const uint8_t *ssid,size_t length)
{
    /* Only this exact bounded builder is admitted, never a caller's arbitrary frame. */
    uint8_t frame[132];
    size_t n=cerberus_wifi_ap_association(job,ssid,length,frame,sizeof(frame));
    if(!n)return ESP_ERR_INVALID_ARG;
    approved_length=n;approved_frame=frame;
    esp_err_t error=esp_wifi_80211_tx(WIFI_IF_STA,frame,(int)n,true);
    approved_frame=NULL;approved_length=0;return error;
}
esp_err_t cerberus_c5_raw_sae_commit(const cerberus_wifi_ap_job_t *job,
    const uint8_t *frame,size_t length)
{
    if(!job||!frame||length!=CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES)
        return ESP_ERR_INVALID_ARG;
    /* The sanity hook admits this one serializer output only. It is not a
     * general authentication-frame bypass, and the driver may still reject
     * this subtype after its synchronous sanity call. */
    uint8_t expected[CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES];
    size_t encoded=cerberus_wifi_sae_commit_frame(job,frame+10,frame+32,
        frame+64,NULL,0,expected,sizeof(expected));
    bool valid=encoded==length&&!memcmp(frame,expected,length);
    mbedtls_platform_zeroize(expected,sizeof(expected));
    if(!valid)return ESP_ERR_INVALID_ARG;
    approved_length=length;approved_frame=frame;
    esp_err_t error=esp_wifi_80211_tx(WIFI_IF_STA,frame,(int)length,true);
    approved_frame=NULL;approved_length=0;
    return error;
}
