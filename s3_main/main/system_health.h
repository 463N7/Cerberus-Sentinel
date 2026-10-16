#pragma once

#include "esp_err.h"
#include "esp_system.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    esp_reset_reason_t reset_reason;
    bool previous_reset_fault;
    uint64_t uptime_ms;
    size_t free_internal_bytes;
    size_t minimum_internal_bytes;
    size_t main_task_minimum_stack_bytes;
} cerberus_system_health_t;

#define CERBERUS_HEAP_WARNING_BYTES (32U * 1024U)
#define CERBERUS_MAIN_STACK_WARNING_BYTES 1024U

#define CERBERUS_CRASH_BACKTRACE_PREVIEW 3U

typedef struct {
    bool present;
    bool valid;
    esp_err_t status;
    size_t size_bytes;
    char task_name[16];
    uint32_t exception_pc;
    uint32_t exception_cause;
    uint32_t exception_address;
    uint32_t backtrace_depth;
    bool backtrace_corrupted;
    uint32_t backtrace[CERBERUS_CRASH_BACKTRACE_PREVIEW];
    char app_elf_sha256_prefix[17];
} cerberus_crash_record_t;

const char *cerberus_reset_reason_label(esp_reset_reason_t reason);
bool cerberus_reset_reason_is_fault(esp_reset_reason_t reason);
void cerberus_system_health_bind_main_task(void);
void cerberus_system_health_snapshot(cerberus_system_health_t *health);
esp_err_t cerberus_crash_record_prepare(void);
esp_err_t cerberus_crash_record_inspect(cerberus_crash_record_t *record);
esp_err_t cerberus_crash_record_clear(void);

#ifdef __cplusplus
}
#endif
