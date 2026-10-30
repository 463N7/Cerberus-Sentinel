#include "mac_monitor_session.h"
#include <string.h>

#define MAC_POLL_US UINT64_C(250000)
#define MAC_RENEW_US UINT64_C(1000000)
static bool due(uint64_t now,uint64_t before,uint64_t period){return now>=before && now-before>=period;}
static bool channel(uint8_t ch)
{return (ch>=1 && ch<=14)||(ch>=36 && ch<=64 && ch%4==0)||(ch>=100 && ch<=144 && ch%4==0)||(ch>=149 && ch<=177 && (ch-149)%4==0);}
void cerberus_mac_monitor_session_init(cerberus_mac_monitor_session_t *s){if(s)memset(s,0,sizeof(*s));}
bool cerberus_mac_monitor_session_released(const cerberus_mac_monitor_session_t *s)
{return !s || (!s->radios[0].owned && !s->radios[1].owned);}
bool cerberus_mac_monitor_session_start(cerberus_mac_monitor_session_t *s)
{
    if(!s || s->running || !cerberus_mac_monitor_session_released(s))return false;
    memset(s,0,sizeof(*s));s->running=true;return true;
}
void cerberus_mac_monitor_session_stop(cerberus_mac_monitor_session_t *s)
{
    if(!s)return;
    s->running=false;
    for(unsigned n=0;n<2;++n){s->radios[n].stop_requested=s->radios[n].owned;s->radios[n].poll_due=true;}
}
static void release(cerberus_mac_monitor_radio_t *r,int32_t error)
{r->owned=r->stop_requested=r->reading=false;r->pending_ack=0;r->error=error;r->faulted=error!=0;}
static bool coherent(const cerberus_mac_monitor_radio_t *r,uint8_t radio,const cerberus_mac_monitor_observation_t *o)
{
    const cerberus_mac_status_t *v=&o->status;const cerberus_mac_profile_t *p=&o->profile;
    if(!o->valid || !v->epoch || v->job_id!=r->binding.job_id || (r->binding.epoch && r->binding.epoch!=v->epoch) ||
        v->version!=CERBERUS_MAC_VERSION || v->reserved || v->state<CERBERUS_MAC_STARTING || v->state>CERBERUS_MAC_ERROR ||
        (v->flags&~(CERBERUS_MAC_OWNED|CERBERUS_MAC_RECEIVING)) || p->epoch!=v->epoch || p->version!=CERBERUS_MAC_VERSION)return false;
    for(unsigned n=0;n<sizeof(p->reserved);++n)if(p->reserved[n])return false;
    bool owned=(v->flags&CERBERUS_MAC_OWNED)!=0,receiving=(v->flags&CERBERUS_MAC_RECEIVING)!=0;
    if(receiving && (!owned || v->state!=CERBERUS_MAC_RUNNING || (radio==CERBERUS_MAC_WIFI?!channel(v->channel):v->channel!=0)))return false;
    if(!receiving && v->channel)return false;
    if((v->state==CERBERUS_MAC_STARTING || v->state==CERBERUS_MAC_RUNNING || v->state==CERBERUS_MAC_STOPPING) && !owned)return false;
    if(v->state==CERBERUS_MAC_COMPLETE && owned)return false;
    if(v->state==CERBERUS_MAC_ERROR && !v->error_code)return false;
    if(v->state!=CERBERUS_MAC_ERROR && v->state!=CERBERUS_MAC_STOPPING && v->error_code)return false;
    return radio==CERBERUS_MAC_WIFI?
        p->lease_ms>=CERBERUS_MAC_LEASE_MIN_MS && p->lease_ms<=CERBERUS_MAC_LEASE_MAX_MS &&
        p->dwell_ms>=CERBERUS_MAC_DWELL_MIN_MS && p->dwell_ms<=CERBERUS_MAC_DWELL_MAX_MS &&
        p->channel_count<=42U && (v->state!=CERBERUS_MAC_RUNNING || p->channel_count>0):
        p->lease_ms==0 && p->dwell_ms==0 && p->channel_count==0;
}
static bool apply(cerberus_mac_monitor_radio_t *r,uint8_t radio,cerberus_mac_monitor_operation_t result,
    const cerberus_mac_monitor_observation_t *o)
{
    if(result.no_ownership){release(r,result.error?result.error:CERBERUS_MAC_MONITOR_BAD_SOURCE);return false;}
    if(result.error){r->error=result.error;return false;}
    bool valid=coherent(r,radio,o);
    bool profile=radio==CERBERUS_MAC_BLE || (o->profile.lease_ms==CERBERUS_MAC_LEASE_MS && o->profile.dwell_ms==CERBERUS_MAC_DWELL_MS);
    if(!valid || !profile){
        if(valid && !r->binding.epoch){release(r,CERBERUS_MAC_MONITOR_BAD_SOURCE);return false;}
        r->error=CERBERUS_MAC_MONITOR_BAD_SOURCE;r->stop_requested=true;r->faulted=true;return false;
    }
    r->binding.epoch=o->status.epoch;r->observation=*o;r->observed=true;r->error=o->status.error_code;
    if(!(o->status.flags&CERBERUS_MAC_OWNED)){release(r,o->status.error_code);return false;}
    if(o->status.state==CERBERUS_MAC_ERROR){r->stop_requested=true;r->faulted=true;return false;}
    if(o->status.state==CERBERUS_MAC_STOPPING)r->stop_requested=true;
    return !r->stop_requested && o->status.state==CERBERUS_MAC_RUNNING;
}
static bool ops_valid(const cerberus_mac_monitor_ops_t *o)
{return o && o->now_us && o->reserve && o->start && o->poll && o->renew && o->read_step && o->ack && o->stop;}

