#include "ble_flock_hints.h"
#include "ble_flock_oui.h"
#include "ble_ad_hints.h"
#include "ble_findmy_hints.h"
#include "ble_flipper_hints.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static unsigned checks;
static const uint8_t accessory[] = {
    0x6f,0x2e,0x17,0xdf,0x14,0x18,0xe5,0x9f,0xa8,0x46,0x32,0x95,0x38,0xbb,0xcc,0xe8,
};
static const uint8_t base[] = {0xfb,0x34,0x9b,0x5f,0x80,0,0,0x80,0,0x10,0,0,0,0,0,0};
static const size_t widths[] = {2,4,16,2,4,16};
static const uint8_t types[] = {3,5,7,0x16,0x20,0x21};
static const uint8_t battery[] = "FS Ext Battery";
static const uint8_t manufacturer[] = {0xc8,9,0x42};

static size_t field(uint8_t *out, uint8_t type, const uint8_t *value, size_t length)
{
    assert(length <= 254U);
    out[0]=(uint8_t)(length+1U);out[1]=type;
    if(length)memcpy(out+2,value,length);
    return length+2U;
}
static void uuid(uint8_t *out, size_t width, uint16_t code)
{
    memset(out,0,width);size_t position=0;
    if(width==16U){memcpy(out,base,16);position=12;}
    out[position]=(uint8_t)code;out[position+1]=(uint8_t)(code>>8);
}
static void expect(const uint8_t *data, size_t length, bool valid,
    uint8_t facts, uint8_t acc, uint8_t s31, uint8_t s32, unsigned level)
{
    uint8_t saved[256];if(data && length<=sizeof(saved))memcpy(saved,data,length);
    struct {uint8_t before;cerberus_ble_flock_hints_t hints;uint8_t after;} guarded;
    memset(&guarded,0xa5,sizeof(guarded));
    assert(cerberus_ble_flock_hints_parse(data,length,&guarded.hints)==valid);
    assert(guarded.before==0xa5 && guarded.after==0xa5);
    const cerberus_ble_flock_hints_t expected={facts,acc,s31,s32};
    if(memcmp(&guarded.hints,&expected,sizeof(expected))){
        fprintf(stderr,"case%u len%zu actual %u/%u/%u/%u expected %u/%u/%u/%u\n",checks,length,
            guarded.hints.facts,guarded.hints.accessory_contexts,guarded.hints.raven_3100_contexts,
            guarded.hints.raven_3200_contexts,facts,acc,s31,s32);assert(false);
    }
    assert((unsigned)cerberus_ble_flock_hints_level(&guarded.hints)==level);
    cerberus_ble_ad_hints_t meta;
    cerberus_ble_findmy_hints_t findmy;
    cerberus_ble_flipper_hints_t flipper;
    assert(cerberus_ble_ad_hints_parse(data,length,&meta)==valid);
    assert(cerberus_ble_findmy_hints_parse(data,length,&findmy)==valid);
    assert(cerberus_ble_flipper_hints_parse(data,length,&flipper)==valid);
    if(data && length<=sizeof(saved))assert(!memcmp(saved,data,length));
    ++checks;
}

