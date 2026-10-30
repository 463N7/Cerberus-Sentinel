#undef NDEBUG
#include "cerberus_protocol.h"
#include "mac_observation.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"MAC capture line %d: %s\n",__LINE__,#x);exit(1);}} while(0)
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_NO_MEM=0x101,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103,
    ESP_ERR_NOT_FOUND=0x105,ESP_ERR_NOT_SUPPORTED=0x106,ESP_ERR_TIMEOUT=0x107};
enum {WIFI_BAND_MODE_AUTO=10,WIFI_BAND_2G=2,WIFI_BAND_5G=5,WIFI_SECOND_CHAN_NONE=0,
    WIFI_PROMIS_FILTER_MASK_MGMT=1,WIFI_PROMIS_FILTER_MASK_CTRL=2,WIFI_PROMIS_FILTER_MASK_DATA=4,
    WIFI_PROMIS_CTRL_FILTER_MASK_PSPOLL=1U<<26,WIFI_PROMIS_CTRL_FILTER_MASK_RTS=1U<<27,
    WIFI_PROMIS_CTRL_FILTER_MASK_CFEND=1U<<30,WIFI_PROMIS_CTRL_FILTER_MASK_CFENDACK=1U<<31,
    WIFI_COUNTRY_POLICY_MANUAL=1};
typedef enum {WIFI_PKT_MGMT,WIFI_PKT_CTRL,WIFI_PKT_DATA,WIFI_PKT_MISC} wifi_promiscuous_pkt_type_t;
typedef unsigned wifi_band_mode_t,wifi_second_chan_t;
typedef struct {uint32_t filter_mask;} wifi_promiscuous_filter_t;
typedef struct {bool enable;} wifi_csi_config_t;
typedef struct {char cc[3];uint8_t schan,nchan;int policy;uint32_t wifi_5g_channel_mask;} wifi_country_t;
typedef struct {unsigned rx_state,sig_len,channel;int rssi;} wifi_pkt_rx_ctrl_t;
typedef struct {wifi_pkt_rx_ctrl_t rx_ctrl;uint8_t payload[128];} wifi_promiscuous_pkt_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static int mutex,task;
static void *scanner_mutex=&mutex,*scanner_task_handle=&task;
static bool locked,critical,scanner_ready=true,station_reserved,foreign,test_running,connected;
static atomic_bool mac_radio_reserved,csi_radio_reserved,channel_survey_radio_reserved;
static atomic_uint mac_admission_token;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_relation_table_t relation_capture;
static atomic_bool relation_receiving;
static portMUX_TYPE defense_capture_mux;
static cerberus_wifi_defense_status_payload_t defense_status;
static cerberus_wifi_channel_survey_status_payload_t channel_survey_status;
static int64_t clock_us;
static unsigned allocations,frees,notifications,sdk_calls,fail_step,delay_tune_ms,delay_enable_ms;
static bool allocation_fail,promiscuous,fail_disable,fail_restore,drain_on_delay,csi_enabled;
static bool unsupported[256];
static wifi_country_t country;
static wifi_band_mode_t mode,band;
static uint8_t channel;
static wifi_second_chan_t second;
static wifi_promiscuous_filter_t filter,control;
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
static bool another_radio_operation_is_active(void){return foreign||atomic_load(&mac_radio_reserved)||atomic_load(&csi_radio_reserved);}
static bool cerberus_c5_test_running(void){return test_running;}
static bool wifi_connection_is_active_locked(void){return connected;}
static void *heap_caps_calloc(size_t n,size_t bytes,unsigned caps)
{CHECK(locked&&!critical&&n==1U&&bytes>=2600U&&caps==3U);if(allocation_fail)return NULL;++allocations;return calloc(n,bytes);}
static void test_free(void *p){CHECK(!critical);if(p)++frees;free(p);}
static void esp_fill_random(void *p,size_t n){CHECK(locked&&n==8);uint64_t seed=UINT64_C(0x23000000ABCD0000);memcpy(p,&seed,n);}
static esp_err_t sdk(void){CHECK(locked&&!critical);return ++sdk_calls==fail_step?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_get_promiscuous(bool *out){esp_err_t e=sdk();if(!e)*out=promiscuous;return e;}
static esp_err_t esp_wifi_get_csi_config(wifi_csi_config_t *out){esp_err_t e=sdk();if(!e)out->enable=csi_enabled;return e;}
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *out){esp_err_t e=sdk();if(!e)*out=mode;return e;}
static esp_err_t esp_wifi_get_channel(uint8_t *out,wifi_second_chan_t *s){esp_err_t e=sdk();if(!e){*out=channel;*s=second;}return e;}
static esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *out){esp_err_t e=sdk();if(!e)*out=filter;return e;}
static esp_err_t esp_wifi_get_promiscuous_ctrl_filter(wifi_promiscuous_filter_t *out){esp_err_t e=sdk();if(!e)*out=control;return e;}
static esp_err_t esp_wifi_get_country(wifi_country_t *out){esp_err_t e=sdk();if(!e)*out=country;return e;}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(v==9&&fail_restore)return ESP_FAIL;if(!e)mode=v;return e;}
static esp_err_t esp_wifi_set_band(unsigned v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(!e)band=v;return e;}
static esp_err_t esp_wifi_set_channel(uint8_t v,wifi_second_chan_t s){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);
    clock_us+=(int64_t)delay_tune_ms*1000;if(unsupported[v])return ESP_ERR_NOT_SUPPORTED;if(!e){channel=v;second=s;}return e;}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(!e)filter=*v;return e;}
