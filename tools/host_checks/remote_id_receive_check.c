#undef NDEBUG
#include "cerberus_watch_capture.h"
#include "s3_ble_snapshot.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static bool all_zero(const void *data,size_t size)
{const uint8_t *p=data;for(size_t i=0;i<size;++i)if(p[i])return false;return true;}
static void put16(uint8_t *p,uint16_t value){p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,int32_t value)
{uint32_t u=(uint32_t)value;for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(u>>(8*i));}
static void basic(uint8_t *message,const char *id)
{
    memset(message,0,25);message[0]=0x02;message[1]=0x10;
    CHECK(strlen(id)<=20);memcpy(message+2,id,strlen(id));
}
static void location(uint8_t *message)
{
    memset(message,0,25);message[0]=0x12;
    put32(message+5,123456789);put32(message+9,-987654321);
    put16(message+13,2050);put16(message+15,2030);put16(message+21,35999);
}
static size_t make_pack(uint8_t *out,const char *id,bool coordinates)
{
    out[0]=0xf2;out[1]=25;out[2]=coordinates?2:1;basic(out+3,id);
    if(coordinates)location(out+28);
    return coordinates?53:28;
}
static size_t ble(uint8_t *out,const uint8_t *payload,size_t size)
{
    CHECK(size+5<=255);out[0]=(uint8_t)(size+5);out[1]=0x16;
    out[2]=0xfa;out[3]=0xff;out[4]=0x0d;out[5]=7;
    memcpy(out+6,payload,size);return size+6;
}
static size_t beacon(uint8_t *out,const uint8_t *payload,size_t size)
{
    CHECK(size+5<=255);memset(out,0,2400);out[0]=0x80;memset(out+4,255,6);
    const uint8_t id[6]={2,4,6,8,10,12};memcpy(out+10,id,6);memcpy(out+16,id,6);
    out[32]=100;out[34]=1;out[36]=0;out[37]=0;
    out[38]=221;out[39]=(uint8_t)(size+5);out[40]=0xfa;out[41]=0x0b;out[42]=0xbc;out[43]=0x0d;out[44]=7;
    memcpy(out+45,payload,size);return size+45;
}
typedef struct {
    size_t bitmap,matching_length,matching,response_length,response,info_length,counter,pack;
} nan_wire_layout_t;
/* Construct fields from the wire layout independently of parser cursor logic.
 * Optional filter bytes are deliberately opaque and can contain fake headers. */
