#include "s3_ble_host.h"
#include "sdkconfig.h"

#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include <stdatomic.h>

static atomic_int current_owner;
static atomic_bool initialized;
static atomic_bool synced;
static atomic_uint host_epoch;

static void host_sync(void)
{
    atomic_store(&synced, ble_hs_util_ensure_addr(0) == 0);
}

static void host_reset(int reason)
{
    (void)reason;
    atomic_store(&synced, false);
    atomic_fetch_add(&host_epoch, 1U);
    /* GAP procedures and connections are gone after a controller reset. */
    atomic_store(&current_owner, CERBERUS_S3_BLE_OWNER_NONE);
}

static void host_task(void *unused)
{
    (void)unused;
    nimble_port_run();
    atomic_store(&synced, false);
    /* The host is intentionally kept for this firmware session. A stopped
     * host cannot be silently reused by an already initialized scanner. */
    nimble_port_freertos_deinit();
}

cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void)
{
    return (cerberus_s3_ble_owner_t)atomic_load(&current_owner);
}

bool cerberus_s3_ble_host_initialized(void) { return atomic_load(&initialized); }
bool cerberus_s3_ble_host_synced(void) { return atomic_load(&synced); }
unsigned cerberus_s3_ble_host_epoch(void) { return atomic_load(&host_epoch); }

esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t owner)
{
    if (owner != CERBERUS_S3_BLE_OWNER_SCANNER && owner != CERBERUS_S3_BLE_OWNER_ADVERTISER &&
        owner != CERBERUS_S3_BLE_OWNER_CENTRAL)
        return ESP_ERR_INVALID_ARG;
    int expected = CERBERUS_S3_BLE_OWNER_NONE;
    if (!atomic_compare_exchange_strong(&current_owner, &expected, owner))
        return ESP_ERR_INVALID_STATE;
    if (!atomic_load(&initialized)) {
        esp_err_t err = nimble_port_init();
        if (err != ESP_OK) {
            atomic_store(&current_owner, CERBERUS_S3_BLE_OWNER_NONE);
            return err;
        }
        ble_hs_cfg.reset_cb = host_reset;
        ble_hs_cfg.sync_cb = host_sync;
        atomic_store(&initialized, true);
        nimble_port_freertos_init(host_task);
    }
    for (unsigned i = 0; i < 200U && !atomic_load(&synced); ++i)
        vTaskDelay(pdMS_TO_TICKS(10));
    if (!atomic_load(&synced)) {
        atomic_store(&current_owner, CERBERUS_S3_BLE_OWNER_NONE);
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t owner)
{
    if (owner != CERBERUS_S3_BLE_OWNER_SCANNER && owner != CERBERUS_S3_BLE_OWNER_ADVERTISER &&
        owner != CERBERUS_S3_BLE_OWNER_CENTRAL)
        return ESP_ERR_INVALID_ARG;
    int expected = owner;
    return atomic_compare_exchange_strong(&current_owner, &expected,
        CERBERUS_S3_BLE_OWNER_NONE) ? ESP_OK : ESP_ERR_INVALID_STATE;
}
#else
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void) { return CERBERUS_S3_BLE_OWNER_NONE; }
bool cerberus_s3_ble_host_initialized(void) { return false; }
bool cerberus_s3_ble_host_synced(void) { return false; }
unsigned cerberus_s3_ble_host_epoch(void) { return 0U; }
esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t owner)
{ (void)owner; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t owner)
{ (void)owner; return ESP_ERR_NOT_SUPPORTED; }
#endif
