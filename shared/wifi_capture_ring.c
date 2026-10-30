#include "wifi_capture_ring.h"
#include <string.h>

static void increment(atomic_uint *counter)
{
    unsigned value=atomic_load_explicit(counter,memory_order_relaxed);
    if(value!=UINT32_MAX)atomic_store_explicit(counter,value+1U,memory_order_relaxed);
}

void cerberus_wifi_capture_ring_init(cerberus_wifi_capture_ring_t *ring)
{
    if(!ring)return;
    memset(ring,0,sizeof(*ring));
    atomic_init(&ring->head,0);atomic_init(&ring->tail,0);
    atomic_init(&ring->generation,0);atomic_init(&ring->next_sequence,0);
    atomic_init(&ring->producer_inflight,0);atomic_init(&ring->active,false);
    atomic_init(&ring->accepted,0);atomic_init(&ring->dropped_stopped,0);
    atomic_init(&ring->dropped_stale,0);atomic_init(&ring->dropped_full,0);
    atomic_init(&ring->dropped_oversize,0);atomic_init(&ring->dropped_truncated,0);
    atomic_init(&ring->dropped_invalid,0);atomic_init(&ring->high_watermark,0);
}

bool cerberus_wifi_capture_ring_start(cerberus_wifi_capture_ring_t *ring,uint32_t generation)
{
    if(!ring||!generation||atomic_load_explicit(&ring->active,memory_order_acquire)||
       atomic_load_explicit(&ring->producer_inflight,memory_order_acquire)||
       atomic_load_explicit(&ring->head,memory_order_acquire)!=
           atomic_load_explicit(&ring->tail,memory_order_acquire)||
       generation<=atomic_load_explicit(&ring->generation,memory_order_relaxed))return false;
    atomic_store_explicit(&ring->generation,generation,memory_order_relaxed);
    atomic_store_explicit(&ring->accepted,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_stopped,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_stale,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_full,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_oversize,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_truncated,0,memory_order_relaxed);
    atomic_store_explicit(&ring->dropped_invalid,0,memory_order_relaxed);
    atomic_store_explicit(&ring->high_watermark,0,memory_order_relaxed);
    atomic_store_explicit(&ring->active,true,memory_order_release);
    return true;
}

void cerberus_wifi_capture_ring_stop(cerberus_wifi_capture_ring_t *ring)
{if(ring)atomic_store_explicit(&ring->active,false,memory_order_release);}

static bool valid_channel(uint8_t channel)
{return (channel>=1&&channel<=14)||(channel>=32&&channel<=177);}

static bool valid_metadata(const cerberus_wifi_pcap_metadata_t *metadata,size_t captured)
{
    if(!metadata||!valid_channel(metadata->channel)||
       (metadata->rssi_known&&metadata->rssi_dbm>0)||
       (metadata->frame_has_fcs&&captured<4U))return false;
    if(metadata->time_kind==CERBERUS_WIFI_PCAP_TIME_MONOTONIC)return true;
    return metadata->time_kind==CERBERUS_WIFI_PCAP_TIME_UTC&&
        metadata->capture_time_us>=UINT64_C(946684800000000)&&
        metadata->capture_time_us<=UINT64_C(253402300799999999);
}

