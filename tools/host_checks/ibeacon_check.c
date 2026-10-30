#include "ibeacon.h"
#include "ble_ad_hints.h"
#include "ble_findmy_hints.h"
#include "ble_flipper_hints.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static unsigned checks;
static const uint8_t canonical[25] = {
    0x4C,0,2,0x15,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,
    0x12,0x34,0xAB,0xCD,0xC5,
};

static size_t field(uint8_t *out,uint8_t type,const uint8_t *value,size_t length)
{
    assert(length<=254U);out[0]=(uint8_t)(length+1U);out[1]=type;
    if(length)memcpy(out+2U,value,length);
    return length+2U;
}
static size_t beacon(uint8_t *out,const uint8_t *value)
{return field(out,0xFFU,value?value:canonical,25U);}

/* FOUND expected bytes are decoded independently from the wire fixture.
 * Structural validity is separate from conflicting iBeacon interpretations. */
static cerberus_ibeacon_t expect(const uint8_t *data,size_t length,
    cerberus_ibeacon_parse_result_t state,bool structural,const uint8_t *expected)
{
    struct {uint32_t before;cerberus_ibeacon_t value;uint32_t after;} out;
    memset(&out,0xA5,sizeof(out));out.before=0x12345678U;out.after=0xABCDEF09U;
    uint8_t copy[256];if(data&&length<=sizeof(copy))memcpy(copy,data,length);
    assert(cerberus_ibeacon_parse_ad(data,length,&out.value)==state);
    assert(out.before==0x12345678U&&out.after==0xABCDEF09U);
    if(state==CERBERUS_IBEACON_FOUND){
        assert(expected);assert(!memcmp(out.value.uuid,expected+4U,16U));
        unsigned major=expected[20]*256U+expected[21];
        unsigned minor=expected[22]*256U+expected[23];
        int power=expected[24]<128U?expected[24]:(int)expected[24]-256;
        assert(out.value.major==major&&out.value.minor==minor&&out.value.measured_power_dbm==power);
    }else{
        cerberus_ibeacon_t zero={0};assert(!memcmp(&out.value,&zero,sizeof(zero)));
    }
    cerberus_ble_ad_hints_t meta;
    cerberus_ble_findmy_hints_t findmy;
    cerberus_ble_flipper_hints_t flipper;
    assert(cerberus_ble_ad_hints_parse(data,length,&meta)==structural);
    assert(cerberus_ble_findmy_hints_parse(data,length,&findmy)==structural);
    assert(cerberus_ble_flipper_hints_parse(data,length,&flipper)==structural);
    if(data&&length<=sizeof(copy))assert(!memcmp(copy,data,length));
    ++checks;return out.value;
}

static void canonical_and_encodings(void)
{
    uint8_t data[256],value[25];memcpy(value,canonical,sizeof(value));
    const uint8_t flags=6;
    size_t length=field(data,1,&flags,1U);length+=beacon(data+length,NULL);
    data[length++]=0;assert(length==31U);
    expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    expect(data,beacon(data,NULL),CERBERUS_IBEACON_FOUND,true,canonical);
    for(unsigned power=0;power<256U;++power){
        value[24]=(uint8_t)power;
        expect(data,beacon(data,value),CERBERUS_IBEACON_FOUND,true,value);
    }
    const uint16_t numbers[]={0,1,0x7F,0x80,0xFF,0x100,0x1234,0xABCD,0xFFFF};
    for(size_t major=0;major<sizeof(numbers)/sizeof(numbers[0]);++major)
        for(size_t minor=0;minor<sizeof(numbers)/sizeof(numbers[0]);++minor){
            value[20]=(uint8_t)(numbers[major]>>8U);value[21]=(uint8_t)numbers[major];
            value[22]=(uint8_t)(numbers[minor]>>8U);value[23]=(uint8_t)numbers[minor];
            expect(data,beacon(data,value),CERBERUS_IBEACON_FOUND,true,value);
        }
    for(size_t byte=4U;byte<20U;++byte)for(unsigned pattern=0;pattern<3U;++pattern){
        memcpy(value,canonical,sizeof(value));value[byte]=pattern==0U?0U:pattern==1U?0x80U:0xFFU;
        expect(data,beacon(data,value),CERBERUS_IBEACON_FOUND,true,value);
    }
}

