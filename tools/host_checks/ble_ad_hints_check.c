#include "ble_ad_hints.h"
#include "ble_findmy_hints.h"
#include "ble_flipper_hints.h"
#undef NDEBUG
#include <assert.h>
#include <stdio.h>

static unsigned checks;
static const uint16_t ids[] = {
    0xFD5F,0xFEB7,0xFEB8,0x01AB,0x058E,0x0D53,
    0xFD5A,0xFD69,0x004C,0x0006,0xFEF3,
};
static const uint8_t types[] = {0xFF,0x03,0x05,0x07,0x16,0x20,0x21};
static const size_t widths[] = {2,2,4,16,2,4,16};
static const uint8_t base_le[] = {
    0xFB,0x34,0x9B,0x5F,0x80,0,0,0x80,0,0x10,0,0,0,0,0,0,
};
typedef struct { unsigned id, context; } pair_t;

static size_t field(uint8_t *out, uint8_t type, const uint8_t *value, size_t length)
{
    assert(length <= 254U);
    out[0]=(uint8_t)(length+1U);out[1]=type;
    if(length)memcpy(out+2U,value,length);
    return length+2U;
}
static void uuid(uint8_t *out, size_t width, uint16_t id)
{
    assert(width==2U||width==4U||width==16U);
    memset(out,0,width);
    size_t offset=0U;
    if(width==16U){memcpy(out,base_le,16U);offset=12U;}
    out[offset]=(uint8_t)id;out[offset+1U]=(uint8_t)(id>>8U);
}
static size_t one(uint8_t *out, unsigned id, unsigned context)
{
    uint8_t value[16];assert(id<11U&&context<7U);
    uuid(value,widths[context],ids[id]);
    return field(out,types[context],value,widths[context]);
}

/* Expected pair construction is independent of production add/has/enumerate;
 * verify exact bytes, every helper result, compatibility flags, immutable
 * source, output canaries, and the common structural-validation contract. */
static cerberus_ble_ad_hints_t expect(const uint8_t *data,size_t length,
    bool valid,const pair_t *pairs,size_t count)
{
    uint8_t copy[256],want[10]={0};
    if(data&&length<=sizeof(copy))memcpy(copy,data,length);
    bool allowed=false,blocked=false;
    for(size_t i=0;i<count;++i){
        assert(pairs[i].id<11U&&pairs[i].context<7U);
        unsigned bit=pairs[i].id*7U+pairs[i].context;
        want[bit/8U]|=(uint8_t)(1U<<(bit%8U));
        allowed|=pairs[i].id<6U;blocked|=pairs[i].id>=6U;
    }
    struct {uint32_t before;cerberus_ble_ad_hints_t value;uint32_t after;} guarded;
    memset(&guarded,0xA5,sizeof(guarded));
    guarded.before=0xABCDEF09U;guarded.after=0x76543210U;
    assert(cerberus_ble_ad_hints_parse(data,length,&guarded.value)==valid);
    assert(guarded.before==0xABCDEF09U&&guarded.after==0x76543210U);
    const cerberus_ble_ad_hints_t *out=&guarded.value;
    assert(!memcmp(out->meta.pairs,want,sizeof(want)));
    assert(out->meta_id==allowed&&out->blocked_id==blocked);
    assert(cerberus_ble_ad_meta_hint(out)==(allowed&&!blocked));
    assert(cerberus_ble_meta_evidence_allowed(&out->meta)==allowed);
    assert(cerberus_ble_meta_evidence_blocked(&out->meta)==blocked);
    size_t found=0;
    for(unsigned id=0;id<11U;++id)for(unsigned context=0;context<7U;++context){
        unsigned bit=id*7U+context;
        bool wanted=(want[bit/8U]&(uint8_t)(1U<<(bit%8U)))!=0U;
        assert(cerberus_ble_meta_evidence_has(&out->meta,id,
            (cerberus_ble_meta_context_t)context)==wanted);
        if(!wanted)continue;
        uint16_t actual_id=0;cerberus_ble_meta_context_t actual_context=CERBERUS_BLE_META_MFG16;
        assert(cerberus_ble_meta_evidence_at(&out->meta,found,&actual_id,&actual_context));
        assert(actual_id==ids[id]&&(unsigned)actual_context==context);++found;
    }
    assert(cerberus_ble_meta_evidence_count(&out->meta)==found);
    uint16_t unchanged_id=0xABCD;cerberus_ble_meta_context_t unchanged_context=CERBERUS_BLE_META_DATA128;
    assert(!cerberus_ble_meta_evidence_at(&out->meta,found,&unchanged_id,&unchanged_context));
    assert(unchanged_id==0xABCD&&unchanged_context==CERBERUS_BLE_META_DATA128);
    if(!valid){cerberus_ble_ad_hints_t zero={0};assert(!memcmp(out,&zero,sizeof(zero)));}
    cerberus_ble_findmy_hints_t findmy;cerberus_ble_flipper_hints_t flipper;
    assert(cerberus_ble_findmy_hints_parse(data,length,&findmy)==valid);
    assert(cerberus_ble_flipper_hints_parse(data,length,&flipper)==valid);
    if(data&&length<=sizeof(copy))assert(!memcmp(copy,data,length));
    ++checks;return *out;
}

