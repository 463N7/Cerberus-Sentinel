#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_sae_commit.h"

static uint8_t digit(char c)
{
    if(c>='0'&&c<='9')return (uint8_t)(c-'0');
    if(c>='a'&&c<='f')return (uint8_t)(c-'a'+10);
    assert(0);return 0;
}
static void from_hex(const char *hex,uint8_t *out,size_t length)
{
    assert(strlen(hex)==length*2U);
    for(size_t i=0;i<length;++i)out[i]=(uint8_t)((digit(hex[i*2U])<<4)|digit(hex[i*2U+1U]));
}
int main(void)
{
    cerberus_wifi_ap_job_t job={.job_id=17,.generation=4,.index=2,
        .bssid={2,0x11,0x22,0x33,0x44,0x55},.channel=6,
        .seconds=10,.scope=CERBERUS_AP_BROADCAST,.mode=7};
    const uint8_t source[6]={6,0xaa,0xbb,0xcc,0xdd,0xee};
    uint8_t scalar[32]={0},element[64],token[126],output[258];
    scalar[31]=2;
    /* The standard secp256r1 2G point matches scalar 2. */
    from_hex("7cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1",element,64);
    memset(output,0xa5,sizeof(output));
    size_t n=cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output+1,256);
    assert(n==128&&output[0]==0xa5&&output[129]==0xa5);
    const uint8_t header[32]={
        0xb0,0,0,0, 2,0x11,0x22,0x33,0x44,0x55,
        6,0xaa,0xbb,0xcc,0xdd,0xee,
        2,0x11,0x22,0x33,0x44,0x55,
        0,0, 3,0, 1,0, 0,0, 19,0,
    };
    assert(!memcmp(output+1,header,sizeof(header)));
    assert(!memcmp(output+33,scalar,32)&&!memcmp(output+65,element,64));
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,127));
    for(unsigned i=0;i<sizeof(token);++i)token[i]=(uint8_t)i;
    memset(output,0xa5,sizeof(output));
    n=cerberus_wifi_sae_commit_frame(&job,source,scalar,element,token,sizeof(token),output+1,256);
    assert(n==256&&output[0]==0xa5&&output[257]==0xa5);
    assert(output[129]==0x4c&&output[130]==126&&!memcmp(output+131,token,126));
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,token,126,output,255));
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,token,127,output,258));
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,1,output,258));
    job.mode=6;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.mode=7;
    job.channel=12;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.channel=36;
    assert(cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258)==128);
    job.scope=CERBERUS_AP_ONE_STATION;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.scope=0;
    job.station[5]=1;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.station[5]=0;
    job.seconds=31;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.seconds=10;
    job.seconds=0;assert(cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258)==128);
    job.seconds=30;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.seconds=10;
    job.bssid[0]=1;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));job.bssid[0]=2;
    uint8_t bad_source[6]={4,0xaa,0xbb,0xcc,0xdd,0xee};
    assert(!cerberus_wifi_sae_commit_frame(&job,bad_source,scalar,element,NULL,0,output,258));
    bad_source[0]=7;assert(!cerberus_wifi_sae_commit_frame(&job,bad_source,scalar,element,NULL,0,output,258));
    scalar[31]=1;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    scalar[31]=0;assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    from_hex("ffffffff00000000ffffffffffffffffbce6faada7179e84f3b9cac2fc632551",scalar,32);
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    --scalar[31];assert(cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258)==128);
    memset(scalar,0,32);scalar[31]=2;
    uint8_t good_element[64];memcpy(good_element,element,64);
    memset(element,0,64);assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    memcpy(element,good_element,64);
    from_hex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",element,32);
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    memcpy(element,good_element,64);
    from_hex("ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",element+32,32);
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,element,NULL,0,output,258));
    assert(!cerberus_wifi_sae_commit_frame(NULL,source,scalar,good_element,NULL,0,output,258));
    assert(!cerberus_wifi_sae_commit_frame(&job,source,scalar,good_element,NULL,0,NULL,258));
    puts("SAE group-19 serializer passed: exact auth layout, bounded token, AP/source identity, scalar/coordinate bounds and output guards.");
}
