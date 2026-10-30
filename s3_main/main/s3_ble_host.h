#pragma once

#include "esp_err.h"
#include <stdbool.h>

typedef enum {
    CERBERUS_S3_BLE_OWNER_NONE = 0,
    CERBERUS_S3_BLE_OWNER_SCANNER,
    CERBERUS_S3_BLE_OWNER_ADVERTISER,
    CERBERUS_S3_BLE_OWNER_CENTRAL,
} cerberus_s3_ble_owner_t;

/* One NimBLE host serves passive Recon scanning and the Radio Lab advertiser.
 * Claim before a GAP operation; release only after discovery/advertising ends. */
esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t owner);
esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t owner);
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void);
bool cerberus_s3_ble_host_initialized(void);
bool cerberus_s3_ble_host_synced(void);
/* Changes whenever the host resets; an in-flight client operation becomes stale. */
unsigned cerberus_s3_ble_host_epoch(void);
