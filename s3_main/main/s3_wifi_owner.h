#pragma once
#include "esp_err.h"
typedef enum {
    CERBERUS_S3_WIFI_NONE,
    CERBERUS_S3_WIFI_WEB,
    CERBERUS_S3_WIFI_RECEIVER,
    CERBERUS_S3_WIFI_PORTAL,
} cerberus_s3_wifi_owner_t;
/* Claim before touching the driver or hardware entropy source. Release only
 * after successful cleanup. Failed cleanup retains ownership for retry/reset. */
esp_err_t cerberus_s3_wifi_claim(cerberus_s3_wifi_owner_t owner);
esp_err_t cerberus_s3_wifi_release(cerberus_s3_wifi_owner_t owner);
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void);
esp_err_t cerberus_s3_wifi_prepare(cerberus_s3_wifi_owner_t owner);