static size_t nan_variable(uint8_t *out,const uint8_t *payload,size_t size,
    uint8_t control,uint8_t instance,uint8_t requestor,size_t matching,size_t response,
    nan_wire_layout_t *layout)
{
    CHECK(matching<=255 && response<=255 && size<255);
    memset(out,0,2400);out[0]=0xd0;
    const uint8_t destination[6]={0x51,0x6f,0x9a,1,0,0};memcpy(out+4,destination,6);
    const uint8_t id[6]={2,4,6,8,10,12};memcpy(out+10,id,6);memcpy(out+16,id,6);
    const uint8_t header[]={4,9,0x50,0x6f,0x9a,0x13};memcpy(out+24,header,6);
    out[30]=3;
    const uint8_t service[]={0x88,0x69,0x19,0x9d,0x92,9};memcpy(out+33,service,6);
    out[39]=instance;out[40]=requestor;out[41]=control;
    nan_wire_layout_t positions={0};size_t end=42;
    if(control&0x40){positions.bitmap=end;out[end++]=0xa5;out[end++]=0x5a;}
    if(control&0x04){positions.matching_length=end;out[end++]=(uint8_t)matching;
        positions.matching=end;memset(out+end,0x96,matching);end+=matching;}
    if(control&0x08){positions.response_length=end;out[end++]=(uint8_t)response;
        positions.response=end;memset(out+end,0x69,response);end+=response;}
    positions.info_length=end;out[end++]=(uint8_t)(size+1);positions.counter=end;out[end++]=7;
    positions.pack=end;memcpy(out+end,payload,size);end+=size;
    put16(out+31,(uint16_t)(end-33));if(layout)*layout=positions;return end;
}
static size_t nan(uint8_t *out,const uint8_t *payload,size_t size)
{return nan_variable(out,payload,size,0x10,1,0,0,0,NULL);}
static void rejected(bool wifi,const uint8_t *bytes,size_t length)
{
    sentinel_watch_protocol_record_t result;memset(&result,0xa5,sizeof(result));
    CHECK(!(wifi?sentinel_watch_protocol_decode_wifi(bytes,length,&result):sentinel_watch_protocol_decode_ble(bytes,length,&result)));
    CHECK(all_zero(&result,sizeof(result)));
}
static void expect_claim(const sentinel_watch_protocol_record_t *r,unsigned transport,bool coordinates)
{
    CHECK(r->kind==SENTINEL_WATCH_PROTOCOL_DRONE && r->transport==transport);
    CHECK(!strcmp(r->id,"TEST-A") && (r->flags&SENTINEL_WATCH_PROTOCOL_HAS_ID));
    if(coordinates) {
        CHECK(r->flags==7 && r->latitude_e7==123456789 && r->longitude_e7==-987654321);
        CHECK(r->altitude_dm==150 && r->altitude_kind==SENTINEL_WATCH_ALTITUDE_GEODETIC);
    } else CHECK(r->flags==1 && !r->latitude_e7 && !r->longitude_e7 && !r->altitude_dm && !r->altitude_kind);
}
static void parser_vectors(void)
{
    uint8_t payload[256],bytes[2400];sentinel_watch_protocol_record_t result;
    basic(payload,"TEST-A");size_t length=ble(bytes,payload,25);CHECK(length==31);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));expect_claim(&result,1,false);
    for(size_t cut=0;cut<length;++cut)rejected(false,bytes,cut);
    size_t packed=make_pack(payload,"TEST-A",true);length=ble(bytes,payload,packed);CHECK(length==59);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));expect_claim(&result,1,true);
    for(size_t cut=0;cut<length;++cut)rejected(false,bytes,cut);
    /* Full extended container, with unrelated complete data after the claim. */
    bytes[length]=195;bytes[length+1]=0xfe;memset(bytes+length+2,0x11,194);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,255,&result));expect_claim(&result,1,true);
    rejected(false,bytes,254);rejected(false,bytes,256);
    length=ble(bytes,payload,packed);bytes[length]=0;bytes[length+1]=0;
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length+2,&result));
    bytes[length+1]=1;rejected(false,bytes,length+2);
    const uint8_t malformed[][4]={{1,1,0,0},{2,2,0,0},{3,4,0,0},{2,6,0,0},{2,0xff,0,0},{2,0x16,0,0},{3,0x20,0,0},{3,0x21,0,0}};
    for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);++i){
        length=ble(bytes,payload,packed);size_t tail=(size_t)malformed[i][0]+1;
        memcpy(bytes+length,malformed[i],tail);rejected(false,bytes,length+tail);
    }
    length=ble(bytes,payload,packed);memcpy(bytes+length,bytes,length);rejected(false,bytes,2*length);
    /* Right UUID in a wrong AD context does not claim Remote ID. */
    length=ble(bytes,payload,packed);bytes[1]=0xff;rejected(false,bytes,length);
    length=ble(bytes,payload,packed);bytes[4]=0x0e;rejected(false,bytes,length);
    length=ble(bytes,payload,packed);bytes[2]^=1;rejected(false,bytes,length);
    for(unsigned count=1;count<=9;++count){
        payload[0]=0xf2;payload[1]=25;payload[2]=(uint8_t)count;
        for(unsigned i=0;i<count;++i)basic(payload+3+25*i,"TEST-A");
        length=ble(bytes,payload,3+25*count);
        CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));expect_claim(&result,1,false);
    }
    packed=make_pack(payload,"TEST-A",true);
    for(unsigned form=0;form<2;++form){
        length=form?nan(bytes,payload,packed):beacon(bytes,payload,packed);
        CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,form?3:2,true);
        for(size_t cut=0;cut<length;++cut)rejected(true,bytes,cut);
        bytes[length]=99;rejected(true,bytes,length+1);
        if(form){bytes[length+1]=0;bytes[length+2]=0;CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+3,&result));}
        else {bytes[length+1]=0;CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+2,&result));}
        for(unsigned bit=0;bit<8;++bit)if((1U<<bit)&0xc7U){
            bytes[1]=(uint8_t)(1U<<bit);rejected(true,bytes,length);
        }
        bytes[1]=0;bytes[22]=1;rejected(true,bytes,length);
    }
    length=beacon(bytes,payload,packed);bytes[40]=0x90;bytes[41]=0x3a;bytes[42]=0xe6;
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,2,true);
    length=beacon(bytes,payload,packed);memcpy(bytes+length,bytes+38,length-38);rejected(true,bytes,2*length-38);
    length=nan(bytes,payload,packed);bytes[41]|=1;CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));
    length=nan(bytes,payload,packed);bytes[39]=2;CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));
    length=nan(bytes,payload,packed);bytes[42]--;rejected(true,bytes,length);
    length=nan(bytes,payload,packed);bytes[33]^=1;rejected(true,bytes,length);
    length=nan(bytes,payload,packed);memcpy(bytes+length,bytes+30,length-30);rejected(true,bytes,2*length-30);
    for(unsigned form=0;form<2;++form){
        length=form?nan(bytes,payload,packed):beacon(bytes,payload,packed);
        if(form){bytes[length]=99;put16(bytes+length+1,(uint16_t)(2304-length-3));memset(bytes+length+3,0,2304-length-3);}
        else {
            while(length+257<=2304){bytes[length]=99;bytes[length+1]=255;memset(bytes+length+2,0,255);length+=257;}
            size_t tail=2304-length;CHECK(tail>=2 && tail<=257);
            bytes[length]=99;bytes[length+1]=(uint8_t)(tail-2);memset(bytes+length+2,0,tail-2);
        }
        CHECK(sentinel_watch_protocol_decode_wifi(bytes,2304,&result));expect_claim(&result,form?3:2,true);
        rejected(true,bytes,2305);
    }
    /* Message interpretation boundaries and transactional failure after ID. */
    for(unsigned defect=0;defect<8;++defect){
        packed=make_pack(payload,"TEST-A",true);
        if(defect==0)put32(payload+28+5,900000001);
        if(defect==1)put32(payload+28+9,-1800000001);
        if(defect==2)payload[28+24]=1;
        if(defect==3)payload[28]=0x11; /* pack/message versions disagree */
        if(defect==4)payload[28+1]=0x50;
        if(defect==5)payload[28+2]=182;
        if(defect==6)put16(payload+28+21,36001);
        if(defect==7)payload[3+23]=1;
        length=ble(bytes,payload,packed);rejected(false,bytes,length);
    }
    packed=make_pack(payload,"TEST-A",true);payload[2]=3;memcpy(payload+53,payload+28,25);
    length=ble(bytes,payload,78);rejected(false,bytes,length); /* duplicate Location */
    location(payload);put16(payload+15,0);length=ble(bytes,payload,25);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));
    CHECK(result.altitude_kind==SENTINEL_WATCH_ALTITUDE_PRESSURE && result.altitude_dm==250);
    put16(payload+13,0);put32(payload+5,0);put32(payload+9,0);length=ble(bytes,payload,25);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));CHECK(result.flags==0);
    /* System location is not aircraft location, and never becomes operator UI data. */
    memset(payload,0,25);payload[0]=0x42;put32(payload+2,456789123);put32(payload+6,-123456789);
    length=ble(bytes,payload,25);CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));
    CHECK(result.flags==0 && !result.latitude_e7 && !result.longitude_e7);
    basic(payload,"12345678901234567890");length=ble(bytes,payload,25);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));CHECK(strlen(result.id)==20);
    basic(payload,"TEST-A");payload[2]=31;length=ble(bytes,payload,25);rejected(false,bytes,length);
    basic(payload,"TEST-A");payload[12]='x';length=ble(bytes,payload,25);rejected(false,bytes,length);
    basic(payload,"TEST-A");payload[1]=0x30;length=ble(bytes,payload,25);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&result));CHECK(result.flags==0 && !result.id[0]);
    rejected(false,NULL,31);rejected(true,NULL,36);
    CHECK(!sentinel_watch_protocol_decode_ble(bytes,length,NULL));CHECK(!sentinel_watch_protocol_decode_wifi(bytes,length,NULL));
}

