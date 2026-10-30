"""Exercise production S3 MultiSSID ownership, snapshots and receive-only follow."""
import os
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
out = root / "artifacts/wifi-multissid-session-checks"
stubs = out / "stubs"
(stubs / "freertos").mkdir(parents=True, exist_ok=True)
(stubs / "freertos/FreeRTOS.h").write_text("""#pragma once
#include <stdint.h>
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
""", encoding="utf-8")
(stubs / "freertos/semphr.h").write_text("""#pragma once
#include <stdint.h>
typedef void *SemaphoreHandle_t;
SemaphoreHandle_t xSemaphoreCreateMutex(void);
int xSemaphoreTake(SemaphoreHandle_t mutex, uint32_t ticks);
int xSemaphoreGive(SemaphoreHandle_t mutex);
""", encoding="utf-8")
(stubs / "esp_heap_caps.h").write_text("""#pragma once
#include <stddef.h>
#define MALLOC_CAP_8BIT 2U
#define MALLOC_CAP_SPIRAM 4U
void *heap_caps_calloc(size_t count, size_t size, unsigned caps);
void heap_caps_free(void *pointer);
""", encoding="utf-8")
exe = out / "wifi-multissid-session-check.exe"
env = dict(os.environ, ZIG_GLOBAL_CACHE_DIR=str(out / "zig-cache"))
subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1",
    "-Wall", "-Wextra", "-Werror", "-I", str(stubs),
    "-I", str(root / "tools/host_checks/stubs"),
    "-I", str(root / "s3_main/main"), "-I", str(root / "shared"),
    str(root / "tools/host_checks/wifi_multissid_session_check.c"),
    str(root / "s3_main/main/wifi_multissid_session.c"),
    str(root / "s3_main/main/wifi_beacon_session.c"),
    str(root / "s3_main/main/wifi_pinescan_session.c"),
    str(root / "shared/wifi_pinescan.c"),
    str(root / "shared/wifi_beacon_analysis.c"), "-o", str(exe)], check=True, env=env)
subprocess.run([str(exe)], check=True)
