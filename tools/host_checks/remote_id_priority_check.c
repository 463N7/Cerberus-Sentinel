#undef NDEBUG
#include "cerberus_watch_capture.h"
#include "remote_id_channel_plan.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
typedef int esp_err_t;
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_ARG=0x102,ESP_ERR_INVALID_STATE=0x103,
      ESP_ERR_NOT_FOUND=0x105,ESP_ERR_NOT_SUPPORTED=0x106,ESP_ERR_TIMEOUT=0x107,ESP_ERR_WIFI_NOT_CONNECT=0x300f};
enum {WIFI_COUNTRY_POLICY_AUTO,WIFI_COUNTRY_POLICY_MANUAL,WIFI_BAND_MODE_AUTO=10,
      WIFI_BAND_2G=20,WIFI_BAND_5G=50,WIFI_SECOND_CHAN_NONE=0,WIFI_PROMIS_FILTER_MASK_MGMT=1};
typedef unsigned wifi_band_mode_t,wifi_second_chan_t;
typedef struct {char cc[3];uint8_t schan,nchan;int8_t max_tx_power;int policy;uint32_t wifi_5g_channel_mask;} wifi_country_t;
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
typedef unsigned wifi_promiscuous_pkt_type_t;
typedef struct {unsigned rx_state,sig_len,channel;int rssi,noise_floor;} wifi_pkt_rx_ctrl_t;
typedef struct {wifi_pkt_rx_ctrl_t rx_ctrl;uint8_t payload[2400];} wifi_promiscuous_pkt_t;
static void mac_receive(const wifi_promiscuous_pkt_t *p,wifi_promiscuous_pkt_type_t type,int64_t entered,uint32_t token)
{ (void)p;(void)type;(void)entered;(void)token; }
enum {WIFI_PKT_MGMT,WIFI_PKT_CTRL,WIFI_PKT_DATA,WIFI_PKT_MISC};
#define portMAX_DELAY 0
#define pdMS_TO_TICKS(x) (x)
static int mutex,mux,task,defense_task;
static void *scanner_mutex=&mutex,*scanner_task_handle=&task,*defense_task_handle=&defense_task;
#define defense_capture_mux mux
static bool locked,critical,scanner_ready=true,station_reserved,foreign,test_running,connected,promiscuous;
static atomic_bool remote_id_radio_reserved,capture_radio_reserved,packet_monitor_radio_reserved,channel_survey_radio_reserved;
static atomic_bool csi_radio_reserved, mac_radio_reserved;
static atomic_uint mac_admission_token;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_defense_status_payload_t defense_status;
static cerberus_wifi_connection_status_payload_t connection_status;
static uint8_t staged_password[64],staged_password_length;
static bool staged_password_received[64];
static cerberus_wifi_defense_result_payload_t defense_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static struct {uint8_t primary;} scan_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static uint8_t defense_channels[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static bool defense_cancelled,reserve_before_survey_lock;
static cerberus_wifi_channel_survey_status_payload_t channel_survey_status;
static cerberus_watch_capture_t watch_capture;
static cerberus_wifi_probe_table_t probe_capture;
static cerberus_wifi_relation_table_t relation_capture;
static atomic_bool relation_receiving;
static uint64_t watch_capture_epoch,watch_capture_armed_us,watch_capture_deadline_us;
static uint32_t watch_callback_inflight;
static uint8_t watch_capture_channel;
static bool watch_capture_active,defense_capture_active;
static unsigned defense_management_frames,defense_control_frames,defense_data_frames;
static unsigned defense_beacon_frames,defense_deauth_frames,defense_disassoc_frames,defense_noise_samples;
static int64_t defense_noise_sum,clock_us;
static int8_t defense_strongest_rssi;
static unsigned notified,channel_sets,enables,disables,country_reads,failed_channel,fail_restore,fail_disable;
static unsigned tune_delay_ms,enable_delay_ms,stop_in_callback,survey_preempts,disconnects;
static uint8_t receive_channels[64];
static bool drain_on_delay;
static wifi_band_mode_t mode=2,band=WIFI_BAND_2G;
static uint8_t channel=11;
static wifi_second_chan_t second=1;
static wifi_promiscuous_filter_t filter={99};
static wifi_country_t country={{'U','S',' '},1,11,20,WIFI_COUNTRY_POLICY_MANUAL,(1U<<21)|(1U<<28)};
static int64_t esp_timer_get_time(void){return clock_us;}
static void xSemaphoreTake(void *p,unsigned ticks){(void)ticks;CHECK(p==scanner_mutex&&!locked);if(reserve_before_survey_lock){reserve_before_survey_lock=false;atomic_store(&remote_id_radio_reserved,true);}locked=true;}
static void xSemaphoreGive(void *p){CHECK(p==scanner_mutex&&locked);locked=false;}
static void enter(int *p){CHECK(p==&mux&&!critical);critical=true;}
static void leave(int *p){CHECK(p==&mux&&critical);critical=false;}
#define portENTER_CRITICAL(m) enter(m)
#define portEXIT_CRITICAL(m) leave(m)
static void vTaskDelay(unsigned ms){clock_us+=(int64_t)ms*1000;if(drain_on_delay)watch_callback_inflight=0;}
static void xTaskNotifyGive(void *p){CHECK(p==scanner_task_handle||p==defense_task_handle);++notified;}
static bool cerberus_c5_test_running(void){return test_running;}
static bool another_radio_operation_is_active(void){return foreign||atomic_load(&remote_id_radio_reserved);}
static bool wifi_connection_is_active_locked(void){return connected;}
static esp_err_t esp_wifi_disconnect(void){++disconnects;return ESP_OK;}
static esp_err_t cerberus_wifi_channel_survey_preempt(void){++survey_preempts;return ESP_OK;}
static bool cerberus_ieee802154_scanner_is_running(void){return false;}
static void cerberus_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *out){memset(out,0,sizeof(*out));}
static void cerberus_ble_connection_get_status(cerberus_ble_connection_status_payload_t *out){memset(out,0,sizeof(*out));}
static esp_err_t esp_wifi_get_promiscuous(bool *out){*out=promiscuous;return ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous(bool value)
{CHECK(!watch_capture_active);if(!value&&fail_disable)return ESP_FAIL;promiscuous=value;if(value){clock_us+=(int64_t)enable_delay_ms*1000;CHECK(enables<64);receive_channels[enables++]=channel;}else ++disables;return ESP_OK;}
static esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t *out){*out=mode;return ESP_OK;}
static esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t value){CHECK(!promiscuous&&!watch_callback_inflight);if(value==2&&fail_restore)return ESP_FAIL;mode=value;return ESP_OK;}
static esp_err_t esp_wifi_get_channel(uint8_t *a,wifi_second_chan_t *b){*a=channel;*b=second;return ESP_OK;}
static esp_err_t esp_wifi_set_band(unsigned value){CHECK(!promiscuous&&!watch_callback_inflight);band=value;return ESP_OK;}
static esp_err_t esp_wifi_set_channel(uint8_t value,wifi_second_chan_t sec)
{
    CHECK(!promiscuous&&!watch_callback_inflight&&value);++channel_sets;
    clock_us+=(int64_t)tune_delay_ms*1000;
    if(value==failed_channel)return ESP_FAIL;
    if(value==3)return ESP_ERR_INVALID_ARG; /* SDK rejection during discovery. */
    channel=value;second=sec;return ESP_OK;
}
static esp_err_t esp_wifi_get_promiscuous_filter(wifi_promiscuous_filter_t *out){*out=filter;return ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *value){filter=*value;return ESP_OK;}
static esp_err_t esp_wifi_get_country(wifi_country_t *out){++country_reads;*out=country;return ESP_OK;}
void cerberus_wifi_remote_id_stop(uint64_t id,cerberus_remote_id_status_payload_t *out);
static void remote_id_worker_tick(void);
static void scan_observation_receive(const wifi_promiscuous_pkt_t *p,unsigned t)
{
    (void)p;(void)t;
    if(stop_in_callback){
        unsigned id=stop_in_callback;stop_in_callback=0;
        cerberus_remote_id_status_payload_t result;
        CHECK(watch_callback_inflight==1);
        cerberus_wifi_remote_id_stop(id,&result);remote_id_worker_tick();
        CHECK(atomic_load(&remote_id_radio_reserved)&&watch_callback_inflight==1&&!watch_capture_active);
    }
}
static void channel_survey_receive(const wifi_promiscuous_pkt_t *p,unsigned t){(void)p;(void)t;}
static void packet_monitor_receive(const wifi_promiscuous_pkt_t *p,unsigned t){(void)p;(void)t;}
static bool cerberus_wifi_capture_service_active(void){return false;}
static void cerberus_wifi_capture_service_receive(const uint8_t *p,unsigned n,unsigned c,int r,uint64_t t){(void)p;(void)n;(void)c;(void)r;(void)t;CHECK(false);}
static void station_receive(const wifi_promiscuous_pkt_t *p){(void)p;}
static void cerberus_c5_test_receive(const uint8_t *p,unsigned n,unsigned c){(void)p;(void)n;(void)c;}
#include "wifi_remote_id_service.h"
#include "remote_id_priority_callback.h"
#include "remote_id_priority_survey_start.h"
#include "remote_id_priority_disconnect.h"
static cerberus_remote_id_status_payload_t reply;
static unsigned replies;
static void prepare_frame_response(cerberus_spi_command_t c,uint8_t seq,const void *p,size_t size)
{CHECK(c>=90&&c<=93&&seq==42&&size==22);memcpy(&reply,p,size);++replies;}
static void dispatch(const cerberus_spi_frame_t *request)
{switch(request->command){
#include "remote_id_priority_dispatch.h"
default:CHECK(false);}}
static cerberus_remote_id_start_request_t request(uint64_t id)
{return (cerberus_remote_id_start_request_t){.job_id=id,.duration_ms=1000,.lease_ms=3000,.dwell_ms=250};}
static cerberus_remote_id_status_payload_t status(uint64_t id)
{cerberus_remote_id_status_payload_t s;cerberus_wifi_remote_id_status(id,&s);CHECK(!locked&&!critical);return s;}
static void tick(unsigned ms){clock_us+=(int64_t)ms*1000;remote_id_worker_tick();CHECK(!locked&&!critical);}
static void until_running(void)
{for(unsigned i=0;status(0).state==CERBERUS_REMOTE_ID_STATE_STARTING&&i<70;++i)tick(20);CHECK(status(0).state==CERBERUS_REMOTE_ID_STATE_RUNNING);}
static void start(uint64_t id)
{cerberus_remote_id_start_request_t r=request(id);cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.job_id==id&&reply.state==CERBERUS_REMOTE_ID_STATE_STARTING&&reply.flags==1);until_running();}
static void receive_from(const uint8_t addr[6])
{
    wifi_promiscuous_pkt_t packet={0};packet.rx_ctrl=(wifi_pkt_rx_ctrl_t){.sig_len=77,.channel=channel,.rssi=-67};
    uint8_t *p=packet.payload;p[0]=0x80;memset(p+4,255,6);
    memcpy(p+10,addr,6);memcpy(p+16,addr,6);p[32]=100;p[34]=1;
    const uint8_t v[]={221,33,0xfa,0x0b,0xbc,0x0d,7,0xf2,25,1,2,0x10,'T','E','S','T'};memcpy(p+38,v,sizeof(v));
    wifi_promiscuous_callback(&packet,WIFI_PKT_MGMT);CHECK(!watch_callback_inflight&&!critical);
}
static const uint8_t source_address[]={2,4,6,8,10,12};
static void receive(void){receive_from(source_address);}
static void stop(uint64_t id)
{cerberus_wifi_remote_id_stop(id,&reply);tick(0);CHECK(!atomic_load(&remote_id_radio_reserved));}
static void reset(void)
{
    CHECK(!locked&&!critical);scanner_mutex=&mutex;scanner_task_handle=&task;scanner_ready=true;
    station_reserved=foreign=test_running=connected=promiscuous=drain_on_delay=false;
    atomic_store(&remote_id_radio_reserved,false);atomic_store(&capture_radio_reserved,false);
    atomic_store(&packet_monitor_radio_reserved,false);atomic_store(&channel_survey_radio_reserved,false);
    memset(&scan_status,0,sizeof(scan_status));memset(&defense_status,0,sizeof(defense_status));
    memset(&channel_survey_status,0,sizeof(channel_survey_status));memset(&watch_capture,0,sizeof(watch_capture));
    memset(&remote_id_request,0,sizeof(remote_id_request));memset(&remote_id_status,0,sizeof(remote_id_status));
    memset(&remote_id_plan,0,sizeof(remote_id_plan));remote_id_cache_valid=remote_id_saved=remote_id_disable_pending=remote_id_plan_ready=false;
    remote_id_channel_count=remote_id_next_background=0;remote_id_epoch=watch_capture_epoch=watch_capture_armed_us=0;
    watch_capture_deadline_us=remote_id_follow_deadline_us=0U;
    remote_id_follow_bound=remote_id_follow_candidate=false;
    watch_callback_inflight=0;watch_capture_channel=0;watch_capture_active=false;
    clock_us=100000;notified=channel_sets=enables=disables=country_reads=failed_channel=fail_restore=fail_disable=0;
    tune_delay_ms=enable_delay_ms=stop_in_callback=survey_preempts=disconnects=0;reserve_before_survey_lock=false;
    memset(&connection_status,0,sizeof(connection_status));memset(receive_channels,0,sizeof(receive_channels));
    mode=2;band=WIFI_BAND_2G;channel=11;second=1;filter.filter_mask=99;
    country=(wifi_country_t){{'U','S',' '},1,11,20,WIFI_COUNTRY_POLICY_MANUAL,(1U<<21)|(1U<<28)};
}
static void completed_identity_cache_and_data(void)
{
    reset();CHECK(status(0).state==0&&!status(0).job_id);watch_capture.status.generation=254;
    start(UINT64_C(0x100000123));CHECK(reply.generation==255);receive();uint64_t epoch=remote_id_epoch;
    CHECK(watch_capture.status.stored_count==1&&watch_capture.qualifying_valid==1&&watch_capture.records[0].channel==6);
    CHECK(remote_id_channel_count==12&&remote_id_cache_valid); /* 11+149+177 minus rejected3 */
    for(unsigned i=0;i<4;++i)tick(250);
    const uint8_t expected[]={6,149,6,1};CHECK(enables==4&&!memcmp(receive_channels,expected,sizeof(expected)));
    cerberus_remote_id_status_payload_t done=status(0);CHECK(done.state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&done.flags==4&&done.elapsed_ms==1000);
    CHECK(mode==2&&channel==11&&second==1&&filter.filter_mask==99&&!promiscuous);
    cerberus_remote_id_start_request_t r=request(done.job_id);unsigned calls=channel_sets,notifications=notified;
    cerberus_wifi_remote_id_start(&r,&reply);CHECK(!memcmp(&reply,&done,sizeof(done))&&channel_sets==calls&&notified==notifications);
    cerberus_wifi_remote_id_stop(done.job_id,&reply);CHECK(reply.state==done.state&&reply.flags==4);
    ++r.dwell_ms;cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==7&&reply.error_code==ESP_ERR_INVALID_ARG);
    r=request(done.job_id-1);cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==7&&status(0).job_id==done.job_id);
    scan_status.state=CERBERUS_WIFI_SCAN_RUNNING;CHECK(!status(0).flags);scan_status.state=0;CHECK(status(0).flags==4);
    unsigned previous_calls=channel_sets;uint8_t next=remote_id_next_background;
    start(done.job_id+1);CHECK(channel_sets==previous_calls+1&&remote_id_epoch==epoch+1&&remote_id_status.generation==0);
    CHECK(cerberus_remote_id_channel_plan_next_background(&remote_id_plan)==next);
    stop(done.job_id+1);CHECK(status(0).state==5&&!status(0).flags&&!watch_capture.status.stored_count);
    /* Ordinary capture advanced Watch generation between dedicated jobs. */
    watch_capture.status.generation=103;start(done.job_id+2);CHECK(status(done.job_id+2).generation==104);
    stop(done.job_id+2);
}
static void rejection_and_dispatch(void)
{
    for(unsigned defect=0;defect<13;++defect){
        reset();cerberus_remote_id_start_request_t r=request(1);
        if(defect==0)r.job_id=0;if(defect==1)r.flags=1;if(defect==2)r.reserved[6]=1;
        if(defect==3)r.duration_ms=999;if(defect==4)r.duration_ms=30001;if(defect==5)r.lease_ms=999;
        if(defect==6)r.lease_ms=5001;if(defect==7)r.dwell_ms=99;if(defect==8)r.dwell_ms=1001;
        if(defect==9)foreign=true;if(defect==10)promiscuous=true;if(defect==11)channel_survey_status.state=CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING;
        if(defect==12)watch_capture_epoch=UINT64_MAX;
        cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==7&&!reply.flags&&!remote_id_request.job_id&&!notified&&!channel_sets);
    }
    reset();cerberus_spi_frame_t frame={.command=90,.sequence=42,.payload_length=22};
    cerberus_remote_id_start_request_t r=request(44);memcpy(frame.payload,&r,sizeof(r));dispatch(&frame);
    CHECK(reply.state==1&&reply.job_id==44);unsigned count=replies;
    --frame.payload_length;dispatch(&frame);CHECK(replies==count);
    cerberus_remote_id_job_request_t q={45};memcpy(frame.payload,&q,sizeof(q));frame.payload_length=8;frame.command=92;
    dispatch(&frame);CHECK(reply.state==7&&reply.job_id==45&&atomic_load(&remote_id_radio_reserved));
    q.job_id=44;memcpy(frame.payload,&q,sizeof(q));frame.command=93;dispatch(&frame);CHECK(reply.state==1);
    frame.command=91;dispatch(&frame);CHECK(reply.state==1);frame.command=92;dispatch(&frame);CHECK(reply.state==4);tick(0);
    CHECK(status(44).state==5&&!remote_id_saved&&!channel_sets);
}
static void lease_failure_restore_and_drain(void)
{
    reset();start(1);uint64_t original_start=remote_id_window_started_us;tick(500);
    cerberus_wifi_remote_id_renew(1,&reply);CHECK(reply.state==2&&remote_id_window_started_us==original_start);tick(500);
    CHECK(status(1).state==3&&status(1).elapsed_ms==1000);
    uint64_t lease_anchor=remote_id_lease_started_us;unsigned starts=enables;
    cerberus_wifi_remote_id_renew(1,&reply);CHECK(reply.state==3&&reply.flags==4&&
        remote_id_lease_started_us==lease_anchor&&enables==starts);
    reset();cerberus_remote_id_start_request_t r=request(1);r.duration_ms=30000;r.lease_ms=1000;
    cerberus_wifi_remote_id_start(&r,&reply);until_running();tick(1000);CHECK(status(1).state==6&&!status(1).flags&&!promiscuous);
    reset();start(1);clock_us+=3000000;cerberus_wifi_remote_id_renew(1,&reply);
    CHECK(reply.state==6&&(reply.flags&1));tick(0);CHECK(!status(1).flags);
    reset();start(1);fail_disable=1;cerberus_wifi_remote_id_stop(1,&reply);tick(0);
    CHECK(status(1).state==6&&(status(1).flags&1)&&promiscuous&&!watch_capture_active);
    r=request(2);cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==7&&status(0).job_id==1);
    fail_disable=0;stop(1);CHECK(status(1).state==6&&!promiscuous&&mode==2);
    reset();start(1);fail_restore=1;cerberus_wifi_remote_id_stop(1,&reply);tick(0);CHECK(status(1).flags&1);
    fail_restore=0;stop(1);CHECK(status(1).state==6&&filter.filter_mask==99);
    reset();start(1);watch_callback_inflight=1;cerberus_wifi_remote_id_stop(1,&reply);tick(0);CHECK(status(1).flags&1);
    drain_on_delay=true;stop(1);CHECK(!watch_callback_inflight&&status(1).state==6);
    reset();start(1);failed_channel=149;tick(250);CHECK(status(1).state==6&&!status(1).flags&&!remote_id_cache_valid);
    reset();failed_channel=1;cerberus_wifi_remote_id_start(&r,&reply);tick(20);
    CHECK(status(2).state==6&&!status(2).flags&&!remote_id_cache_valid);
    reset();start(1);tune_delay_ms=1000;unsigned enabled=enables;tick(250);
    CHECK(status(1).state==3&&!status(1).channel&&enables==enabled); /* Budget expired during RX-off tune. */
    reset();start(1);tune_delay_ms=4000;enabled=enables;tick(250);
    CHECK(status(1).state==6&&!status(1).flags&&enables==enabled); /* Lease cannot renew itself during SDK call. */
    reset();start(1);enable_delay_ms=1000;tick(250);
    CHECK(status(1).state==3&&!status(1).channel&&!watch_capture_active&&!promiscuous);
    CHECK(!watch_capture.status.reports); /* Late SDK enable never opens the admission gate. */
    reset();start(1);enable_delay_ms=4000;tick(250);
    CHECK(status(1).state==6&&!status(1).flags&&!watch_capture_active&&!promiscuous);
    CHECK(status(1).error_code==ESP_ERR_TIMEOUT&&!watch_capture.status.reports);
    reset();start(1);stop_in_callback=1;receive();
    CHECK(!watch_callback_inflight&&!watch_capture.status.stored_count&&(status(1).flags&1));
    stop(1);CHECK(status(1).state==6&&!promiscuous);
}
static void defense_survey_ownership(void)
{
    reset();scan_status.state=CERBERUS_WIFI_SCAN_COMPLETE;scan_status.stored_count=1;scan_results[0].primary=6;
    start(1);unsigned calls=channel_sets,notifications=notified;
    CHECK(cerberus_wifi_defense_survey_start()==ESP_ERR_INVALID_STATE);
    CHECK(!survey_preempts&&channel_sets==calls&&notified==notifications&&defense_status.state==0);
    CHECK(status(1).state==CERBERUS_REMOTE_ID_STATE_RUNNING&&watch_capture_active);
    stop(1);
    /* Reservation appearing between optimistic check and mutex acquisition is rejected too. */
    reserve_before_survey_lock=true;
    CHECK(cerberus_wifi_defense_survey_start()==ESP_ERR_INVALID_STATE);
    CHECK(survey_preempts==1&&defense_status.state==0&&notified==notifications+1);
    atomic_store(&remote_id_radio_reserved,false);
    CHECK(cerberus_wifi_defense_survey_start()==ESP_OK);
    CHECK(defense_status.state==CERBERUS_WIFI_DEFENSE_RUNNING&&defense_status.current_channel==6);
}
static void disconnect_ownership(void)
{
    reset();start(1);
    connection_status=(cerberus_wifi_connection_status_payload_t){.state=CERBERUS_WIFI_CONNECTION_IDLE,.error_code=123};
    staged_password_length=17;memset(staged_password,0xa5,sizeof(staged_password));
    memset(staged_password_received,1,sizeof(staged_password_received));
    cerberus_wifi_connection_status_payload_t saved=connection_status;
    CHECK(cerberus_wifi_connection_disconnect()==ESP_ERR_INVALID_STATE);
    CHECK(!disconnects&&!memcmp(&saved,&connection_status,sizeof(saved))&&staged_password_length==17);
    for(unsigned i=0;i<64;++i)CHECK(staged_password[i]==0xa5&&staged_password_received[i]);
    CHECK(status(1).state==CERBERUS_REMOTE_ID_STATE_RUNNING&&watch_capture_active);
    stop(1);CHECK(cerberus_wifi_connection_disconnect()==ESP_OK&&disconnects==1);
    CHECK(connection_status.state==CERBERUS_WIFI_CONNECTION_DISCONNECTING&&!staged_password_length);
}
static void country_and_admission(void)
{
    reset();start(1);uint64_t armed=watch_capture_armed_us;clock_us=(int64_t)armed-1;receive();CHECK(!watch_capture.status.reports);
    clock_us=(int64_t)armed;receive();CHECK(watch_capture.status.reports==1);stop(1);
    country.nchan=6;country.wifi_5g_channel_mask=1U<<28;unsigned before=channel_sets;start(2);
    CHECK(remote_id_channel_count==6&&channel_sets>before+1); /* 1..6 minus3, plus177 */
    bool has177=false,has149=false;for(unsigned i=0;i<remote_id_channel_count;++i){has177|=remote_id_channels[i]==177;has149|=remote_id_channels[i]==149;}
    CHECK(has177&&!has149);stop(2);
    reset();watch_capture.status.generation=99;watch_capture.capture_epoch=77;
    cerberus_remote_id_start_request_t r=request(UINT64_MAX);cerberus_wifi_remote_id_start(&r,&reply);
    cerberus_wifi_remote_id_stop(UINT64_MAX,&reply);tick(0);CHECK(watch_capture.capture_epoch==77&&watch_capture.status.generation==99);
    r=request(1);cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==7&&status(0).job_id==UINT64_MAX);
}
static uint64_t seed_follow_source(void)
{
    reset();start(1);tick(250);CHECK(channel==149);receive();
    uint64_t source=watch_capture.qualifying_us[0];tick(750);
    CHECK(status(1).state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&status(1).flags==CERBERUS_REMOTE_ID_FLAG_DATA_READY);
    return source;
}
static cerberus_remote_id_start_request_t follow_request(uint64_t id)
{
    cerberus_remote_id_start_request_t r=request(id);r.duration_ms=8000;
    r.flags=CERBERUS_REMOTE_ID_REQUEST_FOLLOW_BEACON;r.follow_channel=149;
    memcpy(r.transmitter,source_address,6);return r;
}
static void follow_identity_plan_and_deadline(void)
{
    uint64_t source=seed_follow_source();cerberus_remote_id_start_request_t r=follow_request(2);
    cerberus_wifi_remote_id_start(&r,&reply);
    CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_STARTING&&reply.flags==(CERBERUS_REMOTE_ID_FLAG_OWNED|CERBERUS_REMOTE_ID_FLAG_FOLLOW_REQUESTED));
    CHECK(remote_id_follow_deadline_us==source+UINT64_C(30000000));
    unsigned first=enables;until_running();
    CHECK((status(2).flags&24U)==24U&&watch_capture_deadline_us==remote_id_window_started_us+UINT64_C(8000000));
    CHECK(!watch_capture.status.stored_count&&!watch_capture.qualifying_valid&&watch_capture.protected_transport==SENTINEL_WATCH_TRANSPORT_WIFI_BEACON);
    for(unsigned i=0;i<7;++i)tick(250);
    const uint8_t expected[]={6,149,6,149,6,149,6,1};
    CHECK(enables==first+8&&!memcmp(receive_channels+first,expected,sizeof(expected)));
    uint64_t proof=remote_id_follow_deadline_us;cerberus_wifi_remote_id_start(&r,&reply);
    CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_RUNNING&&remote_id_follow_deadline_us==proof&&enables==first+8);
    ++r.follow_channel;cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_REJECTED);
    stop(2);CHECK((status(2).flags&24U)==24U); /* Retained profile, no claim of active RX. */

    source=seed_follow_source();uint64_t deadline=source+UINT64_C(30000000);
    clock_us=(int64_t)deadline-1500000;r=follow_request(2);cerberus_wifi_remote_id_start(&r,&reply);until_running();
    CHECK(remote_id_plan.budget_remaining_ms<1500&&remote_id_plan.budget_remaining_ms>0);
    clock_us=(int64_t)deadline-1;receive();CHECK(watch_capture.status.reports==1);
    CHECK(remote_id_follow_deadline_us==deadline); /* A newer report cannot extend this job. */
    clock_us=(int64_t)deadline;receive();CHECK(watch_capture.status.reports==1&&watch_callback_inflight==0);
    /* No worker tick has happened yet: callback itself enforces exact expiry. */
    cerberus_wifi_remote_id_renew(2,&reply);CHECK(remote_id_follow_deadline_us==deadline);
    tick(0);CHECK(status(2).state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&!promiscuous&&(status(2).flags&24U)==24U);
    r=follow_request(3);r.follow_channel=6;cerberus_wifi_remote_id_start(&r,&reply);until_running();
    CHECK(remote_id_follow_bound&&remote_id_follow_deadline_us==deadline-1+UINT64_C(30000000));stop(3);
    seed_follow_source();r=follow_request(2);r.duration_ms=1000;
    cerberus_wifi_remote_id_start(&r,&reply);until_running();
    clock_us=(int64_t)watch_capture_deadline_us-1;receive();CHECK(watch_capture.status.reports==1);
    ++clock_us;receive();CHECK(watch_capture.status.reports==1);tick(0);
    CHECK(status(2).state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&!promiscuous);
}
static void follow_proof_failures_and_explicit_search(void)
{
    for(unsigned defect=0;defect<13;++defect){
        uint64_t source=seed_follow_source();cerberus_remote_id_start_request_t r=follow_request(2);
        switch(defect){
        case 0:watch_capture.capture_epoch++;break;
        case 1:watch_capture.status.generation++;break;
        case 2:watch_capture.status.state=CERBERUS_WATCH_IDLE;break;
        case 3:watch_capture.records[0].address[5]++;break;
        case 4:watch_capture.records[0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;break;
        case 5:watch_capture.records[0].channel=6;break;
        case 6:watch_capture.records[0].address_type=1;break;
        case 7:watch_capture.records[0].kind=SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;break;
        case 8:watch_capture.qualifying_valid=0;break;
        case 9:watch_capture.qualifying_us[0]=(uint64_t)clock_us+1U;break;
        case 10:clock_us=(int64_t)(source+UINT64_C(30000000));break;
        case 11:watch_capture.status.stored_count=0;break;
        default:r.follow_channel=0;break;
        }
        cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_STARTING);
        until_running();CHECK(!remote_id_follow_bound&&!remote_id_follow_deadline_us&&!watch_capture_deadline_us);
        CHECK((status(2).flags&24U)==8U&&watch_capture.protected_transport==SENTINEL_WATCH_TRANSPORT_WIFI_BEACON);
        CHECK(!watch_capture.status.stored_count);stop(2);
    }
    /* Timestamp zero is real evidence when the producer's validity bit proves it. */
    seed_follow_source();watch_capture.qualifying_us[0]=0;clock_us=1000000;
    cerberus_remote_id_start_request_t r=follow_request(2);cerberus_wifi_remote_id_start(&r,&reply);until_running();
    CHECK(remote_id_follow_bound&&remote_id_follow_deadline_us==UINT64_C(30000000));stop(2);
    /* Matching NAN is separate from Beacon, even at the same transmitter. */
    seed_follow_source();watch_capture.records[0].transport=SENTINEL_WATCH_TRANSPORT_WIFI_NAN;
    r=follow_request(2);r.flags=CERBERUS_REMOTE_ID_REQUEST_FOLLOW_NAN;cerberus_wifi_remote_id_start(&r,&reply);until_running();
    CHECK(remote_id_follow_bound&&watch_capture.protected_transport==SENTINEL_WATCH_TRANSPORT_WIFI_NAN);stop(2);
    /* Country changed after the source was seen: evidence cannot legalize a channel. */
    seed_follow_source();country.wifi_5g_channel_mask=1U<<28;r=follow_request(2);
    cerberus_wifi_remote_id_start(&r,&reply);until_running();CHECK(!remote_id_follow_bound&&(status(2).flags&24U)==8U);stop(2);
    for(unsigned defect=0;defect<5;++defect){
        reset();r=follow_request(1);
        if(defect==0)r.flags=3;if(defect==1)r.transmitter[0]=3;if(defect==2)memset(r.transmitter,0,6);
        if(defect==3)r.follow_channel=150;if(defect==4)r.flags=4;
        cerberus_wifi_remote_id_start(&r,&reply);CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_REJECTED&&!notified&&!channel_sets);
    }
}
static void follow_setup_and_sdk_expiry(void)
{
    for(unsigned stage=0;stage<3;++stage){
        uint64_t deadline=seed_follow_source()+UINT64_C(30000000);
        cerberus_remote_id_start_request_t r=follow_request(2);
        clock_us=(int64_t)deadline-1500;cerberus_wifi_remote_id_start(&r,&reply);
        if(stage==1)tune_delay_ms=2;
        if(stage==2)enable_delay_ms=2;
        tick(stage==0?2:0);CHECK(!remote_id_follow_bound&&(stage==0||!watch_capture_active));
        tune_delay_ms=enable_delay_ms=0;tick(0);
        CHECK(status(2).state==CERBERUS_REMOTE_ID_STATE_RUNNING&&(status(2).flags&24U)==8U&&watch_capture_active);
        CHECK(!watch_capture_deadline_us);stop(2);
    }
    for(unsigned stage=0;stage<2;++stage){
        uint64_t deadline=seed_follow_source()+UINT64_C(30000000);clock_us=(int64_t)deadline-500000;
        cerberus_remote_id_start_request_t r=follow_request(2);cerberus_wifi_remote_id_start(&r,&reply);until_running();
        CHECK(remote_id_follow_bound);if(stage)enable_delay_ms=300;else tune_delay_ms=300;
        tick(250);CHECK(status(2).state==CERBERUS_REMOTE_ID_STATE_COMPLETE&&!watch_capture_active&&!promiscuous);
        CHECK((status(2).flags&24U)==24U&&!watch_capture.status.reports);
    }
}
static void follow_actual_report_capacity(void)
{
    reset();cerberus_remote_id_start_request_t r=follow_request(1);r.follow_channel=0;
    cerberus_wifi_remote_id_start(&r,&reply);until_running();CHECK(!watch_capture.status.stored_count);
    for(unsigned i=0;i<12;++i){uint8_t addr[]={2,9,8,7,6,(uint8_t)i};receive_from(addr);}
    CHECK(watch_capture.status.stored_count==12&&!watch_capture.status.dropped_records);
    clock_us+=100;receive();CHECK(watch_capture.status.stored_count==12&&watch_capture.status.reports==13);
    CHECK(watch_capture.status.dropped_records==1&&watch_capture.status.flags==CERBERUS_WATCH_CAPTURE_OVERFLOW);
    CHECK(!memcmp(watch_capture.records[11].address,source_address,6)&&watch_capture.qualifying_us[11]==(uint64_t)clock_us);
    stop(1);
}
int main(void)
{
    reset();atomic_store(&csi_radio_reserved,true);
    cerberus_remote_id_start_request_t blocked=request(1);
    cerberus_wifi_remote_id_start(&blocked,&reply);
    CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_REJECTED&&!notified&&!channel_sets);
    CHECK(cerberus_wifi_defense_survey_start()==ESP_ERR_INVALID_STATE&&!survey_preempts&&!notified);
    memset(staged_password,0x6d,sizeof(staged_password));
    CHECK(cerberus_wifi_connection_disconnect()==ESP_ERR_INVALID_STATE&&!disconnects);
    for(unsigned i=0;i<sizeof(staged_password);++i)CHECK(staged_password[i]==0x6d);
    atomic_store(&csi_radio_reserved,false);
    atomic_store(&mac_radio_reserved,true);
    cerberus_wifi_remote_id_start(&blocked,&reply);
    CHECK(reply.state==CERBERUS_REMOTE_ID_STATE_REJECTED&&!notified&&!channel_sets);
    CHECK(cerberus_wifi_defense_survey_start()==ESP_ERR_INVALID_STATE&&!survey_preempts&&!notified);
    CHECK(cerberus_wifi_connection_disconnect()==ESP_ERR_INVALID_STATE&&!disconnects);
    for(unsigned i=0;i<sizeof(staged_password);++i)CHECK(staged_password[i]==0x6d);
    atomic_store(&mac_radio_reserved,false);
    completed_identity_cache_and_data();rejection_and_dispatch();lease_failure_restore_and_drain();country_and_admission();defense_survey_ownership();disconnect_ownership();
    follow_identity_plan_and_deadline();follow_proof_failures_and_explicit_search();follow_setup_and_sdk_expiry();follow_actual_report_capacity();
    printf("Production C5 Remote ID priority job: %u checks; exact ID/idempotent dispatch, SDK plan/cache, follow source proof/deadlines/search, protected actual-report admission, window/lease, shared epoch, data readiness and retained cleanup passed.\n",checks);return 0;
}
