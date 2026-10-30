#include "s3_wifi_receiver.h"
#include "s3_wifi_owner.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "s3_ble_central.h"
#include "ble_test.h"
#include "wifi_ap_clone.h"
#include "esp_wifi.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static cerberus_wifi_ap_job_t request;
static cerberus_wifi_ap_station_set_t station_set;
static uint8_t clone_ssid[32],clone_ssid_length,clone_capabilities[2];
static bool clone_capabilities_ready;
static cerberus_s3_rx_status_t status;
static bool stop_requested,receiving;
static uint32_t started,contact;
static uint32_t now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
static esp_err_t quiesce_ble(void)
{
    uint32_t started=now_ms();bool stop_sent=false;
    while(cerberus_s3_ble_host_owner()!=CERBERUS_S3_BLE_OWNER_NONE){
        cerberus_s3_ble_owner_t owner=cerberus_s3_ble_host_owner();
        if(owner==CERBERUS_S3_BLE_OWNER_SCANNER){
            cerberus_ble_scan_status_payload_t scan={0};
            esp_err_t error=cerberus_s3_ble_scanner_get_status(&scan);
            if(error==ESP_OK&&scan.state==CERBERUS_BLE_SCAN_RUNNING&&!stop_sent){
                error=cerberus_s3_ble_scanner_stop(&scan);
                if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return error;
                stop_sent=true;
            }
        }else if(owner==CERBERUS_S3_BLE_OWNER_CENTRAL){
            cerberus_ble_connection_status_payload_t connection={0};
            esp_err_t error=cerberus_s3_ble_central_get_status(&connection);
            if(error!=ESP_OK)return error;
            if(!stop_sent&&(connection.state==CERBERUS_BLE_CONNECTION_CONNECTING||
                connection.state==CERBERUS_BLE_CONNECTION_CONNECTED||
                connection.state==CERBERUS_BLE_CONNECTION_DISCONNECTING)){
                error=cerberus_s3_ble_central_disconnect(&connection);
                if(error!=ESP_OK&&error!=ESP_ERR_INVALID_STATE)return error;
                stop_sent=true;
            }
        }else return ESP_ERR_INVALID_STATE;
        if((uint32_t)(now_ms()-started)>=2000U)return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
        if(cerberus_s3_ble_host_owner()!=owner){
            stop_sent=false;
        }
    }
    return cerberus_s3_ble_host_owner()==CERBERUS_S3_BLE_OWNER_NONE?
        ESP_OK:ESP_ERR_INVALID_STATE;
}
static uint32_t increment(uint32_t n){return n==UINT32_MAX?n:n+1U;}
static bool cancelled(void)
{
    portENTER_CRITICAL(&lock);bool stop=stop_requested||(uint32_t)(now_ms()-contact)>5000U;
    portEXIT_CRITICAL(&lock);return stop;
}
static void receive(void *buffer,wifi_promiscuous_pkt_type_t type)
{
    if(!buffer||(type!=WIFI_PKT_MGMT&&type!=WIFI_PKT_DATA))return;
    const wifi_promiscuous_pkt_t *packet=buffer;
    if(packet->rx_ctrl.rx_state||packet->rx_ctrl.sig_len<28)return;
    if((type==WIFI_PKT_DATA)!=((packet->payload[0]&0x0c)==8))return;
    portENTER_CRITICAL(&lock);
    bool active=receiving&&packet->rx_ctrl.channel==request.channel&&
        (type==WIFI_PKT_MGMT||request.mode==CERBERUS_AP_BAD_MESSAGE);
    cerberus_wifi_ap_job_t target=request;
    cerberus_wifi_ap_station_set_t selected;
    uint8_t source_ssid[32],source_length=0,source_capabilities[2]={0};
    bool have_source_capabilities=false;
    if(active&&target.scope==CERBERUS_AP_STATION_LIST){
        selected.count=station_set.count;
        memcpy(selected.mac,station_set.mac,(size_t)selected.count*6U);
    }
    if(active&&target.mode==CERBERUS_WIFI_AP_CLONE_MODE){
        source_length=clone_ssid_length;
        memcpy(source_ssid,clone_ssid,source_length);
        have_source_capabilities=clone_capabilities_ready;
        if(have_source_capabilities)memcpy(source_capabilities,clone_capabilities,2);
    }
    portEXIT_CRITICAL(&lock);
    if(!active)return;
    unsigned match=0;uint8_t observed_capabilities[2];bool observed_source=false;
    if(target.mode==CERBERUS_WIFI_AP_CLONE_MODE){
        observed_source=cerberus_wifi_ap_beacon_capabilities(&target,source_ssid,source_length,
            packet->payload,packet->rx_ctrl.sig_len-4U,observed_capabilities);
        if(observed_source)match=CERBERUS_AP_RX_BEACON;
        else if(have_source_capabilities&&cerberus_wifi_ap_clone_rx_classify(&target,
            target.station,source_ssid,source_length,source_capabilities,UINT8_MAX,
            packet->payload,packet->rx_ctrl.sig_len-4U))match=CERBERUS_AP_RX_MATCH;
    }else match=target.scope==CERBERUS_AP_STATION_LIST?
        cerberus_wifi_ap_rx_classify_stations(&target,&selected,packet->payload,packet->rx_ctrl.sig_len-4U):
        cerberus_wifi_ap_rx_classify(&target,packet->payload,packet->rx_ctrl.sig_len-4U);
    portENTER_CRITICAL(&lock);
    if(receiving&&target.job_id==request.job_id){
        if(observed_source){memcpy(clone_capabilities,observed_capabilities,2);clone_capabilities_ready=true;}
        status.observed_frames=increment(status.observed_frames);
        if(match&CERBERUS_AP_RX_BEACON)status.target_beacons=increment(status.target_beacons);
        if(match&CERBERUS_AP_RX_MATCH){status.matching_frames=increment(status.matching_frames);status.last_match_rssi=packet->rx_ctrl.rssi;}
    }
    portEXIT_CRITICAL(&lock);
}
static void worker(void *unused)
{
    (void)unused;
    bool initialized=false,started_wifi=false,callback=false,promiscuous=false;
    esp_err_t error=cerberus_s3_wifi_prepare(CERBERUS_S3_WIFI_RECEIVER);
    wifi_init_config_t init=WIFI_INIT_CONFIG_DEFAULT();
    /* This mode does not transmit or join. Bounded dynamic buffers leave room
     * for the UI, SD and independent C5 transport in internal RAM. */
    init.static_rx_buf_num=4;init.dynamic_rx_buf_num=8;init.dynamic_tx_buf_num=4;
    init.rx_ba_win=4;init.ampdu_rx_enable=0;init.ampdu_tx_enable=0;init.amsdu_tx_enable=0;
    if(error==ESP_OK&&!cancelled()){error=esp_wifi_init(&init);initialized=error==ESP_OK;}
    if(error==ESP_OK&&initialized)error=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(error==ESP_OK&&initialized)error=esp_wifi_set_mode(WIFI_MODE_NULL);
    if(error==ESP_OK&&initialized){error=esp_wifi_start();started_wifi=error==ESP_OK;}
    if(error==ESP_OK&&started_wifi)error=esp_wifi_set_channel(request.channel,WIFI_SECOND_CHAN_NONE);
    wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT};
    if(request.mode==CERBERUS_AP_BAD_MESSAGE)filter.filter_mask|=WIFI_PROMIS_FILTER_MASK_DATA;
    if(error==ESP_OK&&started_wifi)error=esp_wifi_set_promiscuous_filter(&filter);
    if(error==ESP_OK&&started_wifi){error=esp_wifi_set_promiscuous_rx_cb(receive);callback=error==ESP_OK;}
    if(error==ESP_OK&&callback&&!cancelled()){
        error=esp_wifi_set_promiscuous(true);promiscuous=error==ESP_OK;
        portENTER_CRITICAL(&lock);receiving=promiscuous;
        if(promiscuous&&!stop_requested)status.state=CERBERUS_TEST_RUNNING;
        portEXIT_CRITICAL(&lock);
    }
    bool was_cancelled=cancelled();
    /* Allow C5 beacon preparation plus TX after RX is ready. Wi-Fi startup
     * latency must not consume this receive window. The lease still applies. */
    uint32_t receive_started=now_ms();
    while(error==ESP_OK&&promiscuous&&!was_cancelled&&
        (!request.seconds||(uint32_t)(now_ms()-receive_started)<((uint32_t)request.seconds+3U)*1000U)){
        uint16_t heap=(uint16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U);
        portENTER_CRITICAL(&lock);if(heap<status.minimum_heap_kib)status.minimum_heap_kib=heap;portEXIT_CRITICAL(&lock);
        vTaskDelay(pdMS_TO_TICKS(25));was_cancelled=cancelled();
    }
    portENTER_CRITICAL(&lock);receiving=false;portEXIT_CRITICAL(&lock);
    esp_err_t cleanup=ESP_OK,next;
    if(promiscuous){next=esp_wifi_set_promiscuous(false);if(next!=ESP_OK)cleanup=next;}
    if(callback){next=esp_wifi_set_promiscuous_rx_cb(NULL);if(next!=ESP_OK&&cleanup==ESP_OK)cleanup=next;}
    if(started_wifi){next=esp_wifi_stop();if(next!=ESP_OK&&next!=ESP_ERR_WIFI_NOT_STARTED&&cleanup==ESP_OK)cleanup=next;}
    if(initialized){next=esp_wifi_deinit();if(next!=ESP_OK&&cleanup==ESP_OK)cleanup=next;}
    /* Even if a later cleanup step worked, keep the owner on any uncertainty. */
    if(cleanup==ESP_OK)cleanup=cerberus_s3_wifi_release(CERBERUS_S3_WIFI_RECEIVER);
    if(error==ESP_OK)error=cleanup;
    portENTER_CRITICAL(&lock);
    status.error_code=error;status.reserved=cleanup!=ESP_OK;status.elapsed_ms=now_ms()-started;
    status.state=error!=ESP_OK?CERBERUS_TEST_ERROR:was_cancelled?CERBERUS_TEST_CANCELLED:CERBERUS_TEST_COMPLETE;
    portEXIT_CRITICAL(&lock);vTaskDelete(NULL);
}
static esp_err_t start_receiver(const cerberus_wifi_ap_job_t *r,const cerberus_wifi_ap_station_set_t *stations,
    const uint8_t *source_ssid,size_t source_ssid_length,bool clone_requested)
{
    if(!cerberus_wifi_ap_job_valid(r)||r->channel>11||
       clone_requested!=(r->mode==CERBERUS_WIFI_AP_CLONE_MODE)||
       (clone_requested?(source_ssid_length>32||(!source_ssid&&source_ssid_length)||stations!=NULL):
            source_ssid!=NULL||source_ssid_length!=0)||
       (r->scope==CERBERUS_AP_STATION_LIST?!cerberus_wifi_ap_station_set_valid(r,stations):stations!=NULL))
        return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&lock);
    bool duplicate=status.job_id==r->job_id,same=duplicate&&!memcmp(&request,r,sizeof(*r));
    if(same&&r->scope==CERBERUS_AP_STATION_LIST)
        same=station_set.count==stations->count&&!memcmp(station_set.mac,stations->mac,(size_t)stations->count*6U);
    if(same&&clone_requested)
        same=clone_ssid_length==source_ssid_length&&
            (!source_ssid_length||!memcmp(clone_ssid,source_ssid,source_ssid_length));
    bool reserved=status.reserved;portEXIT_CRITICAL(&lock);
    if(duplicate)return same?ESP_OK:ESP_ERR_INVALID_ARG;
    if(reserved)return ESP_ERR_INVALID_STATE;
    cerberus_radio_test_status_t ble;cerberus_ble_test_status(0,&ble);
    if(ble.state==CERBERUS_TEST_RUNNING||ble.state==CERBERUS_TEST_STOPPING)return ESP_ERR_INVALID_STATE;
    esp_err_t error=quiesce_ble();if(error!=ESP_OK)return error;
    size_t free_bytes=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(free_bytes<65536U)return ESP_ERR_NO_MEM;
    error=cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_RECEIVER);if(error!=ESP_OK)return error;
    error=quiesce_ble();
    if(error!=ESP_OK){(void)cerberus_s3_wifi_release(CERBERUS_S3_WIFI_RECEIVER);return error;}
    portENTER_CRITICAL(&lock);
    request=*r;memset(&station_set,0,sizeof(station_set));
    if(stations){station_set.count=stations->count;memcpy(station_set.mac,stations->mac,(size_t)stations->count*6U);}
    memset(clone_ssid,0,sizeof(clone_ssid));clone_ssid_length=(uint8_t)source_ssid_length;
    if(source_ssid_length)memcpy(clone_ssid,source_ssid,source_ssid_length);
    memset(clone_capabilities,0,sizeof(clone_capabilities));clone_capabilities_ready=false;
    started=contact=now_ms();stop_requested=receiving=false;
    status=(cerberus_s3_rx_status_t){.job_id=r->job_id,.state=CERBERUS_TEST_STAGED,.channel=r->channel,.reserved=true,
        .minimum_heap_kib=(uint16_t)(free_bytes/1024U)};
    portEXIT_CRITICAL(&lock);
    if(xTaskCreate(worker,"s3_wifi_rx",4096,NULL,3,NULL)!=pdPASS){
        (void)cerberus_s3_wifi_release(CERBERUS_S3_WIFI_RECEIVER);
        portENTER_CRITICAL(&lock);status.reserved=false;status.state=CERBERUS_TEST_ERROR;status.error_code=ESP_ERR_NO_MEM;portEXIT_CRITICAL(&lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}
esp_err_t cerberus_s3_rx_start(const cerberus_wifi_ap_job_t *r)
{return start_receiver(r,NULL,NULL,0,false);}
esp_err_t cerberus_s3_rx_start_stations(const cerberus_wifi_ap_job_t *r,
    const cerberus_wifi_ap_station_set_t *stations)
{return r&&r->scope==CERBERUS_AP_STATION_LIST?start_receiver(r,stations,NULL,0,false):ESP_ERR_INVALID_ARG;}
esp_err_t cerberus_s3_rx_start_clone(const cerberus_wifi_ap_job_t *r,
    const uint8_t *source_ssid,size_t source_ssid_length)
{return start_receiver(r,NULL,source_ssid,source_ssid_length,true);}
void cerberus_s3_rx_stop(uint16_t id)
{
    portENTER_CRITICAL(&lock);
    if(id&&id==status.job_id&&status.reserved&&status.state!=CERBERUS_TEST_ERROR){stop_requested=true;status.state=CERBERUS_TEST_STOPPING;}
    portEXIT_CRITICAL(&lock);
}
void cerberus_s3_rx_status(uint16_t id,cerberus_s3_rx_status_t *out)
{
    if(!out)return;
    portENTER_CRITICAL(&lock);
    if(id&&id==status.job_id)contact=now_ms();
    *out=status;
    if(status.reserved&&status.state!=CERBERUS_TEST_ERROR)out->elapsed_ms=now_ms()-started;
    portEXIT_CRITICAL(&lock);
}
