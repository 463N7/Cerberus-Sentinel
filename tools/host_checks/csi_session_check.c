#include "csi_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line %u: %s\n",__LINE__,#c);abort();}}while(0)

enum {RESERVE,START,POLL,RENEW,STOP,READ,ACK,OPS};
typedef struct {
    uint64_t now,source,boot,epoch;
    uint32_t next_job,job,next_sequence,acked,accepted,invalid,overflow;
    unsigned calls[OPS],queued,inflight;
    cerberus_csi_sample_t queue[16];
    cerberus_csi_target_t target;
    int32_t failure[OPS];
    bool gone[OPS],start_undelivered,ack_applied,stop_held,partial_core;
    uint8_t state;
    int32_t radio_error;
    bool owned;
    unsigned corrupt;
} fake_t;
static fake_t fake;
static cerberus_csi_session_t session;
static cerberus_csi_ops_t ops;

static cerberus_csi_target_t target(void)
{
    cerberus_csi_target_t t={.bssid={2,3,4,5,6,7},.channel=6,.ssid_length=4,.ssid={'T','E','S','T'}};
    return t;
}

static cerberus_csi_operation_t result(unsigned which)
{
    cerberus_csi_operation_t r={.error=fake.failure[which],.no_ownership=fake.gone[which]};
    fake.failure[which]=0;fake.gone[which]=false;return r;
}

static void snapshot(cerberus_csi_observation_t *o)
{
    memset(o,0,sizeof(*o));
    o->status=(cerberus_csi_status_t){.job_id=fake.job,.epoch=fake.epoch,.error_code=fake.radio_error,
        .state=fake.state,.channel=fake.state==CERBERUS_CSI_RUNNING?fake.target.channel:0,
        .flags=fake.owned?CERBERUS_CSI_FLAG_OWNED:0,.queued=(uint8_t)fake.queued,.version=CERBERUS_CSI_VERSION};
    if(fake.owned && fake.state==CERBERUS_CSI_RUNNING)o->status.flags|=CERBERUS_CSI_FLAG_RECEIVING;
    o->clock=(cerberus_csi_clock_t){.epoch=fake.epoch,.source_now_us=fake.source,.accepted=fake.accepted,
        .version=CERBERUS_CSI_VERSION,.pending=(uint8_t)(fake.queued+fake.inflight)};
    o->counters=(cerberus_csi_counters_t){.epoch=fake.epoch,.acked=fake.acked,.invalid=fake.invalid,
        .overflow=fake.overflow,.version=CERBERUS_CSI_VERSION};
    o->target=(cerberus_csi_target_payload_t){.epoch=fake.epoch,.channel=fake.target.channel,
        .lease_ms=CERBERUS_CSI_LEASE_MS,.version=CERBERUS_CSI_VERSION};
    memcpy(o->target.bssid,fake.target.bssid,6);
    o->status_valid=o->timing_valid=true;
    switch(fake.corrupt){
        case 1:o->status.job_id++;break;case 2:o->status.epoch++;break;
        case 3:o->target.bssid[5]^=1;break;case 4:o->target.channel++;break;
        case 5:o->target.lease_ms++;break;case 6:o->clock.epoch++;break;
        case 7:o->counters.epoch++;break;case 8:o->target.epoch++;break;
        case 9:o->status.version++;break;case 10:o->clock.version++;break;
        case 11:o->target.reserved[3]=1;break;case 12:o->counters.reserved=1;break;
        case 13:o->status.flags|=128;break;case 14:o->status.queued=17;break;
        case 15:o->status.channel++;break;case 16:o->timing_valid=false;break;
        case 17:o->status_valid=false;break;
        case 18:o->target.channel++;o->status.channel=o->target.channel;break;
        case 19:o->target.bssid[0]|=1;break;
        case 20:memset(o->target.bssid,0,6);break;
        case 21:o->target.lease_ms=0;break;
        case 22:o->target.lease_ms=CERBERUS_CSI_LEASE_MAX_MS+1;break;
        default:break;
    }
}

