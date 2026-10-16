#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t c5_control_init(void);
void c5_control_hold_reset(void);
void c5_control_reset_normal(void);
void c5_control_enter_bootloader(void);

#ifdef __cplusplus
}
#endif
