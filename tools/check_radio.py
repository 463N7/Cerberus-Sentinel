"""Compile the real C5 job worker with deterministic fake time/driver failures."""
from pathlib import Path
import subprocess
import sys
root=Path(__file__).resolve().parents[1]
out=root/"artifacts/radio-checks"
out.mkdir(parents=True,exist_ok=True)
header=r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#ifndef ESP_ERR_NOT_FOUND
#define ESP_ERR_NOT_FOUND 0x105
#endif
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_NOT_SUPPORTED 0x106
#define ESP_ERR_WIFI_WOULD_BLOCK 0x300e
typedef void *SemaphoreHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) do { if(*(p)) __builtin_trap(); *(p)=1; } while(0)
#define portEXIT_CRITICAL(p) do { if(!*(p)) __builtin_trap(); *(p)=0; } while(0)
#define WIFI_PROMIS_FILTER_MASK_MGMT 1
typedef struct {unsigned filter_mask;} wifi_promiscuous_filter_t;
esp_err_t esp_wifi_set_promiscuous_filter(const wifi_promiscuous_filter_t*);
esp_err_t esp_wifi_set_promiscuous(bool);
typedef int wifi_band_mode_t;
typedef int wifi_second_chan_t;
#define WIFI_BAND_MODE_AUTO 3
#define WIFI_BAND_MODE_2G_ONLY 1
#define WIFI_BAND_MODE_5G_ONLY 2
#define WIFI_SECOND_CHAN_NONE 0
#define WIFI_IF_STA 0
#define WIFI_PHY_RATE_6M 6
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,void*);
void vTaskDelete(void*);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
size_t heap_caps_get_free_size(unsigned);
void esp_fill_random(void*,size_t);
esp_err_t esp_wifi_get_band_mode(wifi_band_mode_t*);
esp_err_t esp_wifi_get_channel(uint8_t*,wifi_second_chan_t*);
esp_err_t esp_wifi_get_mac(int,uint8_t*);
esp_err_t esp_wifi_set_band_mode(wifi_band_mode_t);
esp_err_t esp_wifi_set_channel(uint8_t,wifi_second_chan_t);
esp_err_t esp_wifi_config_80211_tx_rate(int,int);
esp_err_t esp_wifi_80211_tx(int,const void*,int,bool);
struct ble_gap_event {int type;};
struct ble_gap_adv_params {int conn_mode,disc_mode,itvl_min,itvl_max;};
struct fake_host_config {void(*reset_cb)(int);void(*sync_cb)(void);};
extern struct fake_host_config ble_hs_cfg;
#define BLE_GAP_CONN_MODE_NON 0
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_HS_EALREADY 8
int nimble_port_init(void);
void nimble_port_run(void);
void nimble_port_freertos_init(void(*)(void*));
void nimble_port_freertos_deinit(void);
int ble_hs_util_ensure_addr(int);
int ble_hs_id_infer_auto(int,uint8_t*);
int ble_gap_adv_set_data(const uint8_t*,int);
int ble_gap_adv_start(uint8_t,const void*,int32_t,const struct ble_gap_adv_params*,int(*)(struct ble_gap_event*,void*),void*);
int ble_gap_adv_stop(void);
int ble_gap_adv_active(void);
'''
(out/"fake_radio_platform.h").write_text(header)
for name in ["esp_wifi.h","esp_random.h","esp_timer.h","esp_heap_caps.h","freertos/FreeRTOS.h","freertos/semphr.h","freertos/task.h",
             "nimble/nimble_port.h","nimble/nimble_port_freertos.h","host/ble_hs.h","host/util/util.h"]:
    path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text('#include "fake_radio_platform.h"\n')
log_header=out/"esp_log.h"
log_header.write_text('#pragma once\n#include <stdio.h>\n#define ESP_LOGI(tag, format, ...) do { if (0) fprintf(stderr, format, __VA_ARGS__); } while (0)\n')
mbedtls_header=out/"mbedtls/platform_util.h"
mbedtls_header.parent.mkdir(parents=True,exist_ok=True)
mbedtls_header.write_text('#pragma once\n#include <stddef.h>\nvoid mbedtls_platform_zeroize(void *, size_t);\n')
exe=out/"radio-check.exe"
subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-O1","-Wall","-Wextra","-Werror",
    "-I",str(out),"-I",str(root/"tools/host_checks/stubs"),"-I",str(root/"shared"),"-I",str(root/"c5_firmware/main"),
    str(root/"tools/host_checks/radio_service_check.c"),str(root/"shared/radio_test.c"),
    str(root/"shared/wifi_ap_job.c"),str(root/"shared/wifi_ap_clone.c"),
    str(root/"shared/wifi_sae_commit.c"),"-o",str(exe)],check=True)
subprocess.run([str(exe)],check=True)
(out/"sdkconfig.h").write_text("#define CONFIG_BT_NIMBLE_ENABLED 1\n")
exe=out/"ble-check.exe"
subprocess.run([sys.executable,"-m","ziglang","cc","-std=c11","-O1","-Wall","-Wextra","-Werror",
    "-I",str(out),"-I",str(root/"tools/host_checks/stubs"),"-I",str(root/"shared"),"-I",str(root/"s3_main/main"),
    str(root/"tools/host_checks/ble_test_check.c"),str(root/"s3_main/main/s3_ble_host.c"),
    str(root/"shared/radio_test.c"),"-o",str(exe)],check=True)
subprocess.run([str(exe)],check=True)
