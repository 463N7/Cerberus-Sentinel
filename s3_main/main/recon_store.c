#include "recon_store.h"
#include "storage.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
static SemaphoreHandle_t recon_lock;
static uint8_t *scratch;
static uint8_t generation[40];
static uint32_t generation_epoch;
static struct {
    bool valid;
    uint8_t oui[3];
    recon_db_result_t result;
    char vendor[RECON_DB_VENDOR_SIZE];
} cache[8];
static unsigned next;
typedef struct {
    const uint8_t *header;
    uint64_t size;
} recon_source_t;
static void invalidate_cache(void)
{
    memset(cache, 0, sizeof(cache));
    memset(generation, 0, sizeof(generation));
    generation_epoch = 0U;
    next = 0U;
}
static bool read_sd(void *context, uint32_t offset, void *data, size_t size, uint64_t *file_size)
{
    size_t got = 0;
    if (cerberus_storage_read_file_chunk("/CERBERUS", "RECON.DB", offset,
            data, size, &got, file_size) != ESP_OK || got != size) return false;
    const recon_source_t *source = context;
    return source == NULL || (*file_size == source->size &&
        (offset != 0U || size != sizeof(generation) ||
            memcmp(data, source->header, sizeof(generation)) == 0));
}
static recon_db_result_t vendor_locked(const uint8_t mac[6], char vendor[RECON_DB_VENDOR_SIZE])
{
    memset(vendor, 0, RECON_DB_VENDOR_SIZE);
    if (mac[0] & 3U) return RECON_DB_PRIVATE_ADDRESS;
    uint32_t epoch;
    if (cerberus_storage_get_read_epoch(&epoch) != ESP_OK || epoch == 0U) {
        invalidate_cache();
        return RECON_DB_UNAVAILABLE;
    }
    uint8_t header[40]; uint64_t size;
    if (!read_sd(NULL, 0, header, sizeof(header), &size)) {
        invalidate_cache();
        return RECON_DB_UNAVAILABLE;
    }
    uint32_t expected_size = (uint32_t)header[28] | (uint32_t)header[29] << 8 |
        (uint32_t)header[30] << 16 | (uint32_t)header[31] << 24;
    if (size != expected_size) { invalidate_cache(); return RECON_DB_INVALID; }
    if (generation_epoch != epoch || memcmp(generation, header, sizeof(header))) {
        invalidate_cache();
        memcpy(generation, header, sizeof(header));
        generation_epoch = epoch;
    }
    recon_db_result_t result = RECON_DB_UNAVAILABLE;
    bool cached = false;
    for (unsigned i = 0; i < 8; ++i)
        if (cache[i].valid && !memcmp(cache[i].oui, mac, 3)) {
            memcpy(vendor, cache[i].vendor, RECON_DB_VENDOR_SIZE);
            result = cache[i].result;
            cached = true;
            break;
        }
    if (!cached) {
        if (!scratch) {
            scratch = heap_caps_malloc(RECON_DB_SCRATCH, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            if (!scratch) scratch = heap_caps_malloc(RECON_DB_SCRATCH, MALLOC_CAP_8BIT);
        }
        if (scratch) {
            recon_source_t source = {.header = header, .size = size};
            result = recon_db_lookup(read_sd, &source, mac, vendor, scratch);
        }
    }
    /* The epoch detects a complete USB handover/remount even if the file's
     * header is unchanged. The bound reader prevents caching a result under
     * the outer header if RECON.DB changes before the core lookup begins. */
    uint32_t finished_epoch;
    if (cerberus_storage_get_read_epoch(&finished_epoch) != ESP_OK ||
            finished_epoch != epoch) result = RECON_DB_UNAVAILABLE;
    if (result != RECON_DB_FOUND && result != RECON_DB_UNKNOWN) {
        vendor[0] = 0;
        invalidate_cache();
        return result;
    }
    if (!cached) {
        unsigned i = next++ % 8;
        cache[i].valid = true;
        cache[i].result = result;
        memcpy(cache[i].oui, mac, 3);
        memcpy(cache[i].vendor, vendor, RECON_DB_VENDOR_SIZE);
    }
    return result;
}
esp_err_t recon_store_init(void)
{
    if (!recon_lock) recon_lock = xSemaphoreCreateMutex();
    return recon_lock ? ESP_OK : ESP_ERR_NO_MEM;
}
recon_db_result_t recon_store_vendor(const uint8_t mac[6], char vendor[RECON_DB_VENDOR_SIZE])
{
    if (!vendor) return RECON_DB_INVALID;
    vendor[0] = 0;
    if (!mac || !recon_lock || xSemaphoreTake(recon_lock, pdMS_TO_TICKS(250)) != pdTRUE)
        return RECON_DB_UNAVAILABLE;
    recon_db_result_t result = vendor_locked(mac, vendor);
    xSemaphoreGive(recon_lock);
    return result;
}
recon_db_result_t recon_store_ble_vendor(const uint8_t ble_address[6],
    uint8_t address_type, char vendor[RECON_DB_VENDOR_SIZE])
{
    if (!vendor) return RECON_DB_INVALID;
    vendor[0] = 0;
    uint8_t mac[6];
    if (!ble_address || address_type > 3U) return RECON_DB_INVALID;
    if (!recon_db_ble_public_mac(ble_address, address_type, mac))
        return RECON_DB_PRIVATE_ADDRESS;
    return recon_store_vendor(mac, vendor);
}