void cerberus_mac_monitor_session_tick(cerberus_mac_monitor_session_t *s,
    cerberus_mac_monitor_inventory_t *inventory,const cerberus_mac_monitor_ops_t *ops)
{
    if(!s || !inventory)return;
    if(!ops_valid(ops)){for(unsigned n=0;n<2;++n)s->radios[n].error=CERBERUS_MAC_MONITOR_BAD_OPS;return;}
    for(uint8_t radio=0;radio<2;++radio){
        cerberus_mac_monitor_radio_t *r=&s->radios[radio];uint64_t now=ops->now_us(ops->context);
        if(r->faulted && !r->owned)cerberus_mac_monitor_inventory_unavailable(inventory,radio,true);
        cerberus_mac_monitor_observation_t observation={0};cerberus_mac_monitor_operation_t result;
        if(s->running && !r->attempted){
            r->attempted=true;r->last_poll_us=r->last_renew_us=r->last_read_us=now;
            cerberus_mac_binding_t binding={0};int32_t error=ops->reserve(ops->context,radio,&binding);
            if(error || !binding.boot_id || !binding.job_id || binding.epoch){release(r,error?error:CERBERUS_MAC_MONITOR_BAD_SOURCE);
                cerberus_mac_monitor_inventory_unavailable(inventory,radio,true);continue;}
            r->binding=binding;r->owned=true;r->poll_due=true;
            cerberus_mac_start_request_t request={.boot_id=binding.boot_id,.job_id=binding.job_id,.version=CERBERUS_MAC_VERSION,
                .lease_ms=radio==CERBERUS_MAC_WIFI?CERBERUS_MAC_LEASE_MS:0,.dwell_ms=radio==CERBERUS_MAC_WIFI?CERBERUS_MAC_DWELL_MS:0};
            result=ops->start(ops->context,radio,&request,&observation);(void)apply(r,radio,result,&observation);
            continue;
        }
        if(!r->owned)continue;
        if(now<r->last_poll_us || now<r->last_renew_us || now<r->last_read_us){
            r->error=CERBERUS_MAC_MONITOR_BAD_CLOCK;r->stop_requested=true;r->poll_due=true;
        }
        if(r->stop_requested || !s->running){
            r->stop_requested=true;
            if(!r->poll_due && !due(now,r->last_poll_us,MAC_POLL_US))continue;
            r->poll_due=false;r->last_poll_us=now;
            result=ops->stop(ops->context,radio,&r->binding,&observation);(void)apply(r,radio,result,&observation);continue;
        }
        bool renewal=radio==CERBERUS_MAC_WIFI && r->binding.epoch && due(now,r->last_renew_us,MAC_RENEW_US);
        if(renewal){
            cerberus_mac_renew_request_t request={.binding=r->binding,.lease_ms=CERBERUS_MAC_LEASE_MS};
            r->last_renew_us=r->last_poll_us=now;r->poll_due=false;
            result=ops->renew(ops->context,radio,&request,&observation);
            if(!apply(r,radio,result,&observation))continue;
        }else if(r->poll_due || due(now,r->last_poll_us,MAC_POLL_US)){
            r->last_poll_us=now;r->poll_due=false;result=ops->poll(ops->context,radio,&r->binding,&observation);
            if(!apply(r,radio,result,&observation))continue;
        }
        if(!r->owned || !r->binding.epoch || r->observation.status.state!=CERBERUS_MAC_RUNNING)continue;
        if(r->pending_ack){
            cerberus_mac_ack_request_t ack={.job_id=r->binding.job_id,.epoch=r->binding.epoch,.snapshot_id=r->pending_ack};
            result=ops->ack(ops->context,radio,&ack);
            if(result.no_ownership)release(r,result.error?result.error:CERBERUS_MAC_MONITOR_BAD_SOURCE);
            else if(result.error)r->error=result.error;
            else{r->pending_ack=0;r->last_read_us=now;r->error=0;}
            continue;
        }
        if(!r->reading && !due(now,r->last_read_us,MAC_POLL_US))continue;
        r->reading=true;cerberus_mac_monitor_transfer_t transfer={0};
        result=ops->read_step(ops->context,radio,&r->binding,&r->staging,&transfer);
        if(result.no_ownership){release(r,result.error?result.error:CERBERUS_MAC_MONITOR_BAD_SOURCE);continue;}
        if(result.error){r->error=result.error;continue;}
        if(!transfer.complete)continue;
        unsigned publication=cerberus_mac_monitor_inventory_publish(inventory,radio,r->binding.epoch,&r->staging,
            transfer.request_started_us,ops->now_us(ops->context),transfer.timing_valid);
        if(publication!=CERBERUS_MAC_PUBLISHED && publication!=CERBERUS_MAC_DUPLICATE){
            r->error=CERBERUS_MAC_MONITOR_BAD_SNAPSHOT;r->faulted=true;r->stop_requested=true;continue;
        }
        r->pending_ack=r->staging.header.snapshot_id;r->reading=false;r->error=0;
    }
}
