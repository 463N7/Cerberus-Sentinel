#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_capture_ring.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(sizeof(cerberus_wifi_capture_ring_t)<9000U);
    cerberus_wifi_capture_ring_t ring;
    cerberus_wifi_capture_ring_init(&ring);
    cerberus_wifi_pcap_metadata_t meta={.channel=6,.rssi_dbm=-42,.rssi_known=true,
        .time_kind=CERBERUS_WIFI_PCAP_TIME_MONOTONIC,.capture_time_us=1234567};
    uint8_t frames[3][6]={{0x40,0,1,2,3,4},{0xc0,0,5,6,7,8},{0x08,0,9,10,11,12}};
    assert(!cerberus_wifi_capture_ring_start(&ring,0));
    assert(cerberus_wifi_capture_ring_start(&ring,7));
    assert(!cerberus_wifi_capture_ring_start(&ring,8));
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[0],6,6)==CERBERUS_WIFI_CAPTURE_ACCEPTED);
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[1],6,6)==CERBERUS_WIFI_CAPTURE_ACCEPTED);
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[2],6,6)==CERBERUS_WIFI_CAPTURE_DROP_FULL);
    cerberus_wifi_capture_ring_stats_t stats;
    cerberus_wifi_capture_ring_stats(&ring,&stats);
    assert(stats.active&&stats.generation==7&&stats.pending==2&&stats.high_watermark==2&&
        stats.accepted==2&&stats.dropped_full==1&&!stats.producer_inflight);
    const cerberus_wifi_capture_record_t *first=cerberus_wifi_capture_ring_peek(&ring);
    assert(first&&first->generation==7&&first->sequence==1&&first->captured_length==6&&
        first->original_length==6&&!memcmp(first->frame,frames[0],6));
    assert(!cerberus_wifi_capture_ring_ack(&ring,8,1));
    assert(!cerberus_wifi_capture_ring_ack(&ring,7,2));
    assert(cerberus_wifi_capture_ring_peek(&ring)==first);
    assert(cerberus_wifi_capture_ring_ack(&ring,7,1));
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[2],6,6)==CERBERUS_WIFI_CAPTURE_ACCEPTED);
    const cerberus_wifi_capture_record_t *second=cerberus_wifi_capture_ring_peek(&ring);
    assert(second&&second->sequence==2&&!memcmp(second->frame,frames[1],6));
    cerberus_wifi_capture_ring_stop(&ring);
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[0],6,6)==CERBERUS_WIFI_CAPTURE_DROP_STOPPED);
    assert(!cerberus_wifi_capture_ring_start(&ring,8)); /* pending records must drain */
    assert(cerberus_wifi_capture_ring_ack(&ring,7,2));
    const cerberus_wifi_capture_record_t *third=cerberus_wifi_capture_ring_peek(&ring);
    assert(third&&third->sequence==3&&!memcmp(third->frame,frames[2],6));
    assert(cerberus_wifi_capture_ring_ack(&ring,7,3));
    assert(!cerberus_wifi_capture_ring_peek(&ring));
    assert(!cerberus_wifi_capture_ring_start(&ring,7)); /* generation reuse rejected */
    assert(cerberus_wifi_capture_ring_start(&ring,8));
    assert(cerberus_wifi_capture_ring_push(&ring,7,&meta,frames[0],6,6)==CERBERUS_WIFI_CAPTURE_DROP_STALE);
    assert(cerberus_wifi_capture_ring_push(&ring,8,&meta,frames[0],4097,4097)==CERBERUS_WIFI_CAPTURE_DROP_OVERSIZE);
    assert(cerberus_wifi_capture_ring_push(&ring,8,&meta,frames[0],6,7)==CERBERUS_WIFI_CAPTURE_DROP_TRUNCATED);
    meta.channel=0;
    assert(cerberus_wifi_capture_ring_push(&ring,8,&meta,frames[0],6,6)==CERBERUS_WIFI_CAPTURE_DROP_INVALID);
    meta.channel=6;meta.frame_has_fcs=true;
    assert(cerberus_wifi_capture_ring_push(&ring,8,&meta,frames[0],2,2)==CERBERUS_WIFI_CAPTURE_DROP_INVALID);
    meta.frame_has_fcs=false;
    assert(cerberus_wifi_capture_ring_push(&ring,8,&meta,frames[0],6,6)==CERBERUS_WIFI_CAPTURE_ACCEPTED);
    first=cerberus_wifi_capture_ring_peek(&ring);
    assert(first&&first->sequence==4&&first->generation==8);
    uint8_t pcap[CERBERUS_WIFI_PCAP_RECORD_MAX_SIZE];
    assert(cerberus_wifi_pcapng_record(&first->metadata,first->frame,
        first->captured_length,first->original_length,pcap,sizeof(pcap))==40);
    assert(cerberus_wifi_capture_ring_ack(&ring,8,4));
    cerberus_wifi_capture_ring_stop(&ring);
    assert(!cerberus_wifi_capture_ring_start(&ring,7)); /* old generation cannot return */
    cerberus_wifi_capture_ring_stats(&ring,&stats);
    assert(stats.generation==8&&stats.pending==0&&stats.accepted==1&&stats.dropped_stale==1&&
        stats.dropped_oversize==1&&stats.dropped_truncated==1&&stats.dropped_invalid==2&&
        stats.dropped_full==0&&stats.dropped_stopped==0&&stats.high_watermark==1);
    puts("Wi-Fi capture ring passed: 2-slot bound, producer/consumer order, generation, stop/drain, exact ACK, PCAP serialization and per-cause drops.");
    return 0;
}
