#undef NDEBUG
#include "cerberus_protocol.h"
#include "csi_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"CSI check failed at %d: %s\n",__LINE__,#x);exit(1);}} while(0)
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_ARG=0x102,
    ESP_ERR_INVALID_STATE=0x103,ESP_ERR_NOT_FOUND=0x105,ESP_ERR_TIMEOUT=0x107};
enum {WIFI_BAND_MODE_AUTO=10,WIFI_BAND_2G=2,WIFI_BAND_5G=5,WIFI_SECOND_CHAN_NONE=0,
    WIFI_SECOND_CHAN_ABOVE=1,WIFI_SECOND_CHAN_BELOW=2,WIFI_PROMIS_FILTER_MASK_MGMT=1,
    WIFI_PROMIS_FILTER_MASK_DATA=4,ESP_CSI_ACQUIRE_STBC_HELTF1=0};
typedef unsigned wifi_band_mode_t,wifi_second_chan_t;
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
typedef struct {unsigned enable,acquire_csi_legacy,acquire_csi_force_lltf,acquire_csi_ht20,
    acquire_csi_ht40,acquire_csi_vht,acquire_csi_su,acquire_csi_mu,acquire_csi_dcm,
    acquire_csi_beamformed,acquire_csi_he_stbc_mode,val_scale_cfg,dump_ack_en,lltf_bit_mode;} wifi_csi_config_t;
typedef struct {struct {unsigned rx_state,rx_channel_estimate_info_vld,channel,second,cur_bb_format;int rssi;} rx_ctrl;
    uint8_t mac[6];int8_t *buf;uint16_t len;bool first_word_invalid;} wifi_csi_info_t;
