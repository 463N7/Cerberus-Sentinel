#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include "esp_err.h"
#include "cerberus_protocol.h"
#define ESP_ERR_NOT_FOUND 0x105
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2
#define WIFI_BAND_2G 1
#define WIFI_BAND_5G 2
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_BAND_MODE_AUTO 3
typedef enum {WIFI_AUTH_WPA3_PSK=5,WIFI_AUTH_WPA3_ENTERPRISE=8,WIFI_AUTH_WPA3_ENT_192=9,WIFI_AUTH_WAPI_PSK=10,
    WIFI_AUTH_WPA2_WPA3_PSK=11,WIFI_AUTH_WPA2_WPA3_ENTERPRISE=12} wifi_auth_mode_t;
typedef struct {uint16_t sig_len;uint8_t rx_state,channel;int8_t rssi;} wifi_pkt_rx_ctrl_t;
typedef struct {wifi_pkt_rx_ctrl_t rx_ctrl;uint8_t payload[64];} wifi_promiscuous_pkt_t;
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
static unsigned scanner_mutex=1,scanner_task_handle=1,defense_capture_mux=1;
static bool scanner_ready=true;
static atomic_bool csi_radio_reserved, mac_radio_reserved;
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_defense_status_payload_t defense_status;
static struct {uint8_t bssid[6],primary,ssid[33];wifi_auth_mode_t authmode;} scan_results[2];
static unsigned locked,critical,notified,stage_failure,enabled,disabled,restored;
static uint64_t ticks;
static size_t free_bytes=128*1024;
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
static size_t heap_caps_get_free_size(unsigned caps){assert(caps==3);return free_bytes;}
static bool other_radio,connection,test_running,renew_lease,cancel_early,overlap,inject_packets;
static void take(unsigned lock,unsigned wait){assert(lock&&wait==portMAX_DELAY&&!locked);++locked;}
static void give(unsigned lock){assert(lock&&locked==1);--locked;}
#define xSemaphoreTake take
#define xSemaphoreGive give
#define portENTER_CRITICAL(p) do{assert(*(p)&&!critical);++critical;}while(0)
#define portEXIT_CRITICAL(p) do{assert(*(p)&&critical==1);--critical;}while(0)
static int64_t esp_timer_get_time(void){return (int64_t)ticks*1000;}
static bool cerberus_c5_test_running(void){return test_running;}
static void xTaskNotifyGive(unsigned handle){assert(handle==scanner_task_handle);++notified;}
static esp_err_t esp_wifi_set_band(unsigned band){assert(band==WIFI_BAND_2G||band==WIFI_BAND_5G);return stage_failure==1?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_channel(unsigned channel,unsigned secondary){assert(channel==36&&secondary==0);return stage_failure==2?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t *f){assert(f->filter_mask==3);return stage_failure==3?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_promiscuous(bool active){if(active){++enabled;return stage_failure==4?ESP_FAIL:ESP_OK;}++disabled;return stage_failure==5?ESP_FAIL:ESP_OK;}
static esp_err_t esp_wifi_set_band_mode(unsigned mode){assert(mode==WIFI_BAND_MODE_AUTO);++restored;return stage_failure==6?ESP_FAIL:ESP_OK;}
static void vTaskDelay(unsigned ms);
#include "wifi_station_service.h"
static bool another_radio_operation_is_active(void){return other_radio;}
static bool wifi_connection_is_active_locked(void){assert(locked);return connection;}
static void vTaskDelay(unsigned ms)
{
    assert(ms==50&&!locked&&!critical);ticks+=ms;
    if(renew_lease){cerberus_wifi_station_status_t status;cerberus_wifi_station_status(station_request.job_id,&status);}
    if(cancel_early)assert(cerberus_wifi_station_stop(station_request.job_id)==ESP_OK);
    if(overlap){cerberus_wifi_station_request_t r=station_request;++r.job_id;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);overlap=false;}
    if(inject_packets){
        wifi_promiscuous_pkt_t packet={.rx_ctrl={.sig_len=28,.channel=36,.rssi=-61}};
        packet.payload[0]=8;packet.payload[1]=1;memcpy(packet.payload+4,station_request.bssid,6);
        memcpy(packet.payload+10,(uint8_t[]){2,3,4,5,6,7},6);station_receive(&packet);
        packet.rx_ctrl.channel=1;station_receive(&packet); /* Adjacent-channel frames don't contaminate the target. */
        packet.rx_ctrl.channel=36;packet.rx_ctrl.rx_state=1;station_receive(&packet);
    }
}
static cerberus_wifi_station_request_t reset(void)
{
    assert(!locked&&!critical);ticks=100;stage_failure=notified=enabled=disabled=restored=0;
    other_radio=connection=test_running=cancel_early=overlap=false;renew_lease=inject_packets=true;
    station_reserved=station_receiving=station_stop=false;scanner_ready=true;scanner_mutex=scanner_task_handle=1;
    for(unsigned i=0;i<CERBERUS_WIFI_SCAN_MAX_RESULTS;++i){free(station_evidence[i]);station_evidence[i]=NULL;}
    station_active_evidence=NULL;free_bytes=128*1024;
    memset(&station_table,0,sizeof(station_table));memset(&station_request,0,sizeof(station_request));
    scan_status=(cerberus_wifi_scan_status_payload_t){.state=CERBERUS_WIFI_SCAN_COMPLETE,.generation=4,.stored_count=2};
    defense_status.state=CERBERUS_WIFI_DEFENSE_IDLE;
    memcpy(scan_results[1].bssid,(uint8_t[]){2,0,0,0,0,1},6);scan_results[1].primary=36;
    return (cerberus_wifi_station_request_t){.job_id=1,.generation=4,.index=1,.bssid={2,0,0,0,0,1},.channel=36,.seconds=5};
}
static void observe_client(uint8_t id)
{
    wifi_promiscuous_pkt_t packet={.rx_ctrl={.sig_len=28,.channel=station_request.channel,.rssi=-50}};
    packet.payload[0]=8;packet.payload[1]=1;memcpy(packet.payload+4,station_request.bssid,6);
    memcpy(packet.payload+10,(uint8_t[]){2,3,4,5,6,id},6);station_receive(&packet);
}
static void capture_clients(uint8_t ap_id,unsigned count)
{
    cerberus_wifi_station_request_t r={.job_id=(uint16_t)(station_table.status.job_id+1),.generation=4,.index=1,
        .bssid={2,0,0,0,0,ap_id},.channel=36,.seconds=5};
    memcpy(scan_results[1].bssid,r.bssid,6);scan_results[1].primary=36;
    assert(cerberus_wifi_station_start(&r)==ESP_OK);station_receiving=true;
    for(unsigned i=1;i<=count;++i){++ticks;observe_client(i);}
    station_receiving=station_reserved=false;station_table.status.state=CERBERUS_STATION_COMPLETE;
}
int main(void)
{
    cerberus_wifi_station_request_t blocked=reset();atomic_store(&csi_radio_reserved,true);
    assert(cerberus_wifi_station_start(&blocked)==ESP_ERR_INVALID_STATE&&!station_reserved&&!notified&&!enabled);
    atomic_store(&csi_radio_reserved,false);
    atomic_store(&mac_radio_reserved,true);
    assert(cerberus_wifi_station_start(&blocked)==ESP_ERR_INVALID_STATE&&!station_reserved&&!notified&&!enabled);
    atomic_store(&mac_radio_reserved,false);
    cerberus_wifi_station_request_t r=reset();assert(cerberus_wifi_station_start(&r)==ESP_OK&&notified==1&&cerberus_wifi_station_running());
    assert(cerberus_wifi_station_start(&r)==ESP_OK&&notified==1);r.seconds=15;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_ARG);r.seconds=5;
    overlap=true;assert(station_run());assert(station_table.status.state==CERBERUS_STATION_COMPLETE&&!cerberus_wifi_station_running());
    assert(station_table.status.count==1&&station_table.status.matched_frames==100&&station_table.status.observed_frames==100);
    assert(station_table.rows[0].rssi==-61&&enabled==1&&disabled==1&&restored==1);
    cerberus_wifi_station_result_t result;cerberus_wifi_station_query_t q={.job_id=1,.index=0};
    assert(cerberus_wifi_station_result(&q,&result)==ESP_OK&&cerberus_wifi_station_result_valid(&result,1,0));
    q.job_id=2;assert(cerberus_wifi_station_result(&q,&result)==ESP_ERR_NOT_FOUND);
    cerberus_wifi_ap_job_t ap={.job_id=8,.generation=4,.index=1,.bssid={2,0,0,0,0,1},.channel=36,.seconds=10,.mode=CERBERUS_AP_DEAUTH};
    assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
    uint8_t name[32],length=0;memcpy(scan_results[1].ssid,"Test AP",8);
    assert(cerberus_wifi_ap_target_ssid(&ap,name,&length)==ESP_OK&&length==7&&!memcmp(name,"Test AP",7));
    cerberus_wifi_ap_metadata_t meta;
    atomic_store(&mac_radio_reserved,true);
    assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    assert(cerberus_wifi_ap_target_metadata(&ap,&meta)==ESP_ERR_INVALID_STATE);
    atomic_store(&mac_radio_reserved,false);
    const wifi_auth_mode_t short_modes[]={WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_WPA3_ENTERPRISE,
        WIFI_AUTH_WPA2_WPA3_ENTERPRISE,WIFI_AUTH_WPA3_ENT_192,WIFI_AUTH_WAPI_PSK};
    for(unsigned i=0;i<sizeof(short_modes)/sizeof(short_modes[0]);++i){
        scan_results[1].authmode=short_modes[i];assert(cerberus_wifi_ap_target_metadata(&ap,&meta)==ESP_OK&&meta.short_eapol&&meta.ssid_length==7);
    }
    scan_results[1].authmode=0;assert(cerberus_wifi_ap_target_metadata(&ap,&meta)==ESP_OK&&!meta.short_eapol);
    memset(scan_results[1].ssid,'Z',32);assert(cerberus_wifi_ap_target_ssid(&ap,name,&length)==ESP_OK&&length==32);
    ++ap.generation;assert(cerberus_wifi_ap_target_ssid(&ap,name,&length)==ESP_ERR_INVALID_STATE);--ap.generation;
    ++ap.generation;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);--ap.generation;
    ap.bssid[5]=9;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);ap.bssid[5]=1;
    connection=true;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);connection=false;
    ap.scope=CERBERUS_AP_ONE_STATION;memcpy(ap.station,(uint8_t[]){2,3,4,5,6,7},6);assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
    ++ap.station[5];assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);--ap.station[5];
    station_request.bssid[5]=9;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);station_request.bssid[5]=1; /* Earlier AP evidence survives another capture. */
    uint32_t saved_ticks=ticks;ticks+=STATION_EVIDENCE_LIFETIME_MS;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);ticks=saved_ticks;
    station_reserved=true;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);station_reserved=false;
    assert(!station_run());assert(cerberus_wifi_station_start(&r)==ESP_OK&&notified==1); /* Lost ACK retry doesn't restart. */
    r=reset();r.seconds=15;renew_lease=false;assert(cerberus_wifi_station_start(&r)==ESP_OK);assert(station_run());
    assert(station_table.status.state==CERBERUS_STATION_CANCELLED&&station_table.status.elapsed_ms==5050);
    r=reset();assert(cerberus_wifi_station_start(&r)==ESP_OK);cancel_early=true;assert(station_run());assert(station_table.status.state==CERBERUS_STATION_CANCELLED&&disabled==1);
    r=reset();assert(cerberus_wifi_station_start(&r)==ESP_OK);assert(cerberus_wifi_station_stop(r.job_id)==ESP_OK);assert(station_run());assert(!enabled&&station_table.status.state==CERBERUS_STATION_CANCELLED);
    r=reset();ticks=UINT32_MAX-1000;assert(cerberus_wifi_station_start(&r)==ESP_OK);assert(station_run());assert(station_table.status.elapsed_ms==5000);
    for(unsigned failure=1;failure<=6;++failure){
        r=reset();stage_failure=failure;assert(cerberus_wifi_station_start(&r)==ESP_OK);assert(station_run());
        assert(station_table.status.state==CERBERUS_STATION_ERROR&&station_table.status.error_code==ESP_FAIL&&!station_receiving);
        assert(cerberus_wifi_station_running()==(failure>=5)); /* Failed cleanup retains ownership. */
    }
    r=reset();r.bssid[5]=9;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();r.generation=3;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();r.channel=1;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();connection=true;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();other_radio=true;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();test_running=true;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();scan_status.state=CERBERUS_WIFI_SCAN_RUNNING;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();defense_status.state=CERBERUS_WIFI_DEFENSE_RUNNING;assert(cerberus_wifi_station_start(&r)==ESP_ERR_INVALID_STATE);
    r=reset();assert(cerberus_wifi_station_start(&r)==ESP_OK);uint32_t contact=station_contact;ticks+=100;
    cerberus_wifi_station_status_t status;cerberus_wifi_station_status(0,&status);assert(station_contact==contact);
    cerberus_wifi_station_status(99,&status);assert(station_contact==contact);cerberus_wifi_station_status(1,&status);assert(station_contact==ticks);
    r=reset();station_request=r;ticks=UINT32_MAX-50;
    ap=(cerberus_wifi_ap_job_t){.job_id=8,.generation=4,.index=1,.bssid={2,0,0,0,0,1},.channel=36,.seconds=10,
        .scope=CERBERUS_AP_ONE_STATION,.mode=CERBERUS_AP_DEAUTH,.station={2,3,4,5,6,1}};
    observe_client(1);assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE); /* Inactive callback cannot add evidence. */
    free_bytes=0;assert(cerberus_wifi_station_start(&r)==ESP_ERR_NO_MEM&&!station_reserved&&!station_active_evidence);
    free_bytes=128*1024;
    for(unsigned i=1;i<=32;++i)capture_clients(i,40); /* Overflow must not evict any of the 32 exported clients. */
    for(unsigned i=1;i<=32;++i){
        ap.bssid[5]=scan_results[1].bssid[5]=i;
        ap.station[5]=1;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
        ap.station[5]=32;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
        ap.station[5]=33;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    }
    capture_clients(33,1);ap.bssid[5]=scan_results[1].bssid[5]=1;ap.station[5]=1;
    assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE); /* The 33rd AP replaces only the oldest AP. */
    capture_clients(2,3);ap.bssid[5]=scan_results[1].bssid[5]=2;ap.station[5]=32;
    assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    ap.station[5]=3;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
    ap.channel=scan_results[1].primary=6;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    ap.channel=scan_results[1].primary=36;
    ap.bssid[5]=scan_results[1].bssid[5]=99;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    ap.bssid[5]=scan_results[1].bssid[5]=2;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_OK);
    ticks+=STATION_EVIDENCE_LIFETIME_MS;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    ticks+=(UINT64_C(1)<<32)-STATION_EVIDENCE_LIFETIME_MS;assert(cerberus_wifi_ap_target_valid(&ap)==ESP_ERR_INVALID_STATE);
    assert(!locked&&!critical);
    (void)reset();
    puts("Station service passed: 32 APs x 32 clients, overflow isolation, recapture/oldest-AP replacement, heap guard, scope/expiry/64-bit time, inactive RX, lease/cancel and ownership.");
}