cerberus_wifi_capture_push_result_t cerberus_wifi_capture_ring_push(
    cerberus_wifi_capture_ring_t *ring,uint32_t generation,
    const cerberus_wifi_pcap_metadata_t *metadata,const uint8_t *frame,
    size_t captured,size_t original)
{
    if(!ring)return CERBERUS_WIFI_CAPTURE_DROP_INVALID;
    if(!atomic_load_explicit(&ring->active,memory_order_acquire)){
        increment(&ring->dropped_stopped);return CERBERUS_WIFI_CAPTURE_DROP_STOPPED;
    }
    atomic_fetch_add_explicit(&ring->producer_inflight,1,memory_order_acq_rel);
    if(!atomic_load_explicit(&ring->active,memory_order_acquire)){
        atomic_fetch_sub_explicit(&ring->producer_inflight,1,memory_order_release);
        increment(&ring->dropped_stopped);return CERBERUS_WIFI_CAPTURE_DROP_STOPPED;
    }
    if(!generation||generation!=atomic_load_explicit(&ring->generation,memory_order_acquire)){
        increment(&ring->dropped_stale);
        atomic_fetch_sub_explicit(&ring->producer_inflight,1,memory_order_release);
        return CERBERUS_WIFI_CAPTURE_DROP_STALE;
    }
    cerberus_wifi_capture_push_result_t result=CERBERUS_WIFI_CAPTURE_DROP_INVALID;
    atomic_uint *drop=&ring->dropped_invalid;
    if(captured>CERBERUS_WIFI_PCAP_FRAME_MAX){
        result=CERBERUS_WIFI_CAPTURE_DROP_OVERSIZE;drop=&ring->dropped_oversize;
    }else if(!frame||!captured||original<captured||original>UINT16_MAX||
             !valid_metadata(metadata,captured)){
        result=CERBERUS_WIFI_CAPTURE_DROP_INVALID;
    }else if(metadata->time_kind==CERBERUS_WIFI_PCAP_TIME_MONOTONIC&&
             original>captured&&captured!=CERBERUS_WIFI_PCAP_FRAME_MAX){
        result=CERBERUS_WIFI_CAPTURE_DROP_TRUNCATED;drop=&ring->dropped_truncated;
    }else{
        unsigned head=atomic_load_explicit(&ring->head,memory_order_relaxed);
        unsigned tail=atomic_load_explicit(&ring->tail,memory_order_acquire);
        if(head-tail>=CERBERUS_WIFI_CAPTURE_RING_SLOTS){
            result=CERBERUS_WIFI_CAPTURE_DROP_FULL;drop=&ring->dropped_full;
        }else{
            cerberus_wifi_capture_record_t *slot=&ring->slots[head%CERBERUS_WIFI_CAPTURE_RING_SLOTS];
            unsigned sequence=atomic_load_explicit(&ring->next_sequence,memory_order_relaxed)+1U;
            if(!sequence)sequence=1U;
            atomic_store_explicit(&ring->next_sequence,sequence,memory_order_relaxed);
            slot->sequence=sequence;
            slot->generation=generation;
            slot->captured_length=(uint16_t)captured;slot->original_length=(uint16_t)original;
            slot->metadata=*metadata;memcpy(slot->frame,frame,captured);
            atomic_store_explicit(&ring->head,head+1U,memory_order_release);
            unsigned used=head+1U-tail;
            if(used>atomic_load_explicit(&ring->high_watermark,memory_order_relaxed))
                atomic_store_explicit(&ring->high_watermark,used,memory_order_relaxed);
            increment(&ring->accepted);
            atomic_fetch_sub_explicit(&ring->producer_inflight,1,memory_order_release);
            return CERBERUS_WIFI_CAPTURE_ACCEPTED;
        }
    }
    increment(drop);
    atomic_fetch_sub_explicit(&ring->producer_inflight,1,memory_order_release);
    return result;
}

const cerberus_wifi_capture_record_t *cerberus_wifi_capture_ring_peek(
    const cerberus_wifi_capture_ring_t *ring)
{
    if(!ring)return NULL;
    unsigned tail=atomic_load_explicit(&ring->tail,memory_order_relaxed);
    unsigned head=atomic_load_explicit(&ring->head,memory_order_acquire);
    return head==tail?NULL:&ring->slots[tail%CERBERUS_WIFI_CAPTURE_RING_SLOTS];
}

bool cerberus_wifi_capture_ring_ack(cerberus_wifi_capture_ring_t *ring,
    uint32_t generation,uint32_t sequence)
{
    if(!ring||!generation||!sequence)return false;
    unsigned tail=atomic_load_explicit(&ring->tail,memory_order_relaxed);
    unsigned head=atomic_load_explicit(&ring->head,memory_order_acquire);
    if(head==tail)return false;
    const cerberus_wifi_capture_record_t *slot=&ring->slots[tail%CERBERUS_WIFI_CAPTURE_RING_SLOTS];
    if(slot->generation!=generation||slot->sequence!=sequence)return false;
    atomic_store_explicit(&ring->tail,tail+1U,memory_order_release);
    return true;
}

void cerberus_wifi_capture_ring_stats(const cerberus_wifi_capture_ring_t *ring,
    cerberus_wifi_capture_ring_stats_t *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));if(!ring)return;
    out->generation=atomic_load_explicit(&ring->generation,memory_order_acquire);
    out->accepted=atomic_load_explicit(&ring->accepted,memory_order_relaxed);
    out->dropped_stopped=atomic_load_explicit(&ring->dropped_stopped,memory_order_relaxed);
    out->dropped_stale=atomic_load_explicit(&ring->dropped_stale,memory_order_relaxed);
    out->dropped_full=atomic_load_explicit(&ring->dropped_full,memory_order_relaxed);
    out->dropped_oversize=atomic_load_explicit(&ring->dropped_oversize,memory_order_relaxed);
    out->dropped_truncated=atomic_load_explicit(&ring->dropped_truncated,memory_order_relaxed);
    out->dropped_invalid=atomic_load_explicit(&ring->dropped_invalid,memory_order_relaxed);
    out->high_watermark=(uint8_t)atomic_load_explicit(&ring->high_watermark,memory_order_relaxed);
    out->producer_inflight=atomic_load_explicit(&ring->producer_inflight,memory_order_acquire);
    unsigned tail=atomic_load_explicit(&ring->tail,memory_order_acquire);
    unsigned head=atomic_load_explicit(&ring->head,memory_order_acquire);
    unsigned pending=head-tail;
    out->pending=(uint8_t)(pending>CERBERUS_WIFI_CAPTURE_RING_SLOTS?
        CERBERUS_WIFI_CAPTURE_RING_SLOTS:pending);
    out->active=atomic_load_explicit(&ring->active,memory_order_acquire);
}