static void binding(const cerberus_csi_binding_t *b)
{CHECK(b->boot_id==fake.boot && b->job_id==fake.job && (!b->epoch || b->epoch==fake.epoch));}

static int32_t reserve(void *ctx,const cerberus_csi_target_t *t,cerberus_csi_binding_t *b)
{
    CHECK(ctx==&fake);++fake.calls[RESERVE];fake.target=*t;
    if(fake.failure[RESERVE])return result(RESERVE).error;
    *b=(cerberus_csi_binding_t){.boot_id=fake.boot,.job_id=++fake.next_job};
    return 0;
}
static cerberus_csi_operation_t start(void *ctx,const cerberus_csi_start_request_t *r,cerberus_csi_observation_t *o)
{
    CHECK(ctx==&fake);++fake.calls[START];CHECK(r->boot_id==fake.boot && r->job_id==fake.next_job);
    CHECK(r->channel==fake.target.channel && !memcmp(r->bssid,fake.target.bssid,6));
    CHECK(r->lease_ms==CERBERUS_CSI_LEASE_MS && r->version==CERBERUS_CSI_VERSION);
    cerberus_csi_operation_t reply=result(START);
    if(fake.start_undelivered)return reply;
    fake.job=r->job_id;++fake.epoch;fake.state=CERBERUS_CSI_RUNNING;fake.owned=true;
    fake.queued=fake.accepted=fake.acked=fake.invalid=fake.overflow=fake.next_sequence=0;
    if(!reply.error)snapshot(o);
    return reply;
}
static cerberus_csi_operation_t poll(void *ctx,const cerberus_csi_binding_t *b,cerberus_csi_observation_t *o)
{
    CHECK(ctx==&fake);++fake.calls[POLL];binding(b);
    cerberus_csi_operation_t reply=result(POLL);
    if(!reply.error || fake.partial_core)snapshot(o);
    if(reply.error)o->timing_valid=false;
    return reply;
}
static cerberus_csi_operation_t renew(void *ctx,const cerberus_csi_renew_request_t *r,cerberus_csi_observation_t *o)
{
    CHECK(ctx==&fake);++fake.calls[RENEW];
    cerberus_csi_binding_t b={.boot_id=r->boot_id,.job_id=r->job_id,.epoch=r->epoch};binding(&b);
    CHECK(r->lease_ms==CERBERUS_CSI_LEASE_MS);
    cerberus_csi_operation_t reply=result(RENEW);if(!reply.error)snapshot(o);return reply;
}
static cerberus_csi_operation_t stop(void *ctx,const cerberus_csi_binding_t *b,cerberus_csi_observation_t *o)
{
    CHECK(ctx==&fake);++fake.calls[STOP];binding(b);
    cerberus_csi_operation_t reply=result(STOP);
    if(!reply.error && !fake.stop_held){fake.state=CERBERUS_CSI_COMPLETE;fake.owned=false;fake.queued=0;}
    else if(!reply.error){fake.state=CERBERUS_CSI_ERROR;fake.radio_error=77;}
    if(!reply.error || fake.partial_core)snapshot(o);
    if(reply.error)o->timing_valid=false;
    return reply;
}
static cerberus_csi_operation_t read_sample(void *ctx,const cerberus_csi_binding_t *b,cerberus_csi_sample_t *sample,bool *available)
{
    CHECK(ctx==&fake);++fake.calls[READ];binding(b);
    cerberus_csi_operation_t reply=result(READ);if(reply.error || reply.no_ownership)return reply;
    *available=fake.queued!=0;if(*available)*sample=fake.queue[0];return reply;
}
static void pop(void)
{
    if(!fake.queued)return;
    fake.acked=fake.queue[0].sequence;
    memmove(fake.queue,fake.queue+1,(--fake.queued)*sizeof(fake.queue[0]));
}
static cerberus_csi_operation_t ack(void *ctx,const cerberus_csi_ack_request_t *a)
{
    CHECK(ctx==&fake);++fake.calls[ACK];CHECK(a->job_id==fake.job && a->epoch==fake.epoch);
    CHECK((fake.queued && a->sequence==fake.queue[0].sequence) || a->sequence==fake.acked);
    cerberus_csi_operation_t reply=result(ACK);
    if(!reply.error || fake.ack_applied)if(fake.queued && a->sequence==fake.queue[0].sequence)pop();
    return reply;
}
static uint64_t now(void *ctx){CHECK(ctx==&fake);return fake.now;}
static void setup(void)
{
    memset(&fake,0,sizeof(fake));fake.boot=UINT64_C(0xAB00000000000001);fake.epoch=UINT64_C(0xCD00000000000001);
    ops=(cerberus_csi_ops_t){.context=&fake,.reserve=reserve,.start=start,.poll=poll,.renew=renew,
        .stop=stop,.read=read_sample,.ack=ack,.now_us=now};
    cerberus_csi_target_t t=target();CHECK(cerberus_csi_session_init(&session,&t));
}
static void tick(uint64_t elapsed)
{fake.now+=elapsed;fake.source+=elapsed;cerberus_csi_session_tick(&session,&ops);}
static void begin(void)
{CHECK(cerberus_csi_session_start(&session));tick(0);CHECK(session.owned && session.binding.epoch==fake.epoch);}
static void enqueue(uint64_t source)
{
    CHECK(fake.queued<16);cerberus_csi_sample_t *s=&fake.queue[fake.queued++];
    memset(s,0,sizeof(*s));s->source_us=source;s->sequence=++fake.next_sequence;
    s->channel=fake.target.channel;s->rssi=-42;s->phy=1;s->flags=CERBERUS_CSI_SAMPLE_ESTIMATE_VALID;
    s->format=CERBERUS_CSI_FORMAT_LLTF_I8;
    for(size_t i=0;i<CERBERUS_CSI_BYTES;i+=2){s->data[i]=30;s->data[i+1]=50;}
    ++fake.accepted;
}

