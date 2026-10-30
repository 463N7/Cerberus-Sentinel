#pragma once

/* Source-local broadcast facts and separate address-prefix clues. Labels do
 * not authenticate hardware, camera identity, ownership, or a serial number. */
#include "ble_flock_hints.h"
#include <stdio.h>

static inline const char *cerberus_flock_level_label(cerberus_ble_flock_level_t level)
{
    switch (level) {
    case CERBERUS_BLE_FLOCK_LEVEL_CLUE: return "CLUE";
    case CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE: return "SIG";
    case CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED: return "CORROB";
    default: return "NONE";
    }
}

static inline unsigned cerberus_flock_fact_count(uint8_t bits)
{
    unsigned count=0;
    for(;bits;bits>>=1U)count+=bits&1U;
    return count;
}

static inline const char *cerberus_flock_first_context(uint8_t contexts)
{
    if(contexts&CERBERUS_BLE_FLOCK_LIST16)return "L16";
    if(contexts&CERBERUS_BLE_FLOCK_LIST32)return "L32";
    if(contexts&CERBERUS_BLE_FLOCK_LIST128)return "L128";
    if(contexts&CERBERUS_BLE_FLOCK_DATA16)return "D16";
    if(contexts&CERBERUS_BLE_FLOCK_DATA32)return "D32";
    if(contexts&CERBERUS_BLE_FLOCK_DATA128)return "D128";
    return "?";
}

static inline void cerberus_flock_hint_line(const cerberus_ble_flock_hints_t *hints,
    bool response,uint32_t age_ms,char *out,size_t capacity)
{
    if(!out||!capacity)return;
    const char *source=response?"RSP":"ADV";
    cerberus_ble_flock_level_t level=cerberus_ble_flock_hints_level(hints);
    if(level==CERBERUS_BLE_FLOCK_LEVEL_NONE){
        snprintf(out,capacity,"%s NO QUALIFYING FLOCK FORMAT",source);return;
    }
    char reason[32],remainder[20]="";
    unsigned used=0;
    if(level==CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED){
        snprintf(reason,sizeof(reason),"%s+09C8DATA",hints->facts&CERBERUS_BLE_FLOCK_FS_BATTERY?"FSBATTERY":"PENGUIN");used=3;
    }else if(hints->accessory_contexts){
        snprintf(reason,sizeof(reason),"ACCESSORY UUID %s",cerberus_flock_first_context(hints->accessory_contexts));used=1;
    }else if(hints->raven_3100_contexts&&hints->raven_3200_contexts){
        snprintf(reason,sizeof(reason),"3100 %s+3200 %s",cerberus_flock_first_context(hints->raven_3100_contexts),cerberus_flock_first_context(hints->raven_3200_contexts));used=2;
    }else if(level==CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE){
        snprintf(reason,sizeof(reason),"%s NAME",hints->facts&CERBERUS_BLE_FLOCK_FS_BATTERY?"FS BATTERY":"PENGUIN");used=1;
    }else{
        snprintf(reason,sizeof(reason),"09C8+%s",hints->facts&CERBERUS_BLE_FLOCK_SERIAL10?"10DIGIT NAME":"NO NAME");used=2;
    }
    unsigned count=cerberus_flock_fact_count(hints->facts)+cerberus_flock_fact_count(hints->accessory_contexts)+
        cerberus_flock_fact_count(hints->raven_3100_contexts)+cerberus_flock_fact_count(hints->raven_3200_contexts);
    if(count>used)snprintf(remainder,sizeof(remainder)," +%u FACTS",count-used);
    snprintf(out,capacity,"%s %s %s%s / %lus",source,cerberus_flock_level_label(level),reason,remainder,(unsigned long)(age_ms/1000U));
}

static inline void cerberus_flock_sources_line(const cerberus_ble_flock_hints_t *adv,
    const cerberus_ble_flock_hints_t *response,bool oui,char *out,size_t capacity)
{
    if(!out||!capacity)return;
    snprintf(out,capacity,"ADV %s / RSP %s%s",cerberus_flock_level_label(cerberus_ble_flock_hints_level(adv)),
        cerberus_flock_level_label(cerberus_ble_flock_hints_level(response)),oui?" / OUI":"");
}

static inline void cerberus_flock_address_line(const uint8_t address[6],bool oui,
    bool masked,char *out,size_t capacity)
{
    if(!out||!capacity)return;
    if(!oui)snprintf(out,capacity,"HINTS: SIG=FORMAT CORROB=SAME REPORT L=LIST D=DATA");
    else if(masked||!address)snprintf(out,capacity,"ADDRESS OUI CLUE / PREFIX MASKED");
    else snprintf(out,capacity,"ADDRESS OUI %02X:%02X:%02X / CLUE ONLY",address[5],address[4],address[3]);
}