static esp_err_t esp_wifi_set_promiscuous_ctrl_filter(const wifi_promiscuous_filter_t *v){esp_err_t e=sdk();CHECK(!promiscuous&&!csi_enabled);if(!e)control=*v;return e;}
static esp_err_t esp_wifi_set_promiscuous(bool v){esp_err_t e=sdk();if(!v&&fail_disable)return ESP_FAIL;
    if(v)clock_us+=(int64_t)delay_enable_ms*1000;if(!e)promiscuous=v;return e;}
static void *test_memcpy(void *d,const void *s,size_t n)
{if(n==6U&&before_copy){void(*f)(void)=before_copy;before_copy=NULL;f();}return memcpy(d,s,n);}
#define free test_free
#define memcpy test_memcpy
#include "wifi_mac_monitor_service.h"
#undef memcpy
#undef free
#include "mac_callback_entry.h"
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;if(drain_on_delay)mac_inflight=0;}
static unsigned replies;
static uint8_t reply[22];
static void prepare_frame_response(cerberus_spi_command_t cmd,uint8_t seq,const void *p,size_t n)
{CHECK(cmd>=105&&cmd<=111&&seq==42&&n==22);memcpy(reply,p,n);++replies;}
static void dispatch(const cerberus_spi_frame_t *request){switch(request->command){
#include "mac_dispatch.h"
default:CHECK(false);}}
static cerberus_mac_start_request_t request(uint32_t job)
{return(cerberus_mac_start_request_t){.boot_id=mac_boot_id,.job_id=job,.lease_ms=5000,.dwell_ms=250,.version=1};}
static cerberus_mac_binding_t binding(void)
{return(cerberus_mac_binding_t){.boot_id=mac_boot_id,.job_id=mac_status.job_id,.epoch=mac_status.epoch};}
static void tick(unsigned ms){clock_us+=(int64_t)ms*1000;mac_worker_tick();CHECK(!locked&&!critical);}
static void run(void){for(unsigned i=0;i<70U&&mac_status.state==CERBERUS_MAC_STARTING;++i)tick(20);
    CHECK(mac_status.state==CERBERUS_MAC_RUNNING&&mac_accepting&&promiscuous);}
static void start(uint32_t job){cerberus_mac_start_request_t r=request(job);cerberus_mac_status_t s;
    CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK&&s.state==1&&s.flags==1);run();}
static void stop(void){cerberus_mac_binding_t b=binding();cerberus_mac_status_t s;
    CHECK(cerberus_wifi_mac_stop(&b,&s)==ESP_OK);tick(0);CHECK(!mac_store&&!atomic_load(&mac_radio_reserved));}
