#include "csi_session.h"
#include <limits.h>
#include <string.h>

#define POLL_US UINT64_C(250000)
#define RENEW_US UINT64_C(1000000)
#define STALE_US UINT64_C(2000000)

static bool channel_valid(uint8_t channel)
{
    return (channel>=1U && channel<=14U) ||
        (channel>=36U && channel<=64U && channel%4U==0U) ||
        (channel>=100U && channel<=144U && channel%4U==0U) ||
        (channel>=149U && channel<=177U && (channel-149U)%4U==0U);
}

bool cerberus_csi_target_valid(const cerberus_csi_target_t *target)
{
    if(!target || target->ssid_length>32U || !channel_valid(target->channel) || (target->bssid[0]&1U))return false;
    uint8_t any=0;for(size_t i=0;i<6U;++i)any|=target->bssid[i];
    return any!=0U;
}

bool cerberus_csi_session_init(cerberus_csi_session_t *s,const cerberus_csi_target_t *target)
{
    if(!s)return false;
    cerberus_csi_target_t copy={0};
    bool valid=cerberus_csi_target_valid(target);
    if(valid)copy=*target;
    memset(s,0,sizeof(*s));
    if(!valid)return false;
    s->target=copy;s->valid=true;s->state=CERBERUS_CSI_SESSION_READY;
    memset(s->target.ssid+s->target.ssid_length,0,32U-s->target.ssid_length);
    return true;
}

bool cerberus_csi_session_released(const cerberus_csi_session_t *s)
{return !s || (!s->owned && !s->start_requested);}

bool cerberus_csi_session_start(cerberus_csi_session_t *s)
{
    if(!s || !s->valid || !cerberus_csi_session_released(s))return false;
    s->start_requested=true;s->stop_requested=false;s->recalibrate_requested=false;
    s->error=0;s->state=CERBERUS_CSI_SESSION_STARTING;s->poll_due=true;
    return true;
}

void cerberus_csi_session_stop(cerberus_csi_session_t *s)
{
    if(!s || !s->valid)return;
    s->start_requested=false;s->recalibrate_requested=false;s->poll_due=true;
    s->stop_requested=s->owned;
    s->state=s->owned?CERBERUS_CSI_SESSION_STOPPING:CERBERUS_CSI_SESSION_STOPPED;
}

bool cerberus_csi_session_recalibrate(cerberus_csi_session_t *s)
{
    if(!s || !s->valid || !s->owned || s->state!=CERBERUS_CSI_SESSION_RUNNING)return false;
    s->recalibrate_requested=true;s->stop_requested=true;s->poll_due=true;
    s->state=CERBERUS_CSI_SESSION_STOPPING;
    return true;
}

static void released(cerberus_csi_session_t *s,int32_t error)
{
    bool again=s->recalibrate_requested && error==0;
    s->owned=false;s->stop_requested=false;s->pending_ack=0;
    s->start_requested=again;s->recalibrate_requested=false;s->poll_due=true;
    s->error=error;
    s->state=again?CERBERUS_CSI_SESSION_STARTING:error?CERBERUS_CSI_SESSION_ERROR:CERBERUS_CSI_SESSION_STOPPED;
}

static bool status_matches(const cerberus_csi_session_t *s,const cerberus_csi_observation_t *o)
{
    const cerberus_csi_status_t *v=&o->status;
    if(!o->status_valid || !v->epoch || v->job_id!=s->binding.job_id ||
        (s->binding.epoch && v->epoch!=s->binding.epoch) || v->version!=CERBERUS_CSI_VERSION || v->reserved ||
        v->state<CERBERUS_CSI_STARTING || v->state>CERBERUS_CSI_ERROR || v->queued>CERBERUS_CSI_RING_CAPACITY ||
        (v->flags&~(CERBERUS_CSI_FLAG_OWNED|CERBERUS_CSI_FLAG_RECEIVING)))return false;
    bool owned=(v->flags&CERBERUS_CSI_FLAG_OWNED)!=0U;
    bool receiving=(v->flags&CERBERUS_CSI_FLAG_RECEIVING)!=0U;
    if(receiving && (!owned || v->state!=CERBERUS_CSI_RUNNING || !channel_valid(v->channel)))return false;
    if(!receiving && v->channel!=0U)return false;
    if((v->state==CERBERUS_CSI_STARTING || v->state==CERBERUS_CSI_RUNNING || v->state==CERBERUS_CSI_STOPPING) && !owned)return false;
    if(v->state==CERBERUS_CSI_COMPLETE && owned)return false;
    if(v->state==CERBERUS_CSI_ERROR && !v->error_code)return false;
    if(v->state!=CERBERUS_CSI_ERROR && v->state!=CERBERUS_CSI_STOPPING && v->error_code)return false;
    return true;
}

