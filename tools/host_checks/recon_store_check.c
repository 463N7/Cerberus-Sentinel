#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_err.h"
#include "esp_check.h"
#include "recon_store.h"

#define TAG "recon-check"
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define pdTRUE 1
#define pdMS_TO_TICKS(ms) (ms)
#define MALLOC_CAP_SPIRAM 4U
#define MALLOC_CAP_8BIT 2U
#define CERBERUS_STORAGE_MOUNT_PATH "/fixture"
#define CERBERUS_STORAGE_FIRMWARE_PATH "/CERBERUS/FIRMWARE"
#define CERBERUS_STORAGE_SCRIPTS_PATH "/CERBERUS/SCRIPTS"
#define CERBERUS_SD_DAT3_TEST_GPIO -1
#define CERBERUS_SD_CLK_GPIO 1
#define CERBERUS_SD_CMD_GPIO 2
#define CERBERUS_SD_D0_GPIO 3
#define SD_MMC_SLOT 1
#define SD_MMC_MAX_FREQ_KHZ 20000
#define SD_MAX_OPEN_FILES 8
#define SD_MOUNT_ATTEMPTS 2
#define SDMMC_SLOT_FLAG_INTERNAL_PULLUP 1
#define SDMMC_HOST_DEFAULT() ((sdmmc_host_t){0})
#define SDMMC_SLOT_CONFIG_DEFAULT() ((sdmmc_slot_config_t){0})
typedef struct { int slot, max_freq_khz; } sdmmc_host_t;
typedef struct { int width, clk, cmd, d0, flags; } sdmmc_slot_config_t;
typedef struct { bool format_if_mount_failed; int max_files, allocation_unit_size; }
    esp_vfs_fat_sdmmc_mount_config_t;
typedef struct { int unused; } sdmmc_card_t;
typedef enum {
    CERBERUS_STORAGE_OWNER_NONE, CERBERUS_STORAGE_OWNER_FIRMWARE,
    CERBERUS_STORAGE_OWNER_USB,
} cerberus_storage_owner_t;
typedef int *SemaphoreHandle_t;
static int recon_mutex_object;
static bool storage_locked, storage_busy, recon_locked, recon_busy;
static bool sd_mounted, sd_bus_ready, wardrive_busy;
static cerberus_storage_owner_t storage_owner;
static uint32_t sd_read_epoch;
static sdmmc_card_t card_object, *sd_card;
static FILE *sd_wifi_capture_file;
static char sd_operation_source_path[768];
static unsigned mount_failures;
static bool unmount_failure, alloc_failure, psram_failure, mutex_failure;
static unsigned psram_allocs, internal_allocs;

static esp_err_t take_storage_mutex(void)
{
    assert(!storage_locked);
    if (storage_busy) return ESP_ERR_TIMEOUT;
    storage_locked = true;
    return ESP_OK;
}
static void release_storage_mutex(void) { assert(storage_locked); storage_locked = false; }
static void release_sd_bus(void) { sd_bus_ready = false; }
static void prepare_sd_lines(void) {}
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static bool wardrive_storage_busy(void) { return wardrive_busy; }
static esp_err_t ensure_directory_exists(const char *path) { (void)path; return ESP_OK; }
static void sdmmc_card_print_info(FILE *out, sdmmc_card_t *card)
{ (void)out; assert(card == &card_object); }
static esp_err_t esp_vfs_fat_sdmmc_mount(const char *path, sdmmc_host_t *host,
    sdmmc_slot_config_t *slot, esp_vfs_fat_sdmmc_mount_config_t *config, sdmmc_card_t **card)
{
    (void)path; (void)host; (void)slot; (void)config;
    assert(storage_locked && !sd_mounted);
    if (mount_failures) { --mount_failures; return ESP_FAIL; }
    *card = &card_object;
    return ESP_OK;
}
static esp_err_t esp_vfs_fat_sdcard_unmount(const char *path, sdmmc_card_t *card)
{
    (void)path; assert(storage_locked && card == &card_object);
    return unmount_failure ? ESP_FAIL : ESP_OK;
}

#include "storage_epoch_impl.h"

