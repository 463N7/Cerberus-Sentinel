#pragma once

/* Receive-only probe clues corresponding to Marauder develop 79cc6bcba1db6679
 * WiFiScan.cpp's ordinary Wardrive branch and WiFiScan.h's name/OUI lists.
 * These public strings/vendor prefixes are unauthenticated clues, not device
 * identity. No stimulus, association inference or SD resource ownership. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define CERBERUS_WIFI_PROBE_MIN_FIRMWARE 57U
#define CERBERUS_WIFI_PROBE_LIMIT 16U
#define CERBERUS_WIFI_PROBE_CHUNK_BYTES 10U
#define CERBERUS_WIFI_PROBE_CHUNKS 6U
enum { CERBERUS_WIFI_PROBE_NAME_FLOCK=1U, CERBERUS_WIFI_PROBE_NAME_PENGUIN=2U,
    CERBERUS_WIFI_PROBE_NAME_PIGVISION=4U, CERBERUS_WIFI_PROBE_NAME_BATTERY=8U };
enum { CERBERUS_WIFI_PROBE_AVAILABLE=1U, CERBERUS_WIFI_PROBE_OVERFLOW=2U };
enum { CERBERUS_WIFI_PROBE_IDLE=0U, CERBERUS_WIFI_PROBE_RUNNING=1U,
    CERBERUS_WIFI_PROBE_COMPLETE=2U, CERBERUS_WIFI_PROBE_ERROR=3U };
typedef struct __attribute__((packed)) {
    uint8_t transmitter[6], ssid[32], ssid_length, channel;
    int8_t rssi;
    uint8_t name_bits, oui_hint;
    uint64_t callback_ms; /* C5 callback uptime, not S3 time, UTC or RF time. */
    uint32_t frames;
} cerberus_wifi_probe_row_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, count, state, flags;
    uint32_t matched_frames, dropped_frames;
    uint8_t reserved[2];
} cerberus_wifi_probe_status_t;

/* A row key is transmitter + exact SSID bytes + actual RX channel. Completed
 * AVAILABLE banks are immutable. matched_frames is the saturated sum of all
 * row frame counts plus capacity drops; OVERFLOW is equivalent to drops > 0.
 * An unavailable bank has zero flags/count/counters, and may have epoch zero
 * at initialization or after epoch exhaustion. It is not a zero-match result. */
typedef struct __attribute__((packed)) { uint8_t generation; } cerberus_wifi_probe_status_query_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, index, chunk;
} cerberus_wifi_probe_query_t;
typedef struct __attribute__((packed)) {
    uint64_t epoch;
    uint8_t generation, index, chunk, length, data[CERBERUS_WIFI_PROBE_CHUNK_BYTES];
} cerberus_wifi_probe_chunk_t;
#ifdef __cplusplus
static_assert(sizeof(cerberus_wifi_probe_row_t)==55U, "probe row ABI");
static_assert(sizeof(cerberus_wifi_probe_status_t)==22U, "probe status ABI");
static_assert(sizeof(cerberus_wifi_probe_query_t)==11U, "probe query ABI");
static_assert(sizeof(cerberus_wifi_probe_chunk_t)==22U, "probe chunk ABI");
#else
_Static_assert(sizeof(cerberus_wifi_probe_row_t)==55U, "probe row ABI");
_Static_assert(sizeof(cerberus_wifi_probe_status_t)==22U, "probe status ABI");
_Static_assert(sizeof(cerberus_wifi_probe_query_t)==11U, "probe query ABI");
_Static_assert(sizeof(cerberus_wifi_probe_chunk_t)==22U, "probe chunk ABI");
#endif