static void variable_wifi_vectors(void)
{
    uint8_t payload[256],bytes[2400],other[2400],saved[2400];
    sentinel_watch_protocol_record_t result;nan_wire_layout_t offsets;
    size_t packed=make_pack(payload,"TEST-A",true),length;
    /* All presence combinations, each type and range flag; a present empty
     * filter differs from an absent field. 255 is the actual uint8 boundary. */
    const int sizes[]={-1,0,1,255};
    for(unsigned type=0;type<3;++type)for(unsigned range=0;range<2;++range)
    for(unsigned bitmap=0;bitmap<2;++bitmap)for(unsigned m=0;m<4;++m)for(unsigned r=0;r<4;++r){
        uint8_t control=(uint8_t)(0x10|type|(range?0x20:0)|(bitmap?0x40:0)|
            (sizes[m]>=0?0x04:0)|(sizes[r]>=0?0x08:0));
        length=nan_variable(bytes,payload,packed,control,0xfe,0xad,
            sizes[m]<0?0:(size_t)sizes[m],sizes[r]<0?0:(size_t)sizes[r],&offsets);
        CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,3,true);
        CHECK(length==44+packed+(bitmap?2:0)+(sizes[m]<0?0:1+(size_t)sizes[m])+
            (sizes[r]<0?0:1+(size_t)sizes[r]));
        for(size_t cut=0;cut<length;++cut)rejected(true,bytes,cut);
        /* Repaired outer lengths force validation of the inner cursor too. */
        memcpy(saved,bytes,length);
        for(size_t cut=33;cut<length;++cut){put16(bytes+31,(uint16_t)(cut-33));rejected(true,bytes,cut);}
        memcpy(bytes,saved,length);
        bytes[offsets.info_length]--;rejected(true,bytes,length);bytes[offsets.info_length]++;
        bytes[offsets.info_length]++;rejected(true,bytes,length);bytes[offsets.info_length]--;
        bytes[length]=99;rejected(true,bytes,length+1);
        bytes[length+1]=0;rejected(true,bytes,length+2);
        bytes[length+1]=1;bytes[length+2]=0;rejected(true,bytes,length+3);
        bytes[length+1]=0;CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+3,&result));
    }
    /* Arbitrary instance/requestor/counter bytes are metadata, not signatures. */
    for(unsigned value=0;value<=255;++value){
        length=nan_variable(bytes,payload,packed,0x72,(uint8_t)value,(uint8_t)(255-value),0,0,&offsets);
        bytes[offsets.counter]=(uint8_t)value;
        CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,3,true);
    }
    /* Canonical opendroneid-core-c wifi.c 849c03a emits Publish+info SDA,
     * followed by SDEA (instance1/control0x0200/update counter). This literal
     * wire prefix is independent of the variable descriptor constructor. */
    const uint8_t canonical_prefix[]={
        0xd0,0,0,0,0x51,0x6f,0x9a,1,0,0,2,4,6,8,10,12,2,4,6,8,10,12,0,0,
        4,9,0x50,0x6f,0x9a,0x13,3,64,0,0x88,0x69,0x19,0x9d,0x92,9,1,0,0x10,54,7};
    const uint8_t canonical_sdea[]={0x0e,0x04,0x00,0x01,0x00,0x02,7};
    CHECK(sizeof(canonical_prefix)==44 && packed==53);
    memcpy(bytes,canonical_prefix,sizeof(canonical_prefix));memcpy(bytes+44,payload,packed);
    length=44+packed;memcpy(bytes+length,canonical_sdea,sizeof(canonical_sdea));
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+sizeof(canonical_sdea),&result));expect_claim(&result,3,true);
    for(size_t cut=1;cut<sizeof(canonical_sdea);++cut)rejected(true,bytes,length+cut);
    bytes[length+1]=5;rejected(true,bytes,length+sizeof(canonical_sdea));
    bytes[length+1]=4;bytes[41]=0x12; /* true Follow-up remains a separate accepted variant */
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+sizeof(canonical_sdea),&result));expect_claim(&result,3,true);
    for(unsigned control=0;control<=255;++control){
        length=nan_variable(bytes,payload,packed,(uint8_t)control,1,0,1,1,NULL);
        if((control&3)<3 && !(control&0x80) && (control&0x10)){
            CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,3,true);
        }else rejected(true,bytes,length);
    }
    length=nan(bytes,payload,packed);
    for(unsigned i=4;i<10;++i){bytes[i]^=1;rejected(true,bytes,length);bytes[i]^=1;}
    memset(bytes+4,0,6);rejected(true,bytes,length);
    length=nan(bytes,payload,packed);memset(bytes+4,255,6);rejected(true,bytes,length);
    length=nan(bytes,payload,packed);
    for(unsigned i=24;i<30;++i){bytes[i]^=1;rejected(true,bytes,length);bytes[i]^=1;}
    for(unsigned i=33;i<39;++i){bytes[i]^=1;rejected(true,bytes,length);bytes[i]^=1;}
    bytes[30]=14;rejected(true,bytes,length); /* SID in another attribute is no SDA. */
    /* Correct-looking bytes inside either opaque filter cannot supply data. */
    length=nan_variable(bytes,payload,packed,0x1c,1,0,64,64,&offsets);
    memcpy(bytes+offsets.matching,payload,packed);memcpy(bytes+offsets.response,payload,packed);
    bytes[offsets.pack]=0;rejected(true,bytes,length);
    bytes[offsets.pack]=0xf2;bytes[33]^=1;rejected(true,bytes,length);
    /* Oversized filter declarations cannot borrow a later attribute. */
    length=nan_variable(bytes,payload,packed,0x1c,1,0,1,1,&offsets);
    bytes[offsets.matching_length]=255;rejected(true,bytes,length);
    length=nan_variable(bytes,payload,packed,0x1c,1,0,1,1,&offsets);
    bytes[offsets.response_length]=255;rejected(true,bytes,length);
    const uint8_t wrong_info_sizes[]={0,1,28,29,53,55,254,255};
    for(unsigned i=0;i<sizeof(wrong_info_sizes);++i){
        length=nan_variable(bytes,payload,packed,0x10,1,0,0,0,&offsets);
        bytes[offsets.info_length]=wrong_info_sizes[i];rejected(true,bytes,length);
    }
    memset(payload+packed,0,sizeof(payload)-packed);
    length=nan_variable(bytes,payload,254,0x10,1,0,0,0,&offsets);
    CHECK(bytes[offsets.info_length]==255);rejected(true,bytes,length);
    /* Exact MessagePack framing on Wi-Fi, including min/max valid pack count. */
    for(unsigned count=1;count<=9;++count){
        payload[0]=0xf2;payload[1]=25;payload[2]=(uint8_t)count;
        for(unsigned i=0;i<count;++i)basic(payload+3+25*i,"TEST-A");
        for(unsigned form=0;form<3;++form){
            length=form==2?nan_variable(bytes,payload,3+25*count,0x7e,255,255,255,255,NULL):
                beacon(bytes,payload,3+25*count);
            if(form==1){bytes[40]=0x90;bytes[41]=0x3a;bytes[42]=0xe6;}
            CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));expect_claim(&result,form==2?3:2,false);
        }
    }
    payload[2]=10;basic(payload+3+25*9,"TEST-A");
    length=nan(bytes,payload,253);rejected(true,bytes,length);
    basic(payload,"TEST-A");
    for(unsigned form=0;form<3;++form){
        length=form==2?nan(bytes,payload,25):beacon(bytes,payload,25);
        if(form==1){bytes[40]=0x90;bytes[41]=0x3a;bytes[42]=0xe6;}
        rejected(true,bytes,length); /* bare Basic ID remains BLE-only */
    }
    packed=make_pack(payload,"TEST-A",true);
    for(unsigned defect=0;defect<7;++defect){
        length=nan_variable(bytes,payload,packed,0x7e,0,0,0,0,&offsets);
        if(defect==0)bytes[offsets.pack]=0xf3;
        if(defect==1)bytes[offsets.pack+1]=24;
        if(defect==2)bytes[offsets.pack+2]=0;
        if(defect==3)bytes[offsets.pack+2]=10;
        if(defect==4)bytes[offsets.pack+3]=0x01;
        if(defect==5)bytes[offsets.pack+28+24]=1;
        if(defect==6){bytes[length++]=0;put16(bytes+31,(uint16_t)(length-33));++bytes[offsets.info_length];}
        rejected(true,bytes,length);
    }
    /* Duplicate relevant descriptors reject atomically, including old/new OUI
     * mixtures and an invalid second candidate after a complete first claim. */
    for(unsigned first=0;first<2;++first)for(unsigned second=0;second<2;++second){
        length=beacon(bytes,payload,packed);size_t other_length=beacon(other,payload,packed);
        if(first){bytes[40]=0x90;bytes[41]=0x3a;bytes[42]=0xe6;}
        if(second){other[40]=0x90;other[41]=0x3a;other[42]=0xe6;}
        memcpy(bytes+length,other+38,other_length-38);rejected(true,bytes,length+other_length-38);
        bytes[length+7]=0;rejected(true,bytes,length+other_length-38);
    }
    length=nan_variable(bytes,payload,packed,0x7e,99,55,1,1,NULL);
    size_t other_length=nan(other,payload,packed);
    memcpy(bytes+length,other+30,other_length-30);rejected(true,bytes,length+other_length-30);
    bytes[length+11]=0x90;rejected(true,bytes,length+other_length-30);
    /* A structurally complete unrelated attribute may precede/follow SDA;
     * irrelevant descriptors do not impose Remote ID-specific field rules. */
    length=nan(bytes,payload,packed);memmove(bytes+39,bytes+30,length-30);
    const uint8_t unrelated[]={3,6,0,0x88,0x69,0x19,0x9d,0x92,8};memcpy(bytes+30,unrelated,sizeof(unrelated));
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+9,&result));expect_claim(&result,3,true);
    length=nan(bytes,payload,packed);memcpy(bytes+length,unrelated,sizeof(unrelated));
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+9,&result));expect_claim(&result,3,true);
    bytes[length+1]=7;rejected(true,bytes,length+9);
    /* The legacy OUI still needs exact subtype and strict enclosing IE lengths. */
    for(unsigned oui=0;oui<2;++oui){
        length=beacon(bytes,payload,packed);
        if(oui){bytes[40]=0x90;bytes[41]=0x3a;bytes[42]=0xe6;}
        for(unsigned i=40;i<44;++i){bytes[i]^=1;rejected(true,bytes,length);bytes[i]^=1;}
        for(size_t cut=38;cut<length;++cut){
            uint8_t old=bytes[39];if(cut>=40)bytes[39]=(uint8_t)(cut-40);
            rejected(true,bytes,cut);bytes[39]=old;
        }
        bytes[length]=221;bytes[length+1]=5;bytes[length+2]=0;rejected(true,bytes,length+3);
    }
}