static void initial_and_validation(void)
{
    setup();CHECK(cerberus_csi_session_released(&session));
    cerberus_csi_target_t t=target();t.ssid[1]=0;CHECK(cerberus_csi_session_init(&session,&t));t.bssid[5]=99;
    CHECK(session.target.bssid[5]==7 && session.target.ssid[1]==0);
    for(unsigned i=0;i<100;++i){cerberus_csi_session_view_t view;cerberus_csi_session_view(&session,i,&view);tick(1);}
    for(unsigned i=0;i<OPS;++i)CHECK(fake.calls[i]==0);
    CHECK(cerberus_csi_session_start(&session));CHECK(!cerberus_csi_session_start(&session));
    cerberus_csi_session_stop(&session);tick(1);CHECK(fake.calls[START]==0 && cerberus_csi_session_released(&session));
    for(unsigned channel=0;channel<256;++channel){
        t=target();t.channel=(uint8_t)channel;
        bool expected=(channel>=1&&channel<=14)||(channel>=36&&channel<=64&&channel%4==0)||
            (channel>=100&&channel<=144&&channel%4==0)||(channel>=149&&channel<=177&&(channel-149)%4==0);
        CHECK(cerberus_csi_target_valid(&t)==expected);
    }
    t=target();t.ssid_length=33;CHECK(!cerberus_csi_session_init(&session,&t));
    t=target();t.bssid[0]=1;CHECK(!cerberus_csi_target_valid(&t));memset(t.bssid,0,6);CHECK(!cerberus_csi_target_valid(&t));
    setup();fake.failure[RESERVE]=-5;CHECK(cerberus_csi_session_start(&session));tick(0);
    CHECK(cerberus_csi_session_released(&session)&&session.error==-5&&fake.calls[START]==0);
}