static void reset(void)
{
    CHECK(!mac_store&&!locked&&!critical);memset(&mac_request,0,sizeof(mac_request));memset(&mac_status,0,sizeof(mac_status));
    memset(&mac_final_counters,0,sizeof(mac_final_counters));memset(&mac_pinned,0,sizeof(mac_pinned));
    mac_boot_id=mac_epoch_counter=mac_deadline_us=mac_armed_us=mac_clock_floor=mac_tuned_us=0;
    mac_inflight=mac_last_ack=0;mac_channel_count=mac_probe_index=mac_accepted_count=mac_next_index=mac_last_next_channel=0;
    mac_accepting=mac_saved=mac_disable_pending=mac_cache_valid=mac_exhausted=false;memset(mac_channels,0,sizeof(mac_channels));
    memset(&mac_cached_country,0,sizeof(mac_cached_country));atomic_store(&mac_admission_token,0);
    atomic_store(&mac_radio_reserved,false);atomic_store(&csi_radio_reserved,false);atomic_store(&channel_survey_radio_reserved,false);
    memset(&scan_status,0,sizeof(scan_status));memset(&defense_status,0,sizeof(defense_status));memset(&channel_survey_status,0,sizeof(channel_survey_status));
    scanner_mutex=&mutex;scanner_task_handle=&task;scanner_ready=true;station_reserved=foreign=test_running=connected=false;
    clock_us=0;allocations=frees=notifications=sdk_calls=fail_step=delay_tune_ms=delay_enable_ms=replies=0;
    allocation_fail=promiscuous=fail_disable=fail_restore=drain_on_delay=csi_enabled=false;memset(unsupported,0,sizeof(unsupported));
    country=(wifi_country_t){.cc={'U','S',0},.schan=1,.nchan=11};mode=9;band=2;channel=11;second=1;
    filter.filter_mask=17;control.filter_mask=23;before_enter=before_copy=NULL;
    cerberus_mac_info_t info;CHECK(cerberus_wifi_mac_get_info(&info)==ESP_OK&&info.boot_id&&info.source_now_us==0&&info.row_bytes==40&&info.capacity==32);
}
static wifi_promiscuous_pkt_t frame(uint8_t source)
{wifi_promiscuous_pkt_t p={.rx_ctrl={.sig_len=28,.channel=channel,.rssi=-52}};p.payload[0]=0x80;
    p.payload[10]=2;p.payload[15]=source;return p;}
static void receive(uint8_t source){wifi_promiscuous_pkt_t p=frame(source);wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);CHECK(!mac_inflight&&!critical);}
static cerberus_mac_peek_t peek(void){cerberus_mac_binding_t b=binding();cerberus_mac_peek_t p;
    CHECK(cerberus_wifi_mac_peek(&b,&p)==ESP_OK&&p.epoch==b.epoch&&p.snapshot_id&&p.length==28U+p.count*40U);return p;}
static void ack(uint32_t id){cerberus_mac_binding_t b=binding();cerberus_mac_ack_request_t q={b.job_id,b.epoch,id};cerberus_mac_status_t s;
    CHECK(cerberus_wifi_mac_ack(&q,&s)==ESP_OK);}