static inline bool cerberus_wifi_probe_mac_valid(const uint8_t *mac)
{
    static const uint8_t zero[6]={0};
    return mac && !(mac[0]&1U) && memcmp(mac,zero,6U)!=0;
}
static inline bool cerberus_wifi_probe_channel_valid(uint8_t ch)
{
    return (ch>=1U&&ch<=14U) || (ch>=36U&&ch<=64U&&ch%4U==0U) ||
        (ch>=100U&&ch<=144U&&ch%4U==0U) || (ch>=149U&&ch<=177U&&(ch-149U)%4U==0U);
}
static inline uint8_t cerberus_wifi_probe_name_bits(const uint8_t *ssid, size_t length)
{
    if ((!ssid&&length)||length>32U) return 0U;
    static const char *const names[]={"flock","penguin","pigvision","fs ext battery"};
    uint8_t bits=0U;
    for (unsigned n=0U;n<4U;++n) {
        size_t size=strlen(names[n]);
        for (size_t start=0U;size<=length && start<=length-size;++start) {
            size_t i=0U;
            for (;i<size;++i) {
                uint8_t c=ssid[start+i];
                if (c>='A'&&c<='Z') c=(uint8_t)(c+('a'-'A'));
                if (c!=(uint8_t)names[n][i]) break;
            }
            if (i==size) { bits|=(uint8_t)(1U<<n); break; }
        }
    }
    return bits;
}
static inline bool cerberus_wifi_probe_oui_hint(const uint8_t *mac)
{
    if (!cerberus_wifi_probe_mac_valid(mac)||(mac[0]&2U)) return false;
    /* Exact 27-prefix compatibility list, in registry/display byte order. */
    static const uint8_t prefixes[27][3]={
        {0x58,0x8e,0x81},{0xcc,0xcc,0xcc},{0xec,0x1b,0xbd},{0x90,0x35,0xea},
        {0x04,0x0d,0x84},{0xf0,0x82,0xc0},{0x1c,0x34,0xf1},{0x38,0x5b,0x44},
        {0x94,0x34,0x69},{0xb4,0xe3,0xf9},{0x70,0xc9,0x4e},{0x3c,0x91,0x80},
        {0xd8,0xf3,0xbc},{0x80,0x30,0x49},{0x14,0x5a,0xfc},{0x74,0x4c,0xa1},
        {0x08,0x3a,0x88},{0x9c,0x2f,0x9d},{0x94,0x08,0x53},{0xe4,0xaa,0xea},
        {0xf4,0x6a,0xdd},{0xf8,0xa2,0xd6},{0xe0,0x0a,0xf6},{0x00,0xf4,0x8d},
        {0xd0,0x39,0x57},{0xe8,0xd0,0xfc},{0xb4,0x1e,0x52}};
    for (size_t i=0U;i<27U;++i) if (!memcmp(mac,prefixes[i],3U)) return true;
    return false;
}
static inline bool cerberus_wifi_probe_row_valid(const cerberus_wifi_probe_row_t *r)
{
    if (!r||!cerberus_wifi_probe_mac_valid(r->transmitter)||r->ssid_length>32U||
        !cerberus_wifi_probe_channel_valid(r->channel)||r->rssi < -127||r->rssi>0||!r->frames||
        r->callback_ms>UINT64_MAX/1000U||
        r->name_bits!=cerberus_wifi_probe_name_bits(r->ssid,r->ssid_length)||
        r->oui_hint!=(uint8_t)cerberus_wifi_probe_oui_hint(r->transmitter)||
        !(r->name_bits||r->oui_hint)) return false;
    for (unsigned i=r->ssid_length;i<32U;++i) if (r->ssid[i]) return false;
    return true;
}
static inline bool cerberus_wifi_probe_status_valid(const cerberus_wifi_probe_status_t *s)
{
    if (!s||s->state>CERBERUS_WIFI_PROBE_ERROR||s->count>CERBERUS_WIFI_PROBE_LIMIT||
        (s->flags&~3U)||s->reserved[0]||s->reserved[1]||
        s->matched_frames<s->count||s->dropped_frames>s->matched_frames||
        (!s->count&&s->matched_frames)||
        (s->dropped_frames&&s->count!=CERBERUS_WIFI_PROBE_LIMIT)||
        (!!(s->flags&CERBERUS_WIFI_PROBE_OVERFLOW)!=!!s->dropped_frames)) return false;
    if (!(s->flags&CERBERUS_WIFI_PROBE_AVAILABLE))
        return !s->count&&!s->matched_frames&&!s->dropped_frames&&!s->flags;
    return s->epoch && (s->state==CERBERUS_WIFI_PROBE_RUNNING||s->state==CERBERUS_WIFI_PROBE_COMPLETE);
}

/* Input is one complete management frame excluding FCS. A single SSID IE is
 * mandatory, all later IEs must fit, and duplicate SSIDs are ambiguous. Binary
 * SSIDs are compared over their full explicit length, including embedded NUL.
 * Rows begin with frames=1; malformed and nonmatching output is always zero.
 * Retry/Power Management/More Data bits are permitted. DS, fragmentation,
 * protection and extra HT-control framing are rejected. */
static inline bool cerberus_wifi_probe_decode(const uint8_t *p,size_t length,
    uint8_t channel,int rssi,uint64_t callback_us,cerberus_wifi_probe_row_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!p||length<26U||p[0]!=0x40U||(p[1]&0xc7U)||
        (p[22]&15U)||!cerberus_wifi_probe_mac_valid(p+10U)||
        !cerberus_wifi_probe_channel_valid(channel)||rssi < -127||rssi>0) return false;
    const uint8_t *ssid=NULL;uint8_t size=0U;
    for (size_t offset=24U;offset<length;) {
        if (length-offset<2U) return false;
        uint8_t id=p[offset],n=p[offset+1U];offset+=2U;
        if (n>length-offset) return false;
        if (!id) { if (ssid||n>32U) return false;ssid=p+offset;size=n; }
        offset+=n;
    }
    if (!ssid) return false;
    uint8_t bits=cerberus_wifi_probe_name_bits(ssid,size);
    bool oui=cerberus_wifi_probe_oui_hint(p+10U);
    if (!bits&&!oui) return false;
    memcpy(out->transmitter,p+10U,6U);memcpy(out->ssid,ssid,size);
    out->ssid_length=size;out->channel=channel;out->rssi=(int8_t)rssi;
    out->name_bits=bits;out->oui_hint=(uint8_t)oui;
    out->callback_ms=callback_us/1000U;out->frames=1U;
    return true;
}

