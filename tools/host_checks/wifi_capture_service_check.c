#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_capture_service.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int64_t fake_time_us=1000000;
int64_t esp_timer_get_time(void){return fake_time_us;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 81U*1024U;}
void *heap_caps_malloc(size_t size,unsigned caps){(void)caps;return malloc(size);}

int main(void)
{
    cerberus_wifi_capture_status_payload_t status;
    cerberus_wifi_capture_service_status(&status);
    assert(status.state==CERBERUS_WIFI_CAPTURE_IDLE&&status.pending==0);
    cerberus_wifi_capture_start_request_t request={
        .generation=10,.channel=6,.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_PROBE,
        .flags=CERBERUS_WIFI_CAPTURE_FLAG_BSSID,
        .bssid={2,3,4,5,6,7},.lease_ms=5000};
    request.channel=0;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.channel=6;request.bssid[0]=3;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.bssid[0]=2;request.lease_ms=999;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.lease_ms=5000;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_OK);
    assert(cerberus_wifi_capture_service_same_request(&request));
    cerberus_wifi_capture_service_set_deadline(6000);
    uint8_t frame[40]={0x50};memcpy(frame+16,request.bssid,6);
    cerberus_wifi_capture_service_receive(frame,sizeof(frame),6,-42,123456);
    cerberus_wifi_capture_service_receive(frame,sizeof(frame),6,-41,123457);
    cerberus_wifi_capture_service_receive(frame,sizeof(frame),6,-40,123458);
    cerberus_wifi_capture_service_receive(frame,sizeof(frame),11,-40,123459);
    cerberus_wifi_capture_service_status(&status);
    assert(status.state==CERBERUS_WIFI_CAPTURE_RUNNING&&status.generation==10&&
        status.pending==2&&status.accepted==2&&status.dropped_full==1);
    cerberus_wifi_capture_peek_payload_t peek;
    assert(cerberus_wifi_capture_service_peek(9,&peek)==ESP_ERR_INVALID_STATE);
    fake_time_us=2000000;
    assert(cerberus_wifi_capture_service_peek(10,&peek)==ESP_OK);
    assert(peek.sequence==1&&peek.length==40&&peek.channel==6&&peek.rssi_dbm==-42&&
        peek.crc32==cerberus_protocol_crc32(frame,sizeof(frame))&&
        cerberus_wifi_capture_service_deadline_ms()==7000);
    cerberus_wifi_capture_read_request_t query={.generation=10,.sequence=1,.offset=0};
    cerberus_wifi_capture_read_payload_t chunk;
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_OK);
    assert(chunk.offset==0&&!memcmp(chunk.data,frame,20));
    query.offset=20;
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_OK);
    assert(chunk.offset==20&&!memcmp(chunk.data,frame+20,20));
    query.offset=21;
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_ERR_INVALID_ARG);
    query.offset=40;
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_ERR_INVALID_STATE);
    cerberus_wifi_capture_service_stop(ESP_OK);
    cerberus_wifi_capture_service_receive(frame,sizeof(frame),6,-40,123460);
    cerberus_wifi_capture_service_status(&status);
    assert(status.state==CERBERUS_WIFI_CAPTURE_STOPPED&&status.pending==2);
    cerberus_wifi_capture_ack_request_t ack={.generation=10,.sequence=1};
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(10,&peek)==ESP_OK&&peek.sequence==2);
    ack.sequence=2;assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(10,&peek)==ESP_OK&&peek.sequence==0);
    cerberus_wifi_capture_service_status(&status);
    assert(status.state==CERBERUS_WIFI_CAPTURE_STOPPED&&status.pending==0);
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_STATE);
    request.generation=11;request.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_RAW;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_OK);
    cerberus_wifi_capture_service_receive(frame,4097,6,-40,123461);
    cerberus_wifi_capture_service_status(&status);
    assert(status.dropped_other==1&&status.accepted==0);
    cerberus_wifi_capture_service_stop(ESP_ERR_TIMEOUT);
    cerberus_wifi_capture_service_status(&status);
    assert(status.state==CERBERUS_WIFI_CAPTURE_STOPPED&&status.error_code==ESP_ERR_TIMEOUT);
    request.generation=12;request.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT;
    request.channel=36;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_OK);
    uint8_t sae[128]={0xb0};memcpy(sae+16,request.bssid,6);
    sae[24]=3U;sae[26]=1U;sae[30]=19U;
    for(size_t i=32U;i<sizeof(sae);++i)sae[i]=(uint8_t)i;
    cerberus_wifi_capture_service_receive(sae,sizeof(sae),6,-50,222000);
    cerberus_wifi_capture_service_receive(sae,31,36,-50,222001);
    sae[25]=1U;
    cerberus_wifi_capture_service_receive(sae,sizeof(sae),36,-50,222002);
    sae[25]=0U;sae[26]=2U;
    cerberus_wifi_capture_service_receive(sae,sizeof(sae),36,-50,222003);
    sae[26]=1U;sae[16]^=2U;
    cerberus_wifi_capture_service_receive(sae,sizeof(sae),36,-50,222004);
    sae[16]^=2U;
    cerberus_wifi_capture_service_receive(sae,sizeof(sae),36,-51,222005);
    cerberus_wifi_capture_service_status(&status);
    assert(status.mode_mask==CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT&&
        status.channel==36&&status.accepted==1&&status.pending==1&&
        status.dropped_full==0&&status.dropped_other==0);
    assert(cerberus_wifi_capture_service_peek(12,&peek)==ESP_OK&&
        peek.sequence!=0&&peek.length==sizeof(sae)&&peek.channel==36&&
        peek.rssi_dbm==-51&&peek.crc32==cerberus_protocol_crc32(sae,sizeof(sae)));
    query.generation=12;query.sequence=peek.sequence;query.offset=20;
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_OK);
    assert(!memcmp(chunk.data,sae+20,20));
    cerberus_wifi_capture_service_stop(ESP_OK);
    ack.generation=12;ack.sequence=peek.sequence;
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(12,&peek)==ESP_OK&&peek.sequence==0);
    /* Generic hopping has no AP prerequisite and starts with RX admission
     * paused. Changing the admitted channel never restarts the ring. */
    request=(cerberus_wifi_capture_start_request_t){.generation=13,
        .mode_mask=CERBERUS_WIFI_CAPTURE_MODE_BEACON,
        .flags=CERBERUS_WIFI_CAPTURE_FLAG_HOP,.lease_ms=5000};
    request.channel=6;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.channel=0;request.flags|=CERBERUS_WIFI_CAPTURE_FLAG_BSSID;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.flags=CERBERUS_WIFI_CAPTURE_FLAG_HOP;
    request.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_RAW;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_ERR_INVALID_ARG);
    request.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_BEACON;
    assert(cerberus_wifi_capture_service_start(&request)==ESP_OK);
    uint8_t beacon[48]={0x80};beacon[36]=0;beacon[37]=3;
    memcpy(beacon+38,"one",3);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),6,-40,333000);
    cerberus_wifi_capture_service_status(&status);
    assert(status.channel==0&&status.accepted==0&&cerberus_wifi_capture_service_active());
    cerberus_wifi_capture_service_set_channel(6);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),6,-41,333001);
    assert(cerberus_wifi_capture_service_peek(13,&peek)==ESP_OK&&peek.channel==6);
    uint32_t first_sequence=peek.sequence;
    uint32_t first_crc=peek.crc32;
    cerberus_wifi_capture_service_set_channel(0);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),6,-30,333002);
    cerberus_wifi_capture_service_set_channel(36);
    beacon[38]='t';
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),6,-30,333003);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),36,-50,333004);
    cerberus_wifi_capture_service_status(&status);
    assert(status.channel==36&&status.generation==13&&status.accepted==2&&status.pending==2);
    assert(cerberus_wifi_capture_service_same_request(&request));
    assert(cerberus_wifi_capture_service_peek(13,&peek)==ESP_OK&&
        peek.channel==6&&peek.sequence==first_sequence&&peek.crc32==first_crc);
    query=(cerberus_wifi_capture_read_request_t){.generation=13,.sequence=first_sequence,.offset=20};
    assert(cerberus_wifi_capture_service_read(&query,&chunk)==ESP_OK&&chunk.data[18]=='o');
    ack=(cerberus_wifi_capture_ack_request_t){.generation=13,.sequence=first_sequence};
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(13,&peek)==ESP_OK&&
        peek.channel==36&&peek.sequence==first_sequence+1&&peek.rssi_dbm==-50&&
        peek.crc32==cerberus_protocol_crc32(beacon,sizeof(beacon)));
    uint64_t time=0;for(unsigned i=0;i<6;++i)time|=(uint64_t)peek.monotonic_us_le48[i]<<(i*8);
    assert(time==333004);
    cerberus_wifi_capture_service_stop(ESP_OK);
    ack.sequence=peek.sequence;
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(13,&peek)==ESP_OK&&peek.sequence==0);
    puts("Wi-Fi capture service passed: validation, bounded callback, status, lease renewal, read, CRC, ACK retry and stop/drain.");
    return 0;
}
