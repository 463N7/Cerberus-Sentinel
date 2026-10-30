#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_pinescan.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum {FRAME_CAPACITY=256};
static size_t make_beacon(uint8_t frame[FRAME_CAPACITY],uint32_t prefix,uint16_t caps,
    const uint8_t *ssid,uint8_t ssid_length)
{
    assert(ssid_length<=32);memset(frame,0,FRAME_CAPACITY);frame[0]=0x80;
    memset(frame+4,0xff,6);
    frame[10]=(uint8_t)(prefix>>16);frame[11]=(uint8_t)(prefix>>8);frame[12]=(uint8_t)prefix;
    frame[13]=0x11;frame[14]=0x22;frame[15]=0x33;
    memcpy(frame+16,frame+10,6);frame[34]=(uint8_t)caps;frame[35]=(uint8_t)(caps>>8);
    frame[36]=0;frame[37]=ssid_length;if(ssid_length)memcpy(frame+38,ssid,ssid_length);
    size_t offset=38U+ssid_length;
    frame[offset]=3;frame[offset+1]=1;frame[offset+2]=149;
    return offset+3;
}
static bool zero(const void *data,size_t n)
{const unsigned char *p=data;for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static cerberus_wifi_pinescan_evidence_t classify(const uint8_t *frame,size_t length)
{
    cerberus_wifi_pinescan_evidence_t evidence;
    assert(cerberus_wifi_pinescan_decode(frame,length,36,-47,123456,&evidence));
    assert(evidence.beacon.rx_channel==36&&evidence.beacon.observed_us==123456&&evidence.beacon.rssi==-47);
    return evidence;
}
static void prefix_rules(void)
{
    /* Fixture data independently spells every upstream prefix and security
     * condition. No production table or classifier result generates these. */
    const struct {uint32_t prefix;uint8_t profile,condition;} rules[]={
        {0x00c0ca,1,2},{0x001337,2,1},{0x1cbfce,3,2},{0x0cefaf,4,2},
        {0x02c0ca,5,3},{0x021337,5,3},{0x000a00,6,1},{0x000c43,6,1},
        {0x000ce7,6,1},{0x0017a5,6,1},{0x9cefd5,7,1},{0x9ce5d5,7,1},{0xdeadbe,8,1},
    };
    uint8_t frame[FRAME_CAPACITY];const uint8_t ssid[]={'A',0,'B',0xff};
    for(size_t i=0;i<sizeof(rules)/sizeof(*rules);++i)for(unsigned privacy=0;privacy<2;++privacy){
        size_t length=make_beacon(frame,rules[i].prefix,(uint16_t)(0x0101|(privacy?0x10:0)),ssid,sizeof(ssid));
        cerberus_wifi_pinescan_evidence_t e=classify(frame,length);
        bool matches=rules[i].condition==1||(rules[i].condition==2&&!privacy)||(rules[i].condition==3&&privacy);
        assert(e.reasons==(matches?CERBERUS_WIFI_PINESCAN_REASON_OUI:0));
        assert(e.profile==rules[i].profile&&e.condition==rules[i].condition&&e.privacy_bit==(privacy!=0));
        assert(e.ie_count==2&&e.beacon.advertised_channel==149);
        assert(e.beacon.ssid_length==sizeof(ssid)&&!memcmp(e.beacon.ssid,ssid,sizeof(ssid)));
        assert(strcmp(cerberus_wifi_pinescan_profile_name(e.profile),"Unknown rule"));
        assert(strcmp(cerberus_wifi_pinescan_condition_name(e.condition),"No prefix rule"));
        /* Prefix changes cannot inherit an adjacent profile. addr3 alone is
         * irrelevant: leave its matching prefix intact while changing addr2. */
        frame[12]^=0x80;
        e=classify(frame,length);assert(e.reasons==0&&e.profile==0&&e.condition==0);
    }
    size_t length=make_beacon(frame,0x00c0ca,0x0111,NULL,0);
    assert(classify(frame,length).reasons==0);
    frame[10]=2; /* Local bit changes to a separate exact protected rule. */
    assert(classify(frame,length).reasons==CERBERUS_WIFI_PINESCAN_REASON_OUI);
    length=make_beacon(frame,0x021337,0x0101,NULL,0);
    assert(classify(frame,length).reasons==0);
    frame[10]=0;assert(classify(frame,length).reasons==CERBERUS_WIFI_PINESCAN_REASON_OUI);
    /* RSN bytes do not replace the upstream privacy-bit rule. An IE payload
     * is not cryptographically verified security evidence. */
    length=make_beacon(frame,0x00c0ca,0x0101,NULL,0);
    frame[length++]=48;frame[length++]=2;frame[length++]=1;frame[length++]=0;
    assert(classify(frame,length).reasons==CERBERUS_WIFI_PINESCAN_REASON_OUI);
    frame[34]|=0x10;assert(classify(frame,length).reasons==0);
    assert(!strcmp(cerberus_wifi_pinescan_profile_name(255),"Unknown rule"));
}
static void minimal_layout(void)
{
    uint8_t frame[FRAME_CAPACITY];const uint8_t ssid[]="normal";
    size_t length=make_beacon(frame,0x042638,1,ssid,sizeof(ssid)-1);
    cerberus_wifi_pinescan_evidence_t e=classify(frame,length);
    assert(e.reasons==CERBERUS_WIFI_PINESCAN_REASON_MINIMAL&&e.profile==0&&e.ie_count==2&&!e.privacy_bit);
    frame[34]=0;assert(classify(frame,length).reasons==0);
    frame[34]=0x11;assert(classify(frame,length).reasons==0);
    frame[34]=1;frame[35]=1;assert(classify(frame,length).reasons==0);frame[35]=0;
    /* A complete extra zero-length vendor IE is still a third IE. */
    frame[length]=221;frame[length+1]=0;assert(classify(frame,length+2).reasons==0);
    /* Same two IEs in another order do not satisfy SSID-then-DS. */
    uint8_t reordered[FRAME_CAPACITY];memcpy(reordered,frame,length);
    memcpy(reordered+36,frame+length-3,3);memcpy(reordered+39,frame+36,length-39);
    assert(classify(reordered,length).reasons==0);
    length=make_beacon(frame,0x001337,1,NULL,0);
    e=classify(frame,length);assert(e.reasons==3&&e.beacon.hidden_like&&e.beacon.ssid_length==0);
    uint8_t binary[32];for(size_t i=0;i<sizeof(binary);++i)binary[i]=(uint8_t)i;
    length=make_beacon(frame,0x042638,1,binary,sizeof(binary));
    e=classify(frame,length);assert(e.reasons==2&&e.beacon.ssid_length==32&&!memcmp(e.beacon.ssid,binary,32));
    memset(binary,0,sizeof(binary));length=make_beacon(frame,0x042638,1,binary,sizeof(binary));
    e=classify(frame,length);assert(e.reasons==2&&e.beacon.hidden_like&&e.beacon.ssid_length==32);
    /* A five-GHz beacon can omit DS. Vendor rule still works, minimal does not. */
    length=make_beacon(frame,0x001337,1,NULL,0);
    e=classify(frame,length-3);assert(e.reasons==1&&e.beacon.advertised_channel==0&&e.ie_count==1);
}
static void malformed(void)
{
    uint8_t frame[FRAME_CAPACITY],copy[FRAME_CAPACITY];
    size_t length=make_beacon(frame,0x001337,1,(const uint8_t *)"safe",4);
    cerberus_wifi_pinescan_evidence_t e;
    assert(!cerberus_wifi_pinescan_decode(frame,length,1,-40,10,NULL));
    for(size_t n=0;n<38;++n){
        memset(&e,0xa5,sizeof(e));assert(!cerberus_wifi_pinescan_decode(frame,n,1,-40,10,&e));
        assert(zero(&e,sizeof(e)));
    }
    for(unsigned kind=0;kind<17;++kind){
        memcpy(copy,frame,length);size_t used=length;uint8_t channel=1;
        switch(kind){
        case 0:copy[0]=0x50;break;
        case 1:copy[0]=0x81;break;
        case 2:copy[1]=1;break;
        case 3:copy[1]=0x40;break;
        case 4:copy[22]=1;break;
        case 5:copy[10]|=1;break;
        case 6:memset(copy+16,0,6);break;
        case 7:copy[4]=0;break;
        case 8:copy[37]=33;break;
        case 9:copy[36]=1;break;
        case 10:copy[used++]=0;copy[used++]=0;break;
        case 11:copy[used++]=3;copy[used++]=1;copy[used++]=1;break;
        case 12:copy[used++]=221;break; /* Upstream tolerates this one-byte tail. */
        case 13:copy[length-2]=2;break;
        case 14:copy[length-1]=0;break;
        case 15:--used;break;
        case 16:channel=0;break;
        }
        memset(&e,0xa5,sizeof(e));assert(!cerberus_wifi_pinescan_decode(copy,used,channel,-40,10,&e));
        assert(zero(&e,sizeof(e)));
    }
    memset(&e,0xa5,sizeof(e));
    assert(!cerberus_wifi_pinescan_decode(NULL,length,1,-40,10,&e)&&zero(&e,sizeof(e)));
    assert(!cerberus_wifi_pinescan_decode(frame,2305,1,-40,10,&e));
}
static void accumulator(void)
{
    cerberus_wifi_pinescan_t table;
    cerberus_wifi_pinescan_record_t storage[2],frozen;
    uint8_t frame[FRAME_CAPACITY];uint16_t row=0;
    assert(!cerberus_wifi_pinescan_init(NULL,storage,2));
    assert(!cerberus_wifi_pinescan_init(&table,NULL,2)&&zero(&table,sizeof(table)));
    assert(!cerberus_wifi_pinescan_init(&table,storage,129)&&zero(&table,sizeof(table)));
    assert(cerberus_wifi_pinescan_init(&table,storage,2));
    size_t length=make_beacon(frame,0x042638,0x0101,(const uint8_t *)"none",4);
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,6,-60,100,&row)==CERBERUS_WIFI_PINESCAN_NONMATCH);
    assert(row==UINT16_MAX&&table.count==0&&table.valid_beacons==1&&table.matching_beacons==0);
    length=make_beacon(frame,0x00c0ca,0x0101,(const uint8_t *)"match",5);
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,6,-55,110,&row)==CERBERUS_WIFI_PINESCAN_ADDED&&row==0);
    assert(table.count==1&&storage[0].frames==1&&storage[0].matching_frames==1&&storage[0].last_match.reasons==1);
    assert(cerberus_wifi_pinescan_copy(&table,0,&frozen));
    frame[34]=0x11;frame[16]^=4; /* Later unmatched privacy-set beacon, new BSSID. */
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,36,-45,120,&row)==CERBERUS_WIFI_PINESCAN_UPDATED&&row==0);
    assert(storage[0].latest.observed_us==120&&storage[0].latest.capabilities==0x0111&&storage[0].multiple_bssids);
    assert(!memcmp(&storage[0].last_match,&frozen.last_match,sizeof(frozen.last_match))&&storage[0].matching_frames==1);
    assert(storage[0].frames==2&&storage[0].peak_rssi==-45&&storage[0].reasons_seen==1&&frozen.latest.observed_us==110);
    frame[34]=1;frame[35]=0;
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,11,-70,130,&row)==CERBERUS_WIFI_PINESCAN_UPDATED);
    assert(storage[0].last_match.reasons==3&&storage[0].reasons_seen==3&&storage[0].matching_frames==2);
    frame[10]=4; /* Independent unknown prefix, minimal layout still matches. */
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,11,-50,140,&row)==CERBERUS_WIFI_PINESCAN_ADDED&&row==1);
    frame[15]++;
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,11,-40,150,&row)==CERBERUS_WIFI_PINESCAN_FULL);
    assert(row==UINT16_MAX&&table.count==2&&table.capacity_drops==1);
    frame[10]=0;frame[15]--; /* Original row updates even while capacity is full. */
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,149,-30,160,&row)==CERBERUS_WIFI_PINESCAN_UPDATED&&row==0);
    assert(storage[0].latest.rx_channel==149&&storage[0].peak_rssi==-30);
    cerberus_wifi_pinescan_record_t before=storage[0];
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,1,-20,159,&row)==CERBERUS_WIFI_PINESCAN_STALE);
    assert(row==UINT16_MAX&&table.stale_frames==1&&!memcmp(&before,&storage[0],sizeof(before)));
    frame[10]=4;frame[15]+=2;
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,1,-20,159,&row)==CERBERUS_WIFI_PINESCAN_STALE);
    assert(table.capacity_drops==1&&table.stale_frames==2); /* Global source clock, even unknown TX. */
    frame[10]=0;frame[15]-=2;
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,149,-31,160,&row)==CERBERUS_WIFI_PINESCAN_UPDATED);
    table.valid_beacons=table.matching_beacons=UINT32_MAX;
    storage[0].frames=storage[0].matching_frames=UINT32_MAX;
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,149,-32,161,NULL)==CERBERUS_WIFI_PINESCAN_UPDATED);
    assert(table.valid_beacons==UINT32_MAX&&table.matching_beacons==UINT32_MAX&&storage[0].frames==UINT32_MAX&&storage[0].matching_frames==UINT32_MAX);
    table.rejected_frames=UINT32_MAX;
    assert(cerberus_wifi_pinescan_observe(&table,frame,5,1,-40,170,&row)==CERBERUS_WIFI_PINESCAN_INVALID);
    assert(row==UINT16_MAX&&table.rejected_frames==UINT32_MAX);
    assert(cerberus_wifi_pinescan_copy(&table,0,&storage[0])); /* Value-copy alias safe. */
    assert(!cerberus_wifi_pinescan_copy(&table,5,&frozen)&&zero(&frozen,sizeof(frozen)));
    assert(cerberus_wifi_pinescan_init(&table,storage,2));
    assert(table.count==0&&table.latest_observed_us==0&&zero(storage,sizeof(storage)));
    assert(cerberus_wifi_pinescan_observe(&table,frame,length,149,-32,1,&row)==CERBERUS_WIFI_PINESCAN_ADDED);
}
int main(void)
{
    prefix_rules();minimal_layout();malformed();accumulator();
    printf("PineScan: all 13 prefix/privacy rules, strict layout/body, provenance, retained proof, bounded/stale/reset checks passed; %zu-byte row, %zu-byte default bank.\n",
        sizeof(cerberus_wifi_pinescan_record_t),sizeof(cerberus_wifi_pinescan_record_t)*CERBERUS_WIFI_PINESCAN_DEFAULT_ROWS);
    return 0;
}