static SemaphoreHandle_t xSemaphoreCreateMutex(void)
{ return mutex_failure ? NULL : &recon_mutex_object; }
static int xSemaphoreTake(SemaphoreHandle_t handle, unsigned ticks)
{
    (void)ticks; assert(handle == &recon_mutex_object && !recon_locked);
    if (recon_busy) return 0;
    recon_locked = true;
    return pdTRUE;
}
static void xSemaphoreGive(SemaphoreHandle_t handle)
{ assert(handle == &recon_mutex_object && recon_locked); recon_locked = false; }
static void *heap_caps_malloc(size_t size, unsigned caps)
{
    assert(size == RECON_DB_SCRATCH);
    if (caps & MALLOC_CAP_SPIRAM) ++psram_allocs;
    else ++internal_allocs;
    return alloc_failure || ((caps & MALLOC_CAP_SPIRAM) && psram_failure) ? NULL : malloc(size);
}

typedef struct { uint8_t *data; size_t size; } database_t;
static database_t first, second, real, *active;
static unsigned reads, inject_at;
static bool io_failure, short_read;
typedef enum { INJECT_NONE, INJECT_SWAP, INJECT_REMOUNT, INJECT_USB,
               INJECT_BAD_FINAL_SIZE, INJECT_CORRUPT_HEADER } injection_t;
static injection_t injection;
static bool perform_read(uint32_t offset, void *data, size_t capacity, uint64_t *size)
{
    ++reads;
    if (inject_at && reads == inject_at) {
        if (injection == INJECT_SWAP) active = &second;
        if (injection == INJECT_REMOUNT || injection == INJECT_USB) {
            assert(cerberus_storage_reserve_for_usb() == ESP_OK);
            if (injection == INJECT_REMOUNT) {
                assert(cerberus_storage_release_from_usb() == ESP_OK);
                assert(cerberus_storage_mount() == ESP_OK);
            }
        }
    }
    *size = active->size;
    if (io_failure || !sd_mounted || offset > active->size || capacity > active->size - offset)
        return false;
    memcpy(data, active->data + offset, capacity);
    if (inject_at && reads == inject_at) {
        if (injection == INJECT_BAD_FINAL_SIZE) ++*size;
        if (injection == INJECT_CORRUPT_HEADER && offset == 0U) ((uint8_t *)data)[8] ^= 1U;
    }
    return true;
}
static esp_err_t cerberus_storage_read_file_chunk(const char *path, const char *name,
    uint64_t offset, uint8_t *data, size_t capacity, size_t *got, uint64_t *size)
{
    assert(!storage_locked); /* Recon never holds a filesystem lease over the search. */
    assert(!strcmp(path, "/CERBERUS") && !strcmp(name, "RECON.DB") && offset <= UINT32_MAX);
    bool ok = perform_read((uint32_t)offset, data, capacity, size);
    *got = ok ? capacity - (short_read ? 1U : 0U) : 0U;
    return ok ? ESP_OK : ESP_FAIL;
}
static bool read_direct(void *context, uint32_t offset, void *data, size_t capacity, uint64_t *size)
{ (void)context; return perform_read(offset, data, capacity, size); }

#include "recon_store_impl.h"

