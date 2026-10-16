#include "system_health.h"

#include "esp_heap_caps.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

static TaskHandle_t main_task_handle;

const char *cerberus_reset_reason_label(esp_reset_reason_t reason)
{
    switch (reason) {
    case ESP_RST_POWERON:
        return "POWER ON";
    case ESP_RST_EXT:
        return "EXTERNAL";
    case ESP_RST_SW:
        return "SOFTWARE";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "INT WATCHDOG";
    case ESP_RST_TASK_WDT:
        return "TASK WATCHDOG";
    case ESP_RST_WDT:
        return "WATCHDOG";
    case ESP_RST_DEEPSLEEP:
        return "DEEP SLEEP";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_SDIO:
        return "SDIO";
    case ESP_RST_USB:
        return "USB";
    case ESP_RST_JTAG:
        return "JTAG";
    case ESP_RST_EFUSE:
        return "EFUSE ERROR";
    case ESP_RST_PWR_GLITCH:
        return "POWER GLITCH";
    case ESP_RST_CPU_LOCKUP:
        return "CPU LOCKUP";
    default:
        return "UNKNOWN";
    }
}

bool cerberus_reset_reason_is_fault(esp_reset_reason_t reason)
{
    return reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT || reason == ESP_RST_WDT ||
        reason == ESP_RST_BROWNOUT || reason == ESP_RST_EFUSE ||
        reason == ESP_RST_PWR_GLITCH || reason == ESP_RST_CPU_LOCKUP;
}

void cerberus_system_health_bind_main_task(void)
{
    main_task_handle = xTaskGetCurrentTaskHandle();
}

void cerberus_system_health_snapshot(cerberus_system_health_t *health)
{
    if (health == NULL) {
        return;
    }

    memset(health, 0, sizeof(*health));
    health->reset_reason = esp_reset_reason();
    health->previous_reset_fault = cerberus_reset_reason_is_fault(health->reset_reason);
    health->uptime_ms = (uint64_t)(esp_timer_get_time() / 1000);
    health->free_internal_bytes =
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    health->minimum_internal_bytes =
        heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    health->main_task_minimum_stack_bytes =
        (size_t)uxTaskGetStackHighWaterMark2(main_task_handle) * sizeof(StackType_t);
}

esp_err_t cerberus_crash_record_prepare(void)
{
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA,
        ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
        NULL);
    if (partition == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    uint32_t size_marker = 0U;
    esp_err_t err = esp_partition_read(partition, 0U, &size_marker, sizeof(size_marker));
    if (err != ESP_OK) {
        return err;
    }
    if (size_marker != UINT32_MAX) {
        return ESP_OK;
    }

    return esp_core_dump_image_erase();
}

esp_err_t cerberus_crash_record_inspect(cerberus_crash_record_t *record)
{
    if (record == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(record, 0, sizeof(*record));
    record->status = ESP_ERR_NOT_FOUND;

    size_t image_address = 0U;
    esp_err_t err = esp_core_dump_image_get(&image_address, &record->size_bytes);
    (void)image_address;
    if (err != ESP_OK) {
        record->status = err;
        return err;
    }
    record->present = true;

    err = esp_core_dump_image_check();
    if (err != ESP_OK) {
        record->status = err;
        return err;
    }

    esp_core_dump_summary_t summary;
    memset(&summary, 0, sizeof(summary));
    err = esp_core_dump_get_summary(&summary);
    if (err != ESP_OK) {
        record->status = err;
        return err;
    }

    record->valid = true;
    record->status = ESP_OK;
    snprintf(record->task_name, sizeof(record->task_name), "%.15s", summary.exc_task);
    record->exception_pc = summary.exc_pc;
    record->exception_cause = summary.ex_info.exc_cause;
    record->exception_address = summary.ex_info.exc_vaddr;
    record->backtrace_depth = summary.exc_bt_info.depth;
    record->backtrace_corrupted = summary.exc_bt_info.corrupted;
    size_t preview_count = summary.exc_bt_info.depth;
    if (preview_count > CERBERUS_CRASH_BACKTRACE_PREVIEW) {
        preview_count = CERBERUS_CRASH_BACKTRACE_PREVIEW;
    }
    for (size_t index = 0U; index < preview_count; ++index) {
        record->backtrace[index] = summary.exc_bt_info.bt[index];
    }
    snprintf(
        record->app_elf_sha256_prefix,
        sizeof(record->app_elf_sha256_prefix),
        "%.16s",
        (const char *)summary.app_elf_sha256);
    return ESP_OK;
}

esp_err_t cerberus_crash_record_clear(void)
{
    return esp_core_dump_image_erase();
}
