#pragma once
#include "c5_spi_link.h"
#include <stdlib.h>
#include <string.h>
#ifndef CERBERUS_UI_JOB_ALLOC
#define CERBERUS_UI_JOB_ALLOC(n) calloc(1,(n))
#endif
/* Main UI task only. Immutable identity copies survive scan reorder/rescan.
 * Production allocates this bounded collection in PSRAM when available. */
enum {WIFI_TARGET_BROADCAST=0,WIFI_TARGET_PICK_STATIONS=1,WIFI_TARGET_ALL_STATIONS=2};
typedef struct {
    cerberus_c5_wifi_result_t ap;
    cerberus_wifi_station_result_t station;
    uint8_t index;
    bool station_valid;
    uint8_t observed[CERBERUS_WIFI_STATION_LIMIT][6],observed_count;
    uint32_t selected_stations;
    uint8_t deauth_scope,station_scope;
    bool deauth_scope_set;
    uint8_t clone_mac[6]; /* Generated on a frozen AP-clone batch copy. */
} wifi_target_entry_t;
static wifi_target_entry_t *wifi_targets;
static uint8_t wifi_target_count;
static inline int wifi_targets_find(const cerberus_c5_wifi_result_t *ap)
{
    for(unsigned i=0;i<wifi_target_count;++i)
        if(wifi_targets[i].ap.channel==ap->channel&&!memcmp(wifi_targets[i].ap.bssid,ap->bssid,6))return (int)i;
    return -1;
}
static inline void wifi_targets_clear(void)
{free(wifi_targets);wifi_targets=NULL;wifi_target_count=0;}
static inline uint32_t wifi_targets_mask(unsigned count)
{return count>=32?UINT32_MAX:count?(UINT32_C(1)<<count)-1U:0;}
static inline uint8_t wifi_targets_selected_count(const wifi_target_entry_t *entry)
{
    if(!entry)return 0;
    uint32_t bits=entry->selected_stations;uint8_t count=0;
    while(bits){bits&=bits-1U;++count;}return count;
}
static inline int wifi_targets_observed_index(const wifi_target_entry_t *entry,const uint8_t mac[6])
{
    if(!entry)return -1;
    for(unsigned i=0;i<entry->observed_count;++i)if(!memcmp(entry->observed[i],mac,6))return (int)i;
    return -1;
}
static inline uint8_t wifi_targets_scope(const wifi_target_entry_t *entry,uint8_t mode)
{
    if(cerberus_wifi_ap_needs_station(mode))return entry?entry->station_scope:WIFI_TARGET_PICK_STATIONS;
    if(mode!=CERBERUS_AP_DEAUTH)return WIFI_TARGET_BROADCAST;
    if(!entry)return WIFI_TARGET_BROADCAST;
    return entry->deauth_scope_set?entry->deauth_scope:
        wifi_targets_selected_count(entry)?WIFI_TARGET_PICK_STATIONS:WIFI_TARGET_BROADCAST;
}
static inline void wifi_targets_set_scope(wifi_target_entry_t *entry,uint8_t mode,uint8_t scope)
{
    if(!entry||scope>WIFI_TARGET_ALL_STATIONS)return;
    if(cerberus_wifi_ap_needs_station(mode)){
        if(scope!=WIFI_TARGET_BROADCAST)entry->station_scope=scope;
    }else if(mode==CERBERUS_AP_DEAUTH){entry->deauth_scope=scope;entry->deauth_scope_set=true;}
}
static inline uint8_t wifi_targets_scope_count(const wifi_target_entry_t *entry,uint8_t scope)
{return !entry?0:scope==WIFI_TARGET_ALL_STATIONS?entry->observed_count:
    scope==WIFI_TARGET_PICK_STATIONS?wifi_targets_selected_count(entry):0;}
