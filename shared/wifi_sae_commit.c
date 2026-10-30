#include "wifi_sae_commit.h"
#include <string.h>

/* Frame layout follows the pinned Marauder WiFiScan.h:647 and
 * WiFiScan.cpp:8836-8903 at commit 511bb3694d0c4dc064888e762bde44fcc9b7f48a.
 * Copyright (c) 2020 Just Call Me Koko, MIT; see
 * LICENSES/ESP32Marauder.txt. This is a new bounded serializer. */
static const uint8_t p256_order[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x00,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
    0xbc,0xe6,0xfa,0xad,0xa7,0x17,0x9e,0x84,
    0xf3,0xb9,0xca,0xc2,0xfc,0x63,0x25,0x51,
};
static const uint8_t p256_prime[32] = {
    0xff,0xff,0xff,0xff,0x00,0x00,0x00,0x01,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0xff,0xff,0xff,0xff,
    0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};
static bool greater_than_one(const uint8_t scalar[32])
{
    for(unsigned i=0;i<31;++i)if(scalar[i])return true;
    return scalar[31]>1U;
}
static bool nonzero(const uint8_t *bytes,size_t length)
{
    for(size_t i=0;i<length;++i)if(bytes[i])return true;
    return false;
}
size_t cerberus_wifi_sae_commit_frame(const cerberus_wifi_ap_job_t *r,
    const uint8_t source[6],const uint8_t scalar[32],
    const uint8_t element_xy[64],const uint8_t *token,
    size_t token_length,uint8_t *out,size_t capacity)
{
    if(!r||!source||!scalar||!element_xy||!out||
       r->mode!=CERBERUS_AP_SAE_COMMIT||!cerberus_wifi_ap_job_valid(r)||
       r->scope!=CERBERUS_AP_BROADCAST||
       (source[0]&3U)!=2U||!memcmp(source,r->bssid,6)||
       !greater_than_one(scalar)||memcmp(scalar,p256_order,32)>=0||
       !nonzero(element_xy,64)||memcmp(element_xy,p256_prime,32)>=0||
       memcmp(element_xy+32,p256_prime,32)>=0||
       token_length>CERBERUS_WIFI_SAE_COMMIT_TOKEN_MAX||
       (token_length&&!token))return 0;
    size_t length=CERBERUS_WIFI_SAE_COMMIT_BASE_BYTES+
        (token_length?2U+token_length:0U);
    if(capacity<length)return 0;
    memset(out,0,length);
    out[0]=0xb0; /* authentication management frame */
    memcpy(out+4,r->bssid,6);
    memcpy(out+10,source,6);
    memcpy(out+16,r->bssid,6);
    out[24]=3; /* SAE authentication algorithm */
    out[26]=1; /* transaction sequence 1: commit */
    out[30]=19; /* little-endian group 19 / P-256 */
    memcpy(out+32,scalar,32);
    memcpy(out+64,element_xy,64);
    if(token_length){
        out[128]=0x4c; /* anti-clogging token element in pinned source */
        out[129]=(uint8_t)token_length;
        memcpy(out+130,token,token_length);
    }
    return length;
}
