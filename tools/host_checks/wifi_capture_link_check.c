#ifdef NDEBUG
#undef NDEBUG
#endif
#include "c5_spi_link.h"
#include "wifi_packet_monitor_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
static int link_mutex=1;
static bool held,fail_lock;
static unsigned takes,releases,exchanges,status_calls;
static uint32_t firmware;
static esp_err_t status_error,exchange_error;
static cerberus_spi_command_t expected_command;
static cerberus_wifi_capture_start_request_t expected_start;
static cerberus_wifi_capture_status_payload_t response_status;
static cerberus_wifi_capture_peek_payload_t response_peek;
static uint8_t frame[47];
static bool bad_offset,bad_data;
static int take_link_mutex(unsigned ticks)
{assert(link_mutex&&ticks==250&&!held);++takes;if(fail_lock)return 0;held=true;return 1;}
static void give_link_mutex(void){assert(held);held=false;++releases;}
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *out)
{++status_calls;memset(out,0,sizeof(*out));out->firmware_version=firmware;return status_error;}
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command,const void *request,
    size_t request_size,void *out,size_t size)
{
    assert(held&&command==expected_command);++exchanges;
    if(exchange_error!=ESP_OK)return exchange_error;
    if(command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START){
        assert(request_size==sizeof(expected_start)&&!memcmp(request,&expected_start,request_size));
        assert(size==sizeof(response_status));memcpy(out,&response_status,size);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STATUS){
        assert(!request&&request_size==0&&size==sizeof(response_status));memcpy(out,&response_status,size);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP){
        assert(request_size==sizeof(cerberus_wifi_capture_stop_request_t));
        assert(((const cerberus_wifi_capture_stop_request_t *)request)->generation==100);
        assert(size==sizeof(response_status));memcpy(out,&response_status,size);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK){
        assert(request_size==sizeof(cerberus_wifi_capture_stop_request_t));
        assert(((const cerberus_wifi_capture_stop_request_t *)request)->generation==100);
        assert(size==sizeof(response_peek));memcpy(out,&response_peek,size);
    }else if(command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ){
        const cerberus_wifi_capture_read_request_t *read=request;
        assert(request_size==sizeof(*read)&&read->generation==100&&read->sequence==200);
        assert(size==sizeof(cerberus_wifi_capture_read_payload_t)&&read->offset<sizeof(frame));
        cerberus_wifi_capture_read_payload_t *chunk=out;memset(chunk,0,sizeof(*chunk));
        chunk->offset=read->offset+(bad_offset?1:0);
        size_t bytes=sizeof(frame)-read->offset;if(bytes>20)bytes=20;
        memcpy(chunk->data,frame+read->offset,bytes);if(bad_data)chunk->data[0]^=1;
    }else assert(false);
    return ESP_OK;
}
#include "wifi_capture_link_impl.h"