static void service_contexts(void)
{
    uint8_t data[256],value[64],one[16],second[16];
    for(unsigned which=0;which<2;++which)for(unsigned kind=0;kind<6;++kind){
        size_t width=widths[kind];uuid(one,width,(uint16_t)(0x3100U+which*0x100U));
        for(unsigned variant=0;variant<(kind<3?2U:1U);++variant){
            uint8_t type=(uint8_t)(types[kind]-variant);
            for(unsigned position=0;position<(kind<3?3U:1U);++position){
                memset(value,0x55,sizeof(value));memcpy(value+position*width,one,width);
                size_t length=field(data,type,value,kind<3?width*3U:width+5U);
                expect(data,length,true,8,0,which?0:(uint8_t)(1U<<kind),which?(uint8_t)(1U<<kind):0,0);
            }
            size_t length=field(data,type,one,width);
            for(size_t cut=1;cut<length;++cut)expect(data,cut,false,0,0,0,0,0);
            for(size_t byte=0;byte<width;++byte)for(unsigned bit=0;bit<8;++bit){
                memcpy(value,one,width);value[byte]^=(uint8_t)(1U<<bit);
                length=field(data,type,value,width);
                expect(data,length,true,8,0,0,0,0);
            }
        }
        /* Prefix-only service data: the real value after another UUID is not evidence. */
        if(kind>=3){
            memset(value,0x55,width);memcpy(value+width,one,width);
            expect(data,field(data,types[kind],value,width*2U),true,8,0,0,0,0);
        }
    }
    /* Preserve all exact pairs, even when the two services use different contexts. */
    for(unsigned a=0;a<6;++a)for(unsigned b=0;b<6;++b)for(unsigned reverse=0;reverse<2;++reverse){
        uuid(one,widths[a],0x3100);uuid(second,widths[b],0x3200);
        size_t length=0;
        if(reverse)length+=field(data+length,types[b],second,widths[b]);
        length+=field(data+length,types[a],one,widths[a]);
        if(!reverse)length+=field(data+length,types[b],second,widths[b]);
        expect(data,length,true,8,0,(uint8_t)(1U<<a),(uint8_t)(1U<<b),2);
        length+=field(data+length,types[a],one,widths[a]);
        expect(data,length,true,8,0,(uint8_t)(1U<<a),(uint8_t)(1U<<b),2);
    }
    size_t length=0;
    for(unsigned which=0;which<2;++which)for(unsigned kind=0;kind<6;++kind){
        uuid(one,widths[kind],(uint16_t)(0x3100U+which*0x100U));
        length+=field(data+length,types[kind],one,widths[kind]);
    }
    expect(data,length,true,8,0,63,63,2);
    /* Reports remain independent: one service per report never becomes a pair. */
    uuid(one,2,0x3100);expect(data,field(data,3,one,2),true,8,0,1,0,0);
    uuid(one,2,0x3200);expect(data,field(data,3,one,2),true,8,0,0,1,0);
}

static void accessory_and_collision(void)
{
    uint8_t data[256],value[64];
    const uint8_t kinds[]={6,7,0x21};
    for(size_t kind=0;kind<sizeof(kinds);++kind){
        uint8_t context=kind<2?4U:32U;
        size_t length=field(data,kinds[kind],accessory,16);
        expect(data,length,true,8,context,0,0,2);
        for(size_t byte=0;byte<16;++byte)for(unsigned bit=0;bit<8;++bit){
            memcpy(value,accessory,16);value[byte]^=(uint8_t)(1U<<bit);
            expect(data,field(data,kinds[kind],value,16),true,8,0,0,0,0);
        }
        for(size_t i=0;i<16;++i)value[i]=accessory[15U-i];
        expect(data,field(data,kinds[kind],value,16),true,8,0,0,0,0);
    }
    size_t length=field(data,7,accessory,16);
    length+=field(data+length,0x21,accessory,16);
    expect(data,length,true,8,36,0,0,2);
    const uint8_t wrong_types[]={0xff,0x16,0x20,0x7e,8,9};
    for(size_t i=0;i<sizeof(wrong_types);++i)
        expect(data,field(data,wrong_types[i],accessory,16),true,wrong_types[i]>=8 && wrong_types[i]<=9?0:8,0,0,0,0);
    memset(value,0x55,16);memcpy(value+16,accessory,16);
    expect(data,field(data,0x21,value,32),true,8,0,0,0,0);
    /* Exactly31 bytes: prior Watch chooses Flipper HIGH, hiding accessory MEDIUM. */
    const uint8_t flipper[]={0x81,0x30};
    length=field(data,3,flipper,2);length+=field(data+length,9,(const uint8_t *)"Flipper",7);
    length+=field(data+length,7,accessory,16);assert(length==31);
    expect(data,length,true,0,4,0,0,2);
    cerberus_ble_flipper_hints_t other;assert(cerberus_ble_flipper_hints_parse(data,length,&other));
    assert(other.bits==2 && other.flags==9);++checks;
}