static bool observation_valid(const cerberus_csi_session_t *s,const cerberus_csi_observation_t *o)
{
    if(!status_matches(s,o) || !o->timing_valid)return false;
    if(o->clock.epoch!=o->status.epoch || o->counters.epoch!=o->status.epoch || o->target.epoch!=o->status.epoch ||
        o->clock.version!=CERBERUS_CSI_VERSION || o->counters.version!=CERBERUS_CSI_VERSION ||
        o->target.version!=CERBERUS_CSI_VERSION || o->counters.reserved ||
        o->target.lease_ms<CERBERUS_CSI_LEASE_MIN_MS || o->target.lease_ms>CERBERUS_CSI_LEASE_MAX_MS ||
        ((o->status.flags&CERBERUS_CSI_FLAG_RECEIVING) && o->status.channel!=o->target.channel))return false;
    cerberus_csi_target_t target={.channel=o->target.channel};
    memcpy(target.bssid,o->target.bssid,sizeof(target.bssid));
    if(!cerberus_csi_target_valid(&target))return false;
    for(size_t i=0;i<sizeof(o->target.reserved);++i)if(o->target.reserved[i])return false;
    if(s->observed && (o->clock.source_now_us<s->observation.clock.source_now_us ||
        o->clock.accepted<s->observation.clock.accepted || o->counters.invalid<s->observation.counters.invalid ||
        o->counters.overflow<s->observation.counters.overflow || o->counters.acked<s->observation.counters.acked))return false;
    return true;
}

static bool target_matches(const cerberus_csi_session_t *s,const cerberus_csi_observation_t *o)
{
    return o->target.channel==s->target.channel && o->target.lease_ms==CERBERUS_CSI_LEASE_MS &&
        !memcmp(o->target.bssid,s->target.bssid,6U);
}

/* A valid exact-job terminal core can prove release even if later optional
 * telemetry failed. An unbound epoch is adopted only with full target proof. */
static bool apply(cerberus_csi_session_t *s,cerberus_csi_operation_t result,
    const cerberus_csi_observation_t *o,uint64_t requested_us)
{
    if(result.no_ownership){released(s,result.error?result.error:CERBERUS_CSI_SESSION_BAD_SOURCE);return false;}
    if(result.error){
        s->error=result.error;
        if(status_matches(s,o) && !(o->status.flags&CERBERUS_CSI_FLAG_OWNED))
            released(s,o->status.error_code?o->status.error_code:result.error);
        return false;
    }
    bool coherent=observation_valid(s,o);
    if(!coherent || !target_matches(s,o)){
        /* Before adoption, a complete different target proves this reserved
         * ID belongs to another selection. Do not Stop/Renew that capture.
         * Partial or malformed proof remains ambiguous and retains cleanup. */
        if(!s->binding.epoch && coherent){released(s,CERBERUS_CSI_SESSION_BAD_SOURCE);return false;}
        s->error=CERBERUS_CSI_SESSION_BAD_SOURCE;s->stop_requested=true;
        s->recalibrate_requested=false;s->state=CERBERUS_CSI_SESSION_ERROR;
        return false;
    }
    s->binding.epoch=o->status.epoch;s->observation=*o;s->observed=true;
    s->clock_requested_us=requested_us;s->error=o->status.error_code;
    if(!(o->status.flags&CERBERUS_CSI_FLAG_OWNED)){released(s,o->status.error_code);return false;}
    if(o->status.state==CERBERUS_CSI_ERROR){
        s->state=CERBERUS_CSI_SESSION_ERROR;s->stop_requested=true;s->recalibrate_requested=false;
        return false;
    }
    if(s->stop_requested || o->status.state==CERBERUS_CSI_STOPPING){
        s->stop_requested=true;s->state=CERBERUS_CSI_SESSION_STOPPING;return false;
    }
    s->state=o->status.state==CERBERUS_CSI_RUNNING?CERBERUS_CSI_SESSION_RUNNING:CERBERUS_CSI_SESSION_STARTING;
    /* pending includes older admitted callbacks, not only published records. */
    if(o->clock.pending==0U && !s->pending_ack){
        if(!s->model_ready){cerberus_csi_motion_reset(&s->model,o->clock.source_now_us);s->model_ready=true;}
        uint32_t dropped=UINT32_MAX-o->counters.invalid<o->counters.overflow?UINT32_MAX:o->counters.invalid+o->counters.overflow;
        if(!cerberus_csi_motion_advance(&s->model,o->clock.source_now_us,dropped)){
            s->error=CERBERUS_CSI_SESSION_BAD_CLOCK;s->stop_requested=true;
            s->recalibrate_requested=false;s->state=CERBERUS_CSI_SESSION_ERROR;return false;
        }
    }
    return true;
}

