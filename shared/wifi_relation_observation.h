#pragma once

/* Passive AP/station address-header evidence for the ordinary Wi-Fi scan.
 * The reachable Marauder develop 79cc6bcba1db6679 AP_STA path motivates this
 * view; this independent parser does not infer association or payload content.
 * Wi-Fi MACs use on-air/display byte order. Local unicast addresses are valid. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_WIFI_RELATION_MIN_FIRMWARE 60U
#define CERBERUS_WIFI_RELATION_LIMIT 32U
#define CERBERUS_WIFI_RELATION_CHUNK_BYTES 10U
#define CERBERUS_WIFI_RELATION_CHUNKS 5U
#define CERBERUS_WIFI_RELATION_RSSI_UNAVAILABLE 127
enum { CERBERUS_WIFI_RELATION_TO_AP=1U, CERBERUS_WIFI_RELATION_FROM_AP=2U };
enum { CERBERUS_WIFI_RELATION_AVAILABLE=1U, CERBERUS_WIFI_RELATION_OVERFLOW=2U };
enum { CERBERUS_WIFI_RELATION_IDLE=0U, CERBERUS_WIFI_RELATION_RUNNING=1U,
    CERBERUS_WIFI_RELATION_COMPLETE=2U, CERBERUS_WIFI_RELATION_ERROR=3U };

typedef struct __attribute__((packed)) {
    uint8_t bssid[6], station[6], channel, directions, latest_direction, rssi_valid;
    uint32_t to_ap_frames, from_ap_frames;
    uint64_t to_ap_callback_us, from_ap_callback_us;
    int8_t to_ap_rssi, from_ap_rssi;
    uint8_t reserved[2];
} cerberus_wifi_relation_row_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, count, state, flags;
    uint32_t matched_frames, dropped_frames;
    uint8_t reserved[2];
} cerberus_wifi_relation_status_t;
typedef struct __attribute__((packed)) { uint8_t generation; } cerberus_wifi_relation_status_query_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, index, chunk;
} cerberus_wifi_relation_query_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, index, chunk, length, data[CERBERUS_WIFI_RELATION_CHUNK_BYTES];
} cerberus_wifi_relation_chunk_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation;
} cerberus_wifi_relation_clock_query_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch, source_now_us;
    uint8_t generation, count, reserved[4];
} cerberus_wifi_relation_clock_t;
#ifdef __cplusplus
#define CERBERUS_RELATION_ASSERT static_assert
#else
#define CERBERUS_RELATION_ASSERT _Static_assert
#endif
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_row_t)==44U,"relation row ABI");
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_status_t)==22U,"relation status ABI");
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_query_t)==11U,"relation query ABI");
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_chunk_t)==22U,"relation chunk ABI");
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_clock_query_t)==9U,"relation clock query ABI");
CERBERUS_RELATION_ASSERT(sizeof(cerberus_wifi_relation_clock_t)==22U,"relation clock ABI");
#undef CERBERUS_RELATION_ASSERT

static inline bool cerberus_wifi_relation_mac_valid(const uint8_t *mac)
{
    static const uint8_t zero[6]={0};
    return mac && !(mac[0]&1U) && memcmp(mac,zero,6U)!=0;
}
static inline bool cerberus_wifi_relation_channel_valid(uint8_t ch)
{
    return (ch>=1U&&ch<=14U) || (ch>=36U&&ch<=64U&&ch%4U==0U) ||
        (ch>=100U&&ch<=144U&&ch%4U==0U) || (ch>=149U&&ch<=177U&&(ch-149U)%4U==0U);
}
static inline bool cerberus_wifi_relation_rssi_valid(int rssi)
{ return rssi>=-127 && rssi<=0; }
static inline uint32_t cerberus_wifi_relation_add(uint32_t a,uint32_t b)
{ return UINT32_MAX-a<b ? UINT32_MAX : a+b; }
static inline uint64_t cerberus_wifi_relation_latest_us(const cerberus_wifi_relation_row_t *r)
{ return r->latest_direction==CERBERUS_WIFI_RELATION_TO_AP ? r->to_ap_callback_us : r->from_ap_callback_us; }
static inline bool cerberus_wifi_relation_row_valid(const cerberus_wifi_relation_row_t *r)
{
    if (!r || !cerberus_wifi_relation_mac_valid(r->bssid) ||
        !cerberus_wifi_relation_mac_valid(r->station) || !memcmp(r->bssid,r->station,6U) ||
        !cerberus_wifi_relation_channel_valid(r->channel) || !r->directions ||
        (r->directions&~3U) || (r->rssi_valid&~r->directions) ||
        (r->latest_direction!=1U&&r->latest_direction!=2U) ||
        !(r->directions&r->latest_direction) || r->reserved[0] || r->reserved[1]) return false;
    for (unsigned bit=1U;bit<=2U;bit*=2U) {
        uint32_t frames=bit==1U?r->to_ap_frames:r->from_ap_frames;
        uint64_t time=bit==1U?r->to_ap_callback_us:r->from_ap_callback_us;
        int rssi=bit==1U?r->to_ap_rssi:r->from_ap_rssi;
        if (!!(r->directions&bit)!=!!frames || time>INT64_MAX) return false;
        if (!(r->directions&bit) && time) return false;
        if (r->rssi_valid&bit) { if (!cerberus_wifi_relation_rssi_valid(rssi)) return false; }
        else if (rssi!=CERBERUS_WIFI_RELATION_RSSI_UNAVAILABLE) return false;
        if (time>cerberus_wifi_relation_latest_us(r)) return false;
    }
    return true;
}
static inline bool cerberus_wifi_relation_status_valid(const cerberus_wifi_relation_status_t *s)
{
    if (!s || s->state>CERBERUS_WIFI_RELATION_ERROR || s->count>CERBERUS_WIFI_RELATION_LIMIT ||
        (s->flags&~3U) || s->reserved[0] || s->reserved[1] || s->matched_frames<s->count ||
        s->dropped_frames>s->matched_frames || (!s->count&&s->matched_frames) ||
        (s->dropped_frames&&s->count!=CERBERUS_WIFI_RELATION_LIMIT) ||
        (!!(s->flags&CERBERUS_WIFI_RELATION_OVERFLOW)!=!!s->dropped_frames)) return false;
    if (!(s->flags&CERBERUS_WIFI_RELATION_AVAILABLE))
        return !s->flags&&!s->count&&!s->matched_frames&&!s->dropped_frames;
    return s->epoch && (s->state==CERBERUS_WIFI_RELATION_RUNNING||s->state==CERBERUS_WIFI_RELATION_COMPLETE);
}

/* Input is one complete SDK data frame excluding FCS. Only DS1/DS2 clear
 * headers are decoded. QoS/null and HT-control minimum lengths are checked;
 * reserved subtype13, fragments, WDS/ad-hoc and unsupported framing reject.
 * Protected payloads, retry/PM/MoreData bits are permitted. Counts are callback
 * observations, including retries, never unique packets or connected clients. */