static void exact_field_context(void)
{
    uint8_t data[300],value[254];
    /* Every prefix byte must be exact. Mutating an Apple/type/length value
     * leaves an ordinary manufacturer field, not an iBeacon. */
    for(size_t byte=0;byte<4U;++byte)for(unsigned replacement=0;replacement<256U;++replacement){
        if(replacement==canonical[byte])continue;
        memcpy(value,canonical,25U);value[byte]=(uint8_t)replacement;
        expect(data,beacon(data,value),CERBERUS_IBEACON_NONE,true,NULL);
    }
    for(size_t size=0;size<=254U;++size){
        memset(value,0,sizeof(value));memcpy(value,canonical,size<25U?size:25U);
        size_t length=field(data,0xFF,value,size);
        bool structural=size>=2U&&length<=255U;
        cerberus_ibeacon_parse_result_t state=!structural?CERBERUS_IBEACON_MALFORMED:
            size==25U?CERBERUS_IBEACON_FOUND:CERBERUS_IBEACON_NONE;
        expect(data,length,state,structural,state==CERBERUS_IBEACON_FOUND?canonical:NULL);
    }
    const uint8_t ordinary_types[]={0x08,0x09,0x16,0x20,0x21,0x30};
    for(size_t i=0;i<sizeof(ordinary_types);++i)
        expect(data,field(data,ordinary_types[i],canonical,25U),CERBERUS_IBEACON_NONE,true,NULL);
    /* A full 25-byte marker embedded inside another company payload or
     * inside a valid UUID list cannot borrow manufacturer context. */
    memset(value,0x55,32U);memcpy(value+4U,canonical,25U);
    expect(data,field(data,0xFF,value,32U),CERBERUS_IBEACON_NONE,true,NULL);
    for(unsigned type=2U;type<=7U;++type)
        expect(data,field(data,(uint8_t)type,value,32U),CERBERUS_IBEACON_NONE,true,NULL);
    /* Exact Apple prefix but a different AD type is never enough. */
    expect(canonical,sizeof(canonical),CERBERUS_IBEACON_MALFORMED,false,NULL);
}

static void duplicates(void)
{
    uint8_t data[256],value[25],reverse[256];
    size_t length=beacon(data,NULL);length+=beacon(data+length,NULL);
    expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    const uint8_t harmless[]={1,2,3};
    length=beacon(data,NULL);length+=field(data+length,0x30,harmless,sizeof(harmless));
    length+=beacon(data+length,NULL);expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    for(size_t byte=4U;byte<25U;++byte){
        memcpy(value,canonical,25U);value[byte]^=0x80U;
        length=beacon(data,NULL);length+=beacon(data+length,value);
        expect(data,length,CERBERUS_IBEACON_MALFORMED,true,NULL);
        memcpy(reverse,data+27U,27U);memcpy(reverse+27U,data,27U);
        expect(reverse,length,CERBERUS_IBEACON_MALFORMED,true,NULL);
    }
    /* A non-iBeacon manufacturer field is not a conflicting exact field. */
    for(size_t byte=0U;byte<4U;++byte){
        memcpy(value,canonical,25U);value[byte]^=1U;
        length=beacon(data,NULL);length+=beacon(data+length,value);
        expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    }
    length=0U;for(unsigned i=0;i<9U;++i)length+=beacon(data+length,NULL);
    memset(data+length,0,255U-length);
    expect(data,255U,CERBERUS_IBEACON_FOUND,true,canonical);
    data[254]=1;expect(data,255U,CERBERUS_IBEACON_MALFORMED,false,NULL);
}

static void malformed_streams(void)
{
    uint8_t data[300],single[48],value[40]={0};
    const uint8_t types[]={1,2,3,4,5,6,7,0xFF,0x16,0x20,0x21};
    for(size_t type_index=0;type_index<sizeof(types);++type_index)
        for(size_t size=0U;size<34U;++size){
            uint8_t type=types[type_index];bool valid;
            if(type==1U)valid=size==1U;
            else if(type>=2U&&type<=7U){size_t width=type<=3U?2U:type<=5U?4U:16U;valid=size%width==0U;}
            else valid=size>=(type==0x20?4U:type==0x21?16U:2U);
            size_t length=field(single,type,value,size);
            expect(single,length,valid?CERBERUS_IBEACON_NONE:CERBERUS_IBEACON_MALFORMED,valid,NULL);
            size_t prefix=beacon(data,NULL);memcpy(data+prefix,single,length);
            expect(data,prefix+length,valid?CERBERUS_IBEACON_FOUND:CERBERUS_IBEACON_MALFORMED,
                valid,valid?canonical:NULL);
            memcpy(data,single,length);prefix=beacon(data+length,NULL);
            expect(data,length+prefix,valid?CERBERUS_IBEACON_FOUND:CERBERUS_IBEACON_MALFORMED,
                valid,valid?canonical:NULL);
        }
    size_t length=beacon(data,NULL);data[length++]=3;data[length++]=0x30;data[length++]=1;
    expect(data,length,CERBERUS_IBEACON_MALFORMED,false,NULL);
    length=beacon(data,NULL);data[length++]=0;data[length++]=0;
    expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    data[length++]=1;expect(data,length,CERBERUS_IBEACON_MALFORMED,false,NULL);
    const uint8_t name_a[]={'A','B'},name_b[]={'A','C'};
    length=beacon(data,NULL);length+=field(data+length,9,name_a,sizeof(name_a));
    length+=field(data+length,9,name_a,sizeof(name_a));
    expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    data[length-1]='C';expect(data,length,CERBERUS_IBEACON_MALFORMED,false,NULL);
    /* Shortened names do not assert equality with a complete name, and
     * empty complete-name fields do not contradict a nonempty name. */
    length=field(data,9,NULL,0U);length+=field(data+length,9,name_a,sizeof(name_a));
    length+=field(data+length,8,name_b,sizeof(name_b));length+=beacon(data+length,NULL);
    length+=field(data+length,9,NULL,0U);expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    uint8_t long_name[33];memset(long_name,'X',sizeof(long_name));
    length=beacon(data,NULL);length+=field(data+length,9,long_name,sizeof(long_name));
    length+=field(data+length,9,long_name,sizeof(long_name));
    expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    data[length-1]='Y';expect(data,length,CERBERUS_IBEACON_MALFORMED,false,NULL);
}

