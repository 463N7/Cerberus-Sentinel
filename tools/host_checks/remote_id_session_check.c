#undef NDEBUG
#include "remote_id_session.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
enum { ACCEPT,REJECT,LOST_DELIVERED,LOST_UNDELIVERED };
typedef struct {
    cerberus_remote_id_radio_observation_t observation;
    cerberus_watch_scan_status_payload_t status;
    cerberus_watch_observation_t rows[12];
    unsigned starts,stops,polls,statuses,reads,status_round,poll_round;
    unsigned start_mode,stop_mode,status_fail,status_corrupt;
    unsigned observed_reads,timing_defect;
    unsigned detailed_reads,motion_defect;
    int row_fail;
    bool poll_fail,end_poll_fail,end_epoch_change,foreign,wrong_identity,exact_id_recovery;
} fake_radio_t;
typedef struct {
    uint64_t now;
    cerberus_remote_id_session_t session;
    cerberus_remote_id_inventory_t inventory;
    fake_radio_t radios[2];
    bool observed_rows,detailed_rows;
} world_t;
static world_t world;
static unsigned slot(uint8_t radio)
{CHECK(radio==CERBERUS_WATCH_RADIO_WIFI||radio==CERBERUS_WATCH_RADIO_BLE);return radio==CERBERUS_WATCH_RADIO_WIFI?0:1;}
static int32_t poll(void *context,uint8_t radio,bool owned,cerberus_remote_id_radio_observation_t *out)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];++r->polls;++r->poll_round;
    (void)owned;
    if(r->poll_fail || (r->end_poll_fail&&r->status_round==2))return -10;
    *out=r->observation;
    if(r->end_epoch_change&&r->status_round==2)++out->epoch;
    return 0;
}
static cerberus_remote_id_command_result_t command(void *context,uint8_t radio,bool start,
    const cerberus_remote_id_radio_observation_t *before)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];
    CHECK(before->epoch==r->observation.epoch && before->generation==r->observation.generation);
    cerberus_remote_id_command_result_t result={0};
    if(start){
        ++r->starts;CHECK(!r->observation.busy && !r->foreign);
        if(r->start_mode==REJECT){result.error=-11;result.rejected=true;return result;}
        if(r->start_mode==LOST_UNDELIVERED){result.error=-12;result.identity_pending=r->exact_id_recovery;return result;}
        ++r->observation.generation;r->observation.state=SENTINEL_SCAN_RUNNING;r->observation.released=false;
        if(r->start_mode==LOST_DELIVERED){result.error=-12;result.identity_pending=r->exact_id_recovery;return result;}
        result.identity_valid=true;result.generation=r->observation.generation;result.epoch=r->observation.epoch;
        if(r->wrong_identity)++result.generation;
        return result;
    }
    ++r->stops;CHECK(!r->foreign && r->observation.state==SENTINEL_SCAN_RUNNING);
    if(r->stop_mode==REJECT){result.error=-13;return result;}
    if(r->stop_mode==LOST_DELIVERED){
        r->observation.state=SENTINEL_SCAN_IDLE;r->observation.released=true;result.error=-13;
    }
    /* ACCEPT deliberately leaves RUNNING until a later asynchronous event. */
    return result;
}
static int32_t status(void *context,uint8_t radio,uint8_t generation,cerberus_watch_scan_status_payload_t *out)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];++r->statuses;++r->status_round;
    CHECK(generation==r->status.generation && r->status_round<=2);
    if(r->status_fail==r->status_round)return -14;
    *out=r->status;
    if(r->status_corrupt==r->status_round)++out->reports;
    return 0;
}
static int32_t row(void *context,uint8_t radio,uint8_t generation,uint8_t index,cerberus_watch_observation_t *out)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];++r->reads;
    CHECK(r->status_round==1 && generation==r->status.generation && index<r->status.stored_count);
    /* Failed getters may leave or change staging; committed output must survive. */
    memset(out,0xa5,sizeof(*out));if((int)index==r->row_fail)return -15;
    *out=r->rows[index];return 0;
}
static uint64_t now(void *context){return ((world_t *)context)->now;}
static int32_t row_observed(void *context,uint8_t radio,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];++r->observed_reads;
    int32_t error=row(context,radio,generation,index,out);if(error)return error;
    *timing=(cerberus_watch_receive_timing_t){.request_started_us=w->now*1000+999,
        .source_age_ms=(uint64_t)UINT32_MAX+index+1,.valid=true};
    if(r->timing_defect==1)return -16;
    if(r->timing_defect==2)++timing->request_started_us;
    if(r->timing_defect==3)timing->valid=false;
    if(r->timing_defect==4)timing->source_age_ms=CERBERUS_WATCH_MAX_SOURCE_AGE_MS+1;
    return 0;
}
static int32_t row_detailed(void *context,uint8_t radio,uint8_t generation,uint8_t index,
    cerberus_watch_observation_t *out,cerberus_watch_receive_timing_t *timing,
    sentinel_watch_motion_t *motion,cerberus_watch_receive_timing_t *motion_timing)
{
    world_t *w=context;fake_radio_t *r=&w->radios[slot(radio)];++r->detailed_reads;
    int32_t error=row_observed(context,radio,generation,index,out,timing);
    if(error)return error;
    timing->source_age_ms=50;
    *motion=(sentinel_watch_motion_t){.speed_qms=(uint16_t)(w->now%255),.direction_deg=360,
        .height_encoded=1,.flags=SENTINEL_WATCH_MOTION_LOCATION_PRESENT|SENTINEL_WATCH_MOTION_SPEED|
            SENTINEL_WATCH_MOTION_DIRECTION|SENTINEL_WATCH_MOTION_HEIGHT};
    *motion_timing=(cerberus_watch_receive_timing_t){.request_started_us=w->now*1000,.source_age_ms=7000,.valid=true};
    if(r->motion_defect==1)return -40;
    if(r->motion_defect==2)motion->reserved=1;
    if(r->motion_defect==3)memset(motion,0,sizeof(*motion));
    if(r->motion_defect==4)motion_timing->request_started_us=(w->now+1)*1000;
    if(r->motion_defect==5)motion_timing->valid=false;
    if(r->motion_defect==6)motion->flags|=128;
    return 0;
}
static cerberus_remote_id_ops_t operations(void)
{return (cerberus_remote_id_ops_t){.context=&world,.poll=poll,.command=command,.status=status,
    .row=world.observed_rows?NULL:row,.now_ms=now,.row_observed=world.observed_rows?row_observed:NULL,
    .row_detailed=world.detailed_rows?row_detailed:NULL};}