static void ack_and_bounded_drain(void)
{
    for(unsigned delivered=0;delivered<2;++delivered){
        setup();begin();fake.source=100;enqueue(10);fake.failure[ACK]=-8;fake.ack_applied=delivered!=0;
        tick(0);CHECK(session.pending_ack==1 && session.model.view.samples_used==1);
        unsigned reads=fake.calls[READ];tick(250000);
        CHECK(session.pending_ack==0 && session.model.view.samples_used==1 && fake.calls[READ]==reads);
        CHECK(fake.queued==0);tick(250000);CHECK(session.model.view.samples_used==1);
    }
    setup();begin();for(unsigned i=0;i<16;++i)enqueue(i+1);
    fake.source=100;tick(0);CHECK(fake.queued==15 && session.model.view.samples_used==1);
    for(unsigned i=0;i<15;++i){unsigned before=fake.calls[READ];tick(1);CHECK(fake.calls[READ]==before+1);}
    CHECK(fake.queued==0 && session.model.view.samples_used==16);
}

static void lost_start_and_cleanup(void)
{
    setup();fake.failure[START]=-9;CHECK(cerberus_csi_session_start(&session));tick(0);
    CHECK(session.owned && session.binding.epoch==0 && !cerberus_csi_session_released(&session));
    CHECK(fake.calls[START]==1);tick(1);CHECK(session.binding.epoch==fake.epoch && session.state==CERBERUS_CSI_SESSION_RUNNING);
    fake.failure[STOP]=-7;cerberus_csi_session_stop(&session);tick(1);
    CHECK(session.owned && session.error==-7);unsigned stops=fake.calls[STOP];
    for(unsigned i=0;i<100;++i){cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now,&v);}
    CHECK(fake.calls[STOP]==stops);tick(250000);CHECK(cerberus_csi_session_released(&session));
    setup();fake.failure[START]=-12;fake.gone[START]=true;fake.start_undelivered=true;
    CHECK(cerberus_csi_session_start(&session));tick(0);CHECK(!session.owned && session.error==-12);
    setup();begin();fake.stop_held=true;cerberus_csi_session_stop(&session);tick(0);
    CHECK(session.owned && session.state==CERBERUS_CSI_SESSION_ERROR && session.error==77);
    fake.stop_held=false;fake.radio_error=0;cerberus_csi_session_stop(&session);tick(0);
    CHECK(!session.owned && cerberus_csi_session_released(&session));
    setup();begin();fake.failure[POLL]=-21;fake.gone[POLL]=true;tick(1);
    CHECK(!session.owned && session.error==-21 && fake.calls[STOP]==0);
    setup();begin();fake.state=CERBERUS_CSI_COMPLETE;fake.owned=false;fake.partial_core=true;fake.failure[POLL]=-31;
    tick(1);CHECK(!session.owned && session.error==-31);
}

static void source_binding_and_recalibration(void)
{
    for(unsigned defect=1;defect<=22;++defect){
        setup();begin();fake.corrupt=defect;tick(1);
        CHECK(session.owned && session.error==CERBERUS_CSI_SESSION_BAD_SOURCE && session.stop_requested);
        CHECK(fake.calls[READ]==0);fake.corrupt=0;tick(250000);CHECK(!session.owned);
    }
    setup();begin();enqueue(1);fake.source=1;tick(0);CHECK(session.model.view.samples_used==1);
    uint32_t old_job=session.binding.job_id;uint64_t old_epoch=session.binding.epoch;
    CHECK(cerberus_csi_session_recalibrate(&session));fake.failure[STOP]=-9;tick(1);
    CHECK(session.owned && fake.calls[START]==1 && session.model.view.samples_used==1);
    tick(250000);CHECK(session.start_requested && !session.owned && fake.calls[START]==1);
    tick(1);CHECK(session.owned && session.binding.job_id>old_job && session.binding.epoch>old_epoch);
    CHECK(session.model.view.samples_used==0 && !session.model.view.calibrated && fake.calls[START]==2);
    CHECK(cerberus_csi_session_recalibrate(&session));cerberus_csi_session_stop(&session);tick(0);tick(1);
    CHECK(!session.owned && fake.calls[START]==2);
}