static void containers_and_atomic_output(void)
{
    uint8_t data[300],padding[226];memset(padding,0x55,sizeof(padding));
    const uint8_t flags=6,name[]={'N','A','M','E','A','B'};
    size_t length=field(data,1,&flags,1U);
    length+=field(data+length,0x30,padding,4U);
    length+=beacon(data+length,NULL);length+=field(data+length,9,name,sizeof(name));
    assert(length==44U);
    /* Complete AD-boundary prefixes are valid shorter containers. The radio
     * must supply completeness; no byte parser can infer an omitted tail. */
    for(size_t cut=0;cut<=length;++cut){
        bool boundary=cut==0U||cut==3U||cut==9U||cut==36U||cut==44U;
        cerberus_ibeacon_parse_result_t state=!boundary?CERBERUS_IBEACON_MALFORMED:
            cut>=36U?CERBERUS_IBEACON_FOUND:CERBERUS_IBEACON_NONE;
        expect(data,cut,state,boundary,state==CERBERUS_IBEACON_FOUND?canonical:NULL);
    }
    length=beacon(data,NULL);length+=field(data+length,0x30,padding,sizeof(padding));
    assert(length==255U);expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    length=field(data,0x30,padding,sizeof(padding));length+=beacon(data+length,NULL);
    assert(length==255U);expect(data,length,CERBERUS_IBEACON_FOUND,true,canonical);
    data[length++]=0;expect(data,length,CERBERUS_IBEACON_MALFORMED,false,NULL);
    expect(NULL,0,CERBERUS_IBEACON_NONE,true,NULL);
    expect(NULL,1,CERBERUS_IBEACON_MALFORMED,false,NULL);
    assert(cerberus_ibeacon_parse_ad(data,255U,NULL)==CERBERUS_IBEACON_MALFORMED);
    const uint8_t empty[]={1,0x03,1,0x09};
    expect(empty,sizeof(empty),CERBERUS_IBEACON_NONE,true,NULL);
    /* Reuse the SAME output after a success, including invalid arguments. */
    cerberus_ibeacon_t out,zero={0};length=beacon(data,NULL);
    assert(cerberus_ibeacon_parse_ad(data,length,&out)==CERBERUS_IBEACON_FOUND);
    assert(cerberus_ibeacon_parse_ad(empty,sizeof(empty),&out)==CERBERUS_IBEACON_NONE);
    assert(!memcmp(&out,&zero,sizeof(out)));
    assert(cerberus_ibeacon_parse_ad(data,length,&out)==CERBERUS_IBEACON_FOUND);
    assert(cerberus_ibeacon_parse_ad(NULL,1,&out)==CERBERUS_IBEACON_MALFORMED);
    assert(!memcmp(&out,&zero,sizeof(out)));
    assert(cerberus_ibeacon_parse_ad(data,length,&out)==CERBERUS_IBEACON_FOUND);
    assert(cerberus_ibeacon_parse_ad(data,256U,&out)==CERBERUS_IBEACON_MALFORMED);
    assert(!memcmp(&out,&zero,sizeof(out)));
    ++checks;
}

int main(void)
{
    canonical_and_encodings();exact_field_context();duplicates();
    malformed_streams();containers_and_atomic_output();
    printf("iBeacon parser: %u fixture checks passed; exact25B fields, complete255B containers, signed/network values, identical/conflicting duplicates, strict full AD validation and zero nonFOUND output\n",checks);
    return 0;
}