static bool due(uint64_t now,uint64_t before,uint64_t period)
{return now>=before && now-before>=period;}

static bool ops_valid(const cerberus_csi_ops_t *o)
{return o && o->reserve && o->start && o->poll && o->renew && o->stop && o->read && o->ack && o->now_us;}

void cerberus_csi_session_tick(cerberus_csi_session_t *s,const cerberus_csi_ops_t *ops)
{
    if(!s || !s->valid)return;
    if(!ops_valid(ops)){s->error=CERBERUS_CSI_SESSION_BAD_OPS;s->state=CERBERUS_CSI_SESSION_ERROR;return;}
    uint64_t now=ops->now_us(ops->context);
    cerberus_csi_observation_t observation={0};
    cerberus_csi_operation_t result;
    if(s->start_requested && !s->owned){
        cerberus_csi_binding_t binding={0};
        int32_t error=ops->reserve(ops->context,&s->target,&binding);
        s->start_requested=false;
        if(error || !binding.boot_id || !binding.job_id || binding.epoch){released(s,error?error:CERBERUS_CSI_SESSION_BAD_SOURCE);return;}
        s->binding=binding;s->owned=true;s->observed=s->model_ready=s->sample_seen=false;
        s->last_sequence=s->pending_ack=0;s->error=0;s->poll_due=true;s->transfer_due=true;
        memset(&s->observation,0,sizeof(s->observation));memset(&s->model,0,sizeof(s->model));
        s->last_poll_us=s->last_renew_us=s->last_transfer_us=now;
        cerberus_csi_start_request_t request={.boot_id=binding.boot_id,.job_id=binding.job_id,
            .channel=s->target.channel,.version=CERBERUS_CSI_VERSION,.lease_ms=CERBERUS_CSI_LEASE_MS};
        memcpy(request.bssid,s->target.bssid,6U);
        result=ops->start(ops->context,&request,&observation);
        (void)apply(s,result,&observation,now);
        return;
    }
    if(!s->owned)return;
    if(now<s->last_poll_us || now<s->last_renew_us || now<s->last_transfer_us){
        s->error=CERBERUS_CSI_SESSION_BAD_CLOCK;s->stop_requested=true;s->recalibrate_requested=false;s->poll_due=true;
    }
    if(s->stop_requested){
        if(!s->poll_due && !due(now,s->last_poll_us,POLL_US))return;
        s->poll_due=false;s->last_poll_us=now;
        result=ops->stop(ops->context,&s->binding,&observation);
        (void)apply(s,result,&observation,now);
        return;
    }
    /* Renew already returns the complete validated status bundle. Avoid a
     * duplicate poll in the same tick once the capture identity is known. */
    bool renewal_due=s->state==CERBERUS_CSI_SESSION_RUNNING && s->binding.epoch &&
        due(now,s->last_renew_us,RENEW_US);
    if(!renewal_due && (s->poll_due || due(now,s->last_poll_us,POLL_US))){
        s->poll_due=false;s->last_poll_us=now;
        result=ops->poll(ops->context,&s->binding,&observation);
        if(!apply(s,result,&observation,now))return;
    }
    if(!s->owned || s->state!=CERBERUS_CSI_SESSION_RUNNING || !s->binding.epoch)return;
    if(due(now,s->last_renew_us,RENEW_US)){
        cerberus_csi_renew_request_t request={.boot_id=s->binding.boot_id,.job_id=s->binding.job_id,
            .epoch=s->binding.epoch,.lease_ms=CERBERUS_CSI_LEASE_MS};
        s->last_renew_us=now;
        s->last_poll_us=now;s->poll_due=false;
        result=ops->renew(ops->context,&request,&observation);
        if(!apply(s,result,&observation,now))return;
    }
    if(!s->transfer_due && !due(now,s->last_transfer_us,POLL_US))return;
    s->transfer_due=false;s->last_transfer_us=now;
    if(!s->pending_ack){
        bool available=false;
        memset(&s->sample,0,sizeof(s->sample));
        result=ops->read(ops->context,&s->binding,&s->sample,&available);
        if(result.error || result.no_ownership){
            if(result.no_ownership)released(s,result.error?result.error:CERBERUS_CSI_SESSION_BAD_SOURCE);
            else s->error=result.error;
            return;
        }
        if(!available)return;
        if(!s->sample.sequence || s->sample.sequence<s->last_sequence || s->sample.channel!=s->target.channel){
            s->error=CERBERUS_CSI_SESSION_BAD_RECORD;s->stop_requested=true;s->state=CERBERUS_CSI_SESSION_ERROR;return;
        }
        if(s->sample.sequence!=s->last_sequence){
            if(!s->model_ready){cerberus_csi_motion_reset(&s->model,s->sample.source_us);s->model_ready=true;}
            if(cerberus_csi_motion_observe(&s->model,&s->sample,sizeof(s->sample))){
                s->sample_seen=true;s->latest_sample_us=s->sample.source_us;
            }
            s->last_sequence=s->sample.sequence;
        }
        s->pending_ack=s->sample.sequence;
    }
    cerberus_csi_ack_request_t ack={.job_id=s->binding.job_id,.epoch=s->binding.epoch,.sequence=s->pending_ack};
    result=ops->ack(ops->context,&ack);
    if(result.no_ownership){released(s,result.error?result.error:CERBERUS_CSI_SESSION_BAD_SOURCE);return;}
    if(result.error){s->error=result.error;return;}
    s->pending_ack=0;s->error=0;
    /* Next UI tick may drain another record; never an unbounded inner loop. */
    s->transfer_due=true;
}