static bool zero(const void *data,size_t n)
{const unsigned char *p=data;for(size_t i=0;i<n;++i)if(p[i])return false;return true;}
static void reset(uint32_t version,uint8_t mode,bool hopping)
{
    assert(!held);takes=releases=exchanges=status_calls=0;
    link_mutex=1;fail_lock=bad_offset=bad_data=false;status_error=exchange_error=ESP_OK;
    firmware=version;expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START;
    expected_start=(cerberus_wifi_capture_start_request_t){.generation=100,
        .channel=hopping?0:36,.mode_mask=mode,.flags=hopping?CERBERUS_WIFI_CAPTURE_FLAG_HOP:0,.lease_ms=5000};
    response_status=(cerberus_wifi_capture_status_payload_t){.generation=100,
        .state=CERBERUS_WIFI_CAPTURE_RUNNING,.channel=hopping?1:36,.mode_mask=mode};
}
static void gates(void)
{
    cerberus_wifi_capture_status_payload_t out;
    const uint32_t versions[]={44,45,47,48,49,50};
    const uint8_t modes[]={CERBERUS_WIFI_CAPTURE_MODE_RAW,CERBERUS_WIFI_CAPTURE_MODE_SAE_COMMIT,
        CERBERUS_WIFI_CAPTURE_MODE_BEACON};
    for(size_t m=0;m<sizeof(modes);++m)for(size_t v=0;v<sizeof(versions)/sizeof(*versions);++v){
        reset(versions[v],modes[m],false);memset(&out,0xa5,sizeof(out));
        unsigned required=m==0?45:m==1?48:50;
        esp_err_t result=c5_spi_link_wifi_capture_start(&expected_start,&out);
        assert(result==(versions[v]>=required?ESP_OK:ESP_ERR_NOT_SUPPORTED));
        assert(status_calls==1&&!held);
        if(versions[v]<required)assert(exchanges==0&&takes==0&&zero(&out,sizeof(out)));
        else assert(exchanges==1&&takes==1&&releases==1&&out.channel==36);
    }
    reset(49,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_NOT_SUPPORTED&&exchanges==0);
    reset(49,CERBERUS_WIFI_CAPTURE_MODE_RAW,true); /* Invalid hopping scope. */
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_ARG&&exchanges==0&&status_calls==0);
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);expected_start.channel=6;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_ARG&&exchanges==0&&status_calls==0);
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);expected_start.flags|=CERBERUS_WIFI_CAPTURE_FLAG_BSSID;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_ARG&&exchanges==0&&status_calls==0);
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_OK&&out.channel==1);
    response_status.channel=0; /* A retune status is valid for a hopping request. */
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_OK&&out.channel==0);
    const uint8_t hop_channels[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,
        36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165};
    for(unsigned channel=0;channel<=UINT8_MAX;++channel){
        bool valid=false;
        for(size_t i=0;i<sizeof(hop_channels);++i)if(channel==hop_channels[i])valid=true;
        response_status.channel=(uint8_t)channel;
        assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==(valid?ESP_OK:ESP_ERR_INVALID_RESPONSE));
    }
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,false);response_status.channel=40;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_RESPONSE);
    response_status.channel=36;response_status.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_RAW;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_RESPONSE);
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);status_error=ESP_ERR_TIMEOUT;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_TIMEOUT&&exchanges==0);
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);fail_lock=true;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_TIMEOUT&&exchanges==0&&releases==0);
}
static void status_validation(void)
{
    cerberus_wifi_capture_status_payload_t out;
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);
    response_status.generation=101;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_INVALID_RESPONSE);
    response_status.generation=100;response_status.state=CERBERUS_WIFI_CAPTURE_ERROR;
    response_status.error_code=ESP_ERR_NO_MEM;
    assert(c5_spi_link_wifi_capture_start(&expected_start,&out)==ESP_ERR_NO_MEM);
    response_status.state=CERBERUS_WIFI_CAPTURE_RUNNING;response_status.error_code=ESP_OK;
    expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STATUS;
    for(unsigned kind=0;kind<7;++kind){
        cerberus_wifi_capture_status_payload_t saved=response_status;
        if(kind==0)response_status.state=4;
        if(kind==1)response_status.pending=3;
        if(kind==2)response_status.mode_mask=0x80;
        if(kind==3)response_status.mode_mask=0;
        if(kind==4)response_status.channel=35;
        if(kind==5){response_status.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_RAW;response_status.channel=178;}
        if(kind==6){response_status.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_RAW;response_status.channel=0;}
        memset(&out,0xa5,sizeof(out));
        assert(c5_spi_link_wifi_capture_status(&out)==ESP_ERR_INVALID_RESPONSE&&zero(&out,sizeof(out)));
        response_status=saved;
    }
    cerberus_wifi_capture_status_payload_t saved=response_status;
    response_status=(cerberus_wifi_capture_status_payload_t){0}; /* Legacy idle. */
    assert(c5_spi_link_wifi_capture_status(&out)==ESP_OK);
    response_status.state=CERBERUS_WIFI_CAPTURE_ERROR;response_status.error_code=ESP_FAIL;
    assert(c5_spi_link_wifi_capture_status(&out)==ESP_OK);
    response_status=saved;
    exchange_error=ESP_ERR_INVALID_CRC;
    assert(c5_spi_link_wifi_capture_status(&out)==ESP_ERR_INVALID_CRC&&zero(&out,sizeof(out)));
    exchange_error=ESP_OK;expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP;
    response_status.generation=101;
    assert(c5_spi_link_wifi_capture_stop(100,&out)==ESP_ERR_INVALID_RESPONSE);
    response_status.generation=100;response_status.state=CERBERUS_WIFI_CAPTURE_STOPPED;
    assert(c5_spi_link_wifi_capture_stop(100,&out)==ESP_OK);
    unsigned old=exchanges;
    assert(c5_spi_link_wifi_capture_stop(0,&out)==ESP_ERR_INVALID_ARG&&exchanges==old);
    link_mutex=0;
    assert(c5_spi_link_wifi_capture_status(&out)==ESP_ERR_INVALID_STATE&&zero(&out,sizeof(out)));
    assert(takes==releases&&!held);
}
static void immutable_frame(void)
{
    reset(50,CERBERUS_WIFI_CAPTURE_MODE_BEACON,true);
    for(size_t i=0;i<sizeof(frame);++i)frame[i]=(uint8_t)i;
    response_peek=(cerberus_wifi_capture_peek_payload_t){.generation=100,.sequence=200,
        .length=sizeof(frame),.channel=36,.rssi_dbm=-45,.crc32=cerberus_protocol_crc32(frame,sizeof(frame))};
    response_peek.monotonic_us_le48[0]=1;response_peek.monotonic_us_le48[5]=2;
    cerberus_wifi_capture_peek_payload_t peek;
    expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK;
    assert(c5_spi_link_wifi_capture_peek(100,&peek)==ESP_OK&&peek.channel==36);
    assert(c5_spi_link_wifi_capture_monotonic_us(&peek)==((UINT64_C(2)<<40)|1));
    expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ;
    uint8_t result[sizeof(frame)+3];memset(result,0xa5,sizeof(result));
    assert(c5_spi_link_wifi_capture_read_frame(&peek,result,sizeof(result))==ESP_OK);
    assert(!memcmp(result,frame,sizeof(frame))&&result[sizeof(frame)]==0xa5);
    bad_data=true;
    assert(c5_spi_link_wifi_capture_read_frame(&peek,result,sizeof(result))==ESP_ERR_INVALID_CRC&&zero(result,sizeof(frame)));
    bad_data=false;bad_offset=true;
    assert(c5_spi_link_wifi_capture_read_frame(&peek,result,sizeof(result))==ESP_ERR_INVALID_RESPONSE&&zero(result,sizeof(frame)));
    expected_command=CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK;
    response_peek.generation=101;
    assert(c5_spi_link_wifi_capture_peek(100,&peek)==ESP_ERR_INVALID_RESPONSE&&zero(&peek,sizeof(peek)));
    response_peek.generation=100;response_peek.channel=0;
    assert(c5_spi_link_wifi_capture_peek(100,&peek)==ESP_ERR_INVALID_RESPONSE&&zero(&peek,sizeof(peek)));
    assert(takes==releases&&!held);
}
int main(void)
{
    _Static_assert(sizeof(cerberus_wifi_capture_start_request_t)==17,"start wire size");
    _Static_assert(sizeof(cerberus_wifi_capture_status_payload_t)==22,"status wire size");
    _Static_assert(sizeof(cerberus_wifi_capture_peek_payload_t)==22,"peek wire size");
    gates();status_validation();immutable_frame();
    puts("S3 production capture link: v45/v48/v50 gates, no old-firmware START, status/generation errors, lock release, immutable chunks/CRC/source time passed.");
    return 0;
}