static void identifiers_and_contexts(void)
{
    uint8_t data[256],value[64],encoded[16],name[]={'H','C','-','0','5'};
    for(unsigned id=0;id<11U;++id)for(unsigned context=0;context<7U;++context){
        pair_t expected={id,context};size_t width=widths[context];
        size_t length=one(data,id,context);
        expect(data,length,true,&expected,1);
        for(size_t cut=1U;cut<length;++cut)expect(data,cut,false,NULL,0);
        length+=field(data+length,9,name,sizeof(name));
        cerberus_ble_ad_hints_t parsed=expect(data,length,true,&expected,1);
        assert(cerberus_ble_ad_hc_name_hint(&parsed));
        length=field(data,9,name,sizeof(name));length+=one(data+length,id,context);
        parsed=expect(data,length,true,&expected,1);assert(cerberus_ble_ad_hc_name_hint(&parsed));
        uuid(encoded,width,ids[id]);
        if(context>=1U&&context<=3U){
            for(unsigned incomplete=0;incomplete<2U;++incomplete)
                for(unsigned position=0;position<3U;++position){
                    memset(value,0x5A,sizeof(value));memcpy(value+position*width,encoded,width);
                    length=field(data,(uint8_t)(types[context]-incomplete),value,3U*width);
                    expect(data,length,true,&expected,1);
                }
        }else{
            /* Following payload must not become a second identifier. */
            memcpy(value,encoded,width);uuid(value+width,2U,ids[(id+1U)%11U]);
            length=field(data,types[context],value,width+2U);
            expect(data,length,true,&expected,1);
        }
        length=one(data,id,context);length+=one(data+length,id,context);
        expect(data,length,true,&expected,1); /* One repeated exact pair. */
    }
    /* Contexts belong to exact IDs, never to a report-wide width mask. */
    pair_t mixed[]={{0,0},{1,3},{0,5},{10,6}};
    size_t length=0;
    for(size_t i=0;i<4U;++i)length+=one(data+length,mixed[i].id,mixed[i].context);
    expect(data,length,true,mixed,4);
    for(unsigned context=0;context<7U;++context){
        pair_t all[11];length=0;
        for(unsigned id=0;id<11U;++id){all[id]=(pair_t){id,context};length+=one(data+length,id,context);}
        assert(length<=255U);expect(data,length,true,all,11);
    }
}

static void blocked_precedence(void)
{
    uint8_t data[64],reverse[64];
    for(unsigned allowed=0;allowed<6U;++allowed)
        for(unsigned blocked=6U;blocked<11U;++blocked)
            for(unsigned a_context=0;a_context<7U;++a_context)
                for(unsigned b_context=0;b_context<7U;++b_context){
                    pair_t both[]={{allowed,a_context},{blocked,b_context}};
                    size_t a=one(data,allowed,a_context);
                    size_t b=one(data+a,blocked,b_context);
                    memcpy(reverse,data+a,b);memcpy(reverse+b,data,a);
                    expect(data,a+b,true,both,2);expect(reverse,a+b,true,both,2);
                }
}

