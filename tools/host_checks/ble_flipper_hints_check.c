#include "ble_flipper_hints.h"
#include "ble_findmy_hints.h" /* Assert the shared structural validity contract. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static unsigned checks;
static const uint8_t flipper_name[] = {'F','l','i','p','p','e','r'};
static const uint8_t base128[] = {
    0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0x80,0x30,0,0,
};
/* Official serial GATT constant; neither byte order is an advertised code. */
static const uint8_t serial_gatt[] = {
    0,0,0xfe,0x60,0xcc,0x7a,0x48,0x2a,0x98,0x4a,0x7f,0x2e,0xd5,0xb3,0xe5,0x8f,
};

static size_t field(uint8_t *out, uint8_t type, const uint8_t *data, size_t size)
{
    assert(size <= 254U);
    out[0]=(uint8_t)(size+1U);out[1]=type;
    if(size)memcpy(out+2,data,size);
    return size+2U;
}
static void service(uint8_t *value, size_t width, unsigned variant)
{
    assert(variant<=3U && (width==2U || width==4U || width==16U));
    memset(value,0,width);
    if(width==16U){memcpy(value,base128,16U);value[12]+=(uint8_t)variant;}
    else {value[0]=(uint8_t)(0x80U+variant);value[1]=0x30U;}
}
static void expect(const uint8_t *data, size_t size, bool valid, uint8_t bits, uint8_t flags)
{
    uint8_t copy[256];
    if(data && size<=sizeof(copy))memcpy(copy,data,size);
    cerberus_ble_flipper_hints_t out={255,255};
    assert(cerberus_ble_flipper_hints_parse(data,size,&out)==valid);
    assert(out.bits==bits && out.flags==flags);
    cerberus_ble_findmy_hints_t other={255,255};
    assert(cerberus_ble_findmy_hints_parse(data,size,&other)==valid);
    if(data && size<=sizeof(copy))assert(!memcmp(copy,data,size));
    ++checks;
}

static void variants_and_widths(void)
{
    uint8_t data[256], value[64], one[16];
    const size_t widths[]={2,4,16};
    const uint8_t width_flags[]={CERBERUS_BLE_FLIPPER_UUID16,
        CERBERUS_BLE_FLIPPER_UUID32,CERBERUS_BLE_FLIPPER_UUID128};
    for(unsigned variant=0;variant<4U;++variant)
        for(unsigned kind=0;kind<3U;++kind)
            for(unsigned complete=0;complete<2U;++complete){
                size_t width=widths[kind];
                uint8_t type=(uint8_t)(2U+kind*2U+complete);
                service(one,width,variant);
                for(unsigned position=0;position<3U;++position){
                    memset(value,0x5a,sizeof(value));
                    memcpy(value+position*width,one,width);
                    size_t size=field(data,type,value,3U*width);
                    expect(data,size,true,variant?(uint8_t)(1U<<variant):0U,variant?width_flags[kind]:0U);
                    size+=field(data+size,9,flipper_name,sizeof(flipper_name));
                    expect(data,size,true,(uint8_t)(1U<<variant),(uint8_t)(width_flags[kind]|CERBERUS_BLE_FLIPPER_NAME));
                }
                size_t size=field(data,type,one,width);
                for(size_t cut=1U;cut<size;++cut)expect(data,cut,false,0,0);
                for(size_t byte=0;byte<width;++byte){
                    memcpy(value,one,width);value[byte]^=0x40U;
                    size=field(data,type,value,width);
                    expect(data,size,true,0,0);
                }
                size=field(data,9,flipper_name,sizeof(flipper_name));
                size+=field(data+size,type,one,width);
                expect(data,size,true,(uint8_t)(1U<<variant),(uint8_t)(width_flags[kind]|CERBERUS_BLE_FLIPPER_NAME));
            }
    /* Official 16-bit variant with no local name: a real upstream false negative. */
    const uint8_t ordinary_variant[]={2,1,6,3,3,0x81,0x30,2,0x0a,0xc5};
    expect(ordinary_variant,sizeof(ordinary_variant),true,CERBERUS_BLE_FLIPPER_HINT_BLACK,CERBERUS_BLE_FLIPPER_UUID16);
    /* All variants stay independent. Base widths count only after qualification. */
    size_t size=0;
    service(one,2,0);size+=field(data+size,2,one,2);
    service(one,16,2);size+=field(data+size,7,one,16);
    expect(data,size,true,CERBERUS_BLE_FLIPPER_HINT_WHITE,CERBERUS_BLE_FLIPPER_UUID128);
    size+=field(data+size,9,flipper_name,sizeof(flipper_name));
    expect(data,size,true,CERBERUS_BLE_FLIPPER_HINT_BASE|CERBERUS_BLE_FLIPPER_HINT_WHITE,
        CERBERUS_BLE_FLIPPER_UUID16|CERBERUS_BLE_FLIPPER_UUID128|CERBERUS_BLE_FLIPPER_NAME);
    service(one,4,1);size+=field(data+size,5,one,4);
    service(one,2,3);size+=field(data+size,3,one,2);
    expect(data,size,true,15,15);
    size+=field(data+size,3,one,2);expect(data,size,true,15,15);
}

