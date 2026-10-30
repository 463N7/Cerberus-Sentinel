#include "wifi_pinescan.h"
#include <string.h>

typedef struct {uint32_t prefix;uint8_t profile,condition;} prefix_rule_t;
/* Factual compatibility signatures audited at Marauder develop 79cc6bc.
 * These names classify rules, not verified manufacturers/device identities. */
static const prefix_rule_t prefix_rules[]={
    {0x00c0caU,CERBERUS_WIFI_PINESCAN_PROFILE_ALFA,CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR},
    {0x001337U,CERBERUS_WIFI_PINESCAN_PROFILE_ORIENT,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x1cbfceU,CERBERUS_WIFI_PINESCAN_PROFILE_SHENZHEN,CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR},
    {0x0cefafU,CERBERUS_WIFI_PINESCAN_PROFILE_IEEE,CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR},
    {0x02c0caU,CERBERUS_WIFI_PINESCAN_PROFILE_LOCAL,CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET},
    {0x021337U,CERBERUS_WIFI_PINESCAN_PROFILE_LOCAL,CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET},
    {0x000a00U,CERBERUS_WIFI_PINESCAN_PROFILE_MEDIATEK,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x000c43U,CERBERUS_WIFI_PINESCAN_PROFILE_MEDIATEK,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x000ce7U,CERBERUS_WIFI_PINESCAN_PROFILE_MEDIATEK,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x0017a5U,CERBERUS_WIFI_PINESCAN_PROFILE_MEDIATEK,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x9cefd5U,CERBERUS_WIFI_PINESCAN_PROFILE_PANDA,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0x9ce5d5U,CERBERUS_WIFI_PINESCAN_PROFILE_PANDA,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
    {0xdeadbeU,CERBERUS_WIFI_PINESCAN_PROFILE_DEADBE,CERBERUS_WIFI_PINESCAN_CONDITION_ANY},
};

bool cerberus_wifi_pinescan_decode(const uint8_t *frame,size_t length,
    uint8_t rx_channel,int8_t rssi,uint64_t observed_us,
    cerberus_wifi_pinescan_evidence_t *out)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));
    cerberus_wifi_pinescan_evidence_t pending={0};
    if(!cerberus_wifi_beacon_decode(frame,length,rx_channel,rssi,observed_us,&pending.beacon))
        return false;
    pending.privacy_bit=(pending.beacon.capabilities&0x0010U)!=0U;
    /* The shared decoder already validated every complete IE and rejected
     * malformed tails/duplicate SSID or DS tags. Count all, not just known IEs. */
    for(size_t offset=36U;offset<length;offset+=2U+frame[offset+1U])
        ++pending.ie_count;
    uint32_t prefix=((uint32_t)pending.beacon.transmitter[0]<<16U)|
        ((uint32_t)pending.beacon.transmitter[1]<<8U)|pending.beacon.transmitter[2];
    for(size_t i=0;i<sizeof(prefix_rules)/sizeof(*prefix_rules);++i){
        const prefix_rule_t *rule=&prefix_rules[i];
        if(rule->prefix!=prefix)continue;
        pending.profile=rule->profile;
        pending.condition=rule->condition;
        if(rule->condition==CERBERUS_WIFI_PINESCAN_CONDITION_ANY||
           (rule->condition==CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET&&pending.privacy_bit)||
           (rule->condition==CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR&&!pending.privacy_bit))
            pending.reasons|=CERBERUS_WIFI_PINESCAN_REASON_OUI;
        break;
    }
    if(pending.beacon.capabilities==0x0001U&&pending.ie_count==2U&&frame[36]==0U){
        size_t second=38U+frame[37];
        if(second+3U==length&&frame[second]==3U&&frame[second+1U]==1U)
            pending.reasons|=CERBERUS_WIFI_PINESCAN_REASON_MINIMAL;
    }
    *out=pending;
    return true;
}

static void increment(uint32_t *value)
{if(*value!=UINT32_MAX)++*value;}