static void false_positive_bounds(void)
{
    uint8_t data[256],value[96],encoded[16];
    for(unsigned id=0;id<11U;++id)for(unsigned context=0;context<7U;++context){
        size_t width=widths[context];uuid(encoded,width,ids[id]);
        if(width>2U){
            for(size_t byte=0;byte<width;++byte){
                /* Preserve the low ID but mutate every base/upper byte. */
                if(width==4U&&byte<2U)continue;
                if(width==16U&&(byte==12U||byte==13U))continue;
                memcpy(value,encoded,width);value[byte]^=0x80U;
                expect(data,field(data,types[context],value,width),true,NULL,0);
            }
            for(size_t i=0;i<width;++i)value[i]=encoded[width-1U-i];
            expect(data,field(data,types[context],value,width),true,NULL,0);
        }
        if(context==0U||context>=4U){
            uuid(value,width,0x1234);memcpy(value+width,encoded,width);
            expect(data,field(data,types[context],value,width*2U),true,NULL,0);
        }
        expect(data,field(data,0x09,encoded,width),true,NULL,0);
        expect(data,field(data,0x30,encoded,width),true,NULL,0);
        memset(value,0x55,16U);memcpy(value+2U,encoded,2U);
        expect(data,field(data,0x07,value,16U),true,NULL,0);
    }
    const uint16_t unknown[]={0,0xFD5E,0xFD60,0xFEB6,0xFEB9,0x01AA,0x01AC,0x058D,0x058F,0x0D52,0x0D54,0xFFFF};
    for(size_t id=0;id<sizeof(unknown)/sizeof(unknown[0]);++id)
        for(unsigned context=0;context<7U;++context){
            uuid(value,widths[context],unknown[id]);
            expect(data,field(data,types[context],value,widths[context]),true,NULL,0);
        }
}

static void malformed_and_boundaries(void)
{
    uint8_t data[300],bad[40],value[254]={0};pair_t known={0,0};
    const uint8_t known_types[]={0x01,0x02,0x03,0x04,0x05,0x06,0x07,0xFF,0x16,0x20,0x21};
    for(size_t t=0;t<sizeof(known_types);++t)for(size_t size=0;size<34U;++size){
        uint8_t type=known_types[t];bool valid;
        if(type==1U)valid=size==1U;
        else if(type>=2U&&type<=7U){size_t width=type<=3U?2U:type<=5U?4U:16U;valid=size%width==0U;}
        else valid=size>=(type==0x20?4U:type==0x21?16U:2U);
        size_t length=field(bad,type,value,size);
        expect(bad,length,valid,NULL,0);
        size_t prefix=one(data,0,0);memcpy(data+prefix,bad,length);
        expect(data,prefix+length,valid,valid?&known:NULL,valid?1U:0U);
        memcpy(data,bad,length);prefix=one(data+length,0,0);
        expect(data,length+prefix,valid,valid?&known:NULL,valid?1U:0U);
    }
    size_t length=one(data,0,0);
    data[length++]=3;data[length++]=0x30;data[length++]=0xAB;
    expect(data,length,false,NULL,0); /* Positive then truncated unknown field. */
    length=one(data,0,0);memset(data+length,0,255U-length);
    expect(data,255,true,&known,1);
    data[254]=1;expect(data,255,false,NULL,0);
    length=one(data,0,0);length+=field(data+length,0x30,value,249U);
    assert(length==255U);expect(data,length,true,&known,1);
    data[255]=0;expect(data,256,false,NULL,0);
    expect(NULL,0,true,NULL,0);expect(NULL,1,false,NULL,0);
    assert(!cerberus_ble_ad_hints_parse(data,length,NULL));
    const uint8_t empty[]={1,0x03,1,0x09};expect(empty,sizeof(empty),true,NULL,0);
    for(unsigned type=0;type<256U;++type){
        if(type==1U||(type>=2U&&type<=9U)||type==0xFFU||type==0x16U||type==0x20U||type==0x21U)continue;
        expect(data,field(data,(uint8_t)type,value,0U),true,NULL,0);
    }
}