static inline bool cerberus_wifi_relation_decode(const uint8_t *p,size_t length,
    uint8_t channel,int rssi,uint64_t callback_us,cerberus_wifi_relation_row_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!p || length<24U || (p[0]&15U)!=8U || callback_us>INT64_MAX ||
        !cerberus_wifi_relation_channel_valid(channel)) return false;
    uint8_t ds=p[1]&3U,subtype=p[0]>>4U;
    if ((ds!=1U&&ds!=2U) || subtype==13U || (p[1]&4U) || (p[22]&15U)) return false;
    size_t minimum=24U;
    if (subtype&8U) minimum+=2U+((p[1]&0x80U)?4U:0U);
    else if (p[1]&0x80U) return false;
    if (length<minimum || !cerberus_wifi_relation_mac_valid(p+4U) ||
        !cerberus_wifi_relation_mac_valid(p+10U) || !memcmp(p+4U,p+10U,6U)) return false;
    cerberus_wifi_relation_row_t value={0};
    memcpy(value.bssid,p+(ds==1U?4U:10U),6U);
    memcpy(value.station,p+(ds==1U?10U:4U),6U);
    value.channel=channel;value.directions=value.latest_direction=ds;
    value.to_ap_rssi=value.from_ap_rssi=CERBERUS_WIFI_RELATION_RSSI_UNAVAILABLE;
    if (cerberus_wifi_relation_rssi_valid(rssi)) value.rssi_valid=ds;
    if (ds==1U) {
        value.to_ap_frames=1U;value.to_ap_callback_us=callback_us;
        if (value.rssi_valid) value.to_ap_rssi=(int8_t)rssi;
    } else {
        value.from_ap_frames=1U;value.from_ap_callback_us=callback_us;
        if (value.rssi_valid) value.from_ap_rssi=(int8_t)rssi;
    }
    *out=value;
    return true;
}

/* Caller serializes operations. First32 exact pair+channel keys are retained;
 * no eviction. Saturated sum(all direction counts)+drops equals matched_frames.
 * Completed available banks are immutable until next Start or cancellation.
 * Epoch exhaustion, RX failure and negative arm clock are unavailable, never
 * zero detections. Callback time is C5 uptime, not S3/UTC/RF hardware time. */
