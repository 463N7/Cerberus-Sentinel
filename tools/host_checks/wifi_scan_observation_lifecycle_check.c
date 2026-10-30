#undef NDEBUG
#include "wifi_scan_observation.h"
#include <assert.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <stdio.h>

typedef int esp_err_t;
typedef unsigned wifi_promiscuous_pkt_type_t;
typedef struct { unsigned filter_mask; } wifi_promiscuous_filter_t;
typedef struct { unsigned rx_state,sig_len,channel; int rssi,noise_floor; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[2400]; } wifi_promiscuous_pkt_t;
typedef struct { bool show_hidden; unsigned scan_type; struct {unsigned passive;} scan_time; } wifi_scan_config_t;
typedef struct { uint8_t bssid[6],ssid[33],primary; int8_t rssi; uint8_t authmode; } wifi_ap_record_t;
typedef struct { unsigned kind; } sentinel_watch_protocol_record_t;
enum { ESP_OK, ESP_FAIL, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE,
    WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC,
    WIFI_PROMIS_FILTER_MASK_MGMT=1, WIFI_PROMIS_FILTER_MASK_DATA=4, WIFI_SCAN_TYPE_PASSIVE=1,
    SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI=1 };
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
#define WIFI_PASSIVE_DWELL_MS 120U
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)

static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while (0)
static int mutex_token, task_token;
static void *scanner_mutex=&mutex_token, *scanner_task_handle=&task_token;
static bool locked, scanner_ready=true, station_reserved, scan_cancelled;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_radio_stats_payload_t radio_stats;
static cerberus_wifi_defense_status_payload_t defense_status;
static wifi_ap_record_t scan_results[32], sdk_results[32];
static int64_t scan_started_us;
static cerberus_wifi_observation_table_t scan_observations;
static cerberus_wifi_probe_table_t probe_capture;
static cerberus_wifi_relation_table_t relation_capture;
static atomic_bool relation_receiving;
static void (*probe_test_consumer_hook)(void);
static void (*probe_test_admission_hook)(void);
static int scan_observation_mux=3, defense_capture_mux=4;
static bool observation_locked, defense_locked, watch_capture_active, defense_capture_active;
static struct { struct { uint8_t generation,flags; } status; uint64_t capture_epoch; } watch_capture;
static uint64_t watch_capture_epoch;
static uint64_t watch_capture_armed_us,watch_capture_deadline_us;
static uint32_t watch_callback_inflight;
static uint8_t watch_capture_channel;
static atomic_bool remote_id_radio_reserved;
static atomic_bool csi_radio_reserved;
static atomic_bool mac_radio_reserved;
static atomic_uint mac_admission_token;
static void mac_worker_tick(void) { CHECK(false); }
static void mac_receive(const wifi_promiscuous_pkt_t *p,wifi_promiscuous_pkt_type_t type,int64_t entered,uint32_t token)
{ (void)p;(void)type;(void)entered;(void)token; }
static void csi_worker_tick(void) { CHECK(false); }
static atomic_bool packet_monitor_radio_reserved, capture_radio_reserved;
static unsigned defense_management_frames,defense_control_frames,defense_data_frames;
static unsigned defense_beacon_frames,defense_deauth_frames,defense_disassoc_frames,defense_noise_samples;
static int64_t defense_noise_sum;
static int8_t defense_strongest_rssi;
static uint64_t now_us;
static unsigned notifications, scan_calls, enable_calls, disable_calls, sdk_count;
static unsigned stop_during_enable, stop_during_scan, hook_action;
static bool filter_failure,enable_failure,scan_failure,disable_failure,records_failure,num_failure;
static wifi_promiscuous_pkt_t received;
static wifi_promiscuous_pkt_type_t received_type;
static jmp_buf worker_done;
static void wifi_promiscuous_callback(void *,wifi_promiscuous_pkt_type_t);
static void scan_observation_arm(uint8_t,bool);
static void finish_scan(esp_err_t);
static esp_err_t cerberus_wifi_scanner_start(void);
static esp_err_t cerberus_wifi_scanner_stop(void);