static void unexpected_mac_lock(void){CHECK(false);}
static void bytes_and_lifecycle(void)
{
    reset();cerberus_mac_start_request_t r=request(1);cerberus_mac_status_t s;cerberus_mac_binding_t b=binding();cerberus_mac_peek_t p;
    ++r.boot_id;CHECK(cerberus_wifi_mac_start(&r,&s)!=ESP_OK&&!allocations);r=request(1);
    CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK);uint64_t epoch=s.epoch;b=binding();
    CHECK(cerberus_wifi_mac_peek(&b,&p)!=ESP_OK&&!p.snapshot_id);CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK&&allocations==1);
    ++r.dwell_ms;CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_ERR_INVALID_ARG);run();p=peek();
    CHECK(p.length==28&&p.count==0&&cerberus_mac_snapshot_valid(&mac_store->pinned));ack(p.snapshot_id);ack(p.snapshot_id);
    for(uint8_t i=1;i<=32;++i){++clock_us;receive(i);}p=peek();CHECK(p.count==32&&p.length==1308);
    uint8_t restored[1308]={0};b=binding();
    for(unsigned offset=0;offset<p.length;offset+=20){
        cerberus_mac_read_request_t q={.job_id=b.job_id,.epoch=b.epoch,.snapshot_id=p.snapshot_id,.offset=(uint16_t)offset};cerberus_mac_chunk_t part;
        CHECK(cerberus_wifi_mac_read(&q,&part)==ESP_OK&&part.offset==offset);
        unsigned n=p.length-offset<20?p.length-offset:20;memcpy(restored+offset,part.data,n);
        for(unsigned i=n;i<20;++i)CHECK(!part.data[i]);++clock_us;receive(99);
        cerberus_mac_peek_t again=peek();CHECK(!memcmp(&p,&again,sizeof(p)));
    }
    CHECK(!memcmp(restored,&mac_store->pinned,1308)&&cerberus_mac_crc32(restored,1308)==p.crc32);
    CHECK(cerberus_mac_crc32("123456789",9)==UINT32_C(0xcbf43926));
    CHECK(mac_store->live.replacements==1&&mac_store->pinned.header.replacements==0);
    ack(p.snapshot_id);ack(p.snapshot_id);cerberus_mac_peek_t fresh=peek();CHECK(fresh.snapshot_id==p.snapshot_id+1);
    CHECK(mac_store->pinned.header.replacements==1&&mac_store->pinned.header.observations==98);
    b.epoch++;CHECK(cerberus_wifi_mac_peek(&b,&p)!=ESP_OK);b=binding();b.boot_id++;
    CHECK(cerberus_wifi_mac_stop(&b,&s)!=ESP_OK&&mac_accepting);b=binding();b.job_id++;
    CHECK(cerberus_wifi_mac_get_status(&b,&s)==ESP_OK&&s.job_id==1);stop();
    CHECK(frees==1&&mode==9&&channel==11&&second==1&&filter.filter_mask==17&&control.filter_mask==23&&!promiscuous);
    r=request(1);CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK&&s.state==CERBERUS_MAC_COMPLETE&&allocations==1);
    b=binding();CHECK(cerberus_wifi_mac_peek(&b,&p)!=ESP_OK);
    cerberus_mac_counters_t counts;CHECK(cerberus_wifi_mac_get_counters(&b,&counts)==ESP_OK&&counts.observations==98&&counts.replacements==1);
    before_enter=unexpected_mac_lock;receive(1);
    CHECK(before_enter==unexpected_mac_lock);before_enter=NULL; /* Inactive path adds no MAC critical section. */
    start(2);CHECK(mac_status.epoch==epoch+1);stop();CHECK(allocations==frees);
}
static void header_bounds(void)
{
    reset();start(1);uint8_t out[6],kind;
    for(unsigned fc=0;fc<256;++fc)for(unsigned n=0;n<=40;++n){
        wifi_promiscuous_pkt_t p=frame(1);p.payload[0]=(uint8_t)fc;
        unsigned type=(fc>>2U)&3U,sub=fc>>4U;wifi_promiscuous_pkt_type_t t=type==0?WIFI_PKT_MGMT:type==1?WIFI_PKT_CTRL:type==2?WIFI_PKT_DATA:WIFI_PKT_MISC;
        bool expected=!(fc&3U)&&((type==0&&n>=24)||(type==2&&n>=24U+((sub&8U)?2U:0U))||
            (type==1&&(sub==10||sub==11||sub==14||sub==15)&&n==16));
        CHECK(mac_wifi_header(p.payload,n,t,out,&kind)==expected);
    }
    for(unsigned ds=0;ds<4;++ds)for(unsigned qos=0;qos<2;++qos)for(unsigned ht=0;ht<2;++ht){
        wifi_promiscuous_pkt_t p=frame(2);p.payload[0]=qos?0x88:0x08;p.payload[1]=(uint8_t)(ds|(ht?0x80:0));
        unsigned required=24+(ds==3?6:0)+(qos?2+(ht?4:0):0);
        for(unsigned n=0;n<=40;++n)CHECK(mac_wifi_header(p.payload,n,WIFI_PKT_DATA,out,&kind)==(n>=required));
    }
    wifi_promiscuous_pkt_t p=frame(1);p.payload[10]=1;CHECK(!mac_wifi_header(p.payload,24,WIFI_PKT_MGMT,out,&kind));
    memset(p.payload+10,0,6);CHECK(!mac_wifi_header(p.payload,24,WIFI_PKT_MGMT,out,&kind));
    for(unsigned bit=0;bit<8;++bit){p=frame(1);p.payload[1]=(uint8_t)(1U<<bit);
        CHECK(mac_wifi_header(p.payload,28,WIFI_PKT_MGMT,out,&kind)==(bit>=2));}
    for(unsigned n=0;n<28;++n){p=frame(1);p.rx_ctrl.sig_len=n;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);}
    CHECK(mac_store->live.observations==0&&mac_store->live.invalid==28);
    p=frame(1);p.rx_ctrl.rx_state=1;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);p=frame(1);p.rx_ctrl.channel=177;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);
    for(int rssi=-128;rssi<128;++rssi){p=frame(1);p.rx_ctrl.rssi=rssi;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);}
    CHECK(mac_store->live.observations==256&&mac_store->live.rows[0].rssi==127&&!mac_store->live.rows[0].flags);
    /* Signal availability belongs to the latest actual report, not a prior
     * good sample. Invalid RSSI does not erase its source/count/time. */
    p=frame(2);p.rx_ctrl.rssi=-128;++clock_us;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);
    CHECK(mac_store->live.rows[1].observations==1&&mac_store->live.rows[1].rssi==127&&!mac_store->live.rows[1].flags);
    CHECK(mac_store->live.rows[1].last_us==(uint64_t)clock_us);
    p.rx_ctrl.rssi=-61;++clock_us;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);
    CHECK(mac_store->live.rows[1].observations==2&&mac_store->live.rows[1].rssi==-61&&mac_store->live.rows[1].flags==CERBERUS_MAC_RSSI_VALID);
    p.rx_ctrl.rssi=21;++clock_us;wifi_promiscuous_callback(&p,WIFI_PKT_MGMT);
    CHECK(mac_store->live.rows[1].observations==3&&mac_store->live.rows[1].rssi==127&&!mac_store->live.rows[1].flags);
    CHECK(mac_store->live.rows[1].last_us==(uint64_t)clock_us);stop();
}
static void channels_and_failure(void)
{
    reset();unsupported[36]=true;unsupported[177]=true;start(1);CHECK(mac_channel_count==37);
    bool seen[256]={0};for(unsigned i=0;i<mac_channel_count;++i){
        if(!(i%8U)){cerberus_mac_renew_request_t r={binding(),5000};cerberus_mac_status_t s;
            CHECK(cerberus_wifi_mac_renew(&r,&s)==ESP_OK);}
        seen[channel]=true;tick(250);
    }
    CHECK(seen[1]&&seen[11]&&seen[149]&&seen[173]&&!seen[36]&&!seen[177]);
    uint8_t next=mac_last_next_channel;stop();unsigned before=sdk_calls;start(2);
    CHECK(sdk_calls-before<30&&channel==next);stop();country.nchan=1;country.policy=1;country.wifi_5g_channel_mask=1U<<28;
    start(3);CHECK(mac_channel_count==1&&channel==1);stop();
    for(unsigned step=1;step<=10;++step){reset();cerberus_mac_start_request_t r=request(1);cerberus_mac_status_t s;
        CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK);fail_step=step;tick(0);
        CHECK(mac_status.state==CERBERUS_MAC_ERROR&&!mac_store&&frees==1&&!promiscuous);}
    reset();allocation_fail=true;cerberus_mac_start_request_t r=request(1);cerberus_mac_status_t s;
    CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_ERR_NO_MEM&&!notifications);allocation_fail=false;start(1);receive(1);
    fail_disable=true;cerberus_mac_binding_t b=binding();CHECK(cerberus_wifi_mac_stop(&b,&s)==ESP_OK);tick(0);
    CHECK(mac_store&&atomic_load(&mac_radio_reserved)&&!mac_accepting&&!frees);receive(2);CHECK(mac_store->live.count==1);
    fail_disable=false;fail_restore=true;tick(0);CHECK(mac_store&&mac_status.state==CERBERUS_MAC_ERROR);fail_restore=false;tick(0);CHECK(!mac_store&&frees==1);
    reset();start(1);mac_inflight=1;b=binding();CHECK(cerberus_wifi_mac_stop(&b,&s)==ESP_OK);tick(0);
    CHECK(mac_store&&atomic_load(&mac_radio_reserved));drain_on_delay=true;tick(0);CHECK(!mac_store);
    reset();promiscuous=true;r=request(1);CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK);tick(0);CHECK(!mac_store&&promiscuous);
    reset();csi_enabled=true;r=request(1);CHECK(cerberus_wifi_mac_start(&r,&s)==ESP_OK);tick(0);CHECK(!mac_store&&csi_enabled);
}
static void turnover(void){stop();start(2);}
static void stop_during_decode(void){cerberus_mac_binding_t b=binding();cerberus_mac_status_t s;
    CHECK(cerberus_wifi_mac_stop(&b,&s)==ESP_OK);tick(0);CHECK(mac_store&&mac_inflight==1&&!mac_accepting);}
