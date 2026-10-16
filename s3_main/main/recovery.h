#pragma once

#include "esp_err.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Reports whether the S3 ROM download path is still permitted by eFuse policy. */
bool cerberus_recovery_download_mode_available(void);

/* Forces the next reset into the S3 ROM download loader. Never returns on success. */
esp_err_t cerberus_recovery_enter_download_mode(void);

#ifdef __cplusplus
}
#endif