static void name_context(void)
{
    uint8_t data[256], value[40], uuid[2];service(uuid,2,0);
    const struct {const char *text;bool match;} names[]={
        {"Flipper",true},{"Flipper A",true},{"Flipper  ",true},
        {"Flipper ",false},{"FlipperZero",false},{"flipper",false},
        {"Flipper\nA",false},{"Flipper\tA",false},{"Unknown",false},{"",false},
    };
    for(size_t i=0;i<sizeof(names)/sizeof(*names);++i){
        size_t size=field(data,3,uuid,2);
        size+=field(data+size,9,(const uint8_t *)names[i].text,strlen(names[i].text));
        expect(data,size,true,names[i].match?CERBERUS_BLE_FLIPPER_HINT_BASE:0U,
            names[i].match?(CERBERUS_BLE_FLIPPER_NAME|CERBERUS_BLE_FLIPPER_UUID16):0U);
        data[2]=0x81;
        expect(data,size,true,CERBERUS_BLE_FLIPPER_HINT_BLACK,
            CERBERUS_BLE_FLIPPER_UUID16|(names[i].match?CERBERUS_BLE_FLIPPER_NAME:0U));
    }
    memset(value,'A',sizeof(value));memcpy(value,"Flipper ",8);
    for(size_t n=32U;n<=33U;++n){
        size_t size=field(data,3,uuid,2);size+=field(data+size,9,value,n);
        expect(data,size,true,n==32U?1U:0U,n==32U?9U:0U);
    }
    for(unsigned byte=0;byte<2U;++byte){
        value[8]=byte?127U:0U;
        size_t size=field(data,3,uuid,2);size+=field(data+size,9,value,9);
        expect(data,size,true,0,0);
    }
    size_t size=field(data,3,uuid,2);size+=field(data+size,8,flipper_name,sizeof(flipper_name));
    expect(data,size,true,0,0); /* Shortened name cannot corroborate base. */
    /* Never strengthen a base report using a subsequent standalone name report. */
    expect(data,field(data,3,uuid,2),true,0,0);
    expect(data,field(data,9,flipper_name,sizeof(flipper_name)),true,0,CERBERUS_BLE_FLIPPER_NAME);
    size=field(data,3,uuid,2);size+=field(data+size,9,flipper_name,sizeof(flipper_name));
    size+=field(data+size,9,flipper_name,sizeof(flipper_name));expect(data,size,true,1,9);
    data[size-1]='X';expect(data,size,false,0,0);
    size=field(data,3,uuid,2);size+=field(data+size,9,flipper_name,sizeof(flipper_name));
    size+=field(data+size,9,(const uint8_t *)"Flipper A",9);expect(data,size,false,0,0);
}

static void named_service_data(void)
{
    uint8_t data[256], value[40], uuid[16];
    const uint8_t types[]={0x16,0x20,0x21};
    const size_t widths[]={2,4,16};
    for(unsigned variant=0;variant<4U;++variant)
        for(unsigned kind=0;kind<3U;++kind){
            size_t width=widths[kind];service(uuid,width,variant);
            memcpy(value,uuid,width);memset(value+width,0x81,4U);
            size_t size=field(data,types[kind],value,width+4U);
            expect(data,size,true,0,0); /* No name means no service-data hint. */
            size+=field(data+size,9,flipper_name,sizeof(flipper_name));
            uint8_t expected_flags=(uint8_t)((1U<<kind)|CERBERUS_BLE_FLIPPER_NAME|CERBERUS_BLE_FLIPPER_SERVICE_DATA);
            expect(data,size,true,(uint8_t)(1U<<variant),expected_flags);
            size=field(data,9,flipper_name,sizeof(flipper_name));
            size+=field(data+size,types[kind],uuid,width); /* Empty service payload is valid. */
            expect(data,size,true,(uint8_t)(1U<<variant),expected_flags);
            data[1]=8;expect(data,size,true,0,0); /* Shortened name cannot qualify it. */
            /* A supported UUID buried after a different prefix is not evidence. */
            memset(value,0x55,width);memcpy(value+width,uuid,width);
            size=field(data,types[kind],value,width*2U);
            size+=field(data+size,9,flipper_name,sizeof(flipper_name));
            expect(data,size,true,0,CERBERUS_BLE_FLIPPER_NAME);
            memcpy(value,uuid,width);value[0]^=0x40U;
            size=field(data,types[kind],value,width);
            size+=field(data+size,9,flipper_name,sizeof(flipper_name));
            expect(data,size,true,0,CERBERUS_BLE_FLIPPER_NAME);
            /* Names from a different report must never strengthen prior service data. */
            expect(data,field(data,types[kind],uuid,width),true,0,0);
            expect(data,field(data,9,flipper_name,sizeof(flipper_name)),true,0,CERBERUS_BLE_FLIPPER_NAME);
        }
    /* Raw data widths that did not qualify must not leak into list evidence. */
    service(uuid,16,3);size_t size=field(data,0x21,uuid,16);
    service(uuid,2,1);size+=field(data+size,3,uuid,2);
    expect(data,size,true,CERBERUS_BLE_FLIPPER_HINT_BLACK,CERBERUS_BLE_FLIPPER_UUID16);
    size+=field(data+size,9,flipper_name,sizeof(flipper_name));
    expect(data,size,true,CERBERUS_BLE_FLIPPER_HINT_BLACK|CERBERUS_BLE_FLIPPER_HINT_TRANSPARENT,
        CERBERUS_BLE_FLIPPER_UUID16|CERBERUS_BLE_FLIPPER_UUID128|CERBERUS_BLE_FLIPPER_NAME|CERBERUS_BLE_FLIPPER_SERVICE_DATA);
}