static void name_compatibility(void)
{
    uint8_t data[256],name[]={'H','C','-','0','3'},long_name[40];
    for(unsigned digit=3U;digit<=6U;++digit){
        name[4]=(uint8_t)('0'+digit);size_t length=field(data,9,name,sizeof(name));
        cerberus_ble_ad_hints_t out=expect(data,length,true,NULL,0);
        assert(cerberus_ble_ad_hc_name_hint(&out)==(digit!=4U));
        assert(out.complete_name&&!out.name_truncated&&out.name_length==5U&&!memcmp(out.name,name,5U));
        data[1]=8;out=expect(data,length,true,NULL,0);assert(!cerberus_ble_ad_hc_name_hint(&out));
        data[1]=9;length+=field(data+length,9,name,sizeof(name));
        out=expect(data,length,true,NULL,0);assert(cerberus_ble_ad_hc_name_hint(&out)==(digit!=4U));
        data[length-1U]^=1U;expect(data,length,false,NULL,0);
    }
    name[4]='5';const uint8_t short_name[]={'X','Y'};
    size_t length=field(data,8,short_name,sizeof(short_name));length+=field(data+length,9,name,5U);
    cerberus_ble_ad_hints_t out=expect(data,length,true,NULL,0);assert(cerberus_ble_ad_hc_name_hint(&out));
    length=field(data,9,name,5U);length+=field(data+length,8,short_name,sizeof(short_name));
    out=expect(data,length,true,NULL,0);assert(cerberus_ble_ad_hc_name_hint(&out));
    /* Empty complete-name fields supply no name and do not contradict a
     * nonempty complete name: the existing shared policy is preserved. */
    length=field(data,9,NULL,0);length+=field(data+length,9,name,5U);length+=field(data+length,9,NULL,0);
    out=expect(data,length,true,NULL,0);assert(cerberus_ble_ad_hc_name_hint(&out));
    const uint8_t non_hc[][6]={{'h','C','-','0','5',0},{'H','C','-','0','7',0},
        {'H','C','-','0','5','X'},{'H','C','-','0',0,0},{'H','C','-','0',0xFF,0}};
    for(size_t i=0;i<sizeof(non_hc)/sizeof(non_hc[0]);++i){
        out=expect(data,field(data,9,non_hc[i],i==2U?6U:5U),true,NULL,0);
        assert(!cerberus_ble_ad_hc_name_hint(&out));
    }
    memset(long_name,'A',sizeof(long_name));memcpy(long_name,name,5U);
    for(size_t size=32U;size<=33U;++size){
        length=field(data,9,long_name,size);out=expect(data,length,true,NULL,0);
        assert(out.name_length==32U&&out.complete_name&&out.name_truncated==(size>32U));
        assert(!cerberus_ble_ad_hc_name_hint(&out));
        length+=field(data+length,9,long_name,size);expect(data,length,true,NULL,0);
        data[length-1U]='B';expect(data,length,false,NULL,0);
    }
    length=one(data,0,0);length+=field(data+length,9,name,5U);data[length++]=1;data[length++]=1;
    expect(data,length,false,NULL,0); /* Clears names and IDs, not just flags. */
    assert(!cerberus_ble_ad_meta_hint(NULL)&&!cerberus_ble_ad_hc_name_hint(NULL));
}