static sentinel_watch_protocol_record_t drone(bool coordinates)
{
    uint8_t payload[64],bytes[80];sentinel_watch_protocol_record_t out;
    size_t packed=make_pack(payload,"TEST-A",coordinates),length=ble(bytes,payload,packed);
    CHECK(sentinel_watch_protocol_decode_ble(bytes,length,&out));return out;
}
static sentinel_watch_protocol_record_t pwn(void)
{
    uint8_t bytes[2400];memset(bytes,0,sizeof(bytes));bytes[0]=0x80;
    const uint8_t source[]={0xde,0xad,0xbe,0xef,0xde,0xad};memcpy(bytes+10,source,6);
    const char json[]="{\"name\":\"TEST-PWN\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.10\"}";
    size_t size=strlen(json);bytes[36]=222;bytes[37]=31;memcpy(bytes+38,json,31);
    bytes[69]=1;bytes[70]=1;bytes[71]=0x82; /* unrelated IE between JSON chunks */
    bytes[72]=222;bytes[73]=(uint8_t)(size-31);memcpy(bytes+74,json+31,size-31);
    size_t length=74+size-31;sentinel_watch_protocol_record_t result;
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length,&result));
    CHECK(result.kind==1 && result.transport==2 && result.flags==1 && !strcmp(result.id,"TEST-PWN"));
    bytes[length]=223;bytes[length+1]=0;rejected(true,bytes,length+2);
    uint8_t payload[64];size_t packed=make_pack(payload,"TEST-A",true);
    bytes[length]=221;bytes[length+1]=(uint8_t)(packed+5);
    const uint8_t prefix[]={0xfa,0x0b,0xbc,0x0d,0};memcpy(bytes+length+2,prefix,5);memcpy(bytes+length+7,payload,packed);
    sentinel_watch_protocol_record_t combined;
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+7+packed,&combined));
    expect_claim(&combined,2,true); /* supported RID wins over Pwn source context */
    bytes[length+2]=0x90;bytes[length+3]=0x3a;bytes[length+4]=0xe6;
    CHECK(sentinel_watch_protocol_decode_wifi(bytes,length+7+packed,&combined));
    expect_claim(&combined,2,true); /* the legacy OUI has the same precedence */
    bytes[length+7+packed]=99;rejected(true,bytes,length+8+packed);
    return result;
}
static void collector_cases(void)
{
    cerberus_watch_capture_t capture;cerberus_watch_scan_data_payload_t chunk;
    const uint8_t address[6]={2,3,4,5,6,7};sentinel_watch_protocol_record_t claim=drone(true),other=pwn();
    cerberus_watch_capture_begin(&capture,CERBERUS_WATCH_RADIO_BLE,255,UINT32_MAX-5);
    cerberus_watch_capture_observe(&capture,&claim,address,0,-50,0);
    cerberus_watch_capture_observe(&capture,NULL,address,0,-40,0);
    CHECK(capture.status.reports==2 && capture.status.stored_count==1);
    CHECK(!cerberus_watch_capture_get_chunk(&capture,1,255,0,0,&chunk) && all_zero(&chunk,sizeof(chunk)));
    cerberus_watch_capture_observe(&capture,&claim,address,1,-60,0);CHECK(capture.status.stored_count==2);
    claim.transport=2;cerberus_watch_capture_observe(&capture,&claim,address,0,-70,6);CHECK(capture.status.stored_count==3);
    cerberus_watch_capture_observe(&capture,&other,address,0,-80,6);CHECK(capture.status.stored_count==4);
    claim=drone(false);strcpy(claim.id,"TEST-B");
    cerberus_watch_capture_observe(&capture,&claim,address,0,-55,0);
    CHECK(capture.records[0].flags==1 && !strcmp(capture.records[0].id,"TEST-B"));
    CHECK(!capture.records[0].latitude_e7 && !capture.records[0].longitude_e7 && !capture.records[0].altitude_dm && !capture.records[0].altitude_kind);
    CHECK(capture.records[1].flags==7 && !strcmp(capture.records[1].id,"TEST-A"));
    claim=drone(true);cerberus_watch_capture_observe(&capture,&claim,address,0,-50,0);
    claim=drone(false);cerberus_watch_capture_observe(&capture,&claim,address,0,-75,0);
    CHECK(capture.records[0].flags==7 && capture.records[0].rssi==-75); /* same-sweep latest signal, merged claims */
    for(unsigned i=0;i<8;++i){uint8_t unique[6]={4,3,4,5,6,(uint8_t)i};cerberus_watch_capture_observe(&capture,&claim,unique,0,-80,0);}
    CHECK(capture.status.stored_count==12);
    uint8_t overflow[6]={8,8,8,8,8,8};
    cerberus_watch_capture_observe(&capture,&claim,overflow,0,-80,0);
    cerberus_watch_capture_observe(&capture,&claim,overflow,0,-80,0);
    CHECK(capture.status.flags==CERBERUS_WATCH_CAPTURE_OVERFLOW && capture.status.dropped_records==2);
    capture.status.reports=UINT32_MAX;capture.status.dropped_records=UINT16_MAX;
    cerberus_watch_capture_observe(&capture,&claim,overflow,0,-80,0);
    CHECK(capture.status.reports==UINT32_MAX && capture.status.dropped_records==UINT16_MAX);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,4);CHECK(capture.status.duration_ms==10);
    cerberus_watch_capture_t frozen=capture;cerberus_watch_capture_observe(&capture,&claim,address,0,-1,0);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_IDLE,100);CHECK(!memcmp(&capture,&frozen,sizeof(capture)));
    cerberus_watch_observation_t rebuilt={0};
    for(unsigned i=0;i<3;++i){
        CHECK(cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,255,0,(uint8_t)i,&chunk));
        CHECK(chunk.byte_count==(i==2?12:17) && chunk.chunk==i && chunk.index==0 && chunk.generation==255);
        memcpy((uint8_t *)&rebuilt+17*i,chunk.bytes,chunk.byte_count);
        CHECK(all_zero(chunk.bytes+chunk.byte_count,17-chunk.byte_count));
    }
    CHECK(!memcmp(&rebuilt,&capture.records[0],sizeof(rebuilt)));
    CHECK(!cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_WIFI,255,0,0,&chunk));
    CHECK(!cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,0,0,0,&chunk));
    CHECK(!cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,255,12,0,&chunk));
    CHECK(!cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,255,0,3,&chunk));
    CHECK(all_zero(&chunk,sizeof(chunk)));
    cerberus_watch_capture_begin(&capture,CERBERUS_WATCH_RADIO_BLE,0,200);claim=drone(false);
    cerberus_watch_capture_observe(&capture,&claim,address,0,-50,0);CHECK(capture.records[0].flags==1);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_COMPLETE,250);
    CHECK(cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,0,0,0,&chunk));
    CHECK(!cerberus_watch_capture_get_chunk(&capture,CERBERUS_WATCH_RADIO_BLE,255,0,0,&chunk));
    cerberus_watch_capture_begin(&capture,CERBERUS_WATCH_RADIO_BLE,1,260);
    cerberus_watch_capture_observe(&capture,&claim,address,0,-50,0);
    cerberus_watch_capture_finish(&capture,CERBERUS_WATCH_ERROR,300);
    CHECK(capture.status.stored_count==0 && all_zero(capture.records,sizeof(capture.records)));
    /* Real Wi-Fi coexistence uses one shared12-row protocol bank. */
    cerberus_watch_capture_begin(&capture,CERBERUS_WATCH_RADIO_WIFI,2,400);
    claim=drone(true);claim.transport=SENTINEL_WATCH_TRANSPORT_WIFI_BEACON;
    cerberus_watch_capture_observe(&capture,&claim,address,0,-50,6);
    claim.transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    cerberus_watch_capture_observe(&capture,&claim,address,0,-55,149);
    cerberus_watch_capture_observe(&capture,&other,address,0,-60,6);
    CHECK(capture.status.stored_count==3 && capture.status.reports==3);
    CHECK(capture.records[0].kind==2 && capture.records[0].transport==2);
    CHECK(capture.records[1].kind==2 && capture.records[1].transport==3);
    CHECK(capture.records[2].kind==1 && capture.records[2].transport==2);
}

