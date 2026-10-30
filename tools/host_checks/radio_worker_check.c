#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "fake_worker_platform.h"
#include "ieee802154_scanner.c"

static jmp_buf idle;
static void (*task_fn)(void *);
static unsigned created, stack_bytes, notifications, locked, enabled, disabled, receives, detects;
static unsigned mutexes, queues;
static TickType_t ticks;
static bool fail_queue, fail_task, fail_enable, fail_channel, fail_sleep, busy_wifi, busy_test;
static bool inject_overlap, injected,busy_stations;

SemaphoreHandle_t xSemaphoreCreateMutex(void){++mutexes;return (void *)1;}
void vSemaphoreDelete(SemaphoreHandle_t h){assert(h&&mutexes);--mutexes;}
int xSemaphoreTake(SemaphoreHandle_t h,unsigned wait){(void)wait;assert(h&&!locked);++locked;return pdTRUE;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h&&locked==1);--locked;return pdTRUE;}
QueueHandle_t xQueueCreate(unsigned n,unsigned size){assert(n==8&&size>0);if(fail_queue)return NULL;++queues;return (void *)2;}
void vQueueDelete(QueueHandle_t h){assert(h&&queues);--queues;}
int xQueueReceive(QueueHandle_t h,void *out,unsigned wait){assert(h&&out);ticks+=wait;return pdFALSE;}
int xQueueSendFromISR(QueueHandle_t h,const void *in,BaseType_t *woken){assert(h&&in&&woken);return pdTRUE;}
int xQueueReset(QueueHandle_t h){assert(h);return pdTRUE;}
int xTaskCreate(void(*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out)
{assert(name&&arg==NULL&&priority==4);if(fail_task)return 0;++created;stack_bytes+=stack;task_fn=fn;*out=(void *)3;return pdPASS;}
void xTaskNotifyGive(TaskHandle_t h){assert(h==(void *)3);++notifications;}
unsigned ulTaskNotifyTake(int clear,unsigned wait)
{assert(clear==pdTRUE&&wait==portMAX_DELAY);if(!notifications)longjmp(idle,1);unsigned n=notifications;notifications=0;return n;}
TickType_t xTaskGetTickCount(void){return ticks;}
void vTaskDelay(unsigned n){ticks+=n;}
esp_err_t esp_ieee802154_enable(void)
{
    ++enabled;
    if(inject_overlap&&!injected){
        injected=true;
        assert(cerberus_ieee802154_scanner_start()==ESP_ERR_INVALID_STATE);
        assert(cerberus_ieee802154_beacon_scanner_start()==ESP_ERR_INVALID_STATE);
        assert(cerberus_ieee802154_activity_scanner_start()==ESP_ERR_INVALID_STATE);
    }
    return fail_enable?ESP_FAIL:ESP_OK;
}
esp_err_t esp_ieee802154_disable(void){++disabled;return ESP_OK;}
esp_err_t esp_ieee802154_sleep(void){return fail_sleep?ESP_FAIL:ESP_OK;}
esp_err_t esp_ieee802154_set_channel(uint8_t c){assert(c>=11&&c<=26);return fail_channel?ESP_FAIL:ESP_OK;}
esp_err_t esp_ieee802154_set_coordinator(bool v){assert(!v);return ESP_OK;}
esp_err_t esp_ieee802154_set_promiscuous(bool v){(void)v;return ESP_OK;}
esp_err_t esp_ieee802154_receive(void){++receives;return ESP_OK;}
esp_err_t esp_ieee802154_receive_handle_done(uint8_t *f){(void)f;return ESP_OK;}
esp_err_t esp_ieee802154_energy_detect(uint32_t n){assert(n==3125);++detects;esp_ieee802154_energy_detect_done(-77);return ESP_OK;}
bool cerberus_c5_test_running(void){return busy_test;}
void cerberus_wifi_scanner_get_status(cerberus_wifi_scan_status_payload_t *s){memset(s,0,sizeof(*s));s->state=busy_wifi?CERBERUS_WIFI_SCAN_RUNNING:CERBERUS_WIFI_SCAN_IDLE;}
void cerberus_wifi_connection_get_status(cerberus_wifi_connection_status_payload_t *s){memset(s,0,sizeof(*s));}
void cerberus_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *s){memset(s,0,sizeof(*s));}
void cerberus_ble_connection_get_status(cerberus_ble_connection_status_payload_t *s){memset(s,0,sizeof(*s));}
bool cerberus_wifi_defense_survey_is_running(void){return false;}
bool cerberus_wifi_station_running(void){return busy_stations;}
static void run_worker(void){assert(task_fn);if(!setjmp(idle))task_fn(NULL);assert(!locked);}

