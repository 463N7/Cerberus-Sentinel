#include "ble_findmy_hints.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static const uint8_t fmna16[] = {0x44,0xfd};
static const uint8_t fmna32[] = {0x44,0xfd,0,0};
static const uint8_t fmna128[] = {
    0xfb,0x34,0x9b,0x5f,0x80,0x00,0x00,0x80,0x00,0x10,0x00,0x00,0x44,0xfd,0x00,0x00,
};
static const uint8_t dult128[] = {
    0x85,0x2a,0x9f,0x57,0xc5,0x2a,0xed,0x88,0x26,0xc2,0xf4,0x12,0x01,0x00,0x19,0x15,
};
static const uint8_t apple[] = {
    30,0xff,0x4c,0x00,0x12,0x19,
    0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,
    0x0e,0x0f,0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,
};
static unsigned assertions;

static size_t field(uint8_t *out, uint8_t type, const uint8_t *value, size_t length)
{
    assert(length <= 254U);
    out[0] = (uint8_t)(length + 1U); out[1] = type;
    if (length) memcpy(out + 2, value, length);
    return length + 2U;
}

static void expect(const uint8_t *data, size_t length, bool valid, uint8_t bits, uint8_t widths)
{
    uint8_t saved[256];
    if (data && length <= sizeof(saved)) memcpy(saved, data, length);
    cerberus_ble_findmy_hints_t out = {0xffU,0xffU};
    bool actual = cerberus_ble_findmy_hints_parse(data, length, &out);
    assert(actual == valid);
    assert(out.bits == bits && out.fmna_uuid_widths == widths);
    if (data && length <= sizeof(saved)) assert(memcmp(saved, data, length) == 0);
    ++assertions;
}

static void uuid_rules(void)
{
    const uint8_t *uuids[] = {fmna16,fmna32,fmna128,dult128};
    const size_t widths[] = {2,4,16,16};
    const uint8_t flags[] = {CERBERUS_BLE_FINDMY_UUID16,CERBERUS_BLE_FINDMY_UUID32,CERBERUS_BLE_FINDMY_UUID128,0};
    uint8_t data[256], values[64];
    for (unsigned rule = 0; rule < 4U; ++rule) {
        for (unsigned complete = 0; complete < 2U; ++complete) {
            uint8_t type = (uint8_t)((widths[rule] == 2 ? 2 : widths[rule] == 4 ? 4 : 6) + complete);
            for (unsigned position = 0; position < 3U; ++position) {
                memset(values,0x6a,sizeof(values));
                memcpy(values + position * widths[rule], uuids[rule], widths[rule]);
                size_t size = field(data,type,values,3U * widths[rule]);
                expect(data,size,true,rule == 3 ? CERBERUS_BLE_FINDMY_HINT_DULT : CERBERUS_BLE_FINDMY_HINT_FMNA,flags[rule]);
            }
            size_t size = field(data,type,uuids[rule],widths[rule]);
            for (size_t truncated = 1U; truncated < size; ++truncated)
                expect(data,truncated,false,0,0);
            for (size_t byte = 0; byte < widths[rule]; ++byte) {
                memcpy(values,uuids[rule],widths[rule]); values[byte] ^= 0x80U;
                size = field(data,type,values,widths[rule]);
                expect(data,size,true,0,0);
            }
        }
    }
    /* Neither endianness guessing nor a near Bluetooth base is normalization. */
    for (size_t i = 0; i < sizeof(fmna128); ++i) values[i] = fmna128[15U-i];
    expect(data,field(data,7,values,16),true,0,0);
    size_t used = 0U;
    used += field(data + used,2,fmna16,2);
    used += field(data + used,5,fmna32,4);
    used += field(data + used,7,fmna128,16);
    used += field(data + used,6,dult128,16);
    memcpy(data + used,apple,sizeof(apple));used += sizeof(apple);
    expect(data,used,true,7,7);
    memcpy(data + used,apple,sizeof(apple));used += sizeof(apple);
    expect(data,used,true,7,7); /* Repeated valid hints are idempotent. */
    uint8_t service_data_types[] = {0x16,0x20,0x21};
    for (unsigned width = 0; width < 3U; ++width)
        expect(data,field(data,service_data_types[width],uuids[width],widths[width]),true,0,0);
    expect(data,field(data,0x21,dult128,sizeof(dult128)),true,0,0);
}

