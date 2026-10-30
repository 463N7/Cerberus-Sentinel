#ifdef NDEBUG
#undef NDEBUG
#endif
#include "remote_id_inventory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(condition) do { ++checks; assert(condition); } while (0)
static cerberus_remote_id_inventory_t inventory;
static cerberus_watch_observation_t rows[12];
static cerberus_watch_scan_status_payload_t before,after;

static bool zero(const void *data,size_t length)
{ const unsigned char *p=data;for(size_t i=0;i<length;++i)if(p[i])return false;return true; }
static cerberus_watch_observation_t row(uint8_t radio,uint8_t index)
{
    cerberus_watch_observation_t value={.address={0x02,0x11,0x22,0x33,0x44,0},
        .rssi=-42,.channel=radio==CERBERUS_WATCH_RADIO_WIFI?6U:0U,
        .kind=SENTINEL_WATCH_PROTOCOL_DRONE,
        .transport=radio==CERBERUS_WATCH_RADIO_WIFI?SENTINEL_WATCH_TRANSPORT_WIFI_BEACON:SENTINEL_WATCH_TRANSPORT_BLE,
        .flags=SENTINEL_WATCH_PROTOCOL_HAS_ID|SENTINEL_WATCH_PROTOCOL_HAS_POSITION|SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE,
        .altitude_kind=SENTINEL_WATCH_ALTITUDE_GEODETIC,.latitude_e7=-1,.longitude_e7=1,.altitude_dm=123};
    value.address[5]=(uint8_t)(index+1U);
    if(radio==CERBERUS_WATCH_RADIO_BLE)value.address[0]=0xc1U; /* No IEEE group-bit test on NimBLE bytes. */
    strcpy(value.id,"SAME-CLAIMED-ID");
    return value;
}
static void prepare(uint8_t radio,uint8_t count,uint8_t generation)
{
    before=(cerberus_watch_scan_status_payload_t){.radio=radio,.generation=generation,
        .state=CERBERUS_WATCH_COMPLETE,.stored_count=count,.reports=(uint32_t)count+20U,.duration_ms=8000U};
    after=before;
    memset(rows,0,sizeof(rows));
    for(uint8_t i=0;i<count&&i<12U;++i)rows[i]=row(radio,i);
}
static cerberus_remote_id_publish_result_t publish(uint64_t token,uint64_t time)
{ return cerberus_remote_id_inventory_publish(&inventory,token,&before,&after,rows,before.stored_count,time); }
static void reject_unchanged(uint64_t token,uint64_t time,cerberus_remote_id_publish_result_t result)
{
    cerberus_remote_id_inventory_t saved=inventory;
    CHECK(publish(token,time)==result);
    CHECK(!memcmp(&inventory,&saved,sizeof(saved)));
}
static cerberus_remote_id_record_t first_record(void)
{
    cerberus_remote_id_page_t page;
    CHECK(cerberus_remote_id_inventory_page(&inventory,0U,&page));
    CHECK(page.copied>0U);return page.rows[0];
}
static void init_and_bounds(void)
{
    memset(&inventory,0xa5,sizeof(inventory));cerberus_remote_id_inventory_init(&inventory);
    CHECK(zero(&inventory,sizeof(inventory)));cerberus_remote_id_inventory_init(NULL);
    CHECK(!cerberus_remote_id_inventory_bank(NULL,CERBERUS_WATCH_RADIO_WIFI));
    CHECK(!cerberus_remote_id_inventory_bank(&inventory,0U));
    CHECK(!cerberus_remote_id_inventory_bank(&inventory,3U));
    CHECK(!cerberus_remote_id_inventory_bank(&inventory,CERBERUS_WATCH_RADIO_WIFI)->published);
    cerberus_remote_id_page_t page;memset(&page,0xa5,sizeof(page));
    CHECK(!cerberus_remote_id_inventory_page(NULL,0U,&page)&&zero(&page,sizeof(page)));
    CHECK(!cerberus_remote_id_inventory_page(&inventory,0U,NULL));
    CHECK(cerberus_remote_id_inventory_page(&inventory,SIZE_MAX,&page)&&page.total==0U&&page.copied==0U&&page.offset==SIZE_MAX);
    prepare(CERBERUS_WATCH_RADIO_WIFI,1U,7U);
    CHECK(cerberus_remote_id_inventory_publish(NULL,1U,&before,&after,rows,1U,0U)==CERBERUS_REMOTE_ID_INVALID);
    CHECK(cerberus_remote_id_inventory_publish(&inventory,1U,NULL,&after,rows,1U,0U)==CERBERUS_REMOTE_ID_INVALID);
    CHECK(cerberus_remote_id_inventory_publish(&inventory,1U,&before,NULL,rows,1U,0U)==CERBERUS_REMOTE_ID_INVALID);
    CHECK(cerberus_remote_id_inventory_publish(&inventory,1U,&before,&after,NULL,1U,0U)==CERBERUS_REMOTE_ID_INVALID);
    CHECK(cerberus_remote_id_inventory_publish(&inventory,1U,&before,&after,rows,0U,0U)==CERBERUS_REMOTE_ID_INVALID);
    reject_unchanged(0U,0U,CERBERUS_REMOTE_ID_INVALID);
    CHECK(!cerberus_remote_id_inventory_select(NULL,NULL));
    CHECK(!cerberus_remote_id_inventory_select(&inventory,NULL));
    cerberus_remote_id_inventory_clear_selection(NULL);
}
static void separate_banks_and_pages(void)
{
    cerberus_remote_id_inventory_init(&inventory);
    prepare(CERBERUS_WATCH_RADIO_WIFI,3U,7U);
    rows[1].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;rows[1].flags=SENTINEL_WATCH_PROTOCOL_HAS_ID;
    rows[1].latitude_e7=rows[1].longitude_e7=rows[1].altitude_dm=0;rows[1].altitude_kind=0U;
    CHECK(publish(1U,100U)==CERBERUS_REMOTE_ID_PUBLISHED);
    const cerberus_remote_id_bank_t *wifi=cerberus_remote_id_inventory_bank(&inventory,CERBERUS_WATCH_RADIO_WIFI);
    CHECK(wifi->count==2U&&wifi->status.stored_count==3U&&wifi->receipt_ms==100U);
    CHECK(wifi->rows[0].address[5]==1U&&wifi->rows[1].address[5]==3U);
    prepare(CERBERUS_WATCH_RADIO_BLE,3U,7U);
    rows[1].address_type=1U;memcpy(rows[1].address,rows[0].address,6U);
    rows[2].address_type=2U;memcpy(rows[2].address,rows[0].address,6U);
    CHECK(publish(1U,0U)==CERBERUS_REMOTE_ID_PUBLISHED); /* Separate radio clocks/tokens; real receipt time zero. */
    cerberus_remote_id_page_t page;
    CHECK(cerberus_remote_id_inventory_page(&inventory,0U,&page)&&page.total==5U&&page.copied==4U);
    CHECK(page.rows[0].radio==CERBERUS_WATCH_RADIO_WIFI&&page.rows[2].radio==CERBERUS_WATCH_RADIO_BLE);
    CHECK(page.rows[0].receipt_ms==100U&&page.rows[2].receipt_ms==0U);
    CHECK(!cerberus_remote_id_source_equal(&page.rows[2],&page.rows[3]));
    cerberus_remote_id_page_t frozen=page;
    CHECK(cerberus_remote_id_inventory_page(&inventory,4U,&page)&&page.total==5U&&page.copied==1U);
    CHECK(page.rows[0].observation.address_type==2U);
    CHECK(cerberus_remote_id_inventory_page(&inventory,5U,&page)&&page.total==5U&&!page.copied);
    CHECK(cerberus_remote_id_inventory_page(&inventory,SIZE_MAX,&page)&&page.total==5U&&!page.copied);
    prepare(CERBERUS_WATCH_RADIO_WIFI,0U,8U);
    CHECK(publish(2U,101U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(frozen.rows[0].observation.address[5]==1U&&frozen.rows[1].observation.address[5]==3U);
    CHECK(cerberus_remote_id_inventory_page(&inventory,0U,&page)&&page.total==3U&&page.rows[0].radio==CERBERUS_WATCH_RADIO_BLE);
    prepare(CERBERUS_WATCH_RADIO_WIFI,12U,9U);before.flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;
    before.dropped_records=1U;after=before;CHECK(publish(3U,102U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(wifi->status.flags==CERBERUS_WATCH_CAPTURE_OVERFLOW&&wifi->status.dropped_records==1U);
    prepare(CERBERUS_WATCH_RADIO_BLE,12U,9U);CHECK(publish(2U,102U)==CERBERUS_REMOTE_ID_PUBLISHED);
    for(size_t offset=0U;offset<=25U;++offset){
        CHECK(cerberus_remote_id_inventory_page(&inventory,offset,&page));
        CHECK(page.total==24U&&page.offset==offset);
        size_t expected=offset<24U?24U-offset:0U;if(expected>4U)expected=4U;
        CHECK(page.copied==expected);
        for(size_t i=0;i<expected;++i){
            size_t index=offset+i;
            CHECK(page.rows[i].radio==(index<12U?CERBERUS_WATCH_RADIO_WIFI:CERBERUS_WATCH_RADIO_BLE));
            CHECK(page.rows[i].observation.address[5]==index%12U+1U);
        }
    }
    /* Caller-owned staging can be reused immediately without changing committed rows. */
    prepare(CERBERUS_WATCH_RADIO_WIFI,2U,10U);memcpy(inventory.staging,rows,2U*sizeof(rows[0]));
    CHECK(cerberus_remote_id_inventory_publish(&inventory,4U,&before,&after,inventory.staging,2U,103U)==CERBERUS_REMOTE_ID_PUBLISHED);
    memset(inventory.staging,0xa5,sizeof(inventory.staging));
    CHECK(wifi->rows[0].address[5]==1U&&wifi->rows[1].address[5]==2U&&wifi->count==2U);
    CHECK(zero(&wifi->rows[2],10U*sizeof(wifi->rows[0])));
}
static void tokens_and_atomicity(void)
{
    cerberus_remote_id_inventory_init(&inventory);prepare(CERBERUS_WATCH_RADIO_WIFI,1U,0U);
    CHECK(publish(1U,0U)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_record_t painted=first_record();CHECK(cerberus_remote_id_inventory_select(&inventory,&painted));
    reject_unchanged(1U,9999U,CERBERUS_REMOTE_ID_DUPLICATE);
    ++rows[0].rssi;reject_unchanged(1U,9999U,CERBERUS_REMOTE_ID_INVALID);--rows[0].rssi;
    ++before.reports;after=before;reject_unchanged(1U,9999U,CERBERUS_REMOTE_ID_INVALID);--before.reports;after=before;
    /* A byte generation may repeat: only the wide caller token establishes a new sweep. */
    for(uint64_t token=2U;token<=258U;++token){
        before.generation=after.generation=(uint8_t)(token-1U);
        rows[0].rssi=(int8_t)(-40-(int)(token%10U));
        CHECK(publish(token,token*100U)==CERBERUS_REMOTE_ID_PUBLISHED);
        CHECK(inventory.selected.matching_sweeps==token&&inventory.selected.latest.sweep_token==token);
        CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted)));
    }
    reject_unchanged(257U,30000U,CERBERUS_REMOTE_ID_STALE);
    reject_unchanged(259U,1U,CERBERUS_REMOTE_ID_STALE);
    CHECK(publish(UINT64_MAX,UINT64_MAX)==CERBERUS_REMOTE_ID_PUBLISHED);
    reject_unchanged(UINT64_MAX,UINT64_MAX,CERBERUS_REMOTE_ID_DUPLICATE);
    reject_unchanged(1U,UINT64_MAX,CERBERUS_REMOTE_ID_STALE);
    cerberus_remote_id_inventory_init(&inventory);CHECK(publish(1U,0U)==CERBERUS_REMOTE_ID_PUBLISHED);
}
static void selection_lifecycle(void)
{
    cerberus_remote_id_inventory_init(&inventory);prepare(CERBERUS_WATCH_RADIO_WIFI,2U,1U);
    CHECK(publish(1U,0U)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_record_t painted=first_record();CHECK(cerberus_remote_id_inventory_select(&inventory,&painted));
    CHECK(inventory.selected.valid&&inventory.selected.present&&!inventory.selected.radio_unavailable);
    CHECK(inventory.selected.matching_sweeps==1U&&inventory.selected.latest.receipt_ms==0U);
    cerberus_remote_id_selection_t selected=inventory.selected;
    prepare(CERBERUS_WATCH_RADIO_BLE,1U,1U);CHECK(publish(1U,500U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(!memcmp(&selected,&inventory.selected,sizeof(selected)));
    prepare(CERBERUS_WATCH_RADIO_WIFI,1U,2U);rows[0].address[5]=2U;
    CHECK(publish(2U,1000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(!inventory.selected.present&&!inventory.selected.radio_unavailable);
    CHECK(inventory.selected.checked_receipt_ms==1000U&&inventory.selected.latest.receipt_ms==0U);
    CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted))&&inventory.selected.matching_sweeps==1U);
    prepare(CERBERUS_WATCH_RADIO_WIFI,0U,3U);before.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;before.reports=0U;after=before;
    CHECK(publish(3U,2000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(!inventory.selected.present&&inventory.selected.radio_unavailable&&inventory.selected.latest.receipt_ms==0U);
    prepare(CERBERUS_WATCH_RADIO_WIFI,0U,4U);CHECK(publish(4U,3000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(!inventory.selected.present&&!inventory.selected.radio_unavailable&&inventory.selected.matching_sweeps==1U);
    prepare(CERBERUS_WATCH_RADIO_WIFI,1U,5U);
    memset(rows[0].id,0,sizeof(rows[0].id));strcpy(rows[0].id,"CHANGED-CLAIM");
    rows[0].flags=SENTINEL_WATCH_PROTOCOL_HAS_ID;
    rows[0].latitude_e7=rows[0].longitude_e7=rows[0].altitude_dm=0;rows[0].altitude_kind=0;
    CHECK(publish(5U,4000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(inventory.selected.present&&inventory.selected.matching_sweeps==2U);
    CHECK(!memcmp(&inventory.selected.latest.observation,&rows[0],sizeof(rows[0])));
    CHECK(inventory.selected.latest.receipt_ms==4000U&&inventory.selected.latest.observation.latitude_e7==0);
    CHECK(inventory.selected.frozen.observation.latitude_e7==-1&&inventory.selected.frozen.receipt_ms==0U);
    reject_unchanged(5U,9000U,CERBERUS_REMOTE_ID_DUPLICATE);
    inventory.selected.matching_sweeps=UINT32_MAX;before.generation=after.generation=6U;
    CHECK(publish(6U,5000U)==CERBERUS_REMOTE_ID_PUBLISHED&&inventory.selected.matching_sweeps==UINT32_MAX);
    /* Paint at token1, tap after token6: copy the painted identity, not current page index. */
    CHECK(cerberus_remote_id_inventory_select(&inventory,&painted));
    CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted))&&inventory.selected.present);
    CHECK(inventory.selected.latest.sweep_token==6U&&inventory.selected.matching_sweeps==2U);
    prepare(CERBERUS_WATCH_RADIO_WIFI,1U,7U);rows[0].address[5]=2U;CHECK(publish(7U,6000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(cerberus_remote_id_inventory_select(&inventory,&painted)&&!inventory.selected.present);
    CHECK(inventory.selected.latest.sweep_token==painted.sweep_token&&inventory.selected.latest.receipt_ms==0U);
    CHECK(inventory.selected.checked_sweep_token==7U&&inventory.selected.matching_sweeps==1U);
    /* A saved selected record may itself be supplied without aliasing destruction. */
    CHECK(cerberus_remote_id_inventory_select(&inventory,&inventory.selected.frozen));
    CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted)));
    selected=inventory.selected;cerberus_remote_id_record_t forged=first_record();
    ++forged.observation.rssi;CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    CHECK(!memcmp(&selected,&inventory.selected,sizeof(selected)));
    forged=painted;forged.sweep_token=8U;CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    forged=painted;forged.receipt_ms=6001U;CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    forged=painted;forged.observation.kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
    CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    cerberus_remote_id_inventory_clear_selection(&inventory);CHECK(zero(&inventory.selected,sizeof(inventory.selected)));
    /* Same address bytes with a different BLE type cannot satisfy selected source. */
    prepare(CERBERUS_WATCH_RADIO_BLE,1U,2U);CHECK(publish(2U,1000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&inventory,1U,&page)&&page.copied==1U);
    CHECK(cerberus_remote_id_inventory_select(&inventory,&page.rows[0]));
    rows[0].address_type=1U;before.generation=after.generation=3U;
    CHECK(publish(3U,2000U)==CERBERUS_REMOTE_ID_PUBLISHED&&!inventory.selected.present);
    CHECK(inventory.selected.latest.observation.address_type==0U);
}
static void reject_bad_status(void)
{
    cerberus_remote_id_inventory_init(&inventory);prepare(CERBERUS_WATCH_RADIO_WIFI,1U,1U);
    CHECK(publish(1U,1000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    for(size_t byte=0;byte<sizeof(after);++byte){
        prepare(CERBERUS_WATCH_RADIO_WIFI,2U,2U);((uint8_t *)&after)[byte]^=1U;
        reject_unchanged(2U,2000U,CERBERUS_REMOTE_ID_INVALID);
    }
    for(unsigned defect=0U;defect<11U;++defect){
        prepare(CERBERUS_WATCH_RADIO_WIFI,2U,2U);
        if(defect==0U)before.radio=0U;
        if(defect==1U)before.state=CERBERUS_WATCH_RUNNING;
        if(defect==2U)before.stored_count=13U;
        if(defect==3U)before.reports=1U;
        if(defect==4U)before.flags=0x8000U;
        if(defect==5U)before.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;
        if(defect==6U)before.flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;
        if(defect==7U)before.dropped_records=1U;
        if(defect==8U){before.flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;before.dropped_records=1U;}
        if(defect==9U){before.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;before.stored_count=0U;}
        if(defect==10U){before.stored_count=12U;before.reports=12U;before.dropped_records=1U;before.flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;}
        after=before;reject_unchanged(2U,2000U,CERBERUS_REMOTE_ID_INVALID);
    }
}
static void reject_bad_rows(void)
{
    cerberus_remote_id_inventory_init(&inventory);prepare(CERBERUS_WATCH_RADIO_WIFI,1U,1U);
    CHECK(publish(1U,1000U)==CERBERUS_REMOTE_ID_PUBLISHED);
    for(unsigned defect=0U;defect<24U;++defect){
        prepare(CERBERUS_WATCH_RADIO_WIFI,2U,2U);
        cerberus_watch_observation_t *bad=&rows[1];
        switch(defect){
        case 0:bad->address_type=4U;break;
        case 1:bad->flags|=8U;break;
        case 2:bad->kind=99U;break;
        case 3:memset(bad->address,0,6U);break;
        case 4:bad->address[0]|=1U;break;
        case 5:bad->address_type=1U;break;
        case 6:bad->channel=0U;break;
        case 7:bad->transport=SENTINEL_WATCH_TRANSPORT_BLE;break;
        case 8:memset(bad->id,'A',sizeof(bad->id));break;
        case 9:bad->id[0]=1;break;
        case 10:bad->id[20]='X';break;
        case 11:bad->id[0]=0;break;
        case 12:bad->flags&=(uint8_t)~SENTINEL_WATCH_PROTOCOL_HAS_ID;break;
        case 13:bad->latitude_e7=900000001;break;
        case 14:bad->latitude_e7=-900000001;break;
        case 15:bad->longitude_e7=1800000001;break;
        case 16:bad->longitude_e7=-1800000001;break;
        case 17:bad->flags&=(uint8_t)~SENTINEL_WATCH_PROTOCOL_HAS_POSITION;break;
        case 18:bad->altitude_dm=-10000;break;
        case 19:bad->altitude_dm=317676;break;
        case 20:bad->altitude_kind=0;break;
        case 21:bad->flags&=(uint8_t)~SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE;break;
        case 22:bad->kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;break; /* Must validate ignored rows. */
        case 23:*bad=rows[0];break; /* Duplicate exact source, not another observation. */
        }
        reject_unchanged(2U,2000U,CERBERUS_REMOTE_ID_INVALID);
    }
    prepare(CERBERUS_WATCH_RADIO_WIFI,2U,2U);rows[1]=rows[0];rows[1].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    CHECK(publish(2U,2000U)==CERBERUS_REMOTE_ID_PUBLISHED); /* Same addr, separate transport. */
    for(unsigned defect=0U;defect<3U;++defect){
        prepare(CERBERUS_WATCH_RADIO_BLE,1U,1U);
        if(defect==0U)rows[0].channel=6U;
        if(defect==1U)rows[0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
        if(defect==2U)rows[0].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
        reject_unchanged(1U,1000U,CERBERUS_REMOTE_ID_INVALID);
    }
    /* Valid boundary coordinates/altitudes and all BLE address types are kept. */
    for(uint8_t type=0U;type<=3U;++type){
        prepare(CERBERUS_WATCH_RADIO_BLE,2U,type);rows[0].address_type=rows[1].address_type=type;
        rows[0].latitude_e7=-900000000;rows[0].longitude_e7=-1800000000;rows[0].altitude_dm=-9995;
        rows[1].latitude_e7=900000000;rows[1].longitude_e7=1800000000;rows[1].altitude_dm=317675;
        rows[1].altitude_kind=SENTINEL_WATCH_ALTITUDE_PRESSURE;
        CHECK(publish((uint64_t)type+1U,1000U+type)==CERBERUS_REMOTE_ID_PUBLISHED);
    }
}
static void source_keys(void)
{
    cerberus_remote_id_record_t a={.radio=CERBERUS_WATCH_RADIO_WIFI,.observation=row(CERBERUS_WATCH_RADIO_WIFI,0U)};
    cerberus_remote_id_record_t b=a;
    CHECK(cerberus_remote_id_source_equal(&a,&b));
    b.receipt_ms=100U;b.sweep_token=99U;b.observation.id[0]='Z';b.observation.rssi=-99;
    CHECK(cerberus_remote_id_source_equal(&a,&b));
    b.radio=CERBERUS_WATCH_RADIO_BLE;CHECK(!cerberus_remote_id_source_equal(&a,&b));
    b=a;b.observation.address_type=1U;CHECK(!cerberus_remote_id_source_equal(&a,&b));
    b=a;b.observation.transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;CHECK(!cerberus_remote_id_source_equal(&a,&b));
    b=a;b.observation.address[5]++;CHECK(!cerberus_remote_id_source_equal(&a,&b));
    CHECK(!cerberus_remote_id_source_equal(NULL,&b)&&!cerberus_remote_id_source_equal(&a,NULL));
}

static void observed_timing(void)
{
    cerberus_remote_id_inventory_init(&inventory);
    prepare(CERBERUS_WATCH_RADIO_WIFI,3U,255U);
    rows[0].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;rows[0].flags=1;
    rows[0].latitude_e7=rows[0].longitude_e7=rows[0].altitude_dm=rows[0].altitude_kind=0;
    cerberus_watch_receive_timing_t timings[12]={0};
    for(unsigned i=0;i<3;++i)timings[i]=(cerberus_watch_receive_timing_t){
        .request_started_us=1000999U-i,.source_age_ms=(uint64_t)UINT32_MAX+i,.valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,1,&before,&after,
        rows,timings,3,1000)==CERBERUS_REMOTE_ID_PUBLISHED);
    const cerberus_remote_id_bank_t *bank=&inventory.banks[0];
    CHECK(bank->count==2&&bank->rows[0].address[5]==2);
    CHECK(cerberus_watch_receive_timing_equal(&bank->timings[0],&timings[1]));
    CHECK(cerberus_watch_receive_timing_equal(&bank->timings[1],&timings[2]));
    CHECK(zero(bank->timings+2,10*sizeof(bank->timings[0])));
    cerberus_remote_id_record_t painted=first_record();
    CHECK(cerberus_watch_receive_timing_equal(&painted.timing,&timings[1]));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&painted));
    cerberus_remote_id_inventory_t saved=inventory;
    cerberus_remote_id_record_t forged=painted;++forged.timing.source_age_ms;
    CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    /* Later readout of the same completed sweep never refreshes its anchor. */
    for(unsigned i=0;i<3;++i){timings[i].request_started_us=2000999;timings[i].source_age_ms+=1000;}
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,1,&before,&after,
        rows,timings,3,2000)==CERBERUS_REMOTE_ID_DUPLICATE);
    CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    for(unsigned defect=0;defect<4;++defect){
        cerberus_watch_receive_timing_t bad[12];memcpy(bad,timings,sizeof(bad));
        /* Even an ignored Pwn row must have coherent timing. */
        unsigned index=defect==3?0:2;
        if(defect==0||defect==3)bad[index].valid=false;
        if(defect==1)bad[index].source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS+1;
        if(defect==2)bad[index].request_started_us=2001000;
        CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,2,&before,&after,
            rows,bad,3,2000)==CERBERUS_REMOTE_ID_INVALID);
        CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    }
    /* A newer ordinary untimed caller replaces timing rather than inheriting it. */
    prepare(CERBERUS_WATCH_RADIO_WIFI,1,0);rows[0]=painted.observation;
    memset(rows[0].id,0,sizeof(rows[0].id));strcpy(rows[0].id,"NEW-ID");rows[0].flags=1;
    rows[0].latitude_e7=rows[0].longitude_e7=rows[0].altitude_dm=rows[0].altitude_kind=0;
    CHECK(publish(2,2000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(inventory.selected.present&&!inventory.selected.latest.timing.valid);
    CHECK(zero(&inventory.selected.latest.timing,sizeof(inventory.selected.latest.timing)));
    CHECK(!inventory.selected.latest.observation.latitude_e7);
    CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted)));
    timings[0]=(cerberus_watch_receive_timing_t){.request_started_us=3000999,.source_age_ms=12,.valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,3,&before,&after,
        rows,timings,1,3000)==CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_record_t latest=inventory.selected.latest;
    prepare(CERBERUS_WATCH_RADIO_WIFI,0,1);
    CHECK(publish(4,4000)==CERBERUS_REMOTE_ID_PUBLISHED&&!inventory.selected.present);
    CHECK(!memcmp(&inventory.selected.latest,&latest,sizeof(latest)));
    CHECK(inventory.selected.checked_receipt_ms==4000);
    before.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;before.reports=0;after=before;
    CHECK(publish(5,5000)==CERBERUS_REMOTE_ID_PUBLISHED&&inventory.selected.radio_unavailable);
    CHECK(!memcmp(&inventory.selected.latest,&latest,sizeof(latest)));
    prepare(CERBERUS_WATCH_RADIO_WIFI,1,2);rows[0]=painted.observation;
    timings[0]=(cerberus_watch_receive_timing_t){.request_started_us=6000001,.source_age_ms=0,.valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,6,&before,&after,
        rows,timings,1,6000)==CERBERUS_REMOTE_ID_PUBLISHED&&inventory.selected.present);
    CHECK(cerberus_watch_receive_timing_equal(&inventory.selected.latest.timing,&timings[0]));
    CHECK(!memcmp(&inventory.selected.frozen,&painted,sizeof(painted)));
    /* Zero is a valid request time; source ages and S3 anchors stay full-width. */
    cerberus_remote_id_inventory_init(&inventory);
    timings[0]=(cerberus_watch_receive_timing_t){.valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,1,&before,&after,
        rows,timings,1,0)==CERBERUS_REMOTE_ID_PUBLISHED);
    timings[0].request_started_us=UINT64_MAX;timings[0].source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS;
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,2,&before,&after,
        rows,timings,1,UINT64_MAX/1000)==CERBERUS_REMOTE_ID_PUBLISHED);
    bank=&inventory.banks[0];
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,3,&before,&after,
        bank->rows,bank->timings,1,UINT64_MAX/1000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(bank->timings[0].request_started_us==UINT64_MAX);
}
static void follow_hints(void)
{
    cerberus_remote_id_follow_hint_t hint;
    memset(&hint, 0xff, sizeof(hint));
    CHECK(!cerberus_remote_id_inventory_follow_hint(NULL, 0, &hint));
    CHECK(zero(&hint, sizeof(hint)));
    CHECK(!cerberus_remote_id_inventory_follow_hint(&inventory, 0, NULL));
    cerberus_remote_id_inventory_init(&inventory);
    CHECK(!cerberus_remote_id_inventory_follow_hint(&inventory, 0, &hint));
    prepare(CERBERUS_WATCH_RADIO_WIFI, 2, 1);
    rows[1] = rows[0]; rows[1].transport = SENTINEL_WATCH_TRANSPORT_WIFI_NAN; rows[1].channel = 149;
    cerberus_watch_receive_timing_t timings[2] = {{.valid=true}, {.valid=true}};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory, 1, &before, &after,
        rows, timings, 2, 0) == CERBERUS_REMOTE_ID_PUBLISHED);
    cerberus_remote_id_page_t page;
    CHECK(cerberus_remote_id_inventory_page(&inventory, 0, &page));
    CHECK(cerberus_remote_id_inventory_select(&inventory, &page.rows[0]));
    cerberus_remote_id_record_t selected = inventory.selected.frozen;
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 0, &hint));
    CHECK(hint.channel == 6 && hint.transport == SENTINEL_WATCH_TRANSPORT_WIFI_BEACON);
    CHECK(!memcmp(hint.transmitter, selected.observation.address, 6));
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, UINT64_C(29999000), &hint) && hint.channel == 6);
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, UINT64_C(29999001), &hint) && hint.channel == 0);
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, UINT64_C(30000000), &hint) && hint.channel == 0);
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, UINT64_MAX, &hint) && hint.channel == 0);
    inventory.selected.latest.timing.request_started_us = 1;
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 0, &hint) && hint.channel == 0);
    inventory.selected.latest.timing = (cerberus_watch_receive_timing_t){0};
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 0, &hint) && hint.channel == 0);
    /* A newer sweep sees only the same MAC on NAN: it cannot refresh Beacon. */
    prepare(CERBERUS_WATCH_RADIO_WIFI, 1, 2);
    rows[0] = page.rows[1].observation;
    timings[0] = (cerberus_watch_receive_timing_t){.request_started_us=1000000, .valid=true};
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory, 2, &before, &after,
        rows, timings, 1, 1000) == CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(!inventory.selected.present);
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint) && !hint.channel);
    CHECK(hint.transport == SENTINEL_WATCH_TRANSPORT_WIFI_BEACON);
    /* Return with changed claimed ID/channel keeps exact source, fresh evidence. */
    rows[0] = selected.observation; rows[0].channel = 44;
    memset(rows[0].id, 0, sizeof(rows[0].id)); strcpy(rows[0].id, "CHANGED-ID");
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory, 3, &before, &after,
        rows, timings, 1, 1000) == CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint) && hint.channel == 44);
    CHECK(!memcmp(&inventory.selected.frozen, &selected, sizeof(selected)));
    cerberus_remote_id_selection_t valid = inventory.selected;
    for (unsigned channel = 0; channel < 256; ++channel) {
        inventory.selected.latest.observation.channel = (uint8_t)channel;
        bool supported = (channel >= 1 && channel <= 14) || (channel >= 36 && channel <= 64 && channel % 4 == 0) ||
            (channel >= 100 && channel <= 144 && channel % 4 == 0) ||
            (channel >= 149 && channel <= 177 && (channel - 149) % 4 == 0);
        CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint));
        CHECK(hint.channel == (supported ? channel : 0));
    }
    for (unsigned defect = 0; defect < 5; ++defect) {
        inventory.selected = valid;
        if (defect == 0) inventory.selected.present = false;
        if (defect == 1) inventory.selected.radio_unavailable = true;
        if (defect == 2) inventory.selected.latest.observation.address[5] ^= 1;
        if (defect == 3) inventory.selected.latest.observation.transport = SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
        if (defect == 4) inventory.selected.latest.radio = CERBERUS_WATCH_RADIO_BLE;
        CHECK(cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint) && !hint.channel);
        CHECK(!memcmp(hint.transmitter, selected.observation.address, 6));
    }
    inventory.selected = valid; inventory.selected.frozen.observation.address[0] |= 1;
    CHECK(!cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint) && zero(&hint, sizeof(hint)));
    prepare(CERBERUS_WATCH_RADIO_BLE, 1, 1);
    CHECK(publish(1, 1000) == CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(cerberus_remote_id_inventory_page(&inventory, 0, &page));
    CHECK(cerberus_remote_id_inventory_select(&inventory, &page.rows[1]));
    CHECK(!cerberus_remote_id_inventory_follow_hint(&inventory, 1000000, &hint) && zero(&hint, sizeof(hint)));
}