static database_t load_database(const char *name)
{
    FILE *file = fopen(name, "rb"); assert(file);
    assert(fseek(file, 0, SEEK_END) == 0); long size = ftell(file); assert(size >= 40);
    rewind(file); database_t result = {malloc((size_t)size), (size_t)size};
    assert(result.data && fread(result.data, 1U, result.size, file) == result.size);
    assert(fclose(file) == 0); return result;
}
static void reset_store(database_t *db)
{
    assert(!storage_locked && !recon_locked);
    active = db; reads = inject_at = 0U; injection = INJECT_NONE;
    io_failure = short_read = storage_busy = recon_busy = false;
    alloc_failure = psram_failure = false;
    invalidate_cache();
    if (!sd_mounted) {
        if (storage_owner == CERBERUS_STORAGE_OWNER_USB)
            assert(cerberus_storage_release_from_usb() == ESP_OK);
        assert(cerberus_storage_mount() == ESP_OK);
    }
}
static void epoch_tests(void)
{
    uint32_t epoch = 123;
    assert(cerberus_storage_get_read_epoch(NULL) == ESP_ERR_INVALID_ARG);
    assert(cerberus_storage_get_read_epoch(&epoch) == ESP_ERR_INVALID_STATE && epoch == 0U);
    mount_failures = SD_MOUNT_ATTEMPTS;
    assert(cerberus_storage_mount() == ESP_FAIL && sd_read_epoch == 0U);
    assert(cerberus_storage_mount() == ESP_OK);
    assert(cerberus_storage_get_read_epoch(&epoch) == ESP_OK && epoch != 0U);
    uint32_t mounted = epoch;
    assert(cerberus_storage_mount() == ESP_OK && sd_read_epoch == mounted);
    unmount_failure = true;
    assert(cerberus_storage_unmount() == ESP_FAIL && sd_read_epoch == mounted && sd_mounted);
    unmount_failure = false; wardrive_busy = true;
    assert(cerberus_storage_reserve_for_usb() == ESP_ERR_INVALID_STATE && sd_read_epoch == mounted);
    wardrive_busy = false;
    assert(cerberus_storage_reserve_for_usb() == ESP_OK && sd_read_epoch != mounted);
    assert(cerberus_storage_get_read_epoch(&epoch) == ESP_ERR_INVALID_STATE && epoch == 0U);
    assert(cerberus_storage_mount() == ESP_ERR_INVALID_STATE);
    assert(cerberus_storage_release_from_usb() == ESP_OK);
    assert(cerberus_storage_mount() == ESP_OK);
    assert(cerberus_storage_get_read_epoch(&epoch) == ESP_OK && epoch != mounted);
    storage_busy = true;
    assert(cerberus_storage_get_read_epoch(&epoch) == ESP_ERR_TIMEOUT && epoch == 0U);
    storage_busy = false;
    assert(cerberus_storage_unmount() == ESP_OK);
    sd_read_epoch = UINT32_MAX;
    assert(cerberus_storage_mount() == ESP_OK && sd_read_epoch == 1U);
    puts("PASS: real mount/unmount/USB epoch transitions, failure paths, busy state and wrap.");
}
int main(int argc, char **argv)
{
    assert(argc == 4);
    first = load_database(argv[1]); second = load_database(argv[2]); real = load_database(argv[3]);
    epoch_tests();
    mutex_failure = true; assert(recon_store_init() == ESP_ERR_NO_MEM);
    mutex_failure = false; assert(recon_store_init() == ESP_OK);
    uint8_t known[6] = {0x00, 0x10, 0x00, 1, 2, 3};
    uint8_t unknown[6] = {0x00, 0xFF, 0xFE, 1, 2, 3};
    char name[RECON_DB_VENDOR_SIZE];
    reset_store(&first);
    assert(recon_store_vendor(unknown, name) == RECON_DB_UNKNOWN && !name[0]);
    unsigned initial = reads; reads = 0;
    assert(recon_store_vendor(unknown, name) == RECON_DB_UNKNOWN && reads == 1U);
    unknown[5] ^= 1U; reads = 0;
    assert(recon_store_vendor(unknown, name) == RECON_DB_UNKNOWN && reads == 1U);
    assert(recon_store_vendor(known, name) == RECON_DB_FOUND && !strcmp(name, "Fixture One"));
    reads = 0; assert(recon_store_vendor(known, name) == RECON_DB_FOUND && reads == 1U);
    assert(psram_allocs == 1U && internal_allocs == 0U);
    printf("PASS: repeated synthetic UNKNOWN lookup filesystem reads %u -> 1; FOUND still cached.\n", initial);

    for (unsigned i = 0; i < 10U; ++i) {
        uint8_t missing[6] = {0, 0x20, (uint8_t)i, 0, 0, 1};
        assert(recon_store_vendor(missing, name) == RECON_DB_UNKNOWN);
    }
    unsigned valid = 0; for (unsigned i = 0; i < 8U; ++i) valid += cache[i].valid;
    assert(valid == 8U); reads = 0;
    assert(recon_store_vendor(known, name) == RECON_DB_FOUND && reads > 1U);

    /* Same file/header after a complete remount must invalidate both kinds. */
    for (unsigned found = 0; found < 2U; ++found) {
        uint8_t *key = found ? known : unknown;
        reset_store(&first); recon_db_result_t expected = found ? RECON_DB_FOUND : RECON_DB_UNKNOWN;
        assert(recon_store_vendor(key, name) == expected);
        assert(cerberus_storage_reserve_for_usb() == ESP_OK);
        assert(cerberus_storage_release_from_usb() == ESP_OK);
        assert(cerberus_storage_mount() == ESP_OK);
        reads = 0; assert(recon_store_vendor(key, name) == expected && reads > 1U);
        /* Also detect a complete handover between the cached header read and return. */
        reads = 0; injection = INJECT_REMOUNT; inject_at = 1U;
        assert(recon_store_vendor(key, name) == RECON_DB_UNAVAILABLE && !name[0]);
        inject_at = 0; reads = 0;
        assert(recon_store_vendor(key, name) == expected && reads > 1U);
        reset_store(&first); injection = INJECT_REMOUNT; inject_at = 3U;
        assert(recon_store_vendor(key, name) == RECON_DB_UNAVAILABLE && !name[0]);
        inject_at = 0; reads = 0;
        assert(recon_store_vendor(key, name) == expected && reads > 1U);
    }
    puts("PASS: same-header remount invalidates cache and in-flight searches, including cache hits.");

    reset_store(&first); assert(recon_store_vendor(unknown, name) == RECON_DB_UNKNOWN);
    active = &second; reads = 0;
    assert(recon_store_vendor(unknown, name) == RECON_DB_FOUND && !strcmp(name, "New Assignment") && reads > 1U);
    reset_store(&first); injection = INJECT_SWAP; inject_at = 2U;
    assert(recon_store_vendor(unknown, name) == RECON_DB_UNAVAILABLE && !name[0]);
    inject_at = 0;
    assert(recon_store_vendor(unknown, name) == RECON_DB_FOUND && !strcmp(name, "New Assignment"));

    for (unsigned mode = 0; mode < 6U; ++mode) {
        reset_store(&first);
        if (mode == 0U) storage_busy = true;
        if (mode == 1U) io_failure = true;
        if (mode == 2U) short_read = true;
        if (mode == 3U) { injection = INJECT_USB; inject_at = 3U; }
        if (mode == 4U) { free(scratch); scratch = NULL; alloc_failure = true; }
        if (mode == 5U) recon_busy = true;
        assert(recon_store_vendor(unknown, name) == RECON_DB_UNAVAILABLE && !name[0]);
        io_failure = short_read = storage_busy = recon_busy = alloc_failure = false;
        inject_at = 0;
        if (!sd_mounted) {
            assert(cerberus_storage_release_from_usb() == ESP_OK);
            assert(cerberus_storage_mount() == ESP_OK);
        }
        reads = 0; assert(recon_store_vendor(unknown, name) == RECON_DB_UNKNOWN && reads > 1U);
    }
    free(scratch); scratch = NULL; reset_store(&first); psram_failure = true;
    assert(recon_store_vendor(known, name) == RECON_DB_FOUND && internal_allocs > 0U);
    puts("PASS: swapped resources re-resolve; transient storage/USB/I/O/allocation/mutex failures never cache misses.");

    reset_store(&first); uint8_t private_mac[6] = {2, 0x10, 0, 1, 2, 3};
    uint8_t ble[6] = {3, 2, 1, 0, 0x10, 0};
    assert(recon_store_vendor(private_mac, name) == RECON_DB_PRIVATE_ADDRESS && reads == 0U);
    assert(recon_store_ble_vendor(ble, 1, name) == RECON_DB_PRIVATE_ADDRESS && reads == 0U);
    assert(recon_store_ble_vendor(ble, 3, name) == RECON_DB_PRIVATE_ADDRESS && reads == 0U);
    assert(recon_store_ble_vendor(ble, 0, name) == RECON_DB_FOUND);
    assert(recon_store_ble_vendor(ble, 2, name) == RECON_DB_FOUND);
    assert(recon_store_ble_vendor(ble, 4, name) == RECON_DB_INVALID);

    /* Core misses get the same final-header check as positive results. */
    uint8_t core_scratch[RECON_DB_SCRATCH];
    reset_store(&first); reads = 0;
    assert(recon_db_lookup(read_direct, NULL, unknown, name, core_scratch) == RECON_DB_UNKNOWN);
    unsigned final_header_read = reads;
    for (unsigned mode = 0; mode < 3U; ++mode) {
        reset_store(&first); inject_at = final_header_read;
        injection = mode == 0U ? INJECT_SWAP : mode == 1U ? INJECT_BAD_FINAL_SIZE : INJECT_CORRUPT_HEADER;
        assert(recon_db_lookup(read_direct, NULL, unknown, name, core_scratch) == RECON_DB_INVALID && !name[0]);
    }
    reset_store(&first); injection = INJECT_USB; inject_at = final_header_read;
    assert(recon_db_lookup(read_direct, NULL, unknown, name, core_scratch) == RECON_DB_UNAVAILABLE);

    reset_store(&real);
    uint8_t real_unknown[6] = {0x00, 0xFF, 0xFE, 0, 0, 1};
    assert(recon_store_vendor(real_unknown, name) == RECON_DB_UNKNOWN);
    unsigned real_initial = reads; reads = 0;
    assert(recon_store_vendor(real_unknown, name) == RECON_DB_UNKNOWN && reads == 1U);
    printf("PASS: supplied combined SD database repeated UNKNOWN filesystem reads %u -> 1.\n", real_initial);
    assert(!storage_locked && !recon_locked);
    free(scratch); free(first.data); free(second.data); free(real.data);
    return 0;
}