static void tick(void)
{
    for(unsigned i=0;i<2;++i)world.radios[i].status_round=world.radios[i].poll_round=0;
    cerberus_remote_id_ops_t ops=operations();cerberus_remote_id_session_tick(&world.session,&world.inventory,&ops);
}
static void reset(bool both)
{
    memset(&world,0,sizeof(world));world.now=1000;
    cerberus_remote_id_session_init(&world.session);cerberus_remote_id_inventory_init(&world.inventory);
    for(unsigned i=0;i<2;++i){
        fake_radio_t *r=&world.radios[i];r->observation=(cerberus_remote_id_radio_observation_t){
            .epoch=50+i,.generation=255,.state=SENTINEL_SCAN_IDLE,.supported=i==0||both,.released=true};
        r->row_fail=-1;
        for(unsigned j=0;j<12;++j){
            r->rows[j]=(cerberus_watch_observation_t){.kind=SENTINEL_WATCH_PROTOCOL_DRONE,
                .transport=i?SENTINEL_WATCH_TRANSPORT_BLE:SENTINEL_WATCH_TRANSPORT_WIFI_BEACON,
                .address_type=i?1:0,.channel=i?0:6,.rssi=-60,.flags=SENTINEL_WATCH_PROTOCOL_HAS_ID,
                .address={2,3,4,5,6,(uint8_t)j}};
            strcpy(r->rows[j].id,"SAME-CLAIM");
        }
    }
}
static void complete(unsigned index,uint8_t count)
{
    fake_radio_t *r=&world.radios[index];r->observation.state=SENTINEL_SCAN_COMPLETE;r->observation.released=true;
    r->status=(cerberus_watch_scan_status_payload_t){.radio=index?CERBERUS_WATCH_RADIO_BLE:CERBERUS_WATCH_RADIO_WIFI,
        .generation=r->observation.generation,.state=CERBERUS_WATCH_COMPLETE,.stored_count=count,.reports=count,.duration_ms=1000};
}
static void start(void)
{CHECK(cerberus_remote_id_session_start(&world.session));tick();}
static void publish(unsigned index,uint8_t count)
{
    complete(index,count);tick();CHECK(world.session.radios[index].capture_pending);
    ++world.now;tick();CHECK(!world.session.radios[index].capture_pending && !world.session.radios[index].error);
}
static void independent_and_selection(void)
{
    reset(true);
    /* Opening and repainting a quiescent session must not touch either radio,
     * including read-only transport polls. START performs the fresh poll. */
    for(unsigned i=0;i<10;++i){world.now+=500;tick();}
    for(unsigned i=0;i<2;++i){
        CHECK(!world.radios[i].polls&&!world.radios[i].starts&&!world.radios[i].stops);
        CHECK(!world.radios[i].statuses&&!world.radios[i].reads&&!world.session.radios[i].observed);
    }
    CHECK(cerberus_remote_id_session_released(&world.session));
    start();CHECK(world.radios[0].starts==1&&world.radios[1].starts==1);
    CHECK(world.radios[0].polls==1&&world.radios[1].polls==1);
    CHECK(world.session.radios[0].owned&&world.session.radios[1].owned);
    CHECK(!cerberus_remote_id_session_start(&world.session));
    publish(0,1);CHECK(world.inventory.banks[0].count==1&&!world.inventory.banks[1].published);
    CHECK(world.radios[1].statuses==0 && world.session.radios[1].owned);
    cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&world.inventory,0,&page));
    CHECK(cerberus_remote_id_inventory_select(&world.inventory,&page.rows[0]));
    cerberus_remote_id_record_t frozen=world.inventory.selected.frozen;
    publish(1,2);CHECK(world.inventory.banks[1].count==2 && world.inventory.banks[0].count==1);
    CHECK(world.inventory.selected.matching_sweeps==1&&!memcmp(&frozen,&world.inventory.selected.frozen,sizeof(frozen)));
    CHECK(world.radios[0].starts==2); /* first radio rearmed independently */
    world.radios[0].rows[0].flags=7;world.radios[0].rows[0].latitude_e7=1;world.radios[0].rows[0].longitude_e7=-1;
    world.radios[0].rows[0].altitude_dm=10;world.radios[0].rows[0].altitude_kind=1;
    publish(0,1);CHECK(world.inventory.selected.matching_sweeps==2&&world.inventory.selected.latest.observation.flags==7);
    CHECK(!memcmp(&frozen,&world.inventory.selected.frozen,sizeof(frozen)));
    /* Stop prevents any automatic rearm/publication from old terminal polls. */
    cerberus_remote_id_session_stop(&world.session);tick();
    unsigned starts=world.radios[0].starts+world.radios[1].starts;
    world.radios[1].observation.state=SENTINEL_SCAN_IDLE;world.radios[1].observation.released=true;tick();
    CHECK(cerberus_remote_id_session_released(&world.session));
    uint64_t receipt=world.inventory.banks[0].receipt_ms;
    unsigned polls[2],statuses[2],reads[2],stops[2];
    for(unsigned i=0;i<2;++i){
        polls[i]=world.radios[i].polls;statuses[i]=world.radios[i].statuses;
        reads[i]=world.radios[i].reads;stops[i]=world.radios[i].stops;
    }
    for(unsigned i=0;i<10;++i){world.now+=1000;tick();}
    CHECK(world.radios[0].starts+world.radios[1].starts==starts && world.inventory.banks[0].receipt_ms==receipt);
    for(unsigned i=0;i<2;++i){
        CHECK(world.radios[i].polls==polls[i]&&world.radios[i].statuses==statuses[i]);
        CHECK(world.radios[i].reads==reads[i]&&world.radios[i].stops==stops[i]);
    }
}
static void starts_and_foreign_work(void)
{
    reset(true);world.radios[0].foreign=true;world.radios[0].observation.busy=true;
    world.radios[0].observation.state=SENTINEL_SCAN_RUNNING;world.radios[0].observation.released=false;
    start();CHECK(!world.radios[0].starts&&!world.session.radios[0].owned&&world.radios[1].starts==1);
    cerberus_remote_id_session_stop(&world.session);tick();CHECK(!world.radios[0].stops&&world.radios[1].stops==1);
    reset(false);world.radios[0].start_mode=REJECT;start();CHECK(!world.session.radios[0].owned);
    world.now=1999;tick();CHECK(world.radios[0].starts==1);
    world.now=2000;tick();CHECK(world.radios[0].starts==2);
    world.now=3999;tick();CHECK(world.radios[0].starts==2);
    world.now=4000;tick();CHECK(world.radios[0].starts==3&&world.session.radios[0].faulted);
    world.now=100000;tick();CHECK(world.radios[0].starts==3&&!world.radios[0].stops);
    cerberus_remote_id_session_stop(&world.session);world.radios[0].start_mode=ACCEPT;start();CHECK(world.radios[0].starts==4);
    reset(false);world.radios[0].start_mode=REJECT;start();
    world.radios[0].foreign=true;world.radios[0].observation.busy=true;world.radios[0].observation.state=SENTINEL_SCAN_RUNNING;
    world.radios[0].observation.released=false;world.now+=5000;tick();
    CHECK(!world.session.radios[0].owned && world.radios[0].starts==1);
    cerberus_remote_id_session_stop(&world.session);tick();CHECK(!world.radios[0].stops);
    reset(false);world.radios[0].wrong_identity=true;start();tick();
    CHECK(world.session.radios[0].faulted&&!world.session.radios[0].owned&&!world.radios[0].stops);
    for(unsigned mode=LOST_DELIVERED;mode<=LOST_UNDELIVERED;++mode){
        reset(false);world.radios[0].start_mode=mode;start();CHECK(world.session.radios[0].owned);
        tick();
        if(mode==LOST_DELIVERED){
            CHECK(world.session.radios[0].owned&&!world.session.radios[0].scan.command_failed);
            publish(0,1);CHECK(world.inventory.banks[0].published);
        } else {
            CHECK(!world.session.radios[0].owned&&!world.session.radios[0].faulted&&!world.radios[0].stops);
            world.radios[0].start_mode=ACCEPT;world.now=2000;tick();CHECK(world.radios[0].starts==2);
        }
    }
    for(unsigned change=0;change<2;++change){
        reset(false);start();world.radios[0].foreign=true;
        if(change)++world.radios[0].observation.epoch;else ++world.radios[0].observation.generation;
        tick();CHECK(!world.session.radios[0].owned&&world.session.radios[0].faulted);
        cerberus_remote_id_session_stop(&world.session);tick();CHECK(!world.radios[0].stops);
    }
    reset(false);world.session.radios[0].token=UINT64_MAX;start();
    CHECK(!world.radios[0].starts&&!world.session.radios[0].owned&&world.session.radios[0].error==CERBERUS_REMOTE_ID_ERROR_TOKEN_EXHAUSTED);
}
static void atomic_capture_failures(void)
{
    for(unsigned defect=0;defect<7;++defect){
        reset(false);start();publish(0,2);cerberus_remote_id_bank_t previous=world.inventory.banks[0];
        cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&world.inventory,0,&page));
        CHECK(cerberus_remote_id_inventory_select(&world.inventory,&page.rows[0]));
        cerberus_remote_id_selection_t selected=world.inventory.selected;
        tick();CHECK(world.radios[0].starts==2);complete(0,2);tick();
        fake_radio_t *r=&world.radios[0];
        if(defect==0)r->status_fail=1;if(defect==1)r->status_fail=2;if(defect==2)r->row_fail=1;
        if(defect==3)r->status_corrupt=2;if(defect==4)r->end_poll_fail=true;
        if(defect==5)r->end_epoch_change=true;if(defect==6)r->rows[1].flags=0xff;
        tick();CHECK(world.session.radios[0].capture_pending&&world.session.radios[0].error);
        CHECK(!memcmp(&previous,&world.inventory.banks[0],sizeof(previous))&&!memcmp(&selected,&world.inventory.selected,sizeof(selected)));
        CHECK(world.radios[0].starts==2&&!world.radios[0].stops);
        r->status_fail=r->status_corrupt=0;r->row_fail=-1;r->end_poll_fail=r->end_epoch_change=false;r->rows[1].flags=1;
        ++world.now;tick();CHECK(!world.session.radios[0].capture_pending&&!world.session.radios[0].error);
        CHECK(world.inventory.banks[0].sweep_token==2 && world.inventory.selected.matching_sweeps==2);
    }
    reset(false);start();complete(0,1);tick();world.radios[0].row_fail=0;
    world.now+=14999;tick();CHECK(world.session.radios[0].capture_pending);
    ++world.now;tick();CHECK(!world.session.radios[0].capture_pending&&world.session.radios[0].faulted&&!world.inventory.banks[0].published);
    world.now+=100000;tick();CHECK(world.radios[0].starts==1);
    reset(false);start();complete(0,0);world.radios[0].status.flags=CERBERUS_WATCH_CAPTURE_UNAVAILABLE;tick();tick();
    CHECK(world.inventory.banks[0].published&&!world.inventory.banks[0].count&&world.inventory.banks[0].status.flags==CERBERUS_WATCH_CAPTURE_UNAVAILABLE);
    reset(false);start();complete(0,12);world.radios[0].status.reports=16;world.radios[0].status.dropped_records=4;
    world.radios[0].status.flags=CERBERUS_WATCH_CAPTURE_OVERFLOW;tick();tick();CHECK(world.inventory.banks[0].count==12);
}
static void stop_retry_and_wrap(void)
{
    reset(false);start();cerberus_remote_id_session_stop(&world.session);tick();
    CHECK(world.radios[0].stops==1 && !cerberus_remote_id_session_released(&world.session));
    CHECK(!cerberus_remote_id_session_start(&world.session));
    world.radios[0].observation.state=SENTINEL_SCAN_COMPLETE;world.radios[0].observation.released=false;
    unsigned polls=world.radios[0].polls;
    tick();CHECK(world.session.radios[0].owned&&!cerberus_remote_id_session_released(&world.session));
    CHECK(world.radios[0].polls==polls+1); /* STOP keeps draining an unreleased host. */
    world.radios[0].observation.released=true;tick();CHECK(cerberus_remote_id_session_released(&world.session));
    CHECK(!world.inventory.banks[0].published);
    reset(false);start();complete(0,1);tick();CHECK(world.session.radios[0].capture_pending);
    cerberus_remote_id_session_stop(&world.session);tick();
    CHECK(!world.session.radios[0].capture_pending&&!world.radios[0].statuses&&cerberus_remote_id_session_released(&world.session));
    CHECK(!world.inventory.banks[0].published);
    reset(false);start();cerberus_remote_id_session_stop(&world.session);tick();
    world.now+=2999;tick();CHECK(world.radios[0].stops==1);
    ++world.now;tick();CHECK(world.radios[0].stops==1&&world.session.radios[0].scan.failure_count==1);
    world.now+=999;tick();CHECK(world.radios[0].stops==1);
    ++world.now;tick();CHECK(world.radios[0].stops==2&&!cerberus_remote_id_session_released(&world.session));
    reset(false);start();world.radios[0].stop_mode=LOST_DELIVERED;cerberus_remote_id_session_stop(&world.session);tick();tick();
    CHECK(world.radios[0].stops==1&&cerberus_remote_id_session_released(&world.session));
    reset(false);world.now=UINT32_MAX-500U;start();world.radios[0].stop_mode=REJECT;
    cerberus_remote_id_session_stop(&world.session);tick();CHECK(world.radios[0].stops==1);
    world.now+=999;tick();CHECK(world.radios[0].stops==1);++world.now;tick();CHECK(world.radios[0].stops==2);
    world.now+=2000;tick();CHECK(world.radios[0].stops==3&&world.session.radios[0].scan.faulted);
    world.now+=10000;tick();CHECK(world.radios[0].stops==3&&!cerberus_remote_id_session_released(&world.session));
    world.radios[0].stop_mode=LOST_DELIVERED;cerberus_remote_id_session_stop(&world.session);tick();tick();
    CHECK(world.radios[0].stops==4&&cerberus_remote_id_session_released(&world.session));
    reset(false);start();world.radios[0].poll_fail=true;cerberus_remote_id_session_stop(&world.session);tick();
    CHECK(!world.radios[0].stops&&world.session.radios[0].owned);world.radios[0].poll_fail=false;tick();CHECK(world.radios[0].stops==1);
    reset(false);start();
    for(unsigned i=0;i<257;++i){
        publish(0,1);CHECK(world.inventory.banks[0].sweep_token==(uint64_t)i+1);
        CHECK(world.inventory.banks[0].status.generation==(uint8_t)i);
        if(i<256)tick();
    }
    CHECK(world.radios[0].starts==257&&world.session.radios[0].published_sweeps==257);
    CHECK(world.inventory.banks[0].status.generation==0&&world.inventory.banks[0].sweep_token==257);
}
static void timed_publication(void)
{
    for(unsigned defect=1;defect<=5;++defect){
        reset(true);world.observed_rows=true;start();publish(0,2);publish(1,1);
        CHECK(world.inventory.banks[0].timings[0].valid&&world.inventory.banks[1].timings[0].valid);
        CHECK(world.inventory.banks[0].timings[1].source_age_ms==(uint64_t)UINT32_MAX+2);
        cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&world.inventory,0,&page));
        CHECK(cerberus_remote_id_inventory_select(&world.inventory,&page.rows[0]));
        cerberus_remote_id_bank_t previous=world.inventory.banks[0];
        cerberus_remote_id_selection_t selected=world.inventory.selected;
        tick();complete(0,2);tick();
        fake_radio_t *r=&world.radios[0];
        if(defect<=4)r->timing_defect=defect;else r->end_epoch_change=true;
        unsigned reads=r->reads,observed=r->observed_reads;
        tick();CHECK(world.session.radios[0].capture_pending&&world.session.radios[0].error);
        CHECK(r->reads-reads==r->observed_reads-observed); /* No fallback second read. */
        CHECK(!memcmp(&previous,&world.inventory.banks[0],sizeof(previous)));
        CHECK(!memcmp(&selected,&world.inventory.selected,sizeof(selected)));
        r->timing_defect=0;r->end_epoch_change=false;++world.now;tick();
        CHECK(!world.session.radios[0].capture_pending&&!world.session.radios[0].error);
        CHECK(world.inventory.selected.latest.timing.request_started_us==world.now*1000+999);
        CHECK(!memcmp(&world.inventory.selected.frozen,&selected.frozen,sizeof(selected.frozen)));
        /* Falling back by explicit API selection in the next sweep clears old timing. */
        world.observed_rows=false;tick();publish(0,2);
        CHECK(!world.inventory.selected.latest.timing.valid);
        CHECK(world.inventory.selected.frozen.timing.valid);
    }
}
static void exact_job_identity_recovery(void)
{
    for (unsigned scenario=0;scenario<4;++scenario) {
        reset(true);fake_radio_t *radio=&world.radios[0];
        radio->exact_id_recovery=true;
        radio->start_mode=scenario==3?ACCEPT:LOST_DELIVERED;
        start();CHECK(world.session.radios[0].identity_pending==(scenario!=3));
        /* An ordinary scan advanced the shared collector byte before START.
         * Only an exact requested job and the same peer can establish it. */
        radio->observation.generation=23;
        radio->observation.identity_verified=scenario!=1;
        if(scenario==2)++radio->observation.epoch;
        tick();
        if(scenario==0) {
            CHECK(world.session.radios[0].owned&&!world.session.radios[0].identity_pending);
            CHECK(world.session.radios[0].expected_generation==23&&!world.session.radios[0].faulted);
            publish(0,1);CHECK(world.inventory.banks[0].status.generation==23);
        } else {
            CHECK(!world.session.radios[0].owned&&world.session.radios[0].faulted);
            CHECK(!radio->stops&&!world.inventory.banks[0].published);
        }
        CHECK(world.session.radios[1].owned); /* Independent BLE kept running. */
    }
    reset(false);world.radios[0].exact_id_recovery=true;
    world.radios[0].start_mode=LOST_UNDELIVERED;start();tick();
    CHECK(!world.session.radios[0].owned&&!world.session.radios[0].identity_pending);
    CHECK(!world.radios[0].stops&&world.session.radios[0].rejected_starts==1);
    for(unsigned terminal=0;terminal<2;++terminal) {
        reset(false);fake_radio_t *radio=&world.radios[0];
        radio->exact_id_recovery=true;radio->start_mode=LOST_DELIVERED;start();
        radio->observation.generation=71;radio->observation.identity_verified=true;
        complete(0,1);
        if(terminal){radio->observation.state=SENTINEL_SCAN_ERROR;radio->observation.radio_error=-33;}
        tick();CHECK(!world.session.radios[0].owned&&!world.session.radios[0].identity_pending);
        CHECK(!world.session.radios[0].scan.command_failed);
        if(!terminal) {
            CHECK(world.session.radios[0].capture_pending);tick();
            CHECK(world.inventory.banks[0].published&&world.inventory.banks[0].status.generation==71);
        } else CHECK(!world.session.radios[0].capture_pending&&!world.inventory.banks[0].published);
        CHECK(radio->starts==1&&!radio->stops);
    }
}
static void detailed_publication(void)
{
    for(unsigned defect=1;defect<=7;++defect){
        reset(true);world.detailed_rows=true;world.observed_rows=true;start();publish(0,2);publish(1,1);
        CHECK(world.inventory.banks[0].motions[0].flags&&world.inventory.banks[1].motions[0].flags);
        CHECK(world.inventory.banks[0].motion_timings[0].source_age_ms==7000);
        CHECK(world.inventory.banks[0].timings[0].source_age_ms==50);
        cerberus_remote_id_page_t page;CHECK(cerberus_remote_id_inventory_page(&world.inventory,0,&page));
        CHECK(cerberus_remote_id_inventory_select(&world.inventory,&page.rows[0]));
        cerberus_remote_id_bank_t bank=world.inventory.banks[0];
        cerberus_remote_id_selection_t selection=world.inventory.selected;
        tick();complete(0,2);tick();fake_radio_t *r=&world.radios[0];
        if(defect<=6)r->motion_defect=defect;else r->end_epoch_change=true;
        unsigned reads=r->reads,observed=r->observed_reads,detailed=r->detailed_reads;
        tick();CHECK(world.session.radios[0].capture_pending&&world.session.radios[0].error);
        CHECK(r->detailed_reads>detailed&&r->reads-reads==r->detailed_reads-detailed);
        CHECK(r->observed_reads-observed==r->detailed_reads-detailed); /* No fallback invocation. */
        CHECK(!memcmp(&bank,&world.inventory.banks[0],sizeof(bank)));
        CHECK(!memcmp(&selection,&world.inventory.selected,sizeof(selection)));
        CHECK(!world.session.radios[1].faulted);
        r->motion_defect=0;r->end_epoch_change=false;++world.now;tick();
        CHECK(!world.session.radios[0].capture_pending&&!world.session.radios[0].error);
        CHECK(world.inventory.selected.latest.motion.speed_qms==world.now%255);
        CHECK(world.inventory.selected.latest.motion_timing.request_started_us==world.now*1000);
        CHECK(!memcmp(&selection.frozen,&world.inventory.selected.frozen,sizeof(selection.frozen)));
        world.detailed_rows=false;tick();publish(0,2);
        CHECK(!world.inventory.selected.latest.motion.flags&&!world.inventory.selected.latest.motion_timing.valid);
        CHECK(world.inventory.selected.latest.timing.valid&&world.inventory.selected.frozen.motion.flags);
    }
    reset(false);world.detailed_rows=true;
    cerberus_remote_id_ops_t ops=operations();ops.row=NULL;ops.row_observed=NULL;
    CHECK(cerberus_remote_id_session_start(&world.session));
    cerberus_remote_id_session_tick(&world.session,&world.inventory,&ops);
    CHECK(world.radios[0].starts==1);complete(0,1);
    cerberus_remote_id_session_tick(&world.session,&world.inventory,&ops);
    cerberus_remote_id_session_tick(&world.session,&world.inventory,&ops);
    CHECK(world.inventory.banks[0].published&&world.radios[0].detailed_reads==1);
}

int main(void)
{
    independent_and_selection();starts_and_foreign_work();atomic_capture_failures();stop_retry_and_wrap();timed_publication();exact_job_identity_recovery();detailed_publication();
    printf("Remote ID production session/backend/scheduler: %u checks; independent radios, no implicit start, rejected/ambiguous delivery, exact ownership, atomic publication/retry, Stop drain/retry and generation-wrap passed.\n",checks);
    return 0;
}