static void s3_receive_cases(void)
{
    static cerberus_s3_ble_snapshot_t snapshot;
    uint8_t payload[256],bytes[256];basic(payload,"TEST-A");size_t length=ble(bytes,payload,25);
    cerberus_s3_ble_report_t report={.address={2,3,4,5,6,7},.address_type=0,.rssi=-65,.event_type=3,.complete=true,.data=bytes,.length=length};
    cerberus_s3_ble_snapshot_begin(&snapshot,9,100,false);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,110);
    CHECK(snapshot.watch.status.stored_count==1 && snapshot.watch.records[0].flags==1);
    size_t packed=make_pack(payload,"TEST-A",true);length=ble(bytes,payload,packed);
    report.length=length;report.sid=3;report.sid_valid=true;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,120);
    CHECK(snapshot.watch.records[0].flags==7 && snapshot.watch.records[0].latitude_e7==123456789);
    CHECK(snapshot.records[0].advertisement_length==31 && (snapshot.records[0].advertisement_flags&CERBERUS_BLE_ADVERTISEMENT_TRUNCATED));
    CHECK(snapshot.watch.status.reports==2); /* full extended source parsed before bounded raw-copy truncation */
    report.address[5]=8;report.complete=false;cerberus_s3_ble_snapshot_observe(&snapshot,&report,130);
    CHECK(snapshot.watch.status.stored_count==1 && snapshot.watch.status.reports==3);
    report.complete=true;report.address_type=255;cerberus_s3_ble_snapshot_observe(&snapshot,&report,140);
    CHECK(snapshot.watch.status.stored_count==1 && snapshot.watch.status.reports==4);
    report.address_type=0;bytes[length]=1;report.length=length+1;cerberus_s3_ble_snapshot_observe(&snapshot,&report,150);
    CHECK(snapshot.watch.status.stored_count==1 && snapshot.watch.status.reports==5);
    report.length=length;report.scan_response=true;report.event_type=4;report.sid_valid=false;
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,160);
    CHECK(snapshot.watch.status.stored_count==1 && snapshot.watch.status.reports==5); /* no response concatenation/promotion */
    report.scan_response=false;report.event_type=3;report.address[5]=7;
    packed=make_pack(payload,"TEST-B",false);report.length=ble(bytes,payload,packed);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,170);
    CHECK(snapshot.watch.records[0].flags==1 && !snapshot.watch.records[0].latitude_e7);
    CHECK(!strcmp(snapshot.watch.records[0].id,"TEST-B"));
    cerberus_s3_ble_snapshot_finish(&snapshot,false,0,200);
    CHECK(snapshot.watch.status.state==CERBERUS_WATCH_COMPLETE && snapshot.watch.status.duration_ms==100);
    cerberus_s3_ble_snapshot_observe(&snapshot,&report,210);CHECK(snapshot.watch.status.reports==6);
    cerberus_s3_ble_snapshot_begin(&snapshot,10,300,true);
    /* Protocol bank remains independent after ordinary32-row inventory fills. */
    const uint8_t ordinary[]={2,1,6};report.data=ordinary;report.length=sizeof(ordinary);
    for(unsigned i=0;i<32;++i){report.address[5]=(uint8_t)i;cerberus_s3_ble_snapshot_observe(&snapshot,&report,310+i);}
    CHECK(snapshot.status.stored_count==32 && snapshot.watch.status.stored_count==0);
    packed=make_pack(payload,"TEST-A",true);report.data=bytes;report.length=ble(bytes,payload,packed);
    report.address[5]=99;cerberus_s3_ble_snapshot_observe(&snapshot,&report,350);
    CHECK(snapshot.status.stored_count==32 && snapshot.watch.status.stored_count==1 && snapshot.watch.records[0].flags==7);
    cerberus_s3_ble_snapshot_finish(&snapshot,true,0,360);CHECK(snapshot.watch.status.stored_count==0);
    cerberus_s3_ble_snapshot_begin(&snapshot,11,400,true);cerberus_s3_ble_snapshot_observe(&snapshot,&report,401);
    cerberus_s3_ble_snapshot_finish(&snapshot,false,1,410);CHECK(snapshot.watch.status.state==CERBERUS_WATCH_ERROR && !snapshot.watch.status.stored_count);
}