typedef struct {
    cerberus_wifi_probe_status_t status;
    cerberus_wifi_probe_row_t rows[CERBERUS_WIFI_PROBE_LIMIT];
    uint64_t callback_us[CERBERUS_WIFI_PROBE_LIMIT], epoch_counter, armed_us;
    bool accepting;
} cerberus_wifi_probe_table_t;
/* Caller serializes all table operations. Epoch never wraps. Exhaustion is an
 * explicit unavailable bank, not permission to reuse a previous identity. */
static inline void cerberus_wifi_probe_begin(cerberus_wifi_probe_table_t *t,uint8_t generation)
{
    uint64_t counter=t->epoch_counter;
    memset(t,0,sizeof(*t));
    t->epoch_counter=counter==UINT64_MAX?counter:counter+1U;
    t->status.epoch=counter==UINT64_MAX?0U:t->epoch_counter;
    t->status.generation=generation;t->status.state=CERBERUS_WIFI_PROBE_RUNNING;
}
static inline void cerberus_wifi_probe_arm(cerberus_wifi_probe_table_t *t,bool available,uint64_t armed_us)
{
    t->accepting=available&&t->status.epoch&&t->status.state==CERBERUS_WIFI_PROBE_RUNNING;
    if (t->accepting) { t->armed_us=armed_us;t->status.flags|=CERBERUS_WIFI_PROBE_AVAILABLE; }
}
static inline void cerberus_wifi_probe_finish(cerberus_wifi_probe_table_t *t,uint8_t state)
{
    t->accepting=false;
    if (state!=CERBERUS_WIFI_PROBE_COMPLETE) {
        uint64_t epoch=t->status.epoch;uint8_t generation=t->status.generation;
        memset(&t->status,0,sizeof(t->status));
        memset(t->rows,0,sizeof(t->rows));memset(t->callback_us,0,sizeof(t->callback_us));
        t->status.epoch=epoch;t->status.generation=generation;
    }
    t->status.state=state;
}
static inline bool cerberus_wifi_probe_commit(cerberus_wifi_probe_table_t *t,uint64_t epoch,
    const cerberus_wifi_probe_row_t *row,uint64_t callback_us)
{
    if (!t->accepting||!epoch||epoch!=t->status.epoch||callback_us<t->armed_us||
        !cerberus_wifi_probe_row_valid(row)||
        row->callback_ms!=callback_us/1000U) return false;
    unsigned i=0U;
    while (i<t->status.count && (memcmp(t->rows[i].transmitter,row->transmitter,6U)||
        t->rows[i].channel!=row->channel||t->rows[i].ssid_length!=row->ssid_length||
        memcmp(t->rows[i].ssid,row->ssid,row->ssid_length))) ++i;
    if (i<t->status.count && callback_us<t->callback_us[i]) return false;
    if (t->status.matched_frames!=UINT32_MAX) ++t->status.matched_frames;
    if (i==CERBERUS_WIFI_PROBE_LIMIT) {
        if (t->status.dropped_frames!=UINT32_MAX) ++t->status.dropped_frames;
        t->status.flags|=CERBERUS_WIFI_PROBE_OVERFLOW;return false;
    }
    uint32_t frames=i<t->status.count?t->rows[i].frames:0U;
    if (i==t->status.count) ++t->status.count;
    t->rows[i]=*row;t->rows[i].frames=frames==UINT32_MAX?frames:frames+1U;
    t->callback_us[i]=callback_us;return true;
}
static inline bool cerberus_wifi_probe_get_chunk(const cerberus_wifi_probe_table_t *t,
    const cerberus_wifi_probe_query_t *q,cerberus_wifi_probe_chunk_t *out)
{
    if (!out) return false;
    memset(out,0,sizeof(*out));
    if (!t||!q||!q->epoch||q->epoch!=t->status.epoch||q->generation!=t->status.generation||
        t->status.state!=CERBERUS_WIFI_PROBE_COMPLETE||!(t->status.flags&CERBERUS_WIFI_PROBE_AVAILABLE)||
        q->index>=t->status.count||q->chunk>=CERBERUS_WIFI_PROBE_CHUNKS) return false;
    size_t offset=q->chunk*CERBERUS_WIFI_PROBE_CHUNK_BYTES,n=sizeof(t->rows[0])-offset;
    if (n>CERBERUS_WIFI_PROBE_CHUNK_BYTES) n=CERBERUS_WIFI_PROBE_CHUNK_BYTES;
    out->epoch=q->epoch;out->generation=q->generation;out->index=q->index;out->chunk=q->chunk;
    out->length=(uint8_t)n;memcpy(out->data,(const uint8_t *)&t->rows[q->index]+offset,n);
    return true;
}