static void enter_critical(int *mux)
{
    if (mux==&defense_capture_mux && probe_test_admission_hook) {
        void (*hook)(void)=probe_test_admission_hook;probe_test_admission_hook=NULL;hook();
    }
    bool *held=mux==&scan_observation_mux ? &observation_locked : &defense_locked;
    CHECK(!*held); *held=true;
}
static void exit_critical(int *mux)
{
    bool *held=mux==&scan_observation_mux ? &observation_locked : &defense_locked;
    CHECK(*held); *held=false;
    if (mux==&scan_observation_mux && hook_action) {
        unsigned action=hook_action; hook_action=0;
        /* Interrupt exactly after receive captured its epoch and timestamp. */
        CHECK(!locked);
        CHECK(cerberus_wifi_scanner_stop()==ESP_OK);
        finish_scan(ESP_OK);
        if(action==2) {
            CHECK(cerberus_wifi_scanner_start()==ESP_OK);
            scan_observation_arm(scan_status.generation,true);
        }
    }
}
#define portENTER_CRITICAL(m) enter_critical(m)
#define portEXIT_CRITICAL(m) exit_critical(m)
static void xSemaphoreTake(void *mutex,unsigned timeout)
{ CHECK(mutex==&mutex_token && timeout==portMAX_DELAY && !locked); locked=true; }
static void xSemaphoreGive(void *mutex)
{ CHECK(mutex==&mutex_token && locked); locked=false; }
static void xTaskNotifyGive(void *task) { CHECK(task==&task_token); ++notifications; }
static void ulTaskNotifyTake(unsigned clear,unsigned timeout)
{
    CHECK(clear==pdTRUE && timeout==portMAX_DELAY && !locked);
    if(!notifications)longjmp(worker_done,1);
    notifications=0;
}
static int64_t esp_timer_get_time(void) { return (int64_t)now_us; }
static void vTaskDelay(unsigned delay) { (void)delay; CHECK(false); }
static bool cerberus_c5_test_running(void) { return false; }
static unsigned survey_preempts;
static esp_err_t cerberus_wifi_channel_survey_preempt(void) { ++survey_preempts; return ESP_OK; }
static bool another_radio_operation_is_active(void) { return false; }
static bool wifi_connection_is_active_locked(void) { CHECK(locked); return false; }
static void packet_monitor_run(void) { CHECK(false); }
static void wifi_capture_worker_tick(void) { CHECK(false); }
static void remote_id_worker_tick(void) { CHECK(false); }
static bool station_run(void) { return false; }
static void channel_survey_receive(const wifi_promiscuous_pkt_t *p,unsigned type) {(void)p;(void)type;}
static void packet_monitor_receive(const wifi_promiscuous_pkt_t *p,unsigned type) {(void)p;(void)type;}
static bool cerberus_wifi_capture_service_active(void) { return false; }
static void cerberus_wifi_capture_service_receive(const uint8_t *p,unsigned length,unsigned channel,int rssi,uint64_t time)
{ (void)p;(void)length;(void)channel;(void)rssi;(void)time;CHECK(false); }
static void station_receive(const wifi_promiscuous_pkt_t *p)
{(void)p;if(probe_test_consumer_hook)probe_test_consumer_hook();}
static void cerberus_c5_test_receive(const uint8_t *p,unsigned n,unsigned c) {(void)p;(void)n;(void)c;}
static bool sentinel_watch_protocol_decode_wifi(const uint8_t *p,unsigned n,sentinel_watch_protocol_record_t *r)
{(void)p;(void)n;(void)r;return false;}
static void cerberus_watch_capture_observe(void *c,const void *r,const uint8_t *id,unsigned t,int rssi,unsigned ch)
{(void)c;(void)r;(void)id;(void)t;(void)rssi;(void)ch;}
static void cerberus_watch_capture_observe_timed(void *c,const void *r,const uint8_t *id,unsigned t,int rssi,unsigned ch,uint64_t time)
{(void)time;cerberus_watch_capture_observe(c,r,id,t,rssi,ch);}
static void cerberus_watch_capture_begin(void *c,unsigned radio,uint8_t generation,uint32_t time)
{(void)c;(void)radio;(void)time;watch_capture.status.generation=generation;}
static void cerberus_watch_capture_begin_with_epoch(void *c,unsigned radio,uint8_t generation,uint32_t time,uint64_t epoch)
{cerberus_watch_capture_begin(c,radio,generation,time);watch_capture.capture_epoch=epoch;}
static void cerberus_watch_capture_finish(void *c,unsigned state,uint32_t time)
{(void)c;(void)state;(void)time;}
static uint16_t saturating_increment_u16(uint16_t n) { return n==UINT16_MAX?n:(uint16_t)(n+1); }
static void summarize_scan_locked(unsigned total,unsigned stored)
{CHECK(locked);(void)total;(void)stored;}
static uint8_t map_security(uint8_t auth) { return auth; }
static esp_err_t esp_wifi_scan_get_ap_num(uint16_t *count)
{*count=(uint16_t)sdk_count;return num_failure?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count,wifi_ap_record_t *out)
{CHECK(*count==32);*count=(uint16_t)sdk_count;memcpy(out,sdk_results,sdk_count*sizeof(*out));return records_failure?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_scan_stop(void) { return ESP_OK; }
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *filter)
{CHECK(filter->filter_mask==(WIFI_PROMIS_FILTER_MASK_MGMT|WIFI_PROMIS_FILTER_MASK_DATA));return filter_failure?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous(bool enable)
{
    if(enable) {
        ++enable_calls;
        if(stop_during_enable)CHECK(cerberus_wifi_scanner_stop()==ESP_OK);
        wifi_promiscuous_callback(&received,received_type); /* not armed yet */
        return enable_failure?ESP_FAIL:ESP_OK;
    }
    ++disable_calls;
    wifi_promiscuous_callback(&received,received_type); /* admission already closed */
    return disable_failure?ESP_FAIL:ESP_OK;
}
static esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config,bool block)
{
    CHECK(block && config->show_hidden && config->scan_type==WIFI_SCAN_TYPE_PASSIVE && config->scan_time.passive==120);
    ++scan_calls;
    if(stop_during_scan)CHECK(cerberus_wifi_scanner_stop()==ESP_OK);
    now_us+=1234;
    wifi_promiscuous_callback(&received,received_type);
    now_us+=8766;
    return scan_failure?ESP_FAIL:ESP_OK;
}