/* Production Watch getter, with only mutex and transport dependencies replaced.
 * The transport's normal data comes from the actual collector chunk function. */
typedef int esp_err_t;
enum { ESP_OK,ESP_FAIL,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE,ESP_ERR_TIMEOUT,
    ESP_ERR_NOT_SUPPORTED,ESP_ERR_INVALID_RESPONSE };
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
typedef struct {
    uint32_t firmware_version,capabilities,uptime_ms;
    uint8_t protocol_version,reset_reason,boot_count;
} cerberus_c5_status_t;
#ifdef REMOTE_ID_C5_PRODUCER
static int64_t esp_timer_get_time(void);
#else
static int64_t esp_timer_get_time(void){return 1234567;}
#endif
static int mutex_token;
static void *link_mutex=&mutex_token;
static uint8_t sequence;
static bool locked,lock_failure;
static unsigned takes,gives,status_reads,cap_reads,data_reads,fail_stage,fail_chunk,chunk_defect,before_defect,after_defect;
static cerberus_watch_capture_t transport;
static cerberus_c5_status_t peer;
static int take_link_mutex(unsigned timeout)
{CHECK(timeout==250 && !locked);++takes;if(lock_failure)return 0;locked=true;return pdTRUE;}
static void give_link_mutex(void){CHECK(locked);locked=false;++gives;}
static esp_err_t get_status_v2_locked(uint8_t seq,cerberus_c5_status_t *out)
{CHECK(locked && seq==sequence);++cap_reads;*out=peer;return fail_stage==1?ESP_FAIL:ESP_OK;}
static void alter_status(cerberus_watch_scan_status_payload_t *out,unsigned defect)
{
    switch(defect){
    case 1:out->radio^=3;break;
    case 2:++out->generation;break;
    case 3:out->state=CERBERUS_WATCH_RUNNING;break;
    case 4:out->stored_count=13;break;
    case 5:out->flags|=0x80;break;
    case 6:out->flags|=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;break;
    case 7:++out->reports;break;
    case 8:++out->duration_ms;break;
    case 9:++out->dropped_records;break;
    default:break;
    }
}
#ifdef REMOTE_ID_C5_PRODUCER
esp_err_t cerberus_wifi_watch_get_status(uint8_t generation,cerberus_watch_scan_status_payload_t *out);
esp_err_t cerberus_wifi_watch_get_data(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_data_payload_t *out);
#endif
static esp_err_t exchange_v2_locked(uint8_t command,const void *query,size_t query_size,void *reply,size_t reply_size)
{
    CHECK(locked);
    if(command==CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS){
        CHECK(query_size==sizeof(cerberus_watch_scan_request_t) && reply_size==sizeof(cerberus_watch_scan_status_payload_t));
        const cerberus_watch_scan_request_t *q=query;
        CHECK(q->radio==transport.status.radio && q->generation==transport.status.generation);
        ++status_reads;CHECK(status_reads<=2);
        if(fail_stage==2 || (fail_stage==4 && status_reads==2))return ESP_FAIL;
        cerberus_watch_scan_status_payload_t *out=reply;
#ifdef REMOTE_ID_C5_PRODUCER
        CHECK(cerberus_wifi_watch_get_status(q->generation,out)==ESP_OK);
#else
        *out=transport.status;
#endif
        alter_status(out,status_reads==1?before_defect:after_defect);return ESP_OK;
    }
    if(command==CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA){
        CHECK(query_size==sizeof(cerberus_watch_scan_data_request_t) && reply_size==sizeof(cerberus_watch_scan_data_payload_t));
        const cerberus_watch_scan_data_request_t *q=query;
        CHECK(q->chunk==data_reads && q->index<transport.status.stored_count);++data_reads;
        if(fail_stage==3 && q->chunk==fail_chunk)return ESP_FAIL;
        cerberus_watch_scan_data_payload_t *out=reply;
#ifdef REMOTE_ID_C5_PRODUCER
        CHECK(cerberus_wifi_watch_get_data(q,out)==ESP_OK);
#else
        CHECK(cerberus_watch_capture_get_chunk(&transport,q->radio,q->generation,q->index,q->chunk,out));
#endif
        if(q->chunk==fail_chunk){
            switch(chunk_defect){case 1:++out->radio;break;case 2:++out->generation;break;
            case 3:++out->index;break;case 4:++out->chunk;break;case 5:--out->byte_count;break;
            case 6:++out->byte_count;break;default:break;}
        }
        return ESP_OK;
    }
    CHECK(command==CERBERUS_SPI_COMMAND_BLE_ACTIVITY);return ESP_FAIL;
}
#include "c5_watch_link.h"