int main(void)
{
    fail_queue=true;assert(cerberus_ieee802154_scanner_init()==ESP_ERR_NO_MEM);assert(!mutexes&&!queues&&!scanner_mutex);
    fail_queue=false;fail_task=true;assert(cerberus_ieee802154_scanner_init()==ESP_ERR_NO_MEM);assert(!mutexes&&!queues&&!scanner_mutex);
    fail_task=false;assert(cerberus_ieee802154_scanner_init()==ESP_OK);assert(created==1&&stack_bytes==4096);
    assert(cerberus_ieee802154_scanner_init()==ESP_OK);assert(created==1); /* Idempotent init. */
    busy_wifi=true;assert(cerberus_ieee802154_scanner_start()==ESP_ERR_INVALID_STATE);assert(!notifications);busy_wifi=false;
    busy_test=true;assert(cerberus_ieee802154_beacon_scanner_start()==ESP_ERR_INVALID_STATE);assert(!notifications);busy_test=false;
    busy_stations=true;assert(cerberus_ieee802154_scanner_start()==ESP_ERR_INVALID_STATE);
    assert(cerberus_ieee802154_beacon_scanner_start()==ESP_ERR_INVALID_STATE);
    assert(cerberus_ieee802154_activity_scanner_start()==ESP_ERR_INVALID_STATE);assert(!notifications);busy_stations=false;
    inject_overlap=true;
    assert(cerberus_ieee802154_scanner_start()==ESP_OK);assert(cerberus_ieee802154_scanner_is_running());run_worker();
    assert(injected&&scan_status.state==CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE&&scan_status.completed_count==16);
    assert(detects==16&&!cerberus_ieee802154_scanner_is_running()&&enabled==disabled);
    cerberus_ieee802154_energy_scan_result_payload_t energy;
    assert(cerberus_ieee802154_scanner_get_result(scan_status.generation,&energy)==ESP_OK);
    for(unsigned i=0;i<16;++i)assert(energy.power_dbm[i]==-77);
    assert(cerberus_ieee802154_scanner_get_result(scan_status.generation+1,&energy)==ESP_ERR_INVALID_STATE);
    unsigned before=enabled;notifications=1;run_worker();assert(enabled==before); /* Stale wake never restarts RF. */
    ticks=UINT32_MAX-1000U;
    assert(cerberus_ieee802154_beacon_scanner_start()==ESP_OK);run_worker();
    assert(beacon_status.state==CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE&&beacon_status.completed_count==16);
    assert(receives==16&&!beacon_scan_active&&enabled==disabled);
    assert(cerberus_ieee802154_activity_scanner_start()==ESP_OK);run_worker();
    assert(activity_status.state==CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE&&activity_status.completed_count==16);
    assert(receives==32&&!activity_scan_active&&enabled==disabled);
    fail_enable=true;assert(cerberus_ieee802154_scanner_start()==ESP_OK);run_worker();
    assert(scan_status.state==CERBERUS_IEEE802154_ENERGY_SCAN_ERROR&&!cerberus_ieee802154_scanner_is_running());fail_enable=false;
    fail_channel=true;assert(cerberus_ieee802154_beacon_scanner_start()==ESP_OK);run_worker();
    assert(beacon_status.state==CERBERUS_IEEE802154_BEACON_SCAN_ERROR&&!beacon_scan_active);fail_channel=false;
    fail_sleep=true;assert(cerberus_ieee802154_activity_scanner_start()==ESP_OK);run_worker();
    assert(activity_status.state==CERBERUS_IEEE802154_ACTIVITY_SCAN_ERROR&&!activity_scan_active);fail_sleep=false;
    assert(cerberus_ieee802154_scanner_start()==ESP_OK);run_worker();assert(scan_status.state==CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE);
    assert(created==1&&stack_bytes==4096&&enabled==disabled&&!notifications);
    puts("Shared radio worker: one 4 KiB task; all modes, overlap, stale wake, wraparound, failures, retry and cleanup passed.");
}
