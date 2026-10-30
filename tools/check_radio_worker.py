"""Exercise the real shared 802.15.4 worker with deterministic platform fakes."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/radio-worker-checks"
out.mkdir(parents=True, exist_ok=True)
header = r'''
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#define ESP_ERR_TIMEOUT 0x107
#define IRAM_ATTR
typedef void *SemaphoreHandle_t;
typedef void *QueueHandle_t;
typedef void *TaskHandle_t;
typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef struct {uint8_t channel; int8_t rssi; uint8_t lqi;} esp_ieee802154_frame_info_t;
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(x) (x)
#define portYIELD_FROM_ISR(x) ((void)(x))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_LOGW(tag,...) ((void)(tag))
SemaphoreHandle_t xSemaphoreCreateMutex(void);
void vSemaphoreDelete(SemaphoreHandle_t);
int xSemaphoreTake(SemaphoreHandle_t,unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
QueueHandle_t xQueueCreate(unsigned,unsigned);
void vQueueDelete(QueueHandle_t);
int xQueueReceive(QueueHandle_t,void*,unsigned);
int xQueueSendFromISR(QueueHandle_t,const void*,BaseType_t*);
int xQueueReset(QueueHandle_t);
int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);
void xTaskNotifyGive(TaskHandle_t);
unsigned ulTaskNotifyTake(int,unsigned);
TickType_t xTaskGetTickCount(void);
void vTaskDelay(unsigned);
esp_err_t esp_ieee802154_enable(void);
esp_err_t esp_ieee802154_disable(void);
esp_err_t esp_ieee802154_sleep(void);
esp_err_t esp_ieee802154_set_channel(uint8_t);
esp_err_t esp_ieee802154_set_coordinator(bool);
esp_err_t esp_ieee802154_set_promiscuous(bool);
esp_err_t esp_ieee802154_receive(void);
esp_err_t esp_ieee802154_receive_handle_done(uint8_t*);
esp_err_t esp_ieee802154_energy_detect(uint32_t);
'''
(out / "fake_worker_platform.h").write_text(header)
for name in ["esp_attr.h", "esp_ieee802154.h", "esp_log.h", "freertos/FreeRTOS.h",
             "freertos/queue.h", "freertos/semphr.h", "freertos/task.h"]:
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "fake_worker_platform.h"\n')
exe = out / "radio-worker-check.exe"
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"), "-I", str(root / "shared"),
    "-I", str(root / "c5_firmware/main"), str(root / "tools/host_checks/radio_worker_check.c"),
    "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
