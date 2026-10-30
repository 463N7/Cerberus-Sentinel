"""Exercise the real S3 owner, passive receiver and frame classifier on a host."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/s3-rx-checks';out.mkdir(parents=True,exist_ok=True)
(out/'fake_rx_platform.h').write_text(r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_ERR_WIFI_NOT_STARTED 0x3002
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define pdPASS 1
#define pdMS_TO_TICKS(x) (x)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void enter(int*); void leave(int*);
#define portENTER_CRITICAL(p) enter(p)
#define portEXIT_CRITICAL(p) leave(p)
typedef struct {int static_rx_buf_num,dynamic_rx_buf_num,dynamic_tx_buf_num,rx_ba_win,ampdu_rx_enable,ampdu_tx_enable,amsdu_tx_enable;} wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
#define WIFI_STORAGE_RAM 0
#define WIFI_MODE_NULL 0
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
#define WIFI_PROMIS_FILTER_MASK_DATA 2
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
typedef enum {WIFI_PKT_MGMT,WIFI_PKT_DATA} wifi_promiscuous_pkt_type_t;
typedef struct {struct {unsigned sig_len,rx_state,channel;int8_t rssi;} rx_ctrl;uint8_t payload[160];} wifi_promiscuous_pkt_t;
typedef void(*rx_callback)(void*,wifi_promiscuous_pkt_type_t);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_err_t esp_wifi_init(wifi_init_config_t*);
esp_err_t esp_wifi_set_storage(int);
esp_err_t esp_wifi_set_mode(int);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_set_channel(uint8_t,int);
esp_err_t esp_wifi_set_promiscuous_filter(wifi_promiscuous_filter_t*);
esp_err_t esp_wifi_set_promiscuous_rx_cb(rx_callback);
esp_err_t esp_wifi_set_promiscuous(bool);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_deinit(void);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelay(unsigned);
void vTaskDelete(void*);
int64_t esp_timer_get_time(void);
size_t heap_caps_get_free_size(unsigned);
''')
for name in ['esp_netif.h','esp_event.h','esp_wifi.h','esp_heap_caps.h','esp_timer.h','freertos/FreeRTOS.h','freertos/task.h']:
    path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "fake_rx_platform.h"\n')
exe=out/'s3-rx-check.exe'
subprocess.run([sys.executable,'-m','ziglang','cc','-std=c11','-O1','-Wall','-Wextra','-Werror',
    '-I',str(out),'-I',str(root/'tools/host_checks/stubs'),'-I',str(root/'s3_main/main'),'-I',str(root/'shared'),
    str(root/'tools/host_checks/s3_rx_check.c'),str(root/'shared/wifi_ap_job.c'),str(root/'shared/wifi_ap_rx.c'),
    str(root/'shared/wifi_ap_clone.c'),str(root/'shared/wifi_sae_commit.c'),'-o',str(exe)],check=True)
subprocess.run([str(exe)],check=True)