static void apple_rules(void)
{
    uint8_t data[256];
    expect(apple,sizeof(apple),true,CERBERUS_BLE_FINDMY_HINT_APPLE,0);
    for (size_t size = 1U; size < sizeof(apple); ++size) expect(apple,size,false,0,0);
    for (size_t byte = 2U; byte <= 5U; ++byte) {
        memcpy(data,apple,sizeof(apple));data[byte] ^= 1U;
        expect(data,sizeof(apple),true,0,0);
    }
    /* Complete AD fields with a short/long Apple frame are not this hint. */
    memcpy(data,apple,sizeof(apple));--data[0];
    expect(data,sizeof(apple)-1U,true,0,0);
    memcpy(data,apple,sizeof(apple));++data[0];data[sizeof(apple)]=0U;
    expect(data,sizeof(apple)+1U,true,0,0);
    /* Same substrings under another AD type or another manufacturer do not match. */
    memcpy(data,apple,sizeof(apple));data[1]=0x09;
    expect(data,sizeof(apple),true,0,0);
    data[1]=0xff;data[2]=0x00;data[3]=0x4c;
    expect(data,sizeof(apple),true,0,0);
    uint8_t embedded[33]={1,2,3,4};memcpy(embedded+4,apple+2,sizeof(apple)-2U);
    expect(data,field(data,0xff,embedded,sizeof(embedded)),true,0,0);
    expect(data,field(data,9,(const uint8_t *)"AirTag Find My FD44 DULT",23),true,0,0);
}

static void full_stream_validation(void)
{
    uint8_t data[256], value[254]={0};
    memcpy(data,apple,sizeof(apple));
    size_t prefix = sizeof(apple);
    /* A valid hint may not survive an invalid suffix. Every UUID-list width. */
    for (uint8_t type = 2U; type <= 7U; ++type) {
        size_t width = type <= 3 ? 2U : type <= 5 ? 4U : 16U;
        for (size_t size = 1U; size < width * 2U; ++size) {
            if (size == width) continue;
            size_t used = prefix + field(data+prefix,type,value,size);
            expect(data,used,false,0,0);
        }
        expect(data,prefix + field(data+prefix,type,NULL,0),true,CERBERUS_BLE_FINDMY_HINT_APPLE,0);
    }
    const uint8_t types[] = {0xff,0x16,0x20,0x21};
    const size_t minimum[] = {2,2,4,16};
    for (unsigned kind = 0; kind < sizeof(types); ++kind)
        for (size_t size = 0; size < minimum[kind]; ++size)
            expect(data,prefix + field(data+prefix,types[kind],value,size),false,0,0);
    for (size_t size = 0; size < 4U; ++size)
        expect(data,prefix + field(data+prefix,1,value,size),size==1U,size==1U?1U:0U,0);
    data[prefix]=5;data[prefix+1]=0xff;data[prefix+2]=0x4c;
    expect(data,prefix+3U,false,0,0);
    data[prefix]=0;memset(data+prefix+1U,0,9U);
    expect(data,prefix+10U,true,CERBERUS_BLE_FINDMY_HINT_APPLE,0);
    data[prefix+9U]=1;
    expect(data,prefix+10U,false,0,0);
    size_t used = prefix + field(data+prefix,9,(const uint8_t *)"Name",4);
    used += field(data+used,9,(const uint8_t *)"Name",4);
    expect(data,used,true,CERBERUS_BLE_FINDMY_HINT_APPLE,0);
    data[used-1]='X';expect(data,used,false,0,0);
    used = prefix + field(data+prefix,9,(const uint8_t *)"Name",4);
    used += field(data+used,9,(const uint8_t *)"Names",5);
    expect(data,used,false,0,0);
    /* Unknown bounded AD fields, even with arbitrary signature bytes, are data. */
    for (unsigned size=0; size<128U; ++size) {
        for (unsigned i=0; i<size; ++i) value[i]=(uint8_t)(i*37U+size);
        used = field(data,0x7e,value,size);
        expect(data,used,true,0,0);
        memcpy(data+used,apple,sizeof(apple));used += sizeof(apple);
        expect(data,used,true,CERBERUS_BLE_FINDMY_HINT_APPLE,0);
        data[used++]=2;expect(data,used,false,0,0);
    }
    /* Boundary: all 255 bytes must be validated; no large-buffer partial hint. */
    used=field(data,3,fmna16,2);
    used+=field(data+used,0x7e,value,249U);
    assert(used==255U);expect(data,used,true,CERBERUS_BLE_FINDMY_HINT_FMNA,CERBERUS_BLE_FINDMY_UUID16);
    data[255]=0;expect(data,256U,false,0,0);
    expect(NULL,0,true,0,0);expect(NULL,1,false,0,0);
    assert(!cerberus_ble_findmy_hints_parse(data,1,NULL));
    memset(data,0,255U);expect(data,255U,true,0,0);
}

int main(void)
{
    assert(sizeof(apple)==31U && sizeof(cerberus_ble_findmy_hints_t)==2U);
    uuid_rules();apple_rules();full_stream_validation();
    printf("Passive Find My hints: %u fixture checks passed; UUID16/32/base128 normalization, mixed hints, exact Apple/DULT, full AD validation and immutable input; 2-byte result.\n",assertions);
    return 0;
}
