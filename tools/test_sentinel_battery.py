#!/usr/bin/env python3
"""Exercise the actual MAX17048 battery driver against bounded, faulting I2C stubs."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]

STUBS = r'''
#pragma once
#include <stddef.h>
#include <stdint.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
typedef void *i2c_master_dev_handle_t;
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device,
    const uint8_t *write_data, size_t write_size, uint8_t *read_data,
    size_t read_size, int timeout_ms);
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void battery_test_enter(void *lock);
void battery_test_exit(void *lock);
#define portENTER_CRITICAL(lock) battery_test_enter(lock)
#define portEXIT_CRITICAL(lock) battery_test_exit(lock)
'''


def main():
    with tempfile.TemporaryDirectory(prefix="sentinel-battery-") as directory:
        path = Path(directory)
        (path / "stubs.h").write_text(STUBS, encoding="utf-8")
        for name in ("esp_err.h", "driver/i2c_master.h", "freertos/FreeRTOS.h"):
            header = path / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('#include "stubs.h"\n', encoding="utf-8")
        executable = path / ("battery.exe" if os.name == "nt" else "battery")
        subprocess.run([sys.executable, "-m", "ziglang", "cc", "-std=c11", "-O1", "-g",
            "-Wall", "-Wextra", "-Werror", "-I", str(path),
            "-I", str(ROOT / "s3_main/main"),
            str(ROOT / "tools/tests/sentinel_battery_test.c"),
            str(ROOT / "s3_main/main/battery.c"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    main()