bool cerberus_wifi_pinescan_init(cerberus_wifi_pinescan_t *table,
    cerberus_wifi_pinescan_record_t *storage,uint16_t capacity)
{
    if(!table)return false;
    memset(table,0,sizeof(*table));
    if(!storage||!capacity||capacity>CERBERUS_WIFI_PINESCAN_MAX_ROWS)return false;
    memset(storage,0,sizeof(*storage)*capacity);
    table->records=storage;table->capacity=capacity;
    return true;
}

static bool table_valid(const cerberus_wifi_pinescan_t *table)
{
    return table&&table->records&&table->capacity&&
        table->capacity<=CERBERUS_WIFI_PINESCAN_MAX_ROWS&&table->count<=table->capacity;
}

cerberus_wifi_pinescan_event_t cerberus_wifi_pinescan_observe(
    cerberus_wifi_pinescan_t *table,const uint8_t *frame,size_t length,
    uint8_t rx_channel,int8_t rssi,uint64_t observed_us,uint16_t *row_index)
{
    if(row_index)*row_index=UINT16_MAX;
    if(!table_valid(table))return CERBERUS_WIFI_PINESCAN_INVALID;
    cerberus_wifi_pinescan_evidence_t evidence;
    if(!cerberus_wifi_pinescan_decode(frame,length,rx_channel,rssi,observed_us,&evidence)){
        increment(&table->rejected_frames);return CERBERUS_WIFI_PINESCAN_INVALID;
    }
    increment(&table->valid_beacons);
    if(evidence.reasons)increment(&table->matching_beacons);
    if(observed_us<table->latest_observed_us){
        increment(&table->stale_frames);return CERBERUS_WIFI_PINESCAN_STALE;
    }
    table->latest_observed_us=observed_us;
    uint16_t index=0;
    while(index<table->count&&memcmp(table->records[index].latest.transmitter,
        evidence.beacon.transmitter,6U)!=0)++index;
    bool added=index==table->count;
    if(added&&!evidence.reasons)return CERBERUS_WIFI_PINESCAN_NONMATCH;
    if(added&&index==table->capacity){
        increment(&table->capacity_drops);return CERBERUS_WIFI_PINESCAN_FULL;
    }
    cerberus_wifi_pinescan_record_t *record=&table->records[index];
    if(added){
        memset(record,0,sizeof(*record));
        record->first_observed_us=observed_us;
        record->peak_rssi=rssi;
        ++table->count;
    }else if(memcmp(record->latest.bssid,evidence.beacon.bssid,6U)!=0)
        record->multiple_bssids=true;
    record->latest=evidence.beacon;
    increment(&record->frames);
    if(rssi>record->peak_rssi)record->peak_rssi=rssi;
    if(evidence.reasons){
        record->last_match=evidence;
        record->reasons_seen|=evidence.reasons;
        increment(&record->matching_frames);
    }
    if(row_index)*row_index=index;
    return added?CERBERUS_WIFI_PINESCAN_ADDED:CERBERUS_WIFI_PINESCAN_UPDATED;
}

bool cerberus_wifi_pinescan_copy(const cerberus_wifi_pinescan_t *table,
    uint16_t index,cerberus_wifi_pinescan_record_t *out)
{
    if(!out)return false;
    if(table_valid(table)&&index<table->count){*out=table->records[index];return true;}
    memset(out,0,sizeof(*out));return false;
}

const char *cerberus_wifi_pinescan_profile_name(uint8_t profile)
{
    static const char *const names[]={"No prefix rule","Alfa","Orient Power",
        "Shenzhen Century","IEEE RA","Hak5 local-prefix","MediaTek","Panda Wireless","DE:AD:BE pattern"};
    return profile<sizeof(names)/sizeof(*names)?names[profile]:"Unknown rule";
}

const char *cerberus_wifi_pinescan_condition_name(uint8_t condition)
{
    switch(condition){
    case CERBERUS_WIFI_PINESCAN_CONDITION_ANY:return "Any privacy bit";
    case CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_CLEAR:return "Privacy bit clear";
    case CERBERUS_WIFI_PINESCAN_CONDITION_PRIVACY_SET:return "Privacy bit set";
    default:return "No prefix rule";
    }
}