static void epochs_and_deadlines(void)
{
    /* A zero callback clock is valid, and source values do not narrow to
     * milliseconds or a 32-bit host tick when copied into the pinned wire. */
    reset();cerberus_mac_start_request_t zero_request=request(1);cerberus_mac_status_t zero_status;
    CHECK(cerberus_wifi_mac_start(&zero_request,&zero_status)==ESP_OK);
    while(mac_status.state==CERBERUS_MAC_STARTING)tick(0);
    receive(1);cerberus_mac_peek_t zero_peek=peek();
    CHECK(mac_store->pinned.rows[0].first_us==0&&mac_store->pinned.rows[0].last_us==0&&mac_store->pinned.header.source_now_us==0);
    ack(zero_peek.snapshot_id);stop();
    reset();clock_us=INT64_C(1)<<44;start(1);receive(1);zero_peek=peek();
    CHECK(mac_store->pinned.rows[0].last_us==(uint64_t)clock_us&&mac_store->pinned.header.source_now_us==(uint64_t)clock_us);
    ack(zero_peek.snapshot_id);stop();
    reset();start(1);before_enter=turnover;receive(1);CHECK(mac_status.job_id==2&&mac_store->live.observations==0);
    receive(2);CHECK(mac_store->live.observations==1);before_copy=stop_during_decode;receive(3);tick(0);CHECK(!mac_store);
    reset();start(1);uint32_t old=atomic_load(&mac_admission_token);tick(250);wifi_promiscuous_pkt_t p=frame(1);
    mac_receive(&p,WIFI_PKT_MGMT,clock_us,old);CHECK(!mac_store->live.observations);receive(1);CHECK(mac_store->live.observations==1);
    cerberus_mac_binding_t b=binding();cerberus_mac_status_t s;cerberus_mac_renew_request_t r={.binding=b,.lease_ms=1000};
    clock_us=4000000;CHECK(cerberus_wifi_mac_renew(&r,&s)==ESP_OK&&mac_deadline_us==5000000);
    clock_us=4999999;receive(1);CHECK(mac_store->live.observations==2);clock_us=5000000;receive(1);CHECK(mac_store->live.observations==2);
    CHECK(cerberus_wifi_mac_renew(&r,&s)==ESP_OK&&s.state==CERBERUS_MAC_ERROR);tick(0);CHECK(!mac_store);
    CHECK(cerberus_wifi_mac_renew(&r,&s)==ESP_OK&&s.state==CERBERUS_MAC_ERROR);
    for(unsigned enable=0;enable<2;++enable){reset();start(1);clock_us=mac_deadline_us-1000;
        if(enable)delay_enable_ms=2;else delay_tune_ms=2;tick(0);
        CHECK(!mac_store&&!mac_accepting&&!promiscuous&&mac_status.state==CERBERUS_MAC_ERROR);}
    reset();start(1);mac_store->live.next_snapshot_id=UINT32_MAX;cerberus_mac_peek_t d;b=binding();
    CHECK(cerberus_wifi_mac_peek(&b,&d)!=ESP_OK&&mac_exhausted);tick(0);CHECK(!mac_store);
    reset();mac_epoch_counter=UINT64_MAX;cerberus_mac_start_request_t start_request=request(1);
    CHECK(cerberus_wifi_mac_start(&start_request,&s)!=ESP_OK&&!allocations);
    reset();atomic_store(&mac_admission_token,UINT32_MAX);start_request=request(1);CHECK(cerberus_wifi_mac_start(&start_request,&s)!=ESP_OK&&!allocations);
}
static void commands_and_guards(void)
{
    reset();for(unsigned reason=0;reason<9;++reason){
        scanner_ready=reason!=0;station_reserved=reason==1;foreign=reason==2;connected=reason==3;test_running=reason==4;
        atomic_store(&csi_radio_reserved,reason==5);scan_status.state=reason==6?CERBERUS_WIFI_SCAN_RUNNING:0;
        defense_status.state=reason==7?CERBERUS_WIFI_DEFENSE_RUNNING:0;channel_survey_status.state=reason==8?CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING:0;
        cerberus_mac_start_request_t r=request(1);cerberus_mac_status_t s;CHECK(cerberus_wifi_mac_start(&r,&s)!=ESP_OK&&!allocations&&!sdk_calls);
    }
    reset();cerberus_spi_frame_t frame_request={.command=CERBERUS_SPI_COMMAND_MAC_START,.sequence=42};
    cerberus_mac_start_request_t r=request(1);memcpy(frame_request.payload,&r,sizeof(r));frame_request.payload_length=22;dispatch(&frame_request);CHECK(replies==1);run();
    cerberus_mac_status_request_t q={.binding=binding()};frame_request.command=CERBERUS_SPI_COMMAND_MAC_STATUS;frame_request.payload_length=sizeof(q);
    for(unsigned page=0;page<4;++page){q.binding=page==CERBERUS_MAC_PAGE_INFO?(cerberus_mac_binding_t){0}:binding();q.page=page;
        memcpy(frame_request.payload,&q,sizeof(q));unsigned old=replies;dispatch(&frame_request);CHECK(replies==old+1);}
    q.binding=binding();q.page=CERBERUS_MAC_PAGE_INFO;memcpy(frame_request.payload,&q,sizeof(q));unsigned old=replies;dispatch(&frame_request);CHECK(replies==old);
    for(unsigned length=0;length<=22;++length)if(length!=sizeof(q)){frame_request.payload_length=length;dispatch(&frame_request);CHECK(replies==old);}
    cerberus_mac_binding_t b=binding();frame_request.command=CERBERUS_SPI_COMMAND_MAC_PEEK;frame_request.payload_length=sizeof(b);memcpy(frame_request.payload,&b,sizeof(b));dispatch(&frame_request);
    cerberus_mac_peek_t desc;memcpy(&desc,reply,sizeof(desc));CHECK(desc.snapshot_id&&desc.length==28);
    cerberus_mac_read_request_t read={b.job_id,b.epoch,desc.snapshot_id,20};frame_request.command=CERBERUS_SPI_COMMAND_MAC_READ;frame_request.payload_length=sizeof(read);memcpy(frame_request.payload,&read,sizeof(read));dispatch(&frame_request);
    cerberus_mac_ack_request_t ack_request={b.job_id,b.epoch,desc.snapshot_id};frame_request.command=CERBERUS_SPI_COMMAND_MAC_ACK;frame_request.payload_length=sizeof(ack_request);memcpy(frame_request.payload,&ack_request,sizeof(ack_request));dispatch(&frame_request);
    cerberus_mac_renew_request_t renew={b,5000};frame_request.command=CERBERUS_SPI_COMMAND_MAC_RENEW;frame_request.payload_length=sizeof(renew);memcpy(frame_request.payload,&renew,sizeof(renew));dispatch(&frame_request);
    b.epoch=0;frame_request.command=CERBERUS_SPI_COMMAND_MAC_STOP;frame_request.payload_length=sizeof(b);memcpy(frame_request.payload,&b,sizeof(b));dispatch(&frame_request);tick(0);CHECK(!mac_store&&allocations==frees);
}
int main(void)
{bytes_and_lifecycle();header_bounds();channels_and_failure();epochs_and_deadlines();commands_and_guards();
    printf("C5 MAC receive: %u checks passed; store %zu bytes; no added worker\n",checks,sizeof(wifi_mac_store_t));return 0;}
