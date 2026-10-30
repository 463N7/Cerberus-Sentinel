"""Compile the S3 BLE advertiser against a deterministic NimBLE boundary."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-ble-attack-checks"
out.mkdir(parents=True, exist_ok=True)
(out / "fake_ble_attack_platform.h").write_text(r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_NIMBLE_ROLE_PERIPHERAL 1
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
#define BLE_HS_EALREADY 14
#define BLE_HS_ENOTCONN 7
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_CONN_MODE_NON 0
#define BLE_GAP_CONN_MODE_UND 1
#define BLE_GAP_DISC_MODE_GEN 2
#define BLE_OWN_ADDR_RANDOM 1
typedef unsigned TickType_t;
typedef int BaseType_t;
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
BaseType_t xTaskCreate(void (*)(void *), const char *, unsigned, void *, unsigned, void *);
void vTaskDelay(TickType_t);
void vTaskDelete(void *);
int64_t esp_timer_get_time(void);
size_t heap_caps_get_free_size(unsigned);
struct ble_gap_event {
    int type;
    struct { int status; uint16_t conn_handle; } connect;
    struct { struct { uint16_t conn_handle; } conn; } disconnect;
};
struct ble_gap_adv_params { int conn_mode, disc_mode; uint16_t itvl_min, itvl_max; };
int ble_hs_id_set_rnd(const uint8_t *);
int ble_gap_adv_set_data(const uint8_t *, int);
int ble_gap_adv_start(uint8_t, const void *, int32_t, const struct ble_gap_adv_params *,
    int (*)(struct ble_gap_event *, void *), void *);
int ble_gap_adv_stop(void);
int ble_gap_adv_active(void);
int ble_gap_terminate(uint16_t, uint8_t);
''')
for name in ("sdkconfig.h", "esp_heap_caps.h", "esp_random.h", "esp_timer.h",
             "freertos/FreeRTOS.h", "freertos/semphr.h", "freertos/task.h",
             "host/ble_hs.h", "host/ble_hs_id.h", "nimble/ble.h"):
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "fake_ble_attack_platform.h"\n')
exe = out / "s3-ble-attack-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra",
    "-Werror", "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_attack_check.c"),
    str(root / "s3_main/main/s3_ble_attack.c"),
    str(root / "shared/ble_attack_payload.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)