static void pre_adoption_target_collision(void)
{
    /* A complete alternate identity/channel/lease belongs to somebody else,
     * both in the initial START response and after an ambiguous lost ACK. */
    const unsigned alternate[]={3,5,18};
    for(size_t i=0;i<sizeof(alternate)/sizeof(alternate[0]);++i)for(unsigned lost=0;lost<2;++lost){
        setup();fake.corrupt=alternate[i];
        if(lost)fake.failure[START]=-9;
        CHECK(cerberus_csi_session_start(&session));tick(0);
        if(lost){CHECK(session.owned && !session.binding.epoch);tick(1);}
        CHECK(!session.owned && !session.binding.epoch && !session.stop_requested);
        CHECK(session.error==CERBERUS_CSI_SESSION_BAD_SOURCE && fake.owned);
        cerberus_csi_session_stop(&session);tick(5000000);
        CHECK(fake.calls[STOP]==0 && fake.calls[RENEW]==0 && fake.calls[READ]==0 && fake.calls[ACK]==0);
    }
    /* Untrusted or incomplete bytes cannot prove a different owner. */
    const unsigned malformed[]={1,2,4,6,7,8,9,10,11,12,13,14,15,16,17,19,20,21,22};
    for(size_t i=0;i<sizeof(malformed)/sizeof(malformed[0]);++i){
        setup();fake.corrupt=malformed[i];CHECK(cerberus_csi_session_start(&session));tick(0);
        CHECK(session.owned && !session.binding.epoch && session.stop_requested);
        CHECK(session.error==CERBERUS_CSI_SESSION_BAD_SOURCE && fake.calls[STOP]==0);
        fake.corrupt=0;tick(1);CHECK(!session.owned && fake.calls[STOP]==1);
    }
}

static void blocked_delivery_age(void)
{
    for(unsigned blocked_ack=0;blocked_ack<2;++blocked_ack){
        setup();fake.source=UINT64_C(0x10000000000);begin();
        for(unsigned i=0;i<320;++i){fake.now+=100000;fake.source+=100000;enqueue(fake.source);cerberus_csi_session_tick(&session,&ops);}
        CHECK(session.model.view.calibrated && session.model.view.state==CERBERUS_CSI_MOTION_STILL);
        fake.now+=1000;fake.source+=1000;enqueue(fake.source);
        fake.failure[blocked_ack?ACK:READ]=-8;tick(0);
        CHECK(session.pending_ack==(blocked_ack?321U:0U));
        cerberus_csi_motion_model_t unchanged=session.model;
        for(unsigned i=0;i<20;++i){
            fake.failure[blocked_ack?ACK:READ]=-8;tick(250000);
            cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now,&v);
            CHECK(!v.link_stale && v.receive_age_valid);
            CHECK(v.source_stale==(v.receive_age_ms>2000U));
            CHECK(!memcmp(&session.model,&unchanged,sizeof(unchanged)));
        }
        cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now,&v);
        CHECK(v.source_stale && v.motion.state==CERBERUS_CSI_MOTION_STILL && fake.queued==1);
        /* Retrying the old record/ACK does not make its callback fresh. */
        tick(250000);cerberus_csi_session_view(&session,fake.now,&v);CHECK(v.source_stale);
        enqueue(fake.source);tick(1);tick(250000);
        cerberus_csi_session_view(&session,fake.now,&v);
        CHECK(v.receive_age_valid && !v.source_stale && !v.link_stale);
    }
    setup();begin();fake.source=1;enqueue(2);tick(1);
    cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now+2000000,&v);
    CHECK(v.receive_age_ms==2000 && !v.source_stale);
    cerberus_csi_session_view(&session,fake.now+2000001,&v);CHECK(v.source_stale);
    /* Backward/overflowed age never presents historical metrics as current. */
    cerberus_csi_session_view(&session,0,&v);CHECK(!v.receive_age_valid && v.source_stale);
    session.observation.clock.source_now_us=UINT64_MAX;
    cerberus_csi_session_view(&session,fake.now+10,&v);CHECK(!v.receive_age_valid && v.source_stale);
    cerberus_csi_session_stop(&session);cerberus_csi_session_view(&session,fake.now,&v);CHECK(!v.source_stale);
}