void cerberus_csi_session_view(const cerberus_csi_session_t *s,uint64_t now,cerberus_csi_session_view_t *view)
{
    if(!view)return;
    memset(view,0,sizeof(*view));
    if(!s || !s->valid)return;
    view->target=s->target;cerberus_csi_motion_view(&s->model,&view->motion);
    view->job_id=s->binding.job_id;view->epoch=s->binding.epoch;view->state=s->state;view->error=s->error;
    view->busy=!cerberus_csi_session_released(s);view->owned=s->owned;view->recalibrating=s->recalibrate_requested;
    view->accepted=s->observation.clock.accepted;view->invalid=s->observation.counters.invalid;
    view->overflow=s->observation.counters.overflow;view->acked=s->observation.counters.acked;
    view->link_stale=s->owned && (!s->observed || now<s->clock_requested_us || now-s->clock_requested_us>STALE_US);
    if(s->observed && s->sample_seen && now>=s->clock_requested_us){
        /* A newly drained callback can be newer than the last status clock.
         * That request still predates the callback, so its local elapsed time
         * alone remains a conservative bound until the next source clock. */
        uint64_t source=s->observation.clock.source_now_us>=s->latest_sample_us?
            s->observation.clock.source_now_us-s->latest_sample_us:0U;
        uint64_t elapsed=now-s->clock_requested_us;
        if(UINT64_MAX-source>=elapsed){
            uint64_t total=source+elapsed;
            view->receive_age_ms=total/1000U+(total%1000U!=0U);view->receive_age_valid=true;
        }
    }
    /* A fresh status/clock does not make a pinned or unreadable sample fresh.
     * This is presentation only: never advance source-time model/history past
     * queued or in-flight records merely to expire the displayed result. */
    view->source_stale=s->state==CERBERUS_CSI_SESSION_RUNNING && s->sample_seen &&
        (!view->receive_age_valid || view->receive_age_ms>STALE_US/1000U);
}
