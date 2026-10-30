"""Run the real S3 capture worker against deterministic C5 and SD edges."""
from pathlib import Path
import subprocess
import sys
import os

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-capture-session-checks"
stubs = out / "stubs"
(stubs / "freertos").mkdir(parents=True, exist_ok=True)
(stubs / "sdmmc_cmd.h").write_text(
    "#pragma once\ntypedef struct sdmmc_card_t sdmmc_card_t;\n", encoding="utf-8")
(stubs / "esp_heap_caps.h").write_text(
    """#pragma once
#include <stddef.h>
#define MALLOC_CAP_INTERNAL 1U
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_SPIRAM 4U
void *heap_caps_malloc(size_t size, unsigned caps);
void heap_caps_free(void *pointer);
""", encoding="utf-8")
(stubs / "freertos/FreeRTOS.h").write_text(
    """#pragma once
#include <stdint.h>
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void fake_capture_lock_enter(portMUX_TYPE *lock);
void fake_capture_lock_exit(portMUX_TYPE *lock);
#define portENTER_CRITICAL(lock) fake_capture_lock_enter(lock)
#define portEXIT_CRITICAL(lock) fake_capture_lock_exit(lock)
#define pdPASS 1
#define pdMS_TO_TICKS(milliseconds) (((milliseconds) + 9U) / 10U)
""", encoding="utf-8")
(stubs / "freertos/task.h").write_text(
    """#pragma once
#include <stdint.h>
typedef void (*TaskFunction_t)(void *);
int xTaskCreate(TaskFunction_t function,const char *name,uint32_t stack,
    void *argument,unsigned priority,void *handle);
void vTaskDelay(uint32_t ticks);
void vTaskDelete(void *task);
""", encoding="utf-8")
exe = out / "wifi-capture-session-check.exe"
environment = os.environ.copy()
environment["ZIG_GLOBAL_CACHE_DIR"] = str(out / "zig-global-cache")
environment["ZIG_LOCAL_CACHE_DIR"] = str(out / "zig-local-cache")
subprocess.run([
    sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(stubs),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_capture_session_check.c"),
    str(root / "s3_main/main/wifi_capture_session.c"),
    str(root / "shared/wifi_pcap.c"), "-o", str(exe),
], check=True, env=environment)
subprocess.run([str(exe)], check=True, timeout=30)
