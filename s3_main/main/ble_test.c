#include "ble_test.h"
#include "s3_ble_host.h"
#include "s3_wifi_owner.h"
#include "sdkconfig.h"
#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include <stdatomic.h>
#include <string.h>
static _Atomic(SemaphoreHandle_t) mutex;
static cerberus_radio_test_config_t config;
static cerberus_radio_test_status_t status;
static bool running,stop;
static uint32_t contact_ms,started_ms;
static uint32_t now_ms(void){return (uint32_t)(esp_timer_get_time()/1000);}
static int gap_event(struct ble_gap_event *event,void *arg){(void)event;(void)arg;return 0;}
static bool cancelled(void)
{
    xSemaphoreTake(mutex,portMAX_DELAY);bool value=stop||(uint32_t)(now_ms()-contact_ms)>3000U;
    xSemaphoreGive(mutex);return value;
}
static void worker(void *unused)
{
    (void)unused;
    xSemaphoreTake(mutex,portMAX_DELAY);cerberus_radio_test_config_t c=config;xSemaphoreGive(mutex);
    esp_err_t err=cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_ADVERTISER);
    bool claimed=err==ESP_OK;
    bool was_cancelled=cancelled(),advertising=false;
    if(err==ESP_OK&&!was_cancelled){
        uint8_t data[31],address_type;
        size_t length=cerberus_radio_test_ble(&c,data,sizeof(data));
        int rc=ble_hs_id_infer_auto(0,&address_type);
        if(rc==0)rc=ble_gap_adv_set_data(data,(int)length);
        struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_NON,.disc_mode=BLE_GAP_DISC_MODE_GEN,
            .itvl_min=160,.itvl_max=160}; /* 100 ms, one named advertiser. */
        if(rc==0)rc=ble_gap_adv_start(address_type,NULL,(int32_t)c.seconds*1000,&params,gap_event,NULL);
        if(rc!=0)err=ESP_FAIL;else advertising=true;
    }
    uint32_t advert_started=now_ms();
    while(err==ESP_OK&&advertising&&(uint32_t)(now_ms()-advert_started)<(uint32_t)c.seconds*1000U){
        if(cancelled()){was_cancelled=true;break;}
        if(!cerberus_s3_ble_host_synced()){err=ESP_FAIL;break;}
        xSemaphoreTake(mutex,portMAX_DELAY);
        uint32_t free_kib=heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U;
        if(free_kib<status.minimum_heap_kib)status.minimum_heap_kib=(uint16_t)free_kib;
        xSemaphoreGive(mutex);vTaskDelay(pdMS_TO_TICKS(50));
    }
    if(advertising){
        int rc=ble_gap_adv_stop();
        /* A failed STOP is not proof that the radio stopped. Keep ownership
         * and retry until the host reports inactive (the duration is also finite). */
        while(rc!=0&&rc!=BLE_HS_EALREADY&&ble_gap_adv_active()){
            err=ESP_FAIL;
            xSemaphoreTake(mutex,portMAX_DELAY);status.state=CERBERUS_TEST_STOPPING;status.error_code=err;xSemaphoreGive(mutex);
            vTaskDelay(pdMS_TO_TICKS(50));rc=ble_gap_adv_stop();
        }
    }
    if(claimed&&cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_ADVERTISER)!=ESP_OK)err=ESP_FAIL;
    xSemaphoreTake(mutex,portMAX_DELAY);
    status.state=err!=ESP_OK?CERBERUS_TEST_ERROR:was_cancelled?CERBERUS_TEST_CANCELLED:CERBERUS_TEST_COMPLETE;
    status.error_code=err;status.elapsed_ms=now_ms()-started_ms;running=false;
    xSemaphoreGive(mutex);vTaskDelete(NULL);
}
esp_err_t cerberus_ble_test_start(const cerberus_radio_test_config_t *c)
{
    if(!cerberus_radio_test_config_valid(c)||c->mode!=CERBERUS_TEST_BLE_ADVERT)return ESP_ERR_INVALID_ARG;
    if(cerberus_s3_wifi_owner()==CERBERUS_S3_WIFI_RECEIVER)return ESP_ERR_INVALID_STATE;
    /* Only the UI calls start, so lazy mutex creation cannot race. */
    if(!mutex)mutex=xSemaphoreCreateMutex();
    if(!mutex)return ESP_ERR_NO_MEM;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(running){xSemaphoreGive(mutex);return ESP_ERR_INVALID_STATE;}
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<(cerberus_s3_ble_host_initialized()?32768U:98304U)){xSemaphoreGive(mutex);return ESP_ERR_NO_MEM;}
    config=*c;running=true;stop=false;started_ms=contact_ms=now_ms();
    status=(cerberus_radio_test_status_t){.job_id=c->job_id,.state=CERBERUS_TEST_RUNNING,.mode=c->mode,.seconds=c->seconds,.name_length=c->name_length,
        .minimum_heap_kib=(uint16_t)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024U)};
    if(xTaskCreate(worker,"s3_ble_test",4096,NULL,3,NULL)!=pdPASS){running=false;status.state=CERBERUS_TEST_ERROR;status.error_code=ESP_ERR_NO_MEM;}
    esp_err_t err=status.error_code;xSemaphoreGive(mutex);return err;
}
void cerberus_ble_test_status(uint16_t id,cerberus_radio_test_status_t *out)
{
    if(!out)return;
    memset(out,0,sizeof(*out));
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);if(id&&id==status.job_id)contact_ms=now_ms();*out=status;
    if(running)out->elapsed_ms=now_ms()-started_ms;
    xSemaphoreGive(mutex);
}
void cerberus_ble_test_stop(uint16_t id)
{
    if(!mutex)return;
    xSemaphoreTake(mutex,portMAX_DELAY);
    if(id==status.job_id&&running){stop=true;status.state=CERBERUS_TEST_STOPPING;}
    xSemaphoreGive(mutex);
}
#else
#include <string.h>
esp_err_t cerberus_ble_test_start(const cerberus_radio_test_config_t *c){(void)c;return ESP_ERR_NOT_SUPPORTED;}
void cerberus_ble_test_status(uint16_t id,cerberus_radio_test_status_t *out){(void)id;if(out){memset(out,0,sizeof(*out));out->state=CERBERUS_TEST_ERROR;out->error_code=ESP_ERR_NOT_SUPPORTED;}}
void cerberus_ble_test_stop(uint16_t id){(void)id;}
#endif
