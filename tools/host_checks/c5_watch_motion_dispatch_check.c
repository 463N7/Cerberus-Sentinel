#undef NDEBUG
#include "cerberus_protocol.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static unsigned checks,calls,replies,last_route;
#define CHECK(x) do { ++checks; assert(x); } while (0)
enum { ESP_OK,ESP_ERR_INVALID_ARG,ESP_ERR_INVALID_STATE };
typedef int esp_err_t;
static esp_err_t producer_error;
static cerberus_watch_scan_data_request_t seen;
static uint8_t reply[22];
static unsigned reply_command,reply_sequence;
/* Producer entrypoints are mocked here only; separate compiled fixtures above
 * exercise real Wi-Fi/BLE getters and callbacks with the shared collector. */
static esp_err_t motion(unsigned route,const cerberus_watch_scan_data_request_t *q,
    cerberus_watch_scan_motion_payload_t *out)
{
    ++calls;last_route=route;seen=*q;
    memset(out,0,sizeof(*out));out->radio=q->radio;out->generation=q->generation;out->index=q->index;
    out->capture_epoch=UINT64_C(0xfedcba9876543210);out->motion=(sentinel_watch_motion_t){1017,360,65535,31,0};
    return producer_error;
}
static esp_err_t timing(unsigned route,const cerberus_watch_scan_data_request_t *q,
    cerberus_watch_scan_timing_payload_t *out)
{
    ++calls;last_route=route;seen=*q;
    memset(out,0,sizeof(*out));out->radio=q->radio;out->generation=q->generation;out->index=q->index;
    out->capture_epoch=UINT64_C(0xfedcba9876543210);out->flags=1;out->source_age_ms=UINT64_C(0x123456789abcdef0);
    return producer_error;
}
static esp_err_t cerberus_ble_watch_get_motion(const cerberus_watch_scan_data_request_t*q,cerberus_watch_scan_motion_payload_t*p){return motion(1,q,p);}
static esp_err_t cerberus_wifi_watch_get_motion(const cerberus_watch_scan_data_request_t*q,cerberus_watch_scan_motion_payload_t*p){return motion(2,q,p);}
static esp_err_t cerberus_ble_watch_get_motion_timing(const cerberus_watch_scan_data_request_t*q,cerberus_watch_scan_timing_payload_t*p){return timing(3,q,p);}
static esp_err_t cerberus_wifi_watch_get_motion_timing(const cerberus_watch_scan_data_request_t*q,cerberus_watch_scan_timing_payload_t*p){return timing(4,q,p);}
static void prepare_frame_response(cerberus_spi_command_t command,uint8_t seq,const void *p,size_t n)
{CHECK(n==22);++replies;reply_command=command;reply_sequence=seq;memcpy(reply,p,n);}
static void dispatch(const cerberus_spi_frame_t *request)
{
    switch(request->command){
#include "c5_watch_motion_dispatch.h"
    default:break;
    }
}
int main(void)
{
    CHECK(CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION==94&&CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING==95);
    CHECK(sizeof(cerberus_watch_observation_t)==46&&sizeof(cerberus_watch_scan_data_request_t)==4);
    CHECK(sizeof(cerberus_watch_scan_motion_payload_t)==22&&sizeof(cerberus_watch_scan_timing_payload_t)==22);
    for(unsigned command=94;command<=95;++command)for(unsigned radio=0;radio<256;++radio){
        cerberus_spi_frame_t f={0};f.command=(uint8_t)command;f.sequence=0x7b;f.payload_length=4;
        cerberus_watch_scan_data_request_t q={(uint8_t)radio,255,11,0};memcpy(f.payload,&q,4);
        calls=replies=0;producer_error=ESP_OK;dispatch(&f);
        bool valid=radio==CERBERUS_WATCH_RADIO_WIFI||radio==CERBERUS_WATCH_RADIO_BLE;
        CHECK(calls==(unsigned)valid&&replies==(unsigned)valid);
        if(valid){
            CHECK(!memcmp(&seen,&q,4)&&reply_command==command&&reply_sequence==0x7b);
            CHECK(last_route==(radio==CERBERUS_WATCH_RADIO_BLE?1U:2U)+(command==95?2U:0U));
            if(command==94){cerberus_watch_scan_motion_payload_t p;memcpy(&p,reply,22);
                CHECK(p.radio==radio&&p.generation==255&&p.index==11&&!p.reserved0&&!p.reserved[0]&&!p.reserved[1]);
                CHECK(p.capture_epoch==UINT64_C(0xfedcba9876543210)&&p.motion.flags==31&&p.motion.speed_qms==1017&&p.motion.direction_deg==360&&p.motion.height_encoded==65535);
            }else{cerberus_watch_scan_timing_payload_t p;memcpy(&p,reply,22);
                CHECK(p.radio==radio&&p.generation==255&&p.index==11&&p.flags==1&&!p.reserved[0]&&!p.reserved[1]);
                CHECK(p.capture_epoch==UINT64_C(0xfedcba9876543210)&&p.source_age_ms==UINT64_C(0x123456789abcdef0));}
            producer_error=ESP_ERR_INVALID_STATE;calls=replies=0;dispatch(&f);CHECK(calls==1&&!replies);
        }
        producer_error=ESP_OK;
        for(unsigned chunk=1;chunk<256;++chunk){f.payload[3]=(uint8_t)chunk;calls=replies=0;dispatch(&f);CHECK(!calls&&!replies);}
        f.payload[3]=0;
        for(unsigned length=0;length<=22;++length)if(length!=4){f.payload_length=(uint8_t)length;calls=replies=0;dispatch(&f);CHECK(!calls&&!replies);}
    }
    printf("C5 motion dispatch: %u checks; actual command cases, both radios, all chunk/radio bytes, exact four-byte requests, full-width sidecars and producer failure silence passed.\n",checks);
    return 0;
}
