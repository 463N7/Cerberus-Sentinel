#include "mac_observation.h"
#include <limits.h>
#include <string.h>

static uint32_t increment(uint32_t value) { return value==UINT32_MAX?value:value+1U; }
static bool channel_valid(uint8_t c)
{
    return (c>=1U && c<=14U) || (c>=36U && c<=64U && c%4U==0U) ||
        (c>=100U && c<=144U && c%4U==0U) || (c>=149U && c<=177U && (c-149U)%4U==0U);
}
static bool address_valid(const uint8_t address[6],uint8_t radio,uint8_t type)
{
    if(!address || radio>CERBERUS_MAC_BLE)return false;
    if(radio==CERBERUS_MAC_BLE)return type<=3U;
    uint8_t any=0;for(size_t i=0;i<6U;++i)any|=address[i];
    return type==0U && any && !(address[0]&1U);
}
static bool rssi_valid(int value) { return value>=-127 && value<=20; }
static bool type_valid(uint8_t radio,uint8_t type,bool aggregate)
{
    if(!type || (type&~7U))return false;
    if(aggregate)return radio==CERBERUS_MAC_WIFI || (type&3U)!=0U;
    return radio==CERBERUS_MAC_WIFI?(type==1U || type==2U || type==4U):
        ((type&3U)==CERBERUS_MAC_BLE_ORIGINAL || (type&3U)==CERBERUS_MAC_BLE_RESPONSE);
}

bool cerberus_mac_observation_init(cerberus_mac_observation_t *c,uint8_t radio,uint64_t started)
{
    if(!c)return false;
    memset(c,0,sizeof(*c));
    if(radio>CERBERUS_MAC_BLE)return false;
    c->radio=radio;c->started_us=c->latest_us=started;c->initialized=true;
    return true;
}

void cerberus_mac_observation_reject(cerberus_mac_observation_t *c)
{if(c && c->initialized)c->invalid=increment(c->invalid);}

bool cerberus_mac_observation_observe(cerberus_mac_observation_t *c,
    const uint8_t address[6],uint8_t address_type,int rssi,uint8_t channel,
    uint8_t report_type,uint64_t source_us)
{
    if(!c || !c->initialized)return false;
    if(!address_valid(address,c->radio,address_type) || !type_valid(c->radio,report_type,false) ||
        (c->radio==CERBERUS_MAC_WIFI?!channel_valid(channel):channel!=0U) ||
        source_us<c->started_us || source_us<c->latest_us){cerberus_mac_observation_reject(c);return false;}
    cerberus_mac_row_t *row=NULL;
    for(size_t i=0;i<c->count;++i)
        if(c->rows[i].address_type==address_type && !memcmp(c->rows[i].address,address,6U)){row=&c->rows[i];break;}
    if(!row){
        if(c->next_incarnation==UINT32_MAX){cerberus_mac_observation_reject(c);return false;}
        if(c->count<CERBERUS_MAC_CAPACITY)row=&c->rows[c->count++];
        else{
            row=&c->rows[0];
            for(size_t i=1;i<CERBERUS_MAC_CAPACITY;++i)
                if(c->rows[i].last_us<row->last_us || (c->rows[i].last_us==row->last_us &&
                    c->rows[i].incarnation<row->incarnation))row=&c->rows[i];
            c->replacements=increment(c->replacements);
        }
        memset(row,0,sizeof(*row));memcpy(row->address,address,6U);
        row->radio=c->radio;row->address_type=address_type;
        row->incarnation=++c->next_incarnation;row->first_us=source_us;
    }
    row->last_us=source_us;row->channel=channel;
    row->rssi=rssi_valid(rssi)?(int8_t)rssi:127;
    row->flags=rssi_valid(rssi)?CERBERUS_MAC_RSSI_VALID:0U;
    row->type_mask|=report_type;row->observations=increment(row->observations);
    if(c->radio==CERBERUS_MAC_BLE && (report_type&CERBERUS_MAC_BLE_RESPONSE))row->responses=increment(row->responses);
    c->latest_us=source_us;c->observations=increment(c->observations);
    return true;
}

bool cerberus_mac_row_valid(const cerberus_mac_row_t *r,uint8_t radio,uint64_t now)
{
    if(!r || r->radio!=radio || !address_valid(r->address,radio,r->address_type) ||
        !r->incarnation || !r->observations || r->responses>r->observations ||
        r->first_us>r->last_us || r->last_us>now ||
        !type_valid(radio,r->type_mask,true) || (r->flags&~CERBERUS_MAC_RSSI_VALID) ||
        ((r->flags&CERBERUS_MAC_RSSI_VALID)?!rssi_valid(r->rssi):r->rssi!=127))return false;
    if(radio==CERBERUS_MAC_WIFI)return !r->responses && channel_valid(r->channel);
    if(r->channel || ((r->type_mask&CERBERUS_MAC_BLE_RESPONSE)!=0U)!=(r->responses!=0U))return false;
    if(!(r->type_mask&CERBERUS_MAC_BLE_ORIGINAL) && r->responses!=r->observations)return false;
    return r->observations==UINT32_MAX || !(r->type_mask&CERBERUS_MAC_BLE_ORIGINAL) || r->responses<r->observations;
}

bool cerberus_mac_snapshot_valid(const cerberus_mac_snapshot_t *s)
{
    if(!s || !s->header.snapshot_id || s->header.version!=CERBERUS_MAC_VERSION ||
        s->header.radio>CERBERUS_MAC_BLE || s->header.reserved || s->header.count>CERBERUS_MAC_CAPACITY)return false;
    uint32_t sum=0;
    for(size_t i=0;i<s->header.count;++i){
        const cerberus_mac_row_t *r=&s->rows[i];
        if(!cerberus_mac_row_valid(r,s->header.radio,s->header.source_now_us))return false;
        sum=UINT32_MAX-sum<r->observations?UINT32_MAX:sum+r->observations;
        for(size_t j=0;j<i;++j)
            if(s->rows[j].incarnation==r->incarnation || (s->rows[j].address_type==r->address_type &&
                !memcmp(s->rows[j].address,r->address,6U)))return false;
    }
    const uint8_t *unused=(const uint8_t *)&s->rows[s->header.count];
    for(size_t i=0;i<(CERBERUS_MAC_CAPACITY-s->header.count)*sizeof(s->rows[0]);++i)if(unused[i])return false;
    return s->header.observations>=sum &&
        (s->header.replacements ? s->header.count==CERBERUS_MAC_CAPACITY : s->header.observations==sum) &&
        s->header.replacements<=s->header.observations;
}

bool cerberus_mac_observation_snapshot(cerberus_mac_observation_t *c,
    uint64_t now,cerberus_mac_snapshot_t *out)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));
    if(!c || !c->initialized || c->count>CERBERUS_MAC_CAPACITY || now<c->latest_us ||
        now<c->started_us || c->next_snapshot_id==UINT32_MAX)return false;
    out->header=(cerberus_mac_snapshot_header_t){.snapshot_id=c->next_snapshot_id+1U,
        .source_now_us=now,.observations=c->observations,.invalid=c->invalid,
        .replacements=c->replacements,.count=c->count,.version=CERBERUS_MAC_VERSION,.radio=c->radio};
    memcpy(out->rows,c->rows,c->count*sizeof(c->rows[0]));
    if(!cerberus_mac_snapshot_valid(out)){memset(out,0,sizeof(*out));return false;}
    ++c->next_snapshot_id;
    return true;
}
