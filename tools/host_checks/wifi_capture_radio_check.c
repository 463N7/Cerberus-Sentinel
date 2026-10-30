#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wifi_capture_service.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compile the actual service and scanner-private lifecycle. No scheduler or
 * radio implementation is reproduced here; only the ESP/RTOS boundary is fake. */
static int64_t fake_us=1000000;
int64_t esp_timer_get_time(void){return fake_us;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 100000;}
void *heap_caps_malloc(size_t bytes,unsigned caps){(void)caps;return malloc(bytes);}
#include "wifi_capture_service.c"

typedef unsigned wifi_band_mode_t;
typedef unsigned wifi_second_chan_t;
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
enum {WIFI_BAND_MODE_AUTO=0,WIFI_BAND_MODE_2G_ONLY=1,WIFI_BAND_MODE_5G_ONLY=2,
      WIFI_BAND_2G=1,WIFI_BAND_5G=2,WIFI_SECOND_CHAN_NONE=0,
      WIFI_PROMIS_FILTER_MASK_MGMT=1,WIFI_PROMIS_FILTER_MASK_CTRL=2,
      WIFI_PROMIS_FILTER_MASK_DATA=4};
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
static int scanner_mutex=1,scanner_task_handle=1;
static bool scanner_ready=true,station_reserved,locked;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_defense_status_payload_t defense_status;
static atomic_bool capture_radio_reserved;
static atomic_bool csi_radio_reserved, mac_radio_reserved;
static unsigned notifications,delay_count;
static uint32_t drain_at_ms;
static int xSemaphoreTake(int mutex,unsigned ticks)
{(void)ticks;assert(mutex==1&&!locked);locked=true;return 1;}
static void xSemaphoreGive(int mutex){assert(mutex==1&&locked);locked=false;}
static void xTaskNotifyGive(int handle){assert(handle==1);++notifications;}
static void vTaskDelay(unsigned ticks)
{
    fake_us+=(int64_t)ticks*1000;++delay_count;
    if(drain_at_ms&&(uint32_t)(fake_us/1000)>=drain_at_ms)
        atomic_store(&callback_inflight,0);
}
static unsigned survey_preempts;
static esp_err_t cerberus_wifi_channel_survey_preempt(void){++survey_preempts;return ESP_OK;}
static bool cerberus_c5_test_running(void){return false;}
static bool another_radio_operation_is_active(void){return false;}
static bool wifi_connection_is_active_locked(void){assert(locked);return false;}

enum operation {GET_MODE,GET_CHANNEL,SET_MODE,SET_BAND,SET_CHANNEL,FILTER,ENABLE,DISABLE,OP_COUNT};
static unsigned calls[OP_COUNT],fail_count[OP_COUNT];
static bool hop_expected,promiscuous,reject_channel[178],reject_all;
static uint8_t radio_channel=11;
static unsigned radio_band=WIFI_BAND_2G,radio_mode=WIFI_BAND_MODE_AUTO;
static wifi_second_chan_t radio_second=1;
static uint8_t beacon[48]={0x80};

static esp_err_t operation(enum operation op)
{
    assert(locked);++calls[op];
    if(fail_count[op]){--fail_count[op];return ESP_FAIL;}
    return ESP_OK;
}
static void verify_paused(void)
{
    if(!hop_expected)return;
    cerberus_wifi_capture_status_payload_t before,after;
    cerberus_wifi_capture_service_status(&before);
    assert(before.channel==0);
    /* Deliberately deliver callbacks at every driver boundary while paused. */
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),radio_channel,-60,(uint64_t)fake_us);
    cerberus_wifi_capture_service_status(&after);
    assert(after.accepted==before.accepted);
}
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *out)
{esp_err_t e=operation(GET_MODE);if(e==ESP_OK)*out=radio_mode;return e;}
static esp_err_t esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *second)
{esp_err_t e=operation(GET_CHANNEL);if(e==ESP_OK){*out=radio_channel;*second=radio_second;}return e;}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t value)
{verify_paused();assert(!hop_expected||!promiscuous);esp_err_t e=operation(SET_MODE);if(e==ESP_OK)radio_mode=value;return e;}
static esp_err_t esp_wifi_set_band(unsigned value)
{verify_paused();assert(!hop_expected||!promiscuous);esp_err_t e=operation(SET_BAND);if(e==ESP_OK)radio_band=value;return e;}
static esp_err_t esp_wifi_set_channel(uint8_t channel,wifi_second_chan_t second)
{
    verify_paused();assert(!hop_expected||!promiscuous);
    assert(!hop_expected||cerberus_wifi_capture_service_inflight()==0);
    esp_err_t e=operation(SET_CHANNEL);
    if(e==ESP_OK&&cerberus_wifi_capture_service_active()&&(reject_all||reject_channel[channel]))
        e=ESP_ERR_NOT_SUPPORTED;
    if(e==ESP_OK){assert((channel<=14?WIFI_BAND_2G:WIFI_BAND_5G)==radio_band);radio_channel=channel;radio_second=second;}
    return e;
}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter)
{assert(!hop_expected||filter->filter_mask==WIFI_PROMIS_FILTER_MASK_MGMT);return operation(FILTER);}
static esp_err_t esp_wifi_set_promiscuous(bool enable)
{
    verify_paused();esp_err_t e=operation(enable?ENABLE:DISABLE);
    if(e==ESP_OK)promiscuous=enable;
    return e;
}
static esp_err_t wifi_capture_stop_internal(uint32_t generation,esp_err_t reason);
#include "wifi_capture_radio_service.h"