static void names_and_manufacturers(void)
{
    uint8_t data[256],value[64];
    const struct {const char *name;uint8_t facts;unsigned level;} cases[]={
        {"FS Ext Battery",1,2},{"Penguin-0123456789",2,2},{"0123456789",4,0},
        {"",8,0},{"Penguin",0,0},{"Flock",0,0},{"Raven",0,0},{"Pigvision",0,0},
        {"FS Ext Battery ",0,0},{"fs Ext Battery",0,0},{"Penguin-012345678",0,0},
        {"Penguin-01234567890",0,0},{"Penguin-012345678A",0,0},{"012345678",0,0},
    };
    for(size_t c=0;c<sizeof(cases)/sizeof(*cases);++c){
        const uint8_t *name=(const uint8_t *)cases[c].name;size_t n=strlen(cases[c].name);
        expect(data,field(data,9,name,n),true,cases[c].facts,0,0,0,cases[c].level);
        for(unsigned payload=0;payload<2;++payload)for(unsigned reverse=0;reverse<2;++reverse){
            size_t length=0;
            if(reverse)length+=field(data+length,0xff,manufacturer,2U+payload);
            length+=field(data+length,9,name,n);
            if(!reverse)length+=field(data+length,0xff,manufacturer,2U+payload);
            unsigned level=cases[c].level;
            if((cases[c].facts&3U) && payload)level=3;
            else if(cases[c].facts&12U)level=1;
            expect(data,length,true,(uint8_t)(cases[c].facts|16U|(payload?32U:0U)),0,0,0,level);
            data[reverse?5U+payload:1U]=8; /* Exact names shortened: no signature/decimal name. */
            expect(data,length,true,(uint8_t)((n?0U:8U)|16U|(payload?32U:0U)),0,0,0,n?0:1);
        }
    }
    /* Every byte of the exact names matters; no embedded NUL or binary shortcuts. */
    const char *exact[]={"FS Ext Battery","Penguin-0123456789","0123456789"};
    for(size_t n=0;n<3;++n)for(size_t byte=0;byte<strlen(exact[n]);++byte){
        size_t count=strlen(exact[n]);memcpy(value,exact[n],count);value[byte]=0;
        size_t length=field(data,9,value,count);length+=field(data+length,0xff,manufacturer,3);
        expect(data,length,true,48,0,0,0,0);
    }
    /* Empty names supply no name, while any nonempty short/binary name does. */
    size_t length=field(data,0xff,manufacturer,2);
    length+=field(data+length,8,NULL,0);length+=field(data+length,9,NULL,0);
    expect(data,length,true,24,0,0,0,1);
    const uint8_t binary[]={0};length+=field(data+length,8,binary,1);
    expect(data,length,true,16,0,0,0,0);
    length=field(data,9,NULL,0);length+=field(data+length,9,battery,14);
    length+=field(data+length,9,NULL,0);length+=field(data+length,9,battery,14);
    expect(data,length,true,1,0,0,0,2);data[length-1]='X';expect(data,length,false,0,0,0,0,0);
    /* A different manufacturer's bytes cannot supply09C8 payload corroboration. */
    const uint8_t unrelated[]={0x34,0x12,0xc8,9,0x42};
    length=field(data,9,battery,14);length+=field(data+length,0xff,manufacturer,2);
    length+=field(data+length,0xff,unrelated,sizeof(unrelated));expect(data,length,true,17,0,0,0,2);
    length+=field(data+length,0xff,manufacturer,3);expect(data,length,true,49,0,0,0,3);
    length=field(data,0xff,unrelated,sizeof(unrelated));expect(data,length,true,8,0,0,0,0);
    const uint8_t fake[]={0xff,0xc8,9};
    expect(data,field(data,0x7e,fake,sizeof(fake)),true,8,0,0,0,0);
    expect(data,field(data,0x16,manufacturer,3),true,8,0,0,0,0);
    expect(data,field(data,0xff,manufacturer,3),true,56,0,0,0,1);
    expect(data,field(data,9,battery,14),true,1,0,0,0,2); /* No cached manufacturer boost. */
}

