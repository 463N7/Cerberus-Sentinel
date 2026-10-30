#include "radio_test.h"
#include <string.h>
bool cerberus_radio_test_config_valid(const cerberus_radio_test_config_t *c)
{
    if (!c || !c->job_id || !c->seconds || c->seconds > 30 || !c->name_length) return false;
    if (c->mode == CERBERUS_TEST_WIFI_BEACON) {
        if (c->name_length > 32 || (c->channel != 1 && c->channel != 6 && c->channel != 11 &&
            c->channel != 36 && c->channel != 40 && c->channel != 44 && c->channel != 48)) return false;
    } else if (c->mode == CERBERUS_TEST_BLE_ADVERT) {
        if (c->channel || c->name_length > 26) return false;
    } else return false;
    for (unsigned i=0;i<c->name_length;++i) if ((uint8_t)c->name[i] < 32 || (uint8_t)c->name[i] > 126) return false;
    return true;
}
size_t cerberus_radio_test_beacon(const cerberus_radio_test_config_t *c,
    const uint8_t mac[6], uint64_t stamp, uint8_t *out, size_t capacity)
{
    if (!cerberus_radio_test_config_valid(c) || c->mode != CERBERUS_TEST_WIFI_BEACON || !mac || !out || (mac[0]&1)) return 0;
    size_t length=36U+2U+c->name_length+10U+(c->channel<=11?3U:0U);
    if (capacity < length) return 0;
    memset(out,0,length);out[0]=0x80;memset(out+4,0xff,6);memcpy(out+10,mac,6);memcpy(out+16,mac,6);
    for(unsigned i=0;i<8;++i)out[24+i]=(uint8_t)(stamp>>(i*8));
    out[32]=100;out[34]=1;out[36]=0;out[37]=c->name_length;memcpy(out+38,c->name,c->name_length);
    const uint8_t rates[]={1,8,0x8c,0x12,0x98,0x24,0xb0,0x48,0x60,0x6c};
    memcpy(out+38+c->name_length,rates,sizeof(rates));
    if(c->channel<=11){out[length-3]=3;out[length-2]=1;out[length-1]=c->channel;}
    return length;
}
size_t cerberus_radio_test_ble(const cerberus_radio_test_config_t *c,uint8_t *out,size_t capacity)
{
    if (!cerberus_radio_test_config_valid(c) || c->mode != CERBERUS_TEST_BLE_ADVERT || !out || capacity < 5U+c->name_length) return 0;
    out[0]=2;out[1]=1;out[2]=6;out[3]=c->name_length+1;out[4]=9;
    memcpy(out+5,c->name,c->name_length);return 5U+c->name_length;
}