static void exact_hc_codes(void)
{
    const uint8_t names[][5]={{'H','C','-','0','3'},{'H','C','-','0','5'},{'H','C','-','0','6'}};
    const uint8_t codes[]={3,5,6};
    uint8_t data[256],padding[246],mutated[5];
    memset(padding,0x55,sizeof(padding));
    for(size_t i=0;i<3U;++i){
        size_t length=field(data,9,names[i],5U);
        cerberus_ble_ad_hints_t out=expect(data,length,true,NULL,0);
        assert(cerberus_ble_ad_hc_name_code(&out)==codes[i]&&cerberus_ble_ad_hc_name_hint(&out));
        assert(!memcmp(cerberus_ble_hc_name_label(codes[i]),names[i],5U));
        assert(cerberus_ble_hc_name_label(codes[i])[5]=='\0');
        data[1]=8;out=expect(data,length,true,NULL,0);
        assert(cerberus_ble_ad_hc_name_code(&out)==0U&&!cerberus_ble_ad_hc_name_hint(&out));
        for(size_t byte=0;byte<5U;++byte){
            memcpy(mutated,names[i],5U);mutated[byte]=0;
            out=expect(data,field(data,9,mutated,5U),true,NULL,0);
            assert(cerberus_ble_ad_hc_name_code(&out)==0U&&!cerberus_ble_ad_hc_name_hint(&out));
        }
        for(size_t j=0;j<3U;++j){
            length=field(data,9,names[i],5U);length+=field(data+length,9,names[j],5U);
            out=expect(data,length,i==j,NULL,0);
            assert(cerberus_ble_ad_hc_name_code(&out)==(i==j?codes[i]:0U));
            assert(cerberus_ble_ad_hc_name_hint(&out)==(i==j));
        }
        /* The complete255-byte container still admits the exact final name. */
        length=field(data,0x7e,padding,sizeof(padding));length+=field(data+length,9,names[i],5U);
        assert(length==255U);out=expect(data,length,true,NULL,0);
        assert(cerberus_ble_ad_hc_name_code(&out)==codes[i]);
        data[255]=0;out=expect(data,256U,false,NULL,0);assert(!cerberus_ble_ad_hc_name_code(&out));
        length=field(data,9,names[i],5U);
        for(size_t cut=0;cut<length;++cut){
            out=expect(data,cut,cut==0U,NULL,0);assert(!cerberus_ble_ad_hc_name_code(&out));
        }
        data[length++]=1;data[length++]=1; /* Malformed Flags after a positive name. */
        out=expect(data,length,false,NULL,0);assert(!cerberus_ble_ad_hc_name_code(&out));
        const uint8_t wrong_types[]={0xff,0x16,0x7e};
        for(size_t kind=0;kind<sizeof(wrong_types);++kind){
            out=expect(data,field(data,wrong_types[kind],names[i],5U),true,NULL,0);
            assert(!cerberus_ble_ad_hc_name_code(&out));
        }
        out=expect(data,field(data,9,names[i],5U),true,NULL,0);
        out.complete_name=false;assert(!cerberus_ble_ad_hc_name_code(&out));
        out.complete_name=true;out.name_truncated=true;assert(!cerberus_ble_ad_hc_name_code(&out));
        out.name_truncated=false;out.name_length=6U;assert(!cerberus_ble_ad_hc_name_code(&out));
    }
    /* Label input is a bounded code, never an unchecked table index. */
    for(unsigned code=0U;code<=255U;++code){
        const char *want=code==0U?"NONE":code==3U?"HC-03":code==5U?"HC-05":code==6U?"HC-06":"UNKNOWN";
        assert(strcmp(cerberus_ble_hc_name_label((uint8_t)code),want)==0);++checks;
    }
    assert(cerberus_ble_ad_hc_name_code(NULL)==CERBERUS_BLE_HC_NONE);
}

