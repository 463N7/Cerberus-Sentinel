#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fake_radio_platform.h"
#include <assert.h>
#include <stdio.h>
#include "s3_wifi_owner.h"
static cerberus_s3_wifi_owner_t wifi_owner;
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void){return wifi_owner;}
static unsigned tick,free_bytes=128*1024,starts,stop_at,stop_failures,stop_calls;
static bool active,fail_init;
static unsigned init_calls;
static void(*queued)(void*);
static int held;
struct fake_host_config ble_hs_cfg;
SemaphoreHandle_t xSemaphoreCreateMutex(void){return (void*)1;}
int xSemaphoreTake(SemaphoreHandle_t m,unsigned timeout){(void)m;(void)timeout;assert(!held);held=1;return 1;}
int xSemaphoreGive(SemaphoreHandle_t m){(void)m;assert(held);held=0;return 1;}
int xTaskCreate(void(*fn)(void*),const char*n,unsigned stack,void*a,unsigned p,void*out){(void)n;(void)a;(void)p;(void)out;assert(stack==4096);queued=fn;return 1;}
void vTaskDelete(void*p){(void)p;}
int64_t esp_timer_get_time(void){return (int64_t)tick*1000;}
size_t heap_caps_get_free_size(unsigned caps){(void)caps;return free_bytes;}
int nimble_port_init(void){++init_calls;return fail_init?ESP_FAIL:ESP_OK;}
void nimble_port_run(void){}
void nimble_port_freertos_init(void(*fn)(void*)){(void)fn;assert(ble_hs_cfg.sync_cb);ble_hs_cfg.sync_cb();}
void nimble_port_freertos_deinit(void){}
int ble_hs_util_ensure_addr(int p){(void)p;return 0;}
int ble_hs_id_infer_auto(int p,uint8_t*a){(void)p;*a=1;return 0;}
int ble_gap_adv_set_data(const uint8_t*p,int n){assert(n==16&&p[0]==2&&p[1]==1&&p[4]==9);return 0;}
int ble_gap_adv_start(uint8_t type,const void*peer,int32_t ms,const struct ble_gap_adv_params*p,int(*callback)(struct ble_gap_event*,void*),void*arg)
{(void)callback;(void)arg;assert(type==1&&!peer&&ms>=1000&&ms<=30000&&p->conn_mode==0&&p->itvl_min==160&&p->itvl_max==160);active=true;++starts;return 0;}
int ble_gap_adv_stop(void){++stop_calls;if(stop_failures){--stop_failures;return 9;}active=false;return 0;}
int ble_gap_adv_active(void){return active;}
#include "ble_test.c"
void vTaskDelay(unsigned ms){tick+=ms;if(stop_at&&tick>=stop_at){stop_at=0;cerberus_ble_test_stop(status.job_id);}}
static void run(void){void(*fn)(void*)=queued;queued=NULL;assert(fn);fn(NULL);assert(!held&&!running&&!active);}
int main(void)
{
    cerberus_radio_test_config_t c={.job_id=1,.mode=2,.seconds=1,.name_length=11,.name="CerberusLab"};
    wifi_owner=CERBERUS_S3_WIFI_RECEIVER;assert(cerberus_ble_test_start(&c)==ESP_ERR_INVALID_STATE&&!queued);wifi_owner=CERBERUS_S3_WIFI_NONE;
    free_bytes=64000;assert(cerberus_ble_test_start(&c)==ESP_ERR_NO_MEM&&!queued&&!starts);
    free_bytes=128*1024;fail_init=true;assert(cerberus_ble_test_start(&c)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_FAIL&&!starts&&!cerberus_s3_ble_host_initialized());
    fail_init=false;assert(cerberus_ble_test_start(&c)==ESP_OK);assert(cerberus_ble_test_start(&c)==ESP_ERR_INVALID_STATE);run();
    assert(status.state==CERBERUS_TEST_COMPLETE&&starts==1&&init_calls==2&&cerberus_s3_ble_host_initialized());
    assert(cerberus_s3_ble_host_owner()==CERBERUS_S3_BLE_OWNER_NONE);
    assert(cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_SCANNER)==ESP_OK);
    c.job_id=9;assert(cerberus_ble_test_start(&c)==ESP_OK);run();
    assert(status.state==CERBERUS_TEST_ERROR&&status.error_code==ESP_ERR_INVALID_STATE);
    assert(cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_SCANNER)==ESP_OK);
    c.job_id=2;c.seconds=10;assert(cerberus_ble_test_start(&c)==ESP_OK);unsigned before=tick;run();assert(status.state==CERBERUS_TEST_CANCELLED&&tick-before<=3050);
    c.job_id=3;assert(cerberus_ble_test_start(&c)==ESP_OK);stop_at=tick+250;stop_failures=2;before=tick;run();assert(status.state==CERBERUS_TEST_ERROR&&tick-before<=400&&stop_calls>=3);
    cerberus_radio_test_status_t snapshot;before=contact_ms;tick+=100;cerberus_ble_test_status(0,&snapshot);assert(contact_ms==before);cerberus_ble_test_status(3,&snapshot);assert(contact_ms==tick);
    puts("S3 BLE worker checks passed: shared host ownership, lazy initialization, heap guard, duration, lease, stop retries, init failure and read-only status.");
}