static uint32_t generation=100;
static cerberus_wifi_capture_start_request_t request(bool hopping)
{
    hop_expected=hopping;
    return (cerberus_wifi_capture_start_request_t){.generation=++generation,
        .channel=hopping?0:6,.mode_mask=hopping?CERBERUS_WIFI_CAPTURE_MODE_BEACON:CERBERUS_WIFI_CAPTURE_MODE_PROBE,
        .flags=hopping?CERBERUS_WIFI_CAPTURE_FLAG_HOP:0,.lease_ms=1000};
}
static cerberus_wifi_capture_status_payload_t status(void)
{cerberus_wifi_capture_status_payload_t s;cerberus_wifi_capture_status(&s);return s;}
static void advance(unsigned ms){fake_us+=(int64_t)ms*1000;wifi_capture_worker_tick();assert(!locked);}
static void reset_driver(void)
{
    assert(!atomic_load(&capture_radio_reserved)&&!cerberus_wifi_capture_service_active());
    assert(!locked);memset(calls,0,sizeof(calls));memset(fail_count,0,sizeof(fail_count));
    memset(reject_channel,0,sizeof(reject_channel));reject_all=false;drain_at_ms=0;
    promiscuous=false;radio_channel=11;radio_band=WIFI_BAND_2G;
    radio_mode=WIFI_BAND_MODE_2G_ONLY;radio_second=1;delay_count=0;
}
static void drain(uint32_t gen)
{
    cerberus_wifi_capture_peek_payload_t p;
    while(cerberus_wifi_capture_service_peek(gen,&p)==ESP_OK&&p.sequence){
        cerberus_wifi_capture_ack_request_t ack={.generation=gen,.sequence=p.sequence};
        assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    }
}
static void successful_hops(void)
{
    reset_driver();cerberus_wifi_capture_start_request_t r=request(true);
    reject_channel[2]=true;
    assert(cerberus_wifi_capture_start(&r)==ESP_OK&&radio_channel==1&&promiscuous);
    assert(status().channel==1&&status().generation==r.generation);
    unsigned setups=calls[SET_CHANNEL];
    assert(cerberus_wifi_capture_start(&r)==ESP_OK&&calls[SET_CHANNEL]==setups);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),1,-41,(uint64_t)fake_us);
    cerberus_wifi_capture_peek_payload_t p;
    assert(cerberus_wifi_capture_service_peek(r.generation,&p)==ESP_OK&&p.sequence&&p.channel==1);
    uint32_t sequence=p.sequence,crc=p.crc32;
    advance(249);assert(radio_channel==1);
    advance(1);assert(radio_channel==3&&status().channel==3);
    assert(cerberus_wifi_capture_service_peek(r.generation,&p)==ESP_OK&&
        p.sequence==sequence&&p.channel==1&&p.crc32==crc);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),3,-42,(uint64_t)fake_us);
    /* Keep a pinned record across both bands, renewing only via transport. */
    for(unsigned i=0;i<12;++i){
        assert(cerberus_wifi_capture_service_peek(r.generation,&p)==ESP_OK);
        advance(250);
    }
    assert(radio_channel==36&&radio_band==WIFI_BAND_5G&&status().accepted==2);
    assert(cerberus_wifi_capture_stop(r.generation-1)==ESP_ERR_INVALID_STATE&&promiscuous);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);
    assert(!promiscuous&&!atomic_load(&capture_radio_reserved)&&status().state==CERBERUS_WIFI_CAPTURE_STOPPED);
    assert(radio_channel==11&&radio_second==1&&radio_mode==WIFI_BAND_MODE_2G_ONLY);
    assert(cerberus_wifi_capture_service_peek(r.generation,&p)==ESP_OK&&p.sequence==sequence&&p.channel==1);
    cerberus_wifi_capture_start_request_t blocked=r;blocked.generation=r.generation+1U;
    unsigned blocked_calls=calls[SET_CHANNEL];
    assert(cerberus_wifi_capture_start(&blocked)==ESP_ERR_INVALID_STATE&&calls[SET_CHANNEL]==blocked_calls);
    cerberus_wifi_capture_read_request_t read={.generation=r.generation,.sequence=sequence,.offset=20};
    cerberus_wifi_capture_read_payload_t chunk;
    assert(cerberus_wifi_capture_service_read(&read,&chunk)==ESP_OK&&!memcmp(chunk.data,beacon+20,20));
    cerberus_wifi_capture_ack_request_t ack={.generation=r.generation,.sequence=sequence};
    assert(cerberus_wifi_capture_service_ack(&ack)==ESP_OK);
    assert(cerberus_wifi_capture_service_peek(r.generation,&p)==ESP_OK&&p.sequence==sequence+1&&p.channel==3);
    drain(r.generation);
    unsigned restored=calls[SET_CHANNEL];
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK&&calls[SET_CHANNEL]==restored);
}
static void faults_and_retries(void)
{
    for(enum operation op=GET_MODE;op<OP_COUNT;++op){
        reset_driver();cerberus_wifi_capture_start_request_t r=request(true);
        /* A single channel/band rejection is correctly skipped. Exhaust the
         * candidate list instead to exercise full-cycle failure. */
        fail_count[op]=(op==SET_BAND||op==SET_CHANNEL)?(unsigned)sizeof(capture_hop_channels):1;
        assert(cerberus_wifi_capture_start(&r)!=ESP_OK);
        memset(fail_count,0,sizeof(fail_count));
        assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);
        assert(!atomic_load(&capture_radio_reserved)&&status().state==CERBERUS_WIFI_CAPTURE_STOPPED);
        assert(status().error_code!=ESP_OK&&!promiscuous);
    }
    reset_driver();cerberus_wifi_capture_start_request_t r=request(true);
    reject_all=true;
    assert(cerberus_wifi_capture_start(&r)==ESP_ERR_NOT_SUPPORTED);
    assert(calls[ENABLE]==0&&calls[SET_CHANNEL]==sizeof(capture_hop_channels)+1U);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);
    reset_driver();r=request(true);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    fail_count[DISABLE]=2;
    unsigned tunes=calls[SET_CHANNEL];
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_FAIL);
    assert(status().state==CERBERUS_WIFI_CAPTURE_ERROR&&atomic_load(&capture_radio_reserved));
    assert(status().channel==0&&calls[SET_CHANNEL]==tunes);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_FAIL);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK&&!promiscuous);
    assert(status().state==CERBERUS_WIFI_CAPTURE_STOPPED&&!atomic_load(&capture_radio_reserved));
    reset_driver();r=request(true);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    fail_count[SET_MODE]=1;
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_FAIL);
    assert(!promiscuous&&atomic_load(&capture_radio_reserved));
    advance(50);assert(!atomic_load(&capture_radio_reserved)&&radio_channel==11);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);
    /* A failed enable during a later hop stops capture and restores the
     * original channel, without losing the pending pre-hop observation. */
    reset_driver();r=request(true);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),1,-42,(uint64_t)fake_us);
    fail_count[ENABLE]=1;advance(250);
    assert(status().state==CERBERUS_WIFI_CAPTURE_ERROR&&status().pending==1);
    assert(!promiscuous&&!atomic_load(&capture_radio_reserved)&&radio_channel==11);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK&&status().error_code==ESP_FAIL);
    drain(r.generation);
}
static void drain_and_lease(void)
{
    reset_driver();cerberus_wifi_capture_start_request_t r=request(true);
    assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    atomic_store(&callback_inflight,1);drain_at_ms=(uint32_t)(fake_us/1000)+255;
    advance(250);assert(delay_count==5&&radio_channel==2);
    atomic_store(&callback_inflight,1);drain_at_ms=0;
    unsigned tunes=calls[SET_CHANNEL];advance(250);
    assert(calls[SET_CHANNEL]==tunes&&atomic_load(&capture_radio_reserved));
    assert(!cerberus_wifi_capture_service_active()&&!promiscuous);
    atomic_store(&callback_inflight,0);advance(50);
    assert(!atomic_load(&capture_radio_reserved));
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);
    reset_driver();fake_us=((int64_t)UINT32_MAX-500)*1000;r=request(true);
    assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    advance(999);assert(cerberus_wifi_capture_service_active());
    advance(1);assert(status().state==CERBERUS_WIFI_CAPTURE_STOPPED&&status().error_code==ESP_ERR_TIMEOUT);
    assert(!promiscuous&&!atomic_load(&capture_radio_reserved));
    reset_driver();fake_us=((int64_t)UINT32_MAX-999)*1000;r=request(true);
    assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    assert(cerberus_wifi_capture_service_deadline_ms()==1U);
    advance(1001);assert(status().state==CERBERUS_WIFI_CAPTURE_STOPPED&&!atomic_load(&capture_radio_reserved));
    /* Fixed-channel mode keeps its selected channel, original filters,
     * idempotency, callback scope and existing finite lease behavior. */
    reset_driver();r=request(false);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    tunes=calls[SET_CHANNEL];advance(750);
    assert(radio_channel==6&&calls[SET_CHANNEL]==tunes&&calls[GET_MODE]==0);
    advance(250);assert(status().state==CERBERUS_WIFI_CAPTURE_STOPPED&&!promiscuous);
    assert(radio_channel==6&&calls[SET_CHANNEL]==tunes);
    reset_driver();r=request(false);r.channel=36;r.mode_mask=CERBERUS_WIFI_CAPTURE_MODE_BEACON;
    assert(cerberus_wifi_capture_start(&r)==ESP_OK&&radio_channel==36);
    cerberus_wifi_capture_service_receive(beacon,sizeof(beacon),36,-51,(uint64_t)fake_us);
    assert(status().accepted==1&&status().pending==1&&calls[GET_MODE]==0);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_OK);drain(r.generation);
    /* Fixed captures must not acknowledge cleanup while an admitted producer
     * could still publish to the ring after S3 observes pending=0. */
    reset_driver();r=request(false);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    atomic_store(&callback_inflight,1);
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_ERR_TIMEOUT);
    assert(status().state==CERBERUS_WIFI_CAPTURE_ERROR&&atomic_load(&capture_radio_reserved));
    assert(!promiscuous&&delay_count==50);
    atomic_store(&callback_inflight,0);advance(50);
    assert(!atomic_load(&capture_radio_reserved)&&status().state==CERBERUS_WIFI_CAPTURE_STOPPED);
    reset_driver();r=request(false);assert(cerberus_wifi_capture_start(&r)==ESP_OK);
    fail_count[DISABLE]=1;
    assert(cerberus_wifi_capture_stop(r.generation)==ESP_FAIL&&atomic_load(&capture_radio_reserved));
    advance(50);assert(!promiscuous&&!atomic_load(&capture_radio_reserved));
}
int main(void)
{
    reset_driver();cerberus_wifi_capture_start_request_t blocked=request(true);
    atomic_store(&csi_radio_reserved,true);
    assert(cerberus_wifi_capture_start(&blocked)==ESP_ERR_INVALID_STATE&&!atomic_load(&capture_radio_reserved));
    atomic_store(&csi_radio_reserved,false);
    atomic_store(&mac_radio_reserved,true);
    unsigned prior_preempts=survey_preempts;
    assert(cerberus_wifi_capture_start(&blocked)==ESP_ERR_INVALID_STATE&&!atomic_load(&capture_radio_reserved));
    assert(survey_preempts==prior_preempts);
    atomic_store(&mac_radio_reserved,false);
    successful_hops();faults_and_retries();drain_and_lease();
    puts("C5 production capture radio: dual-band hop/pause, pinned CRC/sequence, SDK rejection, drain, rollback/Stop retries, lease wrap, fixed-mode regression passed.");
    return 0;
}
