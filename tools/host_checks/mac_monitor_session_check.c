#include "mac_monitor_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"line%u: %s\n",__LINE__,#c);abort();}}while(0)
enum{RESERVE,START,POLL,RENEW,READ,ACK,STOP,OPS};
typedef struct{
    cerberus_mac_observation_t collector;
    cerberus_mac_snapshot_t pinned;
    cerberus_mac_binding_t binding;
    uint64_t source,anchor;
    uint32_t nextjob;
    unsigned calls[OPS],offset;
    int32_t fail[OPS];bool gone[OPS],owned,held,undelivered;
    unsigned corrupt;
} radio_t;
static struct{uint64_t now;radio_t radios[2];} fake;
static cerberus_mac_monitor_session_t session;
static cerberus_mac_monitor_inventory_t inventory;
static cerberus_mac_monitor_ops_t ops;
static void snapshot(unsigned n,cerberus_mac_monitor_observation_t *o)
{
    radio_t *r=&fake.radios[n];memset(o,0,sizeof(*o));
    o->status=(cerberus_mac_status_t){.job_id=r->binding.job_id,.epoch=r->binding.epoch,.state=r->held?CERBERUS_MAC_ERROR:r->owned?CERBERUS_MAC_RUNNING:CERBERUS_MAC_COMPLETE,
        .error_code=r->held?123:0,.channel=!n&&r->owned&&!r->held?6:0,.flags=r->owned?CERBERUS_MAC_OWNED:0,.version=CERBERUS_MAC_VERSION};
    if(r->owned&&!r->held)o->status.flags|=CERBERUS_MAC_RECEIVING;
    o->profile=(cerberus_mac_profile_t){.epoch=r->binding.epoch,.lease_ms=n?0:CERBERUS_MAC_LEASE_MS,
        .dwell_ms=n?0:CERBERUS_MAC_DWELL_MS,.channel_count=n?0:30,.version=CERBERUS_MAC_VERSION};o->valid=true;
    if(r->corrupt==1)o->profile.lease_ms++;
    if(r->corrupt==2)o->profile.reserved[1]=1;
    if(r->corrupt==3)o->status.job_id++;
    if(r->corrupt==4)o->status.epoch++;
}
static cerberus_mac_monitor_operation_t reply(unsigned radio,unsigned op)
{radio_t *r=&fake.radios[radio];++r->calls[op];cerberus_mac_monitor_operation_t result={r->fail[op],r->gone[op]};r->fail[op]=0;r->gone[op]=false;return result;}
static void bound(unsigned n,const cerberus_mac_binding_t *b)
{radio_t *r=&fake.radios[n];CHECK(b->boot_id==r->binding.boot_id && b->job_id==r->binding.job_id && (!b->epoch || b->epoch==r->binding.epoch));}
static uint64_t now(void *c){CHECK(c==&fake);return fake.now;}
static int32_t reserve(void *c,uint8_t n,cerberus_mac_binding_t *b)
{CHECK(c==&fake);radio_t *r=&fake.radios[n];cerberus_mac_monitor_operation_t result=reply(n,RESERVE);if(result.error)return result.error;r->binding.job_id=++r->nextjob;*b=r->binding;b->epoch=0;return 0;}
static cerberus_mac_monitor_operation_t start(void *c,uint8_t n,const cerberus_mac_start_request_t *q,cerberus_mac_monitor_observation_t *o)
{
    CHECK(c==&fake);radio_t *r=&fake.radios[n];cerberus_mac_monitor_operation_t result=reply(n,START);
    CHECK(q->job_id==r->binding.job_id && q->boot_id==r->binding.boot_id && q->version==1);
    CHECK(q->lease_ms==(n?0:5000) && q->dwell_ms==(n?0:250));
    if(!r->undelivered){++r->binding.epoch;r->owned=true;CHECK(cerberus_mac_observation_init(&r->collector,n,r->source));}
    if(!result.error)snapshot(n,o);return result;
}
static cerberus_mac_monitor_operation_t poll(void *c,uint8_t n,const cerberus_mac_binding_t *b,cerberus_mac_monitor_observation_t *o)
{CHECK(c==&fake);bound(n,b);cerberus_mac_monitor_operation_t result=reply(n,POLL);if(!result.error)snapshot(n,o);return result;}
static cerberus_mac_monitor_operation_t renew(void *c,uint8_t n,const cerberus_mac_renew_request_t *q,cerberus_mac_monitor_observation_t *o)
{CHECK(c==&fake && n==0 && q->lease_ms==5000);bound(n,&q->binding);cerberus_mac_monitor_operation_t result=reply(n,RENEW);if(!result.error)snapshot(n,o);return result;}
static cerberus_mac_monitor_operation_t stop(void *c,uint8_t n,const cerberus_mac_binding_t *b,cerberus_mac_monitor_observation_t *o)
{CHECK(c==&fake);bound(n,b);radio_t *r=&fake.radios[n];cerberus_mac_monitor_operation_t result=reply(n,STOP);
    if(!result.error){if(!r->held)r->owned=false;snapshot(n,o);}return result;}
