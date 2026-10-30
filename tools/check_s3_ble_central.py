"""Exercise S3 read-only GATT lifecycle against a deterministic NimBLE boundary."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/s3-ble-central-checks"
out.mkdir(parents=True, exist_ok=True)
(out / "fake_ble_central_platform.h").write_text(r'''
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define CONFIG_BT_NIMBLE_ENABLED 1
#define CONFIG_BT_NIMBLE_ROLE_CENTRAL 1
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(x) (x)
#define BLE_ADDR_RANDOM_ID 3
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_HS_EDONE 14
#define BLE_HS_EINVAL 3
#define BLE_GATT_CHR_PROP_READ 2U
#define BLE_GATT_CHR_PROP_WRITE 8U
#define BLE_GATT_CHR_PROP_NOTIFY 16U
#define BLE_GATT_CHR_PROP_INDICATE 32U
#define BLE_GAP_EVENT_CONNECT 0
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_TERM_FAILURE 2
#define BLE_GAP_EVENT_NOTIFY_RX 3
#define BLE_UUID_TYPE_16 16
#define BLE_UUID_TYPE_32 32
#define BLE_UUID_TYPE_128 128
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t, unsigned);
int xSemaphoreGive(SemaphoreHandle_t);
void vTaskDelay(unsigned);
int64_t esp_timer_get_time(void);
size_t heap_caps_get_free_size(unsigned);
const char *esp_err_to_name(esp_err_t);
struct ble_npl_event { void (*fn)(struct ble_npl_event *); void *arg; bool queued; };
struct ble_npl_eventq { int unused; };
void ble_npl_event_init(struct ble_npl_event *, void (*)(struct ble_npl_event *), void *);
void ble_npl_event_set_arg(struct ble_npl_event *, void *);
void *ble_npl_event_get_arg(struct ble_npl_event *);
void ble_npl_eventq_put(struct ble_npl_eventq *, struct ble_npl_event *);
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void);
typedef struct { uint8_t type; uint8_t val[6]; } ble_addr_t;
struct os_mbuf;
struct ble_gap_event {
    int type;
    struct { int status; uint16_t conn_handle; } connect;
    struct { int reason; struct { uint16_t conn_handle; } conn; } disconnect;
    struct { int status; } term_failure;
    struct { struct os_mbuf *om; uint16_t attr_handle, conn_handle; uint8_t indication; } notify_rx;
};
typedef int (*ble_gap_event_fn)(struct ble_gap_event *, void *);
int ble_hs_id_infer_auto(int, uint8_t *);
int ble_gap_connect(uint8_t, const ble_addr_t *, int32_t, const void *, ble_gap_event_fn, void *);
int ble_gap_conn_cancel(void);
int ble_gap_conn_active(void);
int ble_gap_terminate(uint16_t, uint8_t);
typedef struct { uint8_t type, pad[3]; union { uint16_t v16; uint32_t v32; uint8_t v128[16]; }; } ble_uuid_t;
typedef struct { ble_uuid_t u; } ble_uuid_any_t;
typedef struct { uint8_t type, pad[3]; uint16_t value; } fake_uuid16_t;
typedef struct { uint8_t type, pad[3]; uint32_t value; } fake_uuid32_t;
typedef struct { uint8_t type, pad[3]; uint8_t value[16]; } fake_uuid128_t;
#define BLE_UUID16(x) ((const fake_uuid16_t *)(x))
#define BLE_UUID32(x) ((const fake_uuid32_t *)(x))
#define BLE_UUID128(x) ((const fake_uuid128_t *)(x))
int ble_uuid_from_str(ble_uuid_any_t *, const char *);
int ble_uuid_cmp(const ble_uuid_t *, const ble_uuid_t *);
struct ble_gatt_error { int status; };
struct ble_gatt_svc { uint16_t start_handle, end_handle; struct { ble_uuid_t u; } uuid; };
struct ble_gatt_chr { uint16_t def_handle, val_handle; uint8_t properties; struct { ble_uuid_t u; } uuid; };
struct ble_gatt_dsc { uint16_t handle; ble_uuid_any_t uuid; };
struct os_mbuf { size_t len; uint8_t data[300]; };
struct ble_gatt_attr { struct os_mbuf *om; };
#define OS_MBUF_PKTLEN(x) ((x)->len)
int os_mbuf_copydata(const struct os_mbuf *, int, int, void *);
typedef int (*ble_gatt_svc_fn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_svc *, void *);
typedef int (*ble_gatt_chr_fn)(uint16_t, const struct ble_gatt_error *, const struct ble_gatt_chr *, void *);
typedef int (*ble_gatt_read_fn)(uint16_t, const struct ble_gatt_error *, struct ble_gatt_attr *, void *);
typedef int (*ble_gatt_dsc_fn)(uint16_t, const struct ble_gatt_error *, uint16_t,
    const struct ble_gatt_dsc *, void *);
int ble_gattc_disc_all_svcs(uint16_t, ble_gatt_svc_fn, void *);
int ble_gattc_disc_svc_by_uuid(uint16_t, const ble_uuid_t *, ble_gatt_svc_fn, void *);
int ble_gattc_disc_all_chrs(uint16_t, uint16_t, uint16_t, ble_gatt_chr_fn, void *);
int ble_gattc_disc_all_dscs(uint16_t, uint16_t, uint16_t, ble_gatt_dsc_fn, void *);
int ble_gattc_read(uint16_t, uint16_t, ble_gatt_read_fn, void *);
int ble_gattc_write_flat(uint16_t, uint16_t, const void *, uint16_t,
    ble_gatt_read_fn, void *);
''')
for name in ("sdkconfig.h", "esp_heap_caps.h", "esp_timer.h", "freertos/FreeRTOS.h",
             "freertos/semphr.h", "freertos/task.h", "host/ble_gatt.h", "host/ble_hs.h",
             "host/util/util.h", "nimble/nimble_npl.h", "nimble/nimble_port.h",
             "os/os_mbuf.h"):
    path = out / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('#include "fake_ble_central_platform.h"\n')
(out / "esp_log.h").write_text('''#include "fake_ble_central_platform.h"
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
''')
exe = out / "s3-ble-central-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(out), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(root / "tools/host_checks/s3_ble_central_check.c"),
    str(root / "s3_main/main/s3_ble_central.c"), "-o", str(exe),
], check=True)
subprocess.run([str(exe)], check=True)

# The bridge/disabled build exposes the same additive API without importing or
# initializing a host. Compile the actual disabled production branch as well.
disabled = out / "disabled"
disabled.mkdir(exist_ok=True)
(disabled / "sdkconfig.h").write_text("/* No NimBLE central in this target. */\n")
(disabled / "check.c").write_text(r'''
#include "s3_ble_central.h"
#include <stdio.h>
#include <string.h>
int main(void)
{
    cerberus_s3_ble_central_diagnostics_t result;
    const cerberus_s3_ble_central_diagnostics_t zero = {0};
    memset(&result, 0xa5, sizeof(result));
    if (cerberus_s3_ble_central_get_diagnostics(NULL) != ESP_ERR_INVALID_ARG ||
        cerberus_s3_ble_central_get_diagnostics(&result) != ESP_ERR_NOT_SUPPORTED ||
        memcmp(&result, &zero, sizeof(result)) != 0) return 1;
    puts("Disabled central diagnostics: explicit unsupported, cleared output, no host dependencies passed.");
    return 0;
}
''')
disabled_exe = disabled / "s3-ble-central-disabled-check.exe"
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
    "-I", str(disabled), "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "shared"), "-I", str(root / "s3_main/main"),
    str(disabled / "check.c"), str(root / "s3_main/main/s3_ble_central.c"),
    "-o", str(disabled_exe),
], check=True)
subprocess.run([str(disabled_exe)], check=True)