static void transport_reset(uint8_t radio)
{
    CHECK(!locked);link_mutex=&mutex_token;lock_failure=false;
    takes=gives=status_reads=cap_reads=data_reads=fail_stage=fail_chunk=chunk_defect=before_defect=after_defect=0;
    peer=(cerberus_c5_status_t){.firmware_version=CERBERUS_WATCH_MIN_FIRMWARE,
        .capabilities=CERBERUS_C5_CAP_WATCH_OBSERVATIONS};
    cerberus_watch_capture_begin(&transport,radio,255,100);
    sentinel_watch_protocol_record_t claim=drone(true);
    uint8_t address[6]={2,3,4,5,6,7};
    if(radio==CERBERUS_WATCH_RADIO_WIFI)claim.transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    cerberus_watch_capture_observe(&transport,&claim,address,radio==CERBERUS_WATCH_RADIO_BLE?1:0,-60,radio==CERBERUS_WATCH_RADIO_WIFI?149:0);
    cerberus_watch_capture_finish(&transport,CERBERUS_WATCH_COMPLETE,1100);
}
static esp_err_t read_result(cerberus_watch_observation_t *out)
{
    memset(out,0xa5,sizeof(*out));
    esp_err_t err=c5_spi_link_get_watch_result(transport.status.radio,255,0,out);
    CHECK(!locked && gives==(lock_failure||!link_mutex?0:1));
    if(err!=ESP_OK)CHECK(all_zero(out,sizeof(*out)));
    return err;
}
static void getter_cases(void)
{
    cerberus_watch_observation_t out;
    for(unsigned radio=1;radio<=2;++radio){
        transport_reset((uint8_t)radio);CHECK(read_result(&out)==ESP_OK);
        CHECK(!memcmp(&out,&transport.records[0],sizeof(out)));
        CHECK(cap_reads==1 && status_reads==2 && data_reads==3 && takes==1 && gives==1);
    }
    transport_reset(1);lock_failure=true;CHECK(read_result(&out)==ESP_ERR_TIMEOUT);
    CHECK(!cap_reads && !status_reads && !data_reads && !gives);
    transport_reset(1);link_mutex=NULL;CHECK(read_result(&out)==ESP_ERR_INVALID_STATE);CHECK(!takes && !cap_reads);
    transport_reset(1);peer.firmware_version--;CHECK(read_result(&out)==ESP_ERR_NOT_SUPPORTED);CHECK(!status_reads && !data_reads);
    transport_reset(1);peer.capabilities=0;CHECK(read_result(&out)==ESP_ERR_NOT_SUPPORTED);CHECK(!status_reads && !data_reads);
    for(unsigned stage=1;stage<=4;++stage){
        transport_reset(2);fail_stage=stage;fail_chunk=1;CHECK(read_result(&out)==ESP_FAIL);
        if(stage==3)CHECK(data_reads==2 && status_reads==1);
    }
    for(unsigned chunk=0;chunk<3;++chunk)for(unsigned defect=1;defect<=6;++defect){
        transport_reset(2);fail_chunk=chunk;chunk_defect=defect;
        CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);CHECK(data_reads==chunk+1 && status_reads==1);
    }
    for(unsigned defect=1;defect<=6;++defect){
        transport_reset(2);before_defect=defect;CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);CHECK(!data_reads);
    }
    for(unsigned defect=1;defect<=9;++defect){
        transport_reset(2);after_defect=defect;CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);CHECK(data_reads==3);
    }
    transport_reset(2);transport.status.stored_count=0;CHECK(read_result(&out)==ESP_ERR_INVALID_ARG);CHECK(!data_reads);
    for(unsigned defect=0;defect<17;++defect){
        transport_reset(2);cerberus_watch_observation_t *r=&transport.records[0];
        switch(defect){
        case 0:r->address_type=1;break;case 1:r->channel=0;break;case 2:r->kind=99;break;
        case 3:r->transport=SENTINEL_WATCH_TRANSPORT_BLE;break;case 4:r->flags|=8;break;
        case 5:memset(r->id,'A',sizeof(r->id));break;case 6:r->id[8]='x';break;
        case 7:r->id[0]=31;break;case 8:r->id[0]=0;break;
        case 9:r->latitude_e7=900000001;break;case 10:r->longitude_e7=-1800000001;break;
        case 11:r->altitude_dm=-10000;break;case 12:r->altitude_dm=317676;break;
        case 13:r->altitude_kind=3;break;case 14:r->flags&=(uint8_t)~2;break;
        case 15:r->flags&=(uint8_t)~4;break;
        case 16:r->flags&=(uint8_t)~1;break;
        }
        CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);
    }
    transport_reset(1);transport.records[0].address_type=4;CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);
    transport_reset(1);transport.records[0].channel=6;CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);
    transport_reset(2);transport.records[0].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
    CHECK(read_result(&out)==ESP_ERR_INVALID_RESPONSE);
    transport_reset(2);sentinel_watch_protocol_record_t p=pwn();
    transport.records[0].kind=p.kind;transport.records[0].transport=p.transport;
    transport.records[0].flags=p.flags;memcpy(transport.records[0].id,p.id,sizeof(p.id));
    transport.records[0].latitude_e7=transport.records[0].longitude_e7=transport.records[0].altitude_dm=0;
    transport.records[0].altitude_kind=0;CHECK(read_result(&out)==ESP_OK);
    transport_reset(2);transport.status.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;
    transport.status.stored_count=0;transport.status.reports=0;
    cerberus_watch_scan_status_payload_t status;memset(&status,0xa5,sizeof(status));
    CHECK(c5_spi_link_get_watch_status(2,255,&status)==ESP_OK && !status.stored_count);
    CHECK(gives==1 && !locked);
    /* Known generic-getter gaps, deliberately not represented as new validation:
     * dedicated inventory validates these producer invariants independently. */
    transport_reset(2);memset(transport.records[0].address,0,6);CHECK(read_result(&out)==ESP_OK);
    transport_reset(2);transport.records[0].address[0]|=1;CHECK(read_result(&out)==ESP_OK);
    transport_reset(2);transport.status.reports=0;CHECK(read_result(&out)==ESP_OK);
    transport_reset(2);transport.status.dropped_records=1;CHECK(read_result(&out)==ESP_OK);
    transport_reset(2);memset(&out,0xa5,sizeof(out));
    CHECK(c5_spi_link_get_watch_result(0,255,0,&out)==ESP_ERR_INVALID_ARG && all_zero(&out,sizeof(out)));
    CHECK(c5_spi_link_get_watch_result(2,255,12,&out)==ESP_ERR_INVALID_ARG && !takes);
    CHECK(c5_spi_link_get_watch_result(2,255,0,NULL)==ESP_ERR_INVALID_ARG && !takes);
}
int main(void)
{
    parser_vectors();variable_wifi_vectors();collector_cases();s3_receive_cases();getter_cases();
    printf("Remote ID receive foundation: %u checks; legacy/complete extended BLE, supported beacon/NAN, transactional rejection, Pwn coexistence, typed collectors, changed-ID clearing, real S3 publication and production Watch chunk/getter failures passed.\n",checks);
    puts("Scope: unauthenticated broadcast claims and completed-sweep aggregates; modern/legacy beacon and bounded variable NAN framing covered. Generic peer address/counter validation remains the dedicated inventory's responsibility.");
    return 0;
}