static void validation_and_limits(void)
{
    uint8_t data[256],value[254]={0};
    size_t positive=field(data,9,battery,14);positive+=field(data+positive,0xff,manufacturer,3);
    for(uint8_t type=2;type<=7;++type){
        size_t width=type<=3?2U:type<=5?4U:16U;
        for(size_t n=1;n<width*2U;++n){
            if(n==width)continue;
            expect(data,positive+field(data+positive,type,value,n),false,0,0,0,0,0);
        }
        expect(data,positive+field(data+positive,type,NULL,0),true,49,0,0,0,3);
    }
    const uint8_t short_types[]={0xff,0x16,0x20,0x21};const size_t minima[]={2,2,4,16};
    for(unsigned kind=0;kind<4;++kind)for(size_t n=0;n<minima[kind];++n)
        expect(data,positive+field(data+positive,short_types[kind],value,n),false,0,0,0,0,0);
    for(size_t n=0;n<4;++n)
        expect(data,positive+field(data+positive,1,value,n),n==1,n==1?49:0,0,0,0,n==1?3:0);
    data[positive]=0;memset(data+positive+1,0,4);expect(data,positive+5,true,49,0,0,0,3);
    data[positive+4]=1;expect(data,positive+5,false,0,0,0,0,0);
    data[positive]=3;data[positive+1]=0x7e;expect(data,positive+2,false,0,0,0,0,0);
    for(size_t cut=0;cut<=positive;++cut){
        bool valid=cut==0 || cut==16 || cut==positive;
        expect(data,cut,valid,cut==0?8:cut==16?1:cut==positive?49:0,0,0,0,
            cut==16?2:cut==positive?3:0);
    }
    memset(value,0x55,sizeof(value));size_t length=field(data,7,accessory,16);
    length+=field(data+length,0x7e,value,235);assert(length==255);
    expect(data,length,true,8,4,0,0,2);data[255]=0;expect(data,256,false,0,0,0,0,0);
    expect(NULL,0,true,8,0,0,0,0);expect(NULL,1,false,0,0,0,0,0);
    assert(!cerberus_ble_flock_hints_parse(data,1,NULL));
    assert(cerberus_ble_flock_hints_level(NULL)==CERBERUS_BLE_FLOCK_LEVEL_NONE);
    memset(data,0,255);expect(data,255,true,8,0,0,0,0);
    /* Unknown bits/invalid context categories do not create helper matches. */
    cerberus_ble_flock_hints_t h={0xc0,0xdb,0xc0,0xc0};
    assert(cerberus_ble_flock_hints_level(&h)==CERBERUS_BLE_FLOCK_LEVEL_NONE);++checks;
}

static void address_clues(void)
{
    /* Independently transcribed displayed prefixes from pinned upstream table. */
    const uint32_t prefixes[]={0x588e81,0xcccccc,0xec1bbd,0x9035ea,0x040d84,0xf082c0,
        0x1c34f1,0x385b44,0x943469,0xb4e3f9,0x70c94e,0x3c9180,0xd8f3bc,0x803049,
        0x145afc,0x744ca1,0x083a88,0x9c2f9d,0x940853,0xe4aaea,0xf46add,0xf8a2d6,
        0xe00af6,0x00f48d,0xd03957,0xe8d0fc,0xb41e52};
    assert(sizeof(prefixes)/sizeof(prefixes[0])==27);
    for(size_t p=0;p<27;++p){
        uint32_t prefix=prefixes[p];
        uint8_t address[]={0x12,0x34,0x56,(uint8_t)prefix,(uint8_t)(prefix>>8),(uint8_t)(prefix>>16)};
        for(unsigned type=0;type<256;++type){
            assert(cerberus_ble_flock_oui_hint(address,(uint8_t)type)==(type==0 || type==2));++checks;
        }
        for(unsigned suffix=0;suffix<256;++suffix){
            address[0]=(uint8_t)suffix;address[1]=(uint8_t)(suffix^0xa5);address[2]=(uint8_t)(255U-suffix);
            assert(cerberus_ble_flock_oui_hint(address,0));++checks;
        }
        for(unsigned flag=1;flag<=3;++flag){
            address[5]=(uint8_t)((prefix>>16)|flag);
            assert(!cerberus_ble_flock_oui_hint(address,0));
            assert(!cerberus_ble_flock_oui_hint(address,2));++checks;
        }
        for(unsigned bit=0;bit<24;++bit){
            uint32_t changed=prefix^(1U<<bit);bool expected=false;
            for(size_t q=0;q<27;++q)expected|=changed==prefixes[q];
            address[3]=(uint8_t)changed;address[4]=(uint8_t)(changed>>8);address[5]=(uint8_t)(changed>>16);
            assert(cerberus_ble_flock_oui_hint(address,0)==expected);++checks;
        }
    }
    const uint8_t display_order[]={0xb4,0x1e,0x52,0x12,0x34,0x56};
    assert(!cerberus_ble_flock_oui_hint(display_order,0));
    assert(!cerberus_ble_flock_oui_hint(NULL,0));checks+=2;
}

int main(void)
{
    assert(sizeof(cerberus_ble_flock_hints_t)==4);
    service_contexts();accessory_and_collision();names_and_manufacturers();
    validation_and_limits();address_clues();
    printf("Passive Flock evidence: %u fixture checks passed; exact per-service contexts, same-report levels, fullAD atomic validation, weak-name distinctions,27 public/global address clues;4-byte result.\n",checks);
    return 0;
}