static void bounded_matrix_helpers(void)
{
    _Static_assert(sizeof(cerberus_ble_meta_evidence_t)==10U,"Meta pairs must stay compact");
    cerberus_ble_meta_evidence_t evidence={0};
    for(unsigned id=0;id<11U;++id){
        assert(cerberus_ble_meta_id_value(id)==ids[id]);
        for(unsigned context=0;context<7U;++context)
            assert(cerberus_ble_meta_evidence_add(&evidence,ids[id],(cerberus_ble_meta_context_t)context));
    }
    assert(cerberus_ble_meta_evidence_count(&evidence)==77U);
    for(size_t byte=0;byte<9U;++byte)assert(evidence.pairs[byte]==0xFFU);
    assert(evidence.pairs[9]==0x1FU);
    assert(cerberus_ble_meta_evidence_allowed(&evidence)&&cerberus_ble_meta_evidence_blocked(&evidence));
    for(unsigned ordinal=0;ordinal<77U;++ordinal){
        uint16_t id=0;cerberus_ble_meta_context_t context;
        assert(cerberus_ble_meta_evidence_at(&evidence,ordinal,&id,&context));
        assert(id==ids[ordinal/7U]&&(unsigned)context==ordinal%7U);
    }
    cerberus_ble_meta_evidence_t before=evidence;
    assert(!cerberus_ble_meta_evidence_add(&evidence,0x1234,CERBERUS_BLE_META_MFG16));
    assert(!cerberus_ble_meta_evidence_add(&evidence,ids[0],(cerberus_ble_meta_context_t)-1));
    assert(!cerberus_ble_meta_evidence_add(&evidence,ids[0],(cerberus_ble_meta_context_t)7));
    assert(!memcmp(&before,&evidence,sizeof(before)));
    assert(!cerberus_ble_meta_evidence_has(&evidence,11U,CERBERUS_BLE_META_MFG16));
    assert(!cerberus_ble_meta_evidence_has(&evidence,SIZE_MAX,CERBERUS_BLE_META_MFG16));
    assert(!cerberus_ble_meta_evidence_has(&evidence,0U,(cerberus_ble_meta_context_t)-1));
    assert(cerberus_ble_meta_id_value(11U)==0U&&cerberus_ble_meta_id_value(SIZE_MAX)==0U);
    uint16_t id=0xABCD;cerberus_ble_meta_context_t context=CERBERUS_BLE_META_LIST32;
    assert(!cerberus_ble_meta_evidence_at(&evidence,77U,&id,&context));
    assert(!cerberus_ble_meta_evidence_at(&evidence,SIZE_MAX,&id,&context));
    assert(!cerberus_ble_meta_evidence_at(&evidence,0U,NULL,&context));
    assert(!cerberus_ble_meta_evidence_at(&evidence,0U,&id,NULL));
    assert(!cerberus_ble_meta_evidence_at(NULL,0U,&id,&context));
    assert(id==0xABCD&&context==CERBERUS_BLE_META_LIST32);
    assert(!cerberus_ble_meta_evidence_add(NULL,ids[0],CERBERUS_BLE_META_MFG16));
    assert(!cerberus_ble_meta_evidence_has(NULL,0,CERBERUS_BLE_META_MFG16));
    assert(!cerberus_ble_meta_evidence_count(NULL));
    assert(!cerberus_ble_meta_evidence_allowed(NULL)&&!cerberus_ble_meta_evidence_blocked(NULL));
    memset(&evidence,0,sizeof(evidence));evidence.pairs[9]=0xE0U;
    assert(!cerberus_ble_meta_evidence_count(&evidence));
    assert(!cerberus_ble_meta_evidence_allowed(&evidence)&&!cerberus_ble_meta_evidence_blocked(&evidence));
    assert(!cerberus_ble_meta_evidence_at(&evidence,0U,&id,&context));
    ++checks;
}

int main(void)
{
    identifiers_and_contexts();blocked_precedence();false_positive_bounds();
    malformed_and_boundaries();name_compatibility();exact_hc_codes();bounded_matrix_helpers();
    printf("BLE AD hints: %u fixture checks passed; exact 11-ID/7-context evidence, full-report atomic validation, blocked precedence, strict UUID widths and exact HC03/05/06 codes with unchanged bool matching; %zu-byte matrix\n",checks,sizeof(cerberus_ble_meta_evidence_t));
    return 0;
}