static void source_newer_than_status_clock(void)
{
    setup();fake.source=UINT64_C(0x10000000000);begin();
    unsigned newer=0;
    for(unsigned i=0;i<330;++i){
        fake.now+=100000;fake.source+=100000;enqueue(fake.source);cerberus_csi_session_tick(&session,&ops);
        cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now,&v);
        CHECK(v.receive_age_valid && !v.source_stale && !v.link_stale);
        CHECK(v.receive_age_ms<=200);
        if(session.latest_sample_us>session.observation.clock.source_now_us){
            ++newer;CHECK(v.receive_age_ms==(fake.now-session.clock_requested_us)/1000U);
        }
    }
    CHECK(newer>100 && session.model.view.calibrated);
    /* Without another clock, the same upper bound still ages to stale. */
    cerberus_csi_session_view_t v;cerberus_csi_session_view(&session,fake.now+3000000,&v);
    CHECK(v.receive_age_valid && v.receive_age_ms>=3000 && v.source_stale);
}

static void calibration_clock_and_age(void)
{
    setup();fake.source=UINT64_C(0x10000000000);begin();
    for(unsigned i=0;i<320;++i){fake.now+=100000;fake.source+=100000;enqueue(fake.source);cerberus_csi_session_tick(&session,&ops);}
    CHECK(session.model.view.calibrated && session.model.view.calibration_windows==30);
    CHECK(session.model.view.samples_used==320 && fake.calls[RENEW]>=30);
    tick(3000000);CHECK(session.model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    cerberus_csi_session_view_t view;cerberus_csi_session_view(&session,fake.now,&view);
    CHECK(view.receive_age_valid && view.receive_age_ms>=3000 && !view.link_stale);
    unsigned calls=fake.calls[POLL];cerberus_csi_session_view(&session,fake.now+2000001,&view);
    CHECK(view.link_stale && fake.calls[POLL]==calls);
    cerberus_csi_session_view(&session,0,&view);CHECK(!view.receive_age_valid && view.link_stale);
    setup();begin();tick(31000000);CHECK(!session.model.view.calibrated && session.model.view.state==CERBERUS_CSI_MOTION_LOW_DATA);
    setup();begin();fake.inflight=1;fake.source=1000000;tick(1);
    CHECK(session.model.latest_us==0);enqueue(500000);fake.inflight=0;tick(250000);
    CHECK(session.model.view.samples_used==1 && session.model.latest_us==500000);
    tick(250000);CHECK(session.model.latest_us==fake.source);
    fake.source=0;tick(250000);CHECK(session.error==CERBERUS_CSI_SESSION_BAD_SOURCE && session.owned);
}

static void repeated_and_failures(void)
{
    setup();for(unsigned i=0;i<300;++i){begin();cerberus_csi_session_stop(&session);tick(1);CHECK(!session.owned);}
    CHECK(fake.calls[START]==300 && fake.calls[STOP]==300 && session.binding.job_id==300);
    setup();begin();fake.failure[READ]=-5;tick(1);CHECK(session.owned&&session.error==-5&&session.model.view.samples_used==0);
    enqueue(2);fake.source=2;tick(250000);CHECK(session.model.view.samples_used==1);
    fake.now=0;tick(0);CHECK(!session.owned && fake.calls[STOP]==1);
    setup();begin();unsigned prior_polls=fake.calls[POLL];fake.failure[RENEW]=-4;tick(1000000);
    CHECK(session.owned&&session.error==-4 && fake.calls[POLL]==prior_polls); /* Renew includes the status bundle. */
    cerberus_csi_session_stop(&session);tick(0);CHECK(!session.owned);
    setup();begin();ops.read=NULL;tick(0);CHECK(session.owned&&session.error==CERBERUS_CSI_SESSION_BAD_OPS);
}

int main(void)
{
    initial_and_validation();ack_and_bounded_drain();lost_start_and_cleanup();
    source_binding_and_recalibration();pre_adoption_target_collision();calibration_clock_and_age();
    blocked_delivery_age();source_newer_than_status_clock();repeated_and_failures();
    printf("CSI session: %u checks passed; session %zu bytes; target frozen; no allocation/task\n",checks,sizeof(session));
    return 0;
}