static void field_context_and_bounds(void)
{
    uint8_t data[256], value[254]={0}, uuid[16];
    service(uuid,16,1);
    const uint8_t types[]={0x16,0x20,0x21,0xff,0x7e,0x08,0x09};
    for(size_t i=0;i<sizeof(types);++i)
        expect(data,field(data,types[i],uuid,16),true,0,0);
    expect(data,field(data,7,serial_gatt,sizeof(serial_gatt)),true,0,0);
    for(size_t i=0;i<16U;++i)value[i]=serial_gatt[15U-i];
    expect(data,field(data,7,value,16),true,0,0);
    memset(value,0,16);value[7]=0x81;value[8]=0x30;
    expect(data,field(data,7,value,16),true,0,0); /* Substring in unrelated UUID. */
    for(size_t i=0;i<16U;++i)value[i]=uuid[15U-i];
    expect(data,field(data,7,value,16),true,0,0); /* No byte-order guessing. */
    const uint8_t prefix[]={3,3,0x81,0x30,8,9,'F','l','i','p','p','e','r'};
    memcpy(data,prefix,sizeof(prefix));size_t used=sizeof(prefix);
    for(uint8_t type=2U;type<=7U;++type){
        size_t width=type<=3?2U:type<=5?4U:16U;
        for(size_t n=1U;n<width*2U;++n){
            if(n==width)continue;
            expect(data,used+field(data+used,type,value,n),false,0,0);
        }
        expect(data,used+field(data+used,type,NULL,0),true,2,9);
    }
    const uint8_t short_types[]={0xff,0x16,0x20,0x21};
    const size_t minimum[]={2,2,4,16};
    for(size_t kind=0;kind<sizeof(short_types);++kind)
        for(size_t n=0;n<minimum[kind];++n)
            expect(data,used+field(data+used,short_types[kind],value,n),false,0,0);
    for(size_t n=0;n<4U;++n)
        expect(data,used+field(data+used,1,value,n),n==1U,n==1U?2U:0U,n==1U?9U:0U);
    data[used]=0;memset(data+used+1,0,4);
    expect(data,used+5U,true,2,9);data[used+4]=1;expect(data,used+5U,false,0,0);
    data[used]=5;data[used+1]=0x7e;expect(data,used+2U,false,0,0);
    /* Every prefix of the multi-field report: only real field boundaries work. */
    for(size_t cut=0;cut<=sizeof(prefix);++cut){
        bool valid=cut==0U || cut==4U || cut==sizeof(prefix);
        expect(prefix,cut,valid,valid&&cut?2U:0U,
            cut==4U?1U:cut==sizeof(prefix)?9U:0U);
    }
    memset(value,0x81,sizeof(value));value[1]=0x30;
    service(uuid,2,3);used=field(data,3,uuid,2);
    used+=field(data+used,0x7e,value,249U);assert(used==255U);
    expect(data,used,true,8,1);data[255]=0;expect(data,256U,false,0,0);
    expect(NULL,0,true,0,0);expect(NULL,1,false,0,0);
    assert(!cerberus_ble_flipper_hints_parse(data,1,NULL));
    memset(data,0,255);expect(data,255,true,0,0);
}

int main(void)
{
    assert(sizeof(cerberus_ble_flipper_hints_t)==2U);
    variants_and_widths();name_context();named_service_data();field_context_and_bounds();
    printf("Passive Flipper hints: %u fixture checks passed; exact16/32/base128 variants, same-report3080/name and named service-data compatibility, customGATT exclusion, fullAD boundaries and immutable input;2-byte result.\n",checks);
    return 0;
}