typedef struct {
    cerberus_wifi_relation_status_t status;
    cerberus_wifi_relation_row_t rows[CERBERUS_WIFI_RELATION_LIMIT];
    uint64_t epoch_counter, armed_us, latest_us;
    bool accepting;
} cerberus_wifi_relation_table_t;
static inline void cerberus_wifi_relation_begin(cerberus_wifi_relation_table_t *t,uint8_t generation)
{
    uint64_t counter=t->epoch_counter;
    memset(t,0,sizeof(*t));
    t->epoch_counter=counter==UINT64_MAX?counter:counter+1U;
    t->status.epoch=counter==UINT64_MAX?0U:t->epoch_counter;
    t->status.generation=generation;t->status.state=CERBERUS_WIFI_RELATION_RUNNING;
}
static inline void cerberus_wifi_relation_arm(cerberus_wifi_relation_table_t *t,bool available,uint64_t armed_us)
{
    t->accepting=available && armed_us<=INT64_MAX && t->status.epoch &&
        t->status.state==CERBERUS_WIFI_RELATION_RUNNING;
    if (t->accepting) {
        t->armed_us=t->latest_us=armed_us;
        t->status.flags|=CERBERUS_WIFI_RELATION_AVAILABLE;
    }
}
static inline void cerberus_wifi_relation_finish(cerberus_wifi_relation_table_t *t,uint8_t state)
{
    t->accepting=false;
    if (state!=CERBERUS_WIFI_RELATION_COMPLETE) {
        uint64_t epoch=t->status.epoch;uint8_t generation=t->status.generation;
        memset(&t->status,0,sizeof(t->status));memset(t->rows,0,sizeof(t->rows));
        t->armed_us=t->latest_us=0U;
        t->status.epoch=epoch;t->status.generation=generation;
    }
    t->status.state=state;
}
static inline bool cerberus_wifi_relation_commit(cerberus_wifi_relation_table_t *t,uint64_t epoch,
    const cerberus_wifi_relation_row_t *r,uint64_t callback_us)
{
    if (!t->accepting || !epoch || epoch!=t->status.epoch || callback_us<t->armed_us ||
        callback_us<t->latest_us || !cerberus_wifi_relation_row_valid(r) ||
        r->directions!=r->latest_direction || cerberus_wifi_relation_latest_us(r)!=callback_us ||
        (uint64_t)r->to_ap_frames+r->from_ap_frames!=1U) return false;
    unsigned i=0U;
    while (i<t->status.count && (memcmp(t->rows[i].bssid,r->bssid,6U) ||
        memcmp(t->rows[i].station,r->station,6U) || t->rows[i].channel!=r->channel)) ++i;
    t->latest_us=callback_us;
    t->status.matched_frames=cerberus_wifi_relation_add(t->status.matched_frames,1U);
    if (i==CERBERUS_WIFI_RELATION_LIMIT) {
        t->status.dropped_frames=cerberus_wifi_relation_add(t->status.dropped_frames,1U);
        t->status.flags|=CERBERUS_WIFI_RELATION_OVERFLOW;return false;
    }
    if (i==t->status.count) {
        t->rows[i]=*r;++t->status.count;return true;
    }
    cerberus_wifi_relation_row_t *row=&t->rows[i];
    const uint8_t direction=r->latest_direction;
    row->directions|=direction;row->latest_direction=direction;
    row->rssi_valid=(uint8_t)((row->rssi_valid&~direction)|r->rssi_valid);
    if (direction==CERBERUS_WIFI_RELATION_TO_AP) {
        row->to_ap_frames=cerberus_wifi_relation_add(row->to_ap_frames,1U);
        row->to_ap_callback_us=callback_us;row->to_ap_rssi=r->to_ap_rssi;
    } else {
        row->from_ap_frames=cerberus_wifi_relation_add(row->from_ap_frames,1U);
        row->from_ap_callback_us=callback_us;row->from_ap_rssi=r->from_ap_rssi;
    }
    return true;
}
static inline bool cerberus_wifi_relation_get_chunk(const cerberus_wifi_relation_table_t *t,
    const cerberus_wifi_relation_query_t *q,cerberus_wifi_relation_chunk_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!t || !q || !q->epoch || q->epoch!=t->status.epoch || q->generation!=t->status.generation ||
        t->status.state!=CERBERUS_WIFI_RELATION_COMPLETE || !(t->status.flags&CERBERUS_WIFI_RELATION_AVAILABLE) ||
        q->index>=t->status.count || q->chunk>=CERBERUS_WIFI_RELATION_CHUNKS) return false;
    size_t offset=q->chunk*CERBERUS_WIFI_RELATION_CHUNK_BYTES,n=sizeof(t->rows[0])-offset;
    if (n>CERBERUS_WIFI_RELATION_CHUNK_BYTES) n=CERBERUS_WIFI_RELATION_CHUNK_BYTES;
    out->epoch=q->epoch;out->generation=q->generation;out->index=q->index;out->chunk=q->chunk;
    out->length=(uint8_t)n;memcpy(out->data,(const uint8_t *)&t->rows[q->index]+offset,n);
    return true;
}
static inline bool cerberus_wifi_relation_get_clock(const cerberus_wifi_relation_table_t *t,
    const cerberus_wifi_relation_clock_query_t *q,uint64_t now_us,cerberus_wifi_relation_clock_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!t || !q || !q->epoch || q->epoch!=t->status.epoch || q->generation!=t->status.generation ||
        t->status.state!=CERBERUS_WIFI_RELATION_COMPLETE || !(t->status.flags&CERBERUS_WIFI_RELATION_AVAILABLE) ||
        now_us>INT64_MAX || now_us<t->latest_us) return false;
    out->epoch=q->epoch;out->source_now_us=now_us;out->generation=q->generation;out->count=t->status.count;
    return true;
}