typedef void (*wifi_csi_cb_t)(void *,wifi_csi_info_t *);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static int mutex,task;
static void *scanner_mutex=&mutex,*scanner_task_handle=&task;
static bool locked,critical,scanner_ready=true,station_reserved,foreign,test_running,connected;
static atomic_bool csi_radio_reserved,channel_survey_radio_reserved,mac_radio_reserved;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_defense_status_payload_t defense_status;
static cerberus_wifi_channel_survey_status_payload_t channel_survey_status;
static int64_t clock_us;
static unsigned notifications,allocations,frees,sdk_calls,fail_step,delay_enable_ms,delay_tune_ms;
static bool allocation_fail,promiscuous,csi_enabled,fail_disable,fail_restore,drain_on_delay;
static wifi_band_mode_t mode=9,band=2;
static uint8_t channel=11;
static wifi_second_chan_t secondary=1;
static wifi_promiscuous_filter_t filter={17};
static wifi_csi_config_t config;
static wifi_csi_cb_t registered_callback;
static void *registered_context;
static void (*before_enter)(void),(*before_copy)(void);
static int64_t esp_timer_get_time(void){return clock_us;}
static void xSemaphoreTake(void *m,unsigned n){(void)n;CHECK(m==scanner_mutex&&!locked);locked=true;}
static void xSemaphoreGive(void *m){CHECK(m==scanner_mutex&&locked);locked=false;}
static void enter(portMUX_TYPE *m){(void)m;if(before_enter){void(*f)(void)=before_enter;before_enter=NULL;f();}CHECK(!critical);critical=true;}
static void leave(portMUX_TYPE *m){(void)m;CHECK(critical);critical=false;}
#define portENTER_CRITICAL(m) enter(m)
#define portEXIT_CRITICAL(m) leave(m)
static void vTaskDelay(unsigned ms);
static void xTaskNotifyGive(void *t){CHECK(t==scanner_task_handle);++notifications;}
static bool cerberus_c5_test_running(void){return test_running;}
static bool another_radio_operation_is_active(void){return foreign||atomic_load(&csi_radio_reserved)||atomic_load(&mac_radio_reserved);}
static bool wifi_connection_is_active_locked(void){return connected;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps)
{CHECK(locked&&!critical&&caps==3&&n==16&&size==124);if(allocation_fail)return NULL;++allocations;return calloc(n,size);}
static void test_free(void *p){CHECK(!critical);if(p)++frees;free(p);}
static void esp_fill_random(void *p,size_t n){CHECK(locked&&n==8);uint64_t seed=UINT64_C(0x1234567887654321);memcpy(p,&seed,n);}
static esp_err_t sdk(void){CHECK(locked&&!critical);++sdk_calls;return sdk_calls==fail_step?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_get_promiscuous(bool *v){esp_err_t e=sdk();if(!e)*v=promiscuous;return e;}
static esp_err_t esp_wifi_get_csi_config(wifi_csi_config_t *v){esp_err_t e=sdk();if(!e)*v=config;return e;}
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *v){esp_err_t e=sdk();if(!e)*v=mode;return e;}
static esp_err_t esp_wifi_get_channel(uint8_t *a,wifi_second_chan_t *b){esp_err_t e=sdk();if(!e){*a=channel;*b=secondary;}return e;}
static esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *v){esp_err_t e=sdk();if(!e)*v=filter;return e;}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(v==9&&fail_restore)return ESP_FAIL;if(!e)mode=v;return e;}
static esp_err_t esp_wifi_set_band(unsigned v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(!e)band=v;return e;}
static esp_err_t esp_wifi_set_channel(uint8_t a,wifi_second_chan_t b){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);clock_us+=(int64_t)delay_tune_ms*1000;if(!e){channel=a;secondary=b;}return e;}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(!e)filter=*v;return e;}
static esp_err_t esp_wifi_set_csi_rx_cb(wifi_csi_cb_t cb,void *ctx){esp_err_t e=sdk();if(!e){registered_callback=cb;registered_context=ctx;}return e;}
static esp_err_t esp_wifi_set_csi_config(const wifi_csi_config_t *v){esp_err_t e=sdk();if(!e)config=*v;return e;}
static esp_err_t esp_wifi_set_csi(bool v){esp_err_t e=sdk();if(!v&&fail_disable)return ESP_FAIL;if(!e){csi_enabled=v;config.enable=v;}return e;}
static esp_err_t esp_wifi_set_promiscuous(bool v){esp_err_t e=sdk();if(!v&&fail_disable)return ESP_FAIL;if(v)clock_us+=(int64_t)delay_enable_ms*1000;if(!e)promiscuous=v;return e;}
static void *test_memcpy(void *d,const void *s,size_t n)
{if(n==106&&before_copy){void(*f)(void)=before_copy;before_copy=NULL;f();}return memcpy(d,s,n);}
#define free test_free
#define memcpy test_memcpy
#include "wifi_csi_service.h"
#undef memcpy
#undef free
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;if(drain_on_delay)csi_inflight=0;}
static unsigned replies;
static uint8_t response[22];
static void prepare_frame_response(cerberus_spi_command_t cmd,uint8_t seq,const void *p,size_t n)
{CHECK(cmd>=98&&cmd<=104&&seq==42&&n==22);memcpy(response,p,n);++replies;}
static void dispatch(const cerberus_spi_frame_t *request){switch(request->command){
#include "csi_dispatch.h"
default:CHECK(false);}}
static cerberus_csi_start_request_t request(uint32_t id)
{return(cerberus_csi_start_request_t){.boot_id=csi_boot_id,.job_id=id,.bssid={2,4,6,8,10,12},.channel=149,.version=1,.lease_ms=5000};}
static cerberus_csi_binding_t binding(void)
{return(cerberus_csi_binding_t){.boot_id=csi_boot_id,.job_id=csi_status.job_id,.epoch=csi_status.epoch};}
static cerberus_csi_status_t status(void)
{cerberus_csi_status_t s;cerberus_csi_binding_t b=binding();CHECK(cerberus_wifi_csi_get_status(&b,&s)==ESP_OK);CHECK(!locked&&!critical);return s;}
static void tick(unsigned ms){clock_us+=(int64_t)ms*1000;csi_worker_tick();CHECK(!locked&&!critical);}
static void start(uint32_t id)
{cerberus_csi_start_request_t r=request(id);cerberus_csi_status_t s;CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK);CHECK(s.state==1&&s.flags==1&&s.epoch);tick(0);CHECK(status().state==2&&status().flags==3);}
static void stop(void)
{cerberus_csi_binding_t b=binding();cerberus_csi_status_t s;CHECK(cerberus_wifi_csi_stop(&b,&s)==ESP_OK);tick(0);CHECK(!atomic_load(&csi_radio_reserved)&&!csi_ring&&!registered_callback);}
static void reset(void)
{
    CHECK(!locked&&!critical);CHECK(!csi_ring);memset(&csi_request,0,sizeof(csi_request));memset(&csi_status,0,sizeof(csi_status));
    csi_boot_id=csi_epoch_counter=csi_deadline_us=csi_clock_floor=csi_armed_us=0;
    csi_token=csi_inflight=csi_sequence=csi_last_ack=csi_accepted=csi_invalid=csi_overflow=csi_acked=0;
    csi_head=csi_count=0;csi_accepting=csi_saved=csi_disable_pending=csi_rx_disable_pending=csi_callback_registered=csi_sequence_exhausted=false;
    scanner_ready=true;scanner_mutex=&mutex;scanner_task_handle=&task;station_reserved=foreign=test_running=connected=false;
    atomic_store(&csi_radio_reserved,false);atomic_store(&channel_survey_radio_reserved,false);
    memset(&scan_status,0,sizeof(scan_status));memset(&defense_status,0,sizeof(defense_status));
    memset(&channel_survey_status,0,sizeof(channel_survey_status));
    clock_us=0;notifications=allocations=frees=sdk_calls=fail_step=delay_enable_ms=delay_tune_ms=replies=0;
    allocation_fail=promiscuous=csi_enabled=fail_disable=fail_restore=drain_on_delay=false;
    mode=9;band=2;channel=11;secondary=1;filter.filter_mask=17;memset(&config,0,sizeof(config));
    registered_callback=NULL;registered_context=NULL;before_enter=before_copy=NULL;
    cerberus_csi_info_t info;CHECK(cerberus_wifi_csi_get_info(&info)==ESP_OK&&info.boot_id&&info.source_now_us==0&&info.capacity==16);
}
static int8_t bytes[106];
static wifi_csi_info_t info(void)
{
    wifi_csi_info_t p={.mac={2,4,6,8,10,12},.buf=bytes,.len=106};
    p.rx_ctrl.channel=149;p.rx_ctrl.rssi=-67;p.rx_ctrl.cur_bb_format=1;p.rx_ctrl.rx_channel_estimate_info_vld=1;
    for(unsigned i=0;i<106;++i)bytes[i]=(int8_t)(i*19U);
    return p;
}
static void receive(void){wifi_csi_info_t p=info();wifi_csi_callback(registered_context,&p);CHECK(!csi_inflight&&!critical);}
static void identity_and_bytes(void)
{
    reset();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
    ++r.boot_id;CHECK(cerberus_wifi_csi_start(&r,&s)!=ESP_OK&&!allocations);r=request(1);
    CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK);uint64_t epoch=s.epoch;
    CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK&&s.epoch==epoch&&allocations==1&&notifications==1);
    ++r.channel;CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_ERR_INVALID_ARG);tick(0);
    CHECK(config.lltf_bit_mode==1&&config.acquire_csi_force_lltf&&config.acquire_csi_legacy&&config.dump_ack_en==0);
    CHECK(config.acquire_csi_ht20&&config.acquire_csi_ht40&&config.acquire_csi_su&&config.acquire_csi_mu&&config.acquire_csi_dcm);
    receive();CHECK(csi_accepted==1&&csi_count==1&&csi_ring[0].source_us==0);
    cerberus_csi_binding_t b=binding();cerberus_csi_peek_t p;CHECK(cerberus_wifi_csi_peek(&b,&p)==ESP_OK&&p.length==124&&p.sequence==1);
    uint8_t restored[124]={0};
    for(unsigned offset=0;offset<124;offset+=20){
        cerberus_csi_read_request_t q={.job_id=b.job_id,.epoch=b.epoch,.sequence=1,.offset=offset};cerberus_csi_chunk_t part;
        CHECK(cerberus_wifi_csi_read(&q,&part)==ESP_OK&&part.offset==offset&&part.length==(offset==120?4:20));
        memcpy(restored+offset,part.data,part.length);
        for(unsigned i=part.length;i<20;++i)CHECK(part.data[i]==0);
        receive(); /* Additional arrivals never change pinned bytes. */
    }
    cerberus_csi_sample_t sample;memcpy(&sample,restored,124);
    CHECK(sample.sequence==1&&sample.source_us==0&&sample.channel==149&&sample.rssi==-67&&sample.format==1&&sample.flags==1);
    CHECK(!memcmp(sample.data,bytes,106)&&p.crc32==cerberus_csi_crc32(restored,124));
    CHECK(cerberus_csi_crc32("123456789",9)==UINT32_C(0xcbf43926));
    cerberus_csi_ack_request_t ack={b.job_id,b.epoch,1};CHECK(cerberus_wifi_csi_ack(&ack,&s)==ESP_OK&&csi_count==7&&csi_acked==1);
    CHECK(cerberus_wifi_csi_ack(&ack,&s)==ESP_OK&&csi_count==7&&csi_acked==1);
    ack.sequence=99;CHECK(cerberus_wifi_csi_ack(&ack,&s)!=ESP_OK&&csi_count==7);
    cerberus_csi_target_payload_t target;CHECK(cerberus_wifi_csi_get_target(&b,&target)==ESP_OK&&target.epoch==epoch&&target.channel==149&&target.lease_ms==5000);
    CHECK(!memcmp(target.bssid,r.bssid,6));
    b.epoch++;CHECK(cerberus_wifi_csi_peek(&b,&p)!=ESP_OK&&!p.epoch);b=binding();++b.boot_id;
    CHECK(cerberus_wifi_csi_stop(&b,&s)!=ESP_OK&&atomic_load(&csi_radio_reserved));
    b=binding();++b.job_id;CHECK(cerberus_wifi_csi_get_status(&b,&s)==ESP_OK&&s.job_id==1);
    stop();CHECK(frees==1&&!promiscuous&&!csi_enabled&&mode==9&&channel==11&&secondary==1&&filter.filter_mask==17);
    b=binding();CHECK(cerberus_wifi_csi_peek(&b,&p)==ESP_OK&&!p.sequence&&!p.length&&!p.crc32);
    r=request(1);CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK&&s.state==4&&allocations==1);
    start(2);CHECK(csi_status.epoch==epoch+1);stop();CHECK(allocations==frees);
}
static void format_capacity_and_clock(void)
{
    reset();start(1);wifi_csi_info_t p=info();
    for(unsigned n=0;n<257;++n){p.len=n;wifi_csi_callback(registered_context,&p);}
    CHECK(csi_accepted==1&&csi_invalid==256);p=info();p.mac[0]=4;wifi_csi_callback(registered_context,&p);CHECK(csi_invalid==256);
    p=info();p.buf=NULL;wifi_csi_callback(registered_context,&p);p=info();p.rx_ctrl.rx_state=1;wifi_csi_callback(registered_context,&p);
    p=info();p.rx_ctrl.rx_channel_estimate_info_vld=0;wifi_csi_callback(registered_context,&p);
    p=info();p.rx_ctrl.channel=6;wifi_csi_callback(registered_context,&p);CHECK(csi_invalid==260);
    for(unsigned phy=0;phy<16;++phy){p=info();p.rx_ctrl.cur_bb_format=phy;wifi_csi_callback(registered_context,&p);}
    CHECK(csi_accepted==9&&csi_invalid==268);
    for(int rssi=-128;rssi<=127;++rssi){p=info();p.rx_ctrl.rssi=rssi;wifi_csi_callback(registered_context,&p);}
    CHECK(csi_count==16&&csi_overflow==141&&csi_invalid==376);
    cerberus_csi_binding_t b=binding();cerberus_csi_clock_t clock;CHECK(cerberus_wifi_csi_get_clock(&b,&clock)==ESP_OK&&clock.pending==16&&clock.accepted==16);
    csi_invalid=csi_overflow=UINT32_MAX;receive();p=info();p.len=0;wifi_csi_callback(registered_context,&p);CHECK(csi_invalid==UINT32_MAX&&csi_overflow==UINT32_MAX);
    while(csi_count){cerberus_csi_ack_request_t q={b.job_id,b.epoch,csi_ring[csi_head].sequence};cerberus_csi_status_t s;CHECK(cerberus_wifi_csi_ack(&q,&s)==ESP_OK);}
    clock_us=123456;CHECK(cerberus_wifi_csi_get_clock(&b,&clock)==ESP_OK&&clock.source_now_us==123456&&clock.pending==0);
    clock_us=123455;receive();CHECK(!csi_count);CHECK(cerberus_wifi_csi_get_clock(&b,&clock)!=ESP_OK&&!clock.epoch);
    clock_us=123456;receive();CHECK(csi_count==1&&csi_ring[csi_head].source_us==123456);
    stop();
}
static void failures_and_lease(void)
{
    for(unsigned step=1;step<=13;++step){reset();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
        CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK);fail_step=step;tick(0);
        CHECK(status().state==CERBERUS_CSI_ERROR&&!atomic_load(&csi_radio_reserved)&&frees==1);
        CHECK(!promiscuous&&!csi_enabled&&!registered_callback);}
    reset();allocation_fail=true;cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
    CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_ERR_NO_MEM&&!atomic_load(&csi_radio_reserved)&&!notifications);
    allocation_fail=false;start(1);receive();fail_disable=true;cerberus_csi_binding_t b=binding();
    CHECK(cerberus_wifi_csi_stop(&b,&s)==ESP_OK);tick(0);CHECK(status().state==5&&status().flags==1&&csi_ring&&frees==0);
    receive();CHECK(csi_count==1);r=request(2);CHECK(cerberus_wifi_csi_start(&r,&s)!=ESP_OK);
    fail_disable=false;fail_restore=true;tick(0);CHECK(status().flags==1&&csi_ring);fail_restore=false;tick(0);CHECK(!status().flags&&frees==1);
    reset();start(1);b=binding();cerberus_csi_renew_request_t renew={.boot_id=b.boot_id,.job_id=b.job_id,.epoch=b.epoch,.lease_ms=1000};
    clock_us=4000000;CHECK(cerberus_wifi_csi_renew(&renew,&s)==ESP_OK);CHECK(csi_deadline_us==5000000);
    clock_us=4999999;receive();CHECK(csi_count==1);clock_us=5000000;receive();CHECK(csi_count==1);
    CHECK(cerberus_wifi_csi_renew(&renew,&s)==ESP_OK&&s.state==5&&!csi_accepting);tick(0);CHECK(!status().flags&&frees==1);
    CHECK(cerberus_wifi_csi_renew(&renew,&s)==ESP_OK&&s.state==5&&!status().flags);
    for(unsigned late=0;late<2;++late){reset();r=request(1);CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK);
        if(late)delay_enable_ms=5000;else delay_tune_ms=5000;tick(0);CHECK(status().state==5&&!status().flags&&!csi_accepting&&frees==1);}
    reset();start(1);csi_sequence=UINT32_MAX;receive();CHECK(csi_sequence_exhausted&&!csi_accepting);tick(0);CHECK(status().state==5&&!status().flags);
}
static void request_guards(void)
{
    for(unsigned reason=0;reason<12;++reason){
        reset();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
        switch(reason){
        case 0:scanner_ready=false;break;case 1:scanner_task_handle=NULL;break;
        case 2:foreign=true;break;case 3:test_running=true;break;case 4:station_reserved=true;break;
        case 5:atomic_store(&channel_survey_radio_reserved,true);break;
        case 6:channel_survey_status.state=CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;break;
        case 7:scan_status.state=CERBERUS_WIFI_SCAN_RUNNING;break;
        case 8:defense_status.state=CERBERUS_WIFI_DEFENSE_RUNNING;break;case 9:connected=true;break;
        case 10:csi_token=UINT32_MAX;break;case 11:csi_epoch_counter=UINT64_MAX;break;
        }
        CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_ERR_INVALID_STATE&&!allocations&&!sdk_calls&&!notifications&&!csi_ring);
    }
    for(unsigned channel_code=0;channel_code<256;++channel_code){
        reset();cerberus_csi_start_request_t r=request(1);r.channel=channel_code;cerberus_csi_status_t s;
        const uint8_t legal[]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,36,40,44,48,52,56,60,64,100,104,108,112,116,120,124,128,132,136,140,144,149,153,157,161,165,169,173,177};
        bool allowed=false;for(unsigned i=0;i<sizeof(legal);++i)if(legal[i]==channel_code)allowed=true;
        CHECK((cerberus_wifi_csi_start(&r,&s)==ESP_OK)==allowed);
        if(allowed){CHECK(!sdk_calls);stop();}
    }
    for(unsigned reason=0;reason<8;++reason){reset();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
        switch(reason){case 0:r.job_id=0;break;case 1:memset(r.bssid,0,6);break;case 2:r.bssid[0]|=1;break;
        case 3:r.version=0;break;case 4:r.version=2;break;case 5:r.lease_ms=999;break;case 6:r.lease_ms=10001;break;case 7:r.boot_id++;break;}
        CHECK(cerberus_wifi_csi_start(&r,&s)!=ESP_OK&&!sdk_calls&&!allocations&&!csi_ring);
    }
    for(unsigned owner=0;owner<2;++owner){reset();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
        if(owner)config.enable=1;else promiscuous=true;
        CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_OK);tick(0);
        CHECK(status().state==5&&!status().flags&&frees==1);
        CHECK(owner?config.enable==1:promiscuous); /* Foreign enabled service is never disabled. */
    }
    reset();start(UINT32_MAX);stop();cerberus_csi_start_request_t r=request(1);cerberus_csi_status_t s;
    CHECK(cerberus_wifi_csi_start(&r,&s)==ESP_ERR_INVALID_STATE&&allocations==1);
}
static void stop_while_copying(void)
{
    CHECK(csi_inflight==1);cerberus_csi_binding_t b=binding();cerberus_csi_status_t s;
    CHECK(cerberus_wifi_csi_stop(&b,&s)==ESP_OK);tick(0);CHECK(csi_ring&&atomic_load(&csi_radio_reserved)&&status().state==5);
}
static void turnover_before_admission(void){stop();start(2);}
static void clock_while_copying(void)
{cerberus_csi_clock_t v;cerberus_csi_binding_t b=binding();clock_us+=100;CHECK(cerberus_wifi_csi_get_clock(&b,&v)==ESP_OK&&v.pending==1&&v.source_now_us==(uint64_t)clock_us);}
static void callback_races(void)
{
    reset();start(1);before_copy=stop_while_copying;receive();CHECK(csi_count==0&&csi_inflight==0&&csi_ring);tick(0);CHECK(!csi_ring&&!status().flags);
    reset();start(1);void *old=registered_context;wifi_csi_info_t p=info();before_enter=turnover_before_admission;
    wifi_csi_callback(old,&p);CHECK(csi_request.job_id==2&&!csi_count);wifi_csi_callback(old,&p);CHECK(!csi_count);receive();CHECK(csi_count==1);stop();
    reset();start(1);before_copy=clock_while_copying;receive();CHECK(csi_count==1&&csi_ring[0].source_us==0);
    cerberus_csi_binding_t b=binding();cerberus_csi_ack_request_t q={b.job_id,b.epoch,1};cerberus_csi_status_t s;
    CHECK(cerberus_wifi_csi_ack(&q,&s)==ESP_OK);cerberus_csi_clock_t v;CHECK(cerberus_wifi_csi_get_clock(&b,&v)==ESP_OK&&v.pending==0&&v.source_now_us==100);stop();
}
static void dispatch_validation(void)
{
    reset();cerberus_spi_frame_t f={.sequence=42,.command=CERBERUS_SPI_COMMAND_CSI_STATUS};
    cerberus_csi_status_request_t q={.page=CERBERUS_CSI_PAGE_INFO};f.payload_length=sizeof(q);memcpy(f.payload,&q,sizeof(q));dispatch(&f);CHECK(replies==1);
    for(unsigned page=0;page<256;++page){q=(cerberus_csi_status_request_t){.page=page};memcpy(f.payload,&q,sizeof(q));unsigned before=replies;dispatch(&f);CHECK(replies==before+(page==3));}
    cerberus_csi_start_request_t r=request(1);f.command=CERBERUS_SPI_COMMAND_CSI_START;f.payload_length=sizeof(r);memcpy(f.payload,&r,sizeof(r));dispatch(&f);tick(0);receive();
    cerberus_csi_binding_t b=binding();
    for(unsigned page=0;page<5;++page){q=(cerberus_csi_status_request_t){.boot_id=b.boot_id,.job_id=b.job_id,.epoch=b.epoch,.page=page};f.command=99;f.payload_length=sizeof(q);memcpy(f.payload,&q,sizeof(q));unsigned before=replies;dispatch(&f);CHECK(replies==before+(page!=3));}
    const unsigned lengths[]={22,21,22,20,20,17,16};
    for(unsigned cmd=98;cmd<=104;++cmd)for(unsigned len=0;len<=22;++len){
        if(len==lengths[cmd-98])continue;memset(f.payload,0,sizeof(f.payload));f.command=cmd;f.payload_length=len;unsigned before=replies;dispatch(&f);CHECK(replies==before);
    }
    stop();
}
int main(void)
{
    reset(); atomic_store(&mac_radio_reserved,true);
    cerberus_csi_start_request_t blocked=request(1);cerberus_csi_status_t blocked_status;
    CHECK(cerberus_wifi_csi_start(&blocked,&blocked_status)==ESP_ERR_INVALID_STATE&&!csi_ring&&!notifications);
    atomic_store(&mac_radio_reserved,false);
    identity_and_bytes();format_capacity_and_clock();failures_and_lease();request_guards();callback_races();dispatch_validation();
    printf("CSI capture checks passed: %u\n",checks);return 0;
}