static void detailed_motion(void)
{
    cerberus_remote_id_inventory_init(&inventory);prepare(CERBERUS_WATCH_RADIO_WIFI,3,7);
    rows[0].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;rows[0].flags=SENTINEL_WATCH_PROTOCOL_HAS_ID;
    rows[0].latitude_e7=rows[0].longitude_e7=rows[0].altitude_dm=rows[0].altitude_kind=0;
    sentinel_watch_motion_t motions[12]={{0}};
    motions[1]=(sentinel_watch_motion_t){.speed_qms=1017,.direction_deg=360,.height_encoded=UINT16_MAX,
        .flags=SENTINEL_WATCH_MOTION_LOCATION_PRESENT|SENTINEL_WATCH_MOTION_SPEED|SENTINEL_WATCH_MOTION_DIRECTION|
            SENTINEL_WATCH_MOTION_HEIGHT|SENTINEL_WATCH_MOTION_HEIGHT_AGL};
    motions[2].flags=SENTINEL_WATCH_MOTION_LOCATION_PRESENT; /* Recognized unavailable fields. */
    cerberus_watch_receive_timing_t timings[12]={{0}},motion_times[12]={{0}};
    for(unsigned i=0;i<3;++i)timings[i]=(cerberus_watch_receive_timing_t){1000000,10,true};
    motion_times[1]=(cerberus_watch_receive_timing_t){1000000,12000,true};
    motion_times[2]=(cerberus_watch_receive_timing_t){1000000,999,true};
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,1,&before,&after,rows,timings,
        motions,motion_times,3,1000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(inventory.banks[0].count==2&&inventory.banks[0].motions[0].speed_qms==1017);
    CHECK(inventory.banks[0].motion_timings[0].source_age_ms==12000);
    CHECK(inventory.banks[0].motions[1].flags==SENTINEL_WATCH_MOTION_LOCATION_PRESENT);
    CHECK(zero(inventory.banks[0].motions+2,10*sizeof(motions[0])));
    cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&inventory,0,&page));
    CHECK(cerberus_remote_id_inventory_select(&inventory,&page.rows[0]));
    cerberus_remote_id_inventory_t saved=inventory;
    cerberus_remote_id_record_t forged=page.rows[0];--forged.motion.direction_deg;
    CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    forged=page.rows[0];++forged.motion_timing.source_age_ms;
    CHECK(!cerberus_remote_id_inventory_select(&inventory,&forged));
    CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    for(unsigned defect=0;defect<7;++defect){
        sentinel_watch_motion_t bad_motion[12];memcpy(bad_motion,motions,sizeof(motions));
        cerberus_watch_receive_timing_t bad_time[12];memcpy(bad_time,motion_times,sizeof(motion_times));
        if(defect==0)bad_motion[2].reserved=1;
        if(defect==1)bad_time[2].valid=false;
        if(defect==2)bad_time[2].request_started_us=2001000;
        if(defect==3)bad_time[0]=(cerberus_watch_receive_timing_t){.valid=true};
        if(defect==4)bad_motion[0].flags=SENTINEL_WATCH_MOTION_LOCATION_PRESENT;
        CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,2,&before,&after,rows,timings,
            defect==5?NULL:bad_motion,defect==6?NULL:bad_time,3,2000)==CERBERUS_REMOTE_ID_INVALID);
        CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    }
    --motions[1].direction_deg;
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,1,&before,&after,rows,timings,
        motions,motion_times,3,1000)==CERBERUS_REMOTE_ID_INVALID);
    ++motions[1].direction_deg;
    motion_times[1].request_started_us=2000000;motion_times[1].source_age_ms=13000;
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,1,&before,&after,rows,timings,
        motions,motion_times,3,2000)==CERBERUS_REMOTE_ID_DUPLICATE);
    CHECK(cerberus_remote_id_inventory_publish_observed(&inventory,1,&before,&after,rows,timings,3,2000)==CERBERUS_REMOTE_ID_DUPLICATE);
    CHECK(!memcmp(&saved,&inventory,sizeof(saved)));
    --motions[1].direction_deg;
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,2,&before,&after,rows,timings,
        motions,motion_times,3,2000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(inventory.selected.latest.motion.direction_deg==359&&inventory.selected.frozen.motion.direction_deg==360);
    CHECK(inventory.selected.latest.motion_timing.source_age_ms==13000);
    CHECK(!memcmp(&saved.selected.frozen,&inventory.selected.frozen,sizeof(saved.selected.frozen)));
    prepare(CERBERUS_WATCH_RADIO_WIFI,0,8);
    CHECK(publish(3,3000)==CERBERUS_REMOTE_ID_PUBLISHED&&!inventory.selected.present);
    CHECK(inventory.selected.latest.motion.direction_deg==359);
    uint64_t age=0;CHECK(cerberus_watch_receive_age_ms(&inventory.selected.latest.motion_timing,3000000,&age)&&age==14000);
    prepare(CERBERUS_WATCH_RADIO_WIFI,1,9);rows[0]=saved.selected.frozen.observation;
    CHECK(publish(4,4000)==CERBERUS_REMOTE_ID_PUBLISHED&&inventory.selected.present);
    CHECK(zero(&inventory.selected.latest.motion,sizeof(motions[0]))&&!inventory.selected.latest.motion_timing.valid);
    CHECK(inventory.selected.frozen.motion.flags);
    motions[0].flags=SENTINEL_WATCH_MOTION_LOCATION_PRESENT;
    motion_times[0]=(cerberus_watch_receive_timing_t){0};
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,5,&before,&after,rows,NULL,
        motions,motion_times,1,5000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(inventory.selected.latest.motion.flags&&!inventory.selected.latest.motion_timing.valid);
    cerberus_remote_id_bank_t *bank=&inventory.banks[0];
    CHECK(cerberus_remote_id_inventory_publish_detailed(&inventory,6,&bank->status,&bank->status,bank->rows,bank->timings,
        bank->motions,bank->motion_timings,bank->count,6000)==CERBERUS_REMOTE_ID_PUBLISHED);
    CHECK(bank->motions[0].flags==SENTINEL_WATCH_MOTION_LOCATION_PRESENT);
}

int main(void)
{
    _Static_assert(CERBERUS_REMOTE_ID_CAPACITY==24U,"Two existing twelve-row radio banks");
    init_and_bounds();separate_banks_and_pages();tokens_and_atomicity();selection_lifecycle();
    reject_bad_status();reject_bad_rows();source_keys();observed_timing();follow_hints();detailed_motion();
    printf("Remote ID inventory: %u checks passed; atomic typed radio banks, immutable painted selection, absent/unavailable/returned source, exact new payload, wide sweep tokens and rejected malformed snapshots. Host sizes inventory=%zu bank=%zu record=%zu selection=%zu page=%zu bytes; target measurement pending.\n",
        checks,sizeof(inventory),sizeof(cerberus_remote_id_bank_t),sizeof(cerberus_remote_id_record_t),
        sizeof(cerberus_remote_id_selection_t),sizeof(cerberus_remote_id_page_t));
    return 0;
}