#include "wifi_scan_observation_impl.h"

static void make_frame(uint8_t channel)
{
    memset(&received,0,sizeof(received));
    received.rx_ctrl.sig_len=46; received.rx_ctrl.channel=channel;
    received.rx_ctrl.rssi=-99;
    received.payload[0]=0x80; memset(received.payload+4,255,6);
    const uint8_t id[6]={2,3,4,5,6,7};
    memcpy(received.payload+10,id,6); memcpy(received.payload+16,id,6);
    received.payload[32]=100; received.payload[34]=1;
    received.payload[36]=0;received.payload[37]=4;memcpy(received.payload+38,"test",4);
    memset(sdk_results,0,sizeof(sdk_results));sdk_count=1;
    memcpy(sdk_results[0].bssid,id,6);memcpy(sdk_results[0].ssid,"test",4);
    sdk_results[0].primary=channel;sdk_results[0].rssi=-31;sdk_results[0].authmode=3;
}
static void reset(void)
{
    CHECK(!locked && !observation_locked && !defense_locked);
    memset(&scan_status,0,sizeof(scan_status));memset(&scan_observations,0,sizeof(scan_observations));
    memset(&radio_stats,0,sizeof(radio_stats));scan_cancelled=false;
    notifications=scan_calls=enable_calls=disable_calls=0;
    stop_during_enable=stop_during_scan=hook_action=0;
    filter_failure=enable_failure=scan_failure=disable_failure=records_failure=num_failure=false;
    now_us=1000000;make_frame(6);
    received_type=WIFI_PKT_MGMT;
    memset(&watch_capture,0,sizeof(watch_capture));watch_capture_epoch=0;
    memset(&probe_capture,0,sizeof(probe_capture));probe_test_consumer_hook=NULL;
    memset(&relation_capture,0,sizeof(relation_capture));atomic_store(&relation_receiving,false);
    probe_test_admission_hook=NULL;
}
static void dispatch(void)
{if(!setjmp(worker_done))wifi_scan_task(NULL);CHECK(!locked && !observation_locked && !defense_locked);}
static cerberus_wifi_scan_result_payload_t get(void)
{
    cerberus_wifi_scan_result_payload_t result;
    CHECK(cerberus_wifi_scanner_get_result(scan_status.generation,0,&result)==ESP_OK);
    CHECK(result.rssi==-31 && result.security==3 && result.ssid_length==4 && result.index==0);
    return result;
}
int main(void)
{
    reset();atomic_store(&mac_radio_reserved,true);
    unsigned prior_preempts=survey_preempts;
    CHECK(cerberus_wifi_scanner_start()==ESP_ERR_INVALID_STATE&&!notifications&&!scan_calls&&!enable_calls);
    CHECK(survey_preempts==prior_preempts&&scan_status.state==CERBERUS_WIFI_SCAN_IDLE);
    CHECK(cerberus_wifi_scanner_stop()==ESP_ERR_INVALID_STATE&&!disable_calls);
    atomic_store(&mac_radio_reserved,false);
    reset();wifi_promiscuous_callback(NULL,WIFI_PKT_MGMT);
    wifi_promiscuous_callback(&received,WIFI_PKT_MISC);CHECK(scan_observations.count==0);
    CHECK(cerberus_wifi_scanner_start()==ESP_OK);
    CHECK(scan_observations.epoch==1 && scan_observations.phase==CERBERUS_WIFI_OBSERVATION_WAITING);
    wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);CHECK(scan_observations.count==0);
    CHECK(cerberus_wifi_scanner_start()==ESP_ERR_INVALID_STATE);dispatch();
    CHECK(scan_status.state==CERBERUS_WIFI_SCAN_COMPLETE && scan_calls==1 && enable_calls==1 && disable_calls==1);
    CHECK(scan_observations.count==1 && scan_observations.entries[0].observed_us==1001234);
    cerberus_wifi_scan_result_payload_t out=get();
    CHECK(out.observation_age_ms==9 && out.observation_source_ms==1001 && out.observation_flags==3 && out.observation_channel==6);
    now_us+=1000;out=get();CHECK(out.observation_age_ms==10);
    CHECK(cerberus_wifi_scanner_get_result(0,0,&out)==ESP_ERR_INVALID_STATE);
    CHECK(cerberus_wifi_scanner_get_result(scan_status.generation,1,&out)==ESP_ERR_INVALID_STATE);
    CHECK(cerberus_wifi_scanner_get_result(scan_status.generation,0,NULL)==ESP_ERR_INVALID_ARG);
    scan_results[0].primary=11;out=get();CHECK(out.observation_flags==0);scan_results[0].primary=6;
    scan_results[0].ssid[1]='X';out=get();CHECK(out.observation_flags==0);scan_results[0].ssid[1]='e';
    scan_results[0].ssid[6]='X';out=get();CHECK(out.observation_flags==0);scan_results[0].ssid[6]=0;
    scan_results[0].bssid[5]++;out=get();CHECK(out.observation_flags==0);scan_results[0].bssid[5]--;
    for(unsigned fault=0;fault<6;++fault) {
        reset();filter_failure=fault==0;enable_failure=fault==1;scan_failure=fault==2;
        disable_failure=fault==3;records_failure=fault==4;num_failure=fault==5;
        CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();
        if(fault<2) {CHECK(scan_status.state==CERBERUS_WIFI_SCAN_COMPLETE);out=get();CHECK(out.observation_flags==0);}
        else CHECK(scan_status.state==CERBERUS_WIFI_SCAN_ERROR);
        CHECK(scan_observations.count==0 && scan_observations.phase==CERBERUS_WIFI_OBSERVATION_FINISHED);
    }
    for(unsigned timing=0;timing<3;++timing) {
        reset();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        if(timing==0)CHECK(cerberus_wifi_scanner_stop()==ESP_OK);
        stop_during_enable=timing==1;stop_during_scan=timing==2;dispatch();
        CHECK(scan_status.state==CERBERUS_WIFI_SCAN_IDLE && scan_observations.count==0);
        CHECK(cerberus_wifi_scanner_get_result(scan_status.generation,0,&out)==ESP_ERR_INVALID_STATE);
    }
    for(unsigned invalid=0;invalid<5;++invalid) {
        reset();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        if(invalid==0)received.rx_ctrl.rx_state=1;
        if(invalid==1)received.rx_ctrl.sig_len=41;
        if(invalid==2)received.payload[37]=5; /* incomplete SSID */
        if(invalid==3)received.rx_ctrl.channel=0;
        if(invalid==4)received.payload[0]=0x08;
        dispatch();out=get();CHECK(out.observation_flags==0);
    }
    for(unsigned action=1;action<=2;++action) {
        reset();CHECK(cerberus_wifi_scanner_start()==ESP_OK);
        scan_observation_arm(scan_status.generation,true);
        hook_action=action;wifi_promiscuous_callback(&received,WIFI_PKT_MGMT);
        CHECK(scan_observations.count==0); /* old parse cannot publish after close/new epoch */
        CHECK(scan_observations.epoch==action);
    }
    reset();scan_status.generation=255;CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();
    CHECK(scan_status.generation==0 && scan_observations.epoch==1);out=get();CHECK(out.observation_flags==3);
    for(unsigned i=0;i<256;++i) {CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();}
    CHECK(scan_status.generation==0 && scan_observations.epoch==257);out=get();CHECK(out.observation_flags==3);
    reset();make_frame(177);CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();
    out=get();CHECK(out.channel==177 && out.observation_channel==177 && out.observation_flags==3);
    reset();received.payload[0]=0x50;CHECK(cerberus_wifi_scanner_start()==ESP_OK);dispatch();
    out=get();CHECK(out.observation_flags==5);
    reset();CHECK(cerberus_wifi_scanner_start()==ESP_OK);scan_observation_arm(scan_status.generation,true);
    wifi_promiscuous_callback(&received,WIFI_PKT_CTRL);CHECK(scan_observations.count==0);
    printf("C5 production observation lifecycle: %u checks; exact callback/Start/Stop/worker/getter, failure/cancel/token and generation-wrap passed.\n",checks);
    return 0;
}