static inline bool wifi_targets_station_set(const wifi_target_entry_t *entry,uint8_t scope,cerberus_wifi_ap_station_set_t *out)
{
    if(!out)return false;
    memset(out,0,sizeof(*out));if(!entry||scope==WIFI_TARGET_BROADCAST)return true;
    for(unsigned i=0;i<entry->observed_count;++i){
        if(scope==WIFI_TARGET_ALL_STATIONS||(entry->selected_stations&(UINT32_C(1)<<i)))
            memcpy(out->mac[out->count++],entry->observed[i],6);
    }
    return out->count>0;
}
static inline bool wifi_targets_add(const cerberus_c5_wifi_result_t *ap,uint8_t index)
{
    if(wifi_targets_find(ap)>=0)return true;
    if(wifi_target_count>=CERBERUS_WIFI_SCAN_MAX_RESULTS)return false;
    if(!wifi_targets){wifi_targets=CERBERUS_UI_JOB_ALLOC(sizeof(*wifi_targets)*CERBERUS_WIFI_SCAN_MAX_RESULTS);if(!wifi_targets)return false;}
    wifi_targets[wifi_target_count++]=(wifi_target_entry_t){.ap=*ap,.index=index,.station_scope=WIFI_TARGET_PICK_STATIONS};return true;
}
static inline bool wifi_targets_toggle(const cerberus_c5_wifi_result_t *ap,uint8_t index)
{
    int i=wifi_targets_find(ap);
    if(i<0)return wifi_targets_add(ap,index);
    --wifi_target_count;memmove(wifi_targets+i,wifi_targets+i+1,(wifi_target_count-(unsigned)i)*sizeof(*wifi_targets));
    if(!wifi_target_count)wifi_targets_clear();
    return true;
}
static inline bool wifi_targets_single(const cerberus_c5_wifi_result_t *ap,uint8_t index)
{
    int i=wifi_targets_find(ap);
    if(i>=0){wifi_target_entry_t saved=wifi_targets[i];wifi_targets[0]=saved;wifi_target_count=1;return true;}
    if(!wifi_targets)return wifi_targets_add(ap,index);
    wifi_targets[0]=(wifi_target_entry_t){.ap=*ap,.index=index,.station_scope=WIFI_TARGET_PICK_STATIONS};wifi_target_count=1;return true;
}
static inline bool wifi_targets_station(const uint8_t bssid[6],const cerberus_wifi_station_result_t *station,bool selected)
{
    for(unsigned i=0;i<wifi_target_count;++i)
        if(wifi_targets[i].ap.channel==station->channel&&!memcmp(wifi_targets[i].ap.bssid,bssid,6)){
            wifi_target_entry_t *entry=&wifi_targets[i];
            int row=wifi_targets_observed_index(entry,station->mac);
            if(row<0){
                if(!selected)return true;
                if(entry->observed_count>=CERBERUS_WIFI_STATION_LIMIT)return false;
                row=entry->observed_count++;memcpy(entry->observed[row],station->mac,6);
            }
            if(selected)entry->selected_stations|=UINT32_C(1)<<row;
            else entry->selected_stations&=~(UINT32_C(1)<<row);
            entry->station_valid=entry->selected_stations!=0;
            if(selected)entry->station=*station;
            int focused=wifi_targets_observed_index(entry,entry->station.mac);
            if(entry->station_valid&&(focused<0||!(entry->selected_stations&(UINT32_C(1)<<focused)))){
                for(unsigned j=0;j<entry->observed_count;++j)if(entry->selected_stations&(UINT32_C(1)<<j)){
                    memcpy(entry->station.mac,entry->observed[j],6);entry->station.channel=entry->ap.channel;break;
                }
            }
            if(selected){
                if(!entry->deauth_scope_set)entry->deauth_scope=WIFI_TARGET_PICK_STATIONS;
                if(entry->deauth_scope==WIFI_TARGET_ALL_STATIONS){entry->deauth_scope=WIFI_TARGET_PICK_STATIONS;entry->deauth_scope_set=true;}
                entry->station_scope=WIFI_TARGET_PICK_STATIONS;
            }
            return true;
        }
    return false;
}
/* Apply a completed C5 snapshot atomically. Old picks survive only when the
 * same MAC appears in this fresh capture for the same BSSID/channel. */
static inline bool wifi_targets_inventory(const uint8_t bssid[6],uint8_t channel,
    const cerberus_wifi_station_result_t *rows,uint8_t count)
{
    if(count>CERBERUS_WIFI_STATION_LIMIT||(!rows&&count))return false;
    for(unsigned i=0;i<wifi_target_count;++i)if(wifi_targets[i].ap.channel==channel&&
        !memcmp(wifi_targets[i].ap.bssid,bssid,6)){
        wifi_target_entry_t *entry=&wifi_targets[i];
        uint8_t previous[CERBERUS_WIFI_STATION_LIMIT][6];uint8_t n=entry->observed_count;
        memcpy(previous,entry->observed,sizeof(previous));uint32_t selected=entry->selected_stations;
        entry->observed_count=count;entry->selected_stations=0;
        memset(entry->observed,0,sizeof(entry->observed));
        for(unsigned j=0;j<count;++j){
            memcpy(entry->observed[j],rows[j].mac,6);
            for(unsigned k=0;k<n;++k)if((selected&(UINT32_C(1)<<k))&&!memcmp(previous[k],rows[j].mac,6)){
                entry->selected_stations|=UINT32_C(1)<<j;break;
            }
        }
        entry->station_valid=entry->selected_stations!=0;
        if(entry->station_valid)for(unsigned j=0;j<count;++j)if(entry->selected_stations&(UINT32_C(1)<<j)){
            entry->station=rows[j];break;
        }
        return true;
    }
    return false;
}
static inline void wifi_targets_select_all(wifi_target_entry_t *entry)
{
    if(!entry)return;
    entry->selected_stations=wifi_targets_mask(entry->observed_count);
    entry->station_valid=entry->selected_stations!=0;
    if(entry->station_valid){memcpy(entry->station.mac,entry->observed[0],6);entry->station.channel=entry->ap.channel;}
    entry->station_scope=WIFI_TARGET_PICK_STATIONS;
    if(!entry->deauth_scope_set||entry->deauth_scope==WIFI_TARGET_ALL_STATIONS){
        entry->deauth_scope=WIFI_TARGET_PICK_STATIONS;entry->deauth_scope_set=true;
    }
}
static inline void wifi_targets_clear_stations(wifi_target_entry_t *entry)
{
    if(!entry)return;
    entry->selected_stations=0;entry->station_valid=false;
    entry->station_scope=WIFI_TARGET_PICK_STATIONS;
    if(entry->deauth_scope==WIFI_TARGET_ALL_STATIONS){
        entry->deauth_scope=WIFI_TARGET_PICK_STATIONS;entry->deauth_scope_set=true;
    }
}