static cerberus_mac_monitor_operation_t read_step(void *c,uint8_t n,const cerberus_mac_binding_t *b,cerberus_mac_snapshot_t *out,cerberus_mac_monitor_transfer_t *t)
{
    CHECK(c==&fake);bound(n,b);radio_t *r=&fake.radios[n];cerberus_mac_monitor_operation_t result=reply(n,READ);if(result.error||result.no_ownership)return result;
    if(!r->offset){CHECK(cerberus_mac_observation_snapshot(&r->collector,r->source,&r->pinned));r->anchor=fake.now;memset(out,0,sizeof(*out));}
    size_t bytes=n?sizeof(*out):80U;if(bytes>sizeof(*out)-r->offset)bytes=sizeof(*out)-r->offset;
    memcpy((uint8_t *)out+r->offset,(const uint8_t *)&r->pinned+r->offset,bytes);r->offset+=(unsigned)bytes;
    if(r->offset==sizeof(*out)){t->complete=t->timing_valid=true;t->request_started_us=r->anchor;}
    return result;
}
static cerberus_mac_monitor_operation_t ack(void *c,uint8_t n,const cerberus_mac_ack_request_t *a)
{
    CHECK(c==&fake);radio_t *r=&fake.radios[n];CHECK(a->job_id==r->binding.job_id && a->epoch==r->binding.epoch && a->snapshot_id==r->pinned.header.snapshot_id);
    cerberus_mac_monitor_operation_t result=reply(n,ACK);if(!result.error)r->offset=0;return result;
}
static void setup(void)
{
    memset(&fake,0,sizeof(fake));cerberus_mac_monitor_session_init(&session);cerberus_mac_monitor_inventory_init(&inventory);
    for(unsigned n=0;n<2;++n){fake.radios[n].binding.boot_id=UINT64_C(0x10000000000)+n;fake.radios[n].binding.epoch=UINT64_C(0x20000000000)+n;fake.radios[n].source=UINT64_C(0x30000000000)+n;}
    ops=(cerberus_mac_monitor_ops_t){.context=&fake,.now_us=now,.reserve=reserve,.start=start,.poll=poll,.renew=renew,.read_step=read_step,.ack=ack,.stop=stop};
}
static void tick(uint64_t elapsed)
{fake.now+=elapsed;for(unsigned n=0;n<2;++n)fake.radios[n].source+=elapsed;cerberus_mac_monitor_session_tick(&session,&inventory,&ops);}
static void begin(void){CHECK(cerberus_mac_monitor_session_start(&session));tick(0);CHECK(session.radios[0].owned && session.radios[1].owned);}
static void add(unsigned n,unsigned id)
{radio_t *r=&fake.radios[n];uint8_t address[6]={2,3,4,5,6,(uint8_t)id};CHECK(cerberus_mac_observation_observe(&r->collector,address,0,-40,n?0:6,1,r->source));}
static void drain(unsigned ticks){for(unsigned n=0;n<ticks;++n)tick(100000);}
static void lifetime(void)
{
    setup();for(unsigned n=0;n<10;++n)tick(1000);CHECK(!fake.radios[0].calls[RESERVE] && !fake.radios[1].calls[START]);
    CHECK(cerberus_mac_monitor_session_start(&session));cerberus_mac_monitor_session_stop(&session);tick(1);CHECK(!fake.radios[0].calls[START]);
    begin();add(0,1);add(1,2);drain(24);CHECK(inventory.banks[0].header.count==1 && inventory.banks[1].header.count==1);
    CHECK(fake.radios[0].calls[RENEW]>=2 && !fake.radios[1].calls[RENEW]);
    CHECK(inventory.banks[0].records[0].observations==1 && inventory.banks[1].records[0].observations==1);
    CHECK(cerberus_mac_monitor_inventory_select(&inventory,&inventory.banks[0].records[0]));
    cerberus_mac_monitor_record_t frozen=inventory.selected.frozen;
    add(0,1);add(1,2);drain(44);CHECK(inventory.banks[0].records[0].observations==2 && inventory.banks[1].records[0].observations==2);
    CHECK(!memcmp(&frozen,&inventory.selected.frozen,sizeof(frozen)));
    fake.radios[0].held=true;fake.radios[1].fail[STOP]=-8;cerberus_mac_monitor_session_stop(&session);tick(1);
    CHECK(!cerberus_mac_monitor_session_released(&session) && session.radios[0].error==123 && session.radios[1].error==-8);
    CHECK(!cerberus_mac_monitor_session_start(&session));fake.radios[0].held=false;tick(250000);CHECK(cerberus_mac_monitor_session_released(&session));
    unsigned starts=fake.radios[0].calls[START];drain(10);CHECK(fake.radios[0].calls[START]==starts);
}
static void lost_start_ack_retry_and_failures(void)
{
    setup();fake.radios[0].fail[START]=-7;begin();CHECK(!session.radios[0].binding.epoch);tick(1);CHECK(session.radios[0].binding.epoch==fake.radios[0].binding.epoch);
    add(0,1);add(1,2);while(!session.radios[0].pending_ack)tick(100000);
    uint32_t publications=inventory.banks[0].publications;unsigned reads=fake.radios[0].calls[READ];
    for(unsigned n=0;n<10;++n){fake.radios[0].fail[ACK]=-8;tick(100000);CHECK(inventory.banks[0].publications==publications && fake.radios[0].calls[READ]==reads);}
    CHECK(inventory.banks[1].publications>1);tick(1);CHECK(!session.radios[0].pending_ack);
    fake.radios[0].fail[POLL]=-9;tick(250000);CHECK(session.radios[0].owned);
    fake.radios[0].gone[POLL]=true;tick(250000);CHECK(!session.radios[0].owned && !fake.radios[0].calls[STOP]);
    cerberus_mac_monitor_session_stop(&session);tick(1);CHECK(cerberus_mac_monitor_session_released(&session));
    setup();fake.radios[0].fail[RESERVE]=-10;CHECK(cerberus_mac_monitor_session_start(&session));tick(0);
    CHECK(!session.radios[0].owned && session.radios[1].owned && inventory.banks[0].unavailable);
    add(1,9);drain(5);CHECK(inventory.banks[1].header.count==1);
    setup();fake.radios[0].corrupt=1;CHECK(cerberus_mac_monitor_session_start(&session));tick(0);
    CHECK(!session.radios[0].owned && session.radios[1].owned); /* Coherent foreign profile, no control. */
}
static void malformed_and_repeated(void)
{
    for(unsigned flaw=2;flaw<=4;++flaw){setup();fake.radios[0].corrupt=flaw;begin();CHECK(session.radios[0].stop_requested);
        fake.radios[0].corrupt=0;tick(1);CHECK(!session.radios[0].owned && fake.radios[0].calls[STOP]==1);}
    setup();begin();add(0,7);drain(3);CHECK(fake.radios[0].offset && !inventory.banks[0].published);
    cerberus_mac_monitor_session_stop(&session);tick(1);CHECK(cerberus_mac_monitor_session_released(&session) && !inventory.banks[0].published);
    setup();for(unsigned n=0;n<260;++n){begin();cerberus_mac_monitor_session_stop(&session);tick(1);CHECK(cerberus_mac_monitor_session_released(&session));}
    CHECK(fake.radios[0].binding.job_id==260 && fake.radios[1].binding.job_id==260);
    setup();begin();tick(1000000);fake.now=0;tick(0);CHECK(cerberus_mac_monitor_session_released(&session));
}
int main(void){lifetime();lost_start_ack_retry_and_failures();malformed_and_repeated();printf("MAC session: %u checks passed; session %zu bytes\n",checks,sizeof(session));return 0;}
