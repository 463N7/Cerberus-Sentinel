#include "storage.h"

#include "cerberus_pins.h"
#include "driver/gpio.h"
#include "driver/sdmmc_host.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#include "sdmmc_cmd.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#define SD_MMC_SLOT SDMMC_HOST_SLOT_1
#define SD_MMC_MAX_FREQ_KHZ 20000
#define SD_MOUNT_ATTEMPTS 2
#define SD_MAX_OPEN_FILES 8
#define SD_HASH_BUFFER_SIZE 4096
#define SD_DIAGNOSTIC_FILE_LIMIT 9999U
#define SD_QR_FILE_LIMIT 9999U
#define SD_CONFIG_FILE_LIMIT 9999U
#define SD_CONFIG_MAX_BYTES 4096U
#define SD_SYSTEM_LOG_MAX_BYTES (64U * 1024U)
#define SD_SYSTEM_LOG_ROTATIONS 2U
#define SD_QR_QUIET_ZONE_MODULES 4U
#define SD_QR_TARGET_SIZE 256U
#define SD_QR_MAX_MODULE_COUNT 57U
#define SD_QR_MAX_ROW_BYTES (SD_QR_TARGET_SIZE * 3U)
#define SD_BENCHMARK_FILE_NAME "SD_BENCH.TMP"
#define SD_WIFI_CAPTURE_FILE_LIMIT 9999U
#define SD_WARDRIVE_FILE_LIMIT 999U

static const char *TAG = "storage";
static sdmmc_card_t *sd_card;
static SemaphoreHandle_t storage_mutex;
static bool sd_bus_ready;
static bool sd_mounted;
static cerberus_storage_owner_t storage_owner;
/* Protected by storage_mutex. A token identifies one mounted filesystem
 * session, not a particular file's contents. Zero is never a mounted token. */
static uint32_t sd_read_epoch;
static void advance_storage_read_epoch(void)
{
    if (++sd_read_epoch == 0U) ++sd_read_epoch;
}
/* Hashing and benchmarking are serialized by storage_mutex. Keeping this
   workspace in BSS avoids exhausting the UI task's runtime stack. */
static uint8_t sd_hash_buffer[SD_HASH_BUFFER_SIZE];
static char sd_hash_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_diagnostic_directory_path[CERBERUS_STORAGE_PATH_MAX];
static char sd_diagnostic_file_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_diagnostic_temp_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_qr_directory_path[CERBERUS_STORAGE_PATH_MAX];
static char sd_qr_file_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_qr_temp_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static uint8_t sd_qr_row[SD_QR_MAX_ROW_BYTES];
static char sd_config_directory_path[CERBERUS_STORAGE_PATH_MAX];
static char sd_config_file_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_config_temp_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_system_log_directory_path[CERBERUS_STORAGE_PATH_MAX];
static char sd_system_log_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_system_log_rotate_source[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_system_log_rotate_target[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_system_log_line[384];
static char sd_operation_source_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_operation_destination_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_operation_temp_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX + 16U];
static FILE *sd_wifi_capture_file;
static uint64_t sd_wifi_capture_bytes;
static char sd_wifi_capture_temp_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_wifi_capture_final_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
static char sd_wifi_capture_name[CERBERUS_STORAGE_NAME_MAX];
/* Fixed Wardriving paths are short; two compact stream states use less
 * internal RAM than the former single generic-path allocation. */
typedef struct {
    FILE *file;
    uint64_t bytes;
    uint32_t pending_rows;
    int64_t last_sync_us;
    bool write_failed;
    char temp_path[256];
    char final_path[256];
    char name[24];
} wardrive_storage_stream_t;
static wardrive_storage_stream_t sd_wardrive_stream, sd_wigle_stream;
static bool sd_wardrive_session_active;
static bool wardrive_storage_busy(void)
{
    return sd_wardrive_session_active || sd_wardrive_stream.file != NULL ||
        sd_wigle_stream.file != NULL;
}

static void release_sd_bus(void);
static bool relative_path_is_safe(const char *relative_path);
static bool file_name_is_safe(const char *file_name);
static esp_err_t ensure_directory_exists(const char *path);

static esp_err_t build_entry_path(
    char *output,
    size_t output_size,
    const char *relative_path,
    const char *name)
{
    if (output == NULL || !relative_path_is_safe(relative_path) || !file_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *separator = strcmp(relative_path, "/") == 0 ? "" : "/";
    int length = snprintf(output, output_size, "%s%s%s%s",
        CERBERUS_STORAGE_MOUNT_PATH, relative_path, separator, name);
    return length >= 0 && (size_t)length < output_size ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t require_firmware_owner_locked(void)
{
    return sd_mounted && storage_owner == CERBERUS_STORAGE_OWNER_FIRMWARE
        ? ESP_OK
        : ESP_ERR_INVALID_STATE;
}

static esp_err_t unmount_locked(void)
{
    if (!sd_mounted) {
        release_sd_bus();
        return ESP_OK;
    }
    esp_err_t err = esp_vfs_fat_sdcard_unmount(CERBERUS_STORAGE_MOUNT_PATH, sd_card);
    if (err == ESP_OK) {
        sd_mounted = false;
        advance_storage_read_epoch();
        sd_card = NULL;
        /* The VFS unmount calls the SDMMC host deinitializer stored in the
           card descriptor. */
        sd_bus_ready = false;
        ESP_LOGI(TAG, "microSD unmounted");
    }
    return err;
}

static esp_err_t ensure_storage_mutex(void)
{
    if (storage_mutex == NULL) {
        storage_mutex = xSemaphoreCreateMutex();
    }
    return storage_mutex != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

static esp_err_t take_storage_mutex(void)
{
    ESP_RETURN_ON_ERROR(ensure_storage_mutex(), TAG, "storage mutex allocation failed");
    return xSemaphoreTake(storage_mutex, pdMS_TO_TICKS(1000)) == pdTRUE ? ESP_OK : ESP_ERR_TIMEOUT;
}

static void release_storage_mutex(void)
{
    xSemaphoreGive(storage_mutex);
}

static void prepare_sd_lines(void)
{
#if CERBERUS_SD_DAT3_TEST_GPIO >= 0
    gpio_config_t dat3_config = {
        .pin_bit_mask = 1ULL << CERBERUS_SD_DAT3_TEST_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&dat3_config));
    gpio_set_level(CERBERUS_SD_DAT3_TEST_GPIO, 1);

    /* Prototype-only validation without the final external pull-up network.
       CMD and DAT0 use the S3 weak pulls; DAT3 is held high through the
       existing GPIO38 trace so the card enters native SD mode. */
    gpio_pullup_en(CERBERUS_SD_CMD_GPIO);
    gpio_pullup_en(CERBERUS_SD_D0_GPIO);
    gpio_pullup_en(CERBERUS_SD_DAT3_TEST_GPIO);
#endif
}

static void release_sd_bus(void)
{
    if (!sd_bus_ready) {
        return;
    }

    esp_err_t err = sdmmc_host_deinit_slot(SD_MMC_SLOT);
    if (err == ESP_OK) {
        sd_bus_ready = false;
    } else {
        ESP_LOGW(TAG, "SDMMC host release failed: %s", esp_err_to_name(err));
    }
}

static bool relative_path_is_safe(const char *relative_path)
{
    if (relative_path == NULL || relative_path[0] != '/') {
        return false;
    }
    if (strstr(relative_path, "..") != NULL || strchr(relative_path, '\\') != NULL || strchr(relative_path, ':') != NULL) {
        return false;
    }
    return strlen(relative_path) < CERBERUS_STORAGE_PATH_MAX - strlen(CERBERUS_STORAGE_MOUNT_PATH);
}

static bool file_name_is_safe(const char *file_name)
{
    if (file_name == NULL || file_name[0] == '\0' || strcmp(file_name, ".") == 0 || strcmp(file_name, "..") == 0) {
        return false;
    }
    if (strchr(file_name, '/') != NULL || strchr(file_name, '\\') != NULL || strchr(file_name, ':') != NULL) {
        return false;
    }
    return strlen(file_name) < CERBERUS_STORAGE_NAME_MAX;
}

static bool entry_is_protected_system_file(const char *relative_path, const char *name)
{
    return strcmp(relative_path, "/CERBERUS/LOGS") == 0 &&
        (strcasecmp(name, "SYSTEM.CSV") == 0 ||
         strcasecmp(name, "SYSTEM.1.CSV") == 0 ||
         strcasecmp(name, "SYSTEM.2.CSV") == 0);
}

static int compare_entries(const void *left_ptr, const void *right_ptr)
{
    const cerberus_storage_entry_t *left = left_ptr;
    const cerberus_storage_entry_t *right = right_ptr;
    if (left->is_directory != right->is_directory) {
        return left->is_directory ? -1 : 1;
    }
    return strcasecmp(left->name, right->name);
}

esp_err_t cerberus_storage_mount(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");

    if (storage_owner == CERBERUS_STORAGE_OWNER_USB) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (sd_mounted) {
        release_storage_mutex();
        return ESP_OK;
    }

    prepare_sd_lines();
    vTaskDelay(pdMS_TO_TICKS(100));

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SD_MMC_SLOT;
    host.max_freq_khz = SD_MMC_MAX_FREQ_KHZ;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = CERBERUS_SD_CLK_GPIO;
    slot_config.cmd = CERBERUS_SD_CMD_GPIO;
    slot_config.d0 = CERBERUS_SD_D0_GPIO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = SD_MAX_OPEN_FILES,
        .allocation_unit_size = 16 * 1024,
    };

    esp_err_t mount_error = ESP_FAIL;
    for (uint8_t attempt = 1; attempt <= SD_MOUNT_ATTEMPTS; ++attempt) {
        sd_card = NULL;
        mount_error = esp_vfs_fat_sdmmc_mount(
            CERBERUS_STORAGE_MOUNT_PATH,
            &host,
            &slot_config,
            &mount_config,
            &sd_card);
        if (mount_error == ESP_OK) {
            sd_bus_ready = true;
            break;
        }

        ESP_LOGW(TAG, "SD mount attempt %u/%u failed: %s", attempt, SD_MOUNT_ATTEMPTS,
            esp_err_to_name(mount_error));
#if CERBERUS_SD_DAT3_TEST_GPIO >= 0
        gpio_set_level(CERBERUS_SD_DAT3_TEST_GPIO, 1);
#endif
        if (attempt < SD_MOUNT_ATTEMPTS) {
            vTaskDelay(pdMS_TO_TICKS(250));
            prepare_sd_lines();
        }
    }

    if (!sd_bus_ready) {
        sd_card = NULL;
        release_storage_mutex();
        return mount_error;
    }

    sd_mounted = true;
    storage_owner = CERBERUS_STORAGE_OWNER_FIRMWARE;
    advance_storage_read_epoch();
    int directory_length = snprintf(sd_operation_source_path, sizeof(sd_operation_source_path),
        "%s/CERBERUS", CERBERUS_STORAGE_MOUNT_PATH);
    if (directory_length < 0 || (size_t)directory_length >= sizeof(sd_operation_source_path) ||
        ensure_directory_exists(sd_operation_source_path) != ESP_OK) {
        ESP_LOGW(TAG, "cannot prepare Cerberus storage directory");
    } else {
        directory_length = snprintf(sd_operation_source_path, sizeof(sd_operation_source_path),
            "%s%s", CERBERUS_STORAGE_MOUNT_PATH, CERBERUS_STORAGE_FIRMWARE_PATH);
        if (directory_length < 0 || (size_t)directory_length >= sizeof(sd_operation_source_path) ||
            ensure_directory_exists(sd_operation_source_path) != ESP_OK) {
            ESP_LOGW(TAG, "cannot prepare firmware package directory");
        } else {
            directory_length = snprintf(sd_operation_source_path, sizeof(sd_operation_source_path),
                "%s%s", CERBERUS_STORAGE_MOUNT_PATH, CERBERUS_STORAGE_SCRIPTS_PATH);
            if (directory_length < 0 ||
                (size_t)directory_length >= sizeof(sd_operation_source_path) ||
                ensure_directory_exists(sd_operation_source_path) != ESP_OK) {
                ESP_LOGW(TAG, "cannot prepare restricted script directory");
            }
        }
    }
    ESP_LOGI(TAG, "microSD mounted on SDMMC 1-bit at %s", CERBERUS_STORAGE_MOUNT_PATH);
    sdmmc_card_print_info(stdout, sd_card);
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_unmount(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (sd_wifi_capture_file != NULL || wardrive_storage_busy()) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = unmount_locked();
    if (err == ESP_OK && storage_owner == CERBERUS_STORAGE_OWNER_FIRMWARE) {
        storage_owner = CERBERUS_STORAGE_OWNER_NONE;
    }
    release_storage_mutex();
    return err;
}

bool cerberus_storage_is_mounted(void)
{
    return sd_mounted;
}

esp_err_t cerberus_storage_get_read_epoch(uint32_t *epoch)
{
    if (epoch == NULL) return ESP_ERR_INVALID_ARG;
    *epoch = 0U;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage epoch lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) *epoch = sd_read_epoch;
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_try_get_read_epoch(uint32_t *epoch)
{
    if (epoch == NULL) return ESP_ERR_INVALID_ARG;
    *epoch = 0U;
    if (storage_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(storage_mutex, 0) != pdTRUE) return ESP_ERR_TIMEOUT;
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) *epoch = sd_read_epoch;
    release_storage_mutex();
    return err;
}

cerberus_storage_owner_t cerberus_storage_get_owner(void)
{
    return storage_owner;
}

esp_err_t cerberus_storage_reserve_for_usb(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (sd_wifi_capture_file != NULL || wardrive_storage_busy()) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (storage_owner == CERBERUS_STORAGE_OWNER_USB) {
        release_storage_mutex();
        return ESP_OK;
    }
    esp_err_t err = unmount_locked();
    if (err == ESP_OK) {
        storage_owner = CERBERUS_STORAGE_OWNER_USB;
        ESP_LOGI(TAG, "microSD reserved exclusively for USB mass-storage owner");
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_release_from_usb(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (storage_owner != CERBERUS_STORAGE_OWNER_USB) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    storage_owner = CERBERUS_STORAGE_OWNER_NONE;
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_acquire_usb_card(sdmmc_card_t **card)
{
    if (card == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    *card = NULL;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (storage_owner != CERBERUS_STORAGE_OWNER_USB || sd_mounted || sd_card != NULL) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    prepare_sd_lines();
    vTaskDelay(pdMS_TO_TICKS(100));
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SD_MMC_SLOT;
    host.max_freq_khz = SD_MMC_MAX_FREQ_KHZ;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = CERBERUS_SD_CLK_GPIO;
    slot_config.cmd = CERBERUS_SD_CMD_GPIO;
    slot_config.d0 = CERBERUS_SD_D0_GPIO;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    bool host_initialized = false;
    esp_err_t err = host.init();
    if (err == ESP_OK) host_initialized = true;
    if (err == ESP_OK) {
        err = sdmmc_host_init_slot(host.slot, &slot_config);
        if (err == ESP_OK) sd_bus_ready = true;
    }
    if (err == ESP_OK) {
        sd_card = calloc(1U, sizeof(*sd_card));
        if (sd_card == NULL) {
            err = ESP_ERR_NO_MEM;
        }
    }
    if (err == ESP_OK) {
        err = sdmmc_card_init(&host, sd_card);
    }
    if (err != ESP_OK) {
        free(sd_card);
        sd_card = NULL;
        if (sd_bus_ready) {
            release_sd_bus();
        } else if (host_initialized) {
            (void)sdmmc_host_deinit();
        }
        release_storage_mutex();
        return err;
    }

    *card = sd_card;
    ESP_LOGI(TAG, "microSD initialized for exclusive USB block access");
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_release_usb_card(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (storage_owner != CERBERUS_STORAGE_OWNER_USB || sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ESP_OK;
    if (sd_card != NULL) {
        free(sd_card);
        sd_card = NULL;
    }
    if (sd_bus_ready) {
        release_sd_bus();
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_get_status(cerberus_storage_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(status, 0, sizeof(*status));
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");

    if (!sd_mounted || sd_card == NULL) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = sdmmc_get_status(sd_card);
    if (err == ESP_OK) {
        status->mounted = true;
        status->sector_size = sd_card->csd.sector_size;
        snprintf(status->card_name, sizeof(status->card_name), "%.7s", sd_card->cid.name);
        err = esp_vfs_fat_info(CERBERUS_STORAGE_MOUNT_PATH, &status->total_bytes, &status->free_bytes);
    }

    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_list_directory(
    const char *relative_path,
    cerberus_storage_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count)
{
    if (!relative_path_is_safe(relative_path) || entries == NULL || entry_capacity == 0 || entry_count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *entry_count = 0;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[CERBERUS_STORAGE_PATH_MAX];
    int path_len = snprintf(full_path, sizeof(full_path), "%s%s", CERBERUS_STORAGE_MOUNT_PATH, relative_path);
    if (path_len < 0 || (size_t)path_len >= sizeof(full_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    DIR *directory = opendir(full_path);
    if (directory == NULL) {
        ESP_LOGW(TAG, "cannot open %s: errno=%d", full_path, errno);
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    struct dirent *dir_entry;
    while (*entry_count < entry_capacity && (dir_entry = readdir(directory)) != NULL) {
        if (strcmp(dir_entry->d_name, ".") == 0 || strcmp(dir_entry->d_name, "..") == 0) {
            continue;
        }

        char entry_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
        const char *separator = strcmp(relative_path, "/") == 0 ? "" : "/";
        int entry_path_len = snprintf(
            entry_path,
            sizeof(entry_path),
            "%s%s%s%s",
            CERBERUS_STORAGE_MOUNT_PATH,
            relative_path,
            separator,
            dir_entry->d_name);
        if (entry_path_len < 0 || (size_t)entry_path_len >= sizeof(entry_path)) {
            continue;
        }

        struct stat file_stat;
        if (stat(entry_path, &file_stat) != 0) {
            continue;
        }

        cerberus_storage_entry_t *result = &entries[*entry_count];
        memset(result, 0, sizeof(*result));
        snprintf(result->name, sizeof(result->name), "%s", dir_entry->d_name);
        result->is_directory = S_ISDIR(file_stat.st_mode);
        result->size_bytes = result->is_directory ? 0 : (uint64_t)file_stat.st_size;
        ++(*entry_count);
    }

    closedir(directory);
    qsort(entries, *entry_count, sizeof(entries[0]), compare_entries);
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_read_file_chunk(
    const char *relative_path,
    const char *file_name,
    uint64_t offset,
    uint8_t *data,
    size_t data_capacity,
    size_t *bytes_read,
    uint64_t *file_size)
{
    if (!relative_path_is_safe(relative_path) || !file_name_is_safe(file_name) || data == NULL ||
        data_capacity == 0 || bytes_read == NULL || file_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *bytes_read = 0;
    *file_size = 0;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    char full_path[CERBERUS_STORAGE_PATH_MAX + CERBERUS_STORAGE_NAME_MAX];
    const char *separator = strcmp(relative_path, "/") == 0 ? "" : "/";
    int path_len = snprintf(
        full_path,
        sizeof(full_path),
        "%s%s%s%s",
        CERBERUS_STORAGE_MOUNT_PATH,
        relative_path,
        separator,
        file_name);
    if (path_len < 0 || (size_t)path_len >= sizeof(full_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat file_stat;
    if (stat(full_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    *file_size = file_stat.st_size > 0 ? (uint64_t)file_stat.st_size : 0;
    if (offset > *file_size || offset > LONG_MAX) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *file = fopen(full_path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "cannot open %s: errno=%d", full_path, errno);
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    if (fseek(file, (long)offset, SEEK_SET) != 0) {
        err = ESP_FAIL;
    } else {
        *bytes_read = fread(data, 1, data_capacity, file);
        if (ferror(file)) {
            *bytes_read = 0;
            err = ESP_FAIL;
        }
    }

    fclose(file);
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_sha256_file(
    const char *relative_path,
    const char *file_name,
    uint8_t digest[32],
    uint64_t *file_size)
{
    if (!relative_path_is_safe(relative_path) || !file_name_is_safe(file_name) || digest == NULL || file_size == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *file_size = 0;
    memset(digest, 0, 32);
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    const char *separator = strcmp(relative_path, "/") == 0 ? "" : "/";
    int path_len = snprintf(
        sd_hash_path,
        sizeof(sd_hash_path),
        "%s%s%s%s",
        CERBERUS_STORAGE_MOUNT_PATH,
        relative_path,
        separator,
        file_name);
    if (path_len < 0 || (size_t)path_len >= sizeof(sd_hash_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat file_stat;
    if (stat(sd_hash_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    FILE *file = fopen(sd_hash_path, "rb");
    if (file == NULL) {
        ESP_LOGW(TAG, "cannot hash %s: errno=%d", sd_hash_path, errno);
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = ESP_OK;
    psa_hash_operation_t operation = PSA_HASH_OPERATION_INIT;
    psa_status_t hash_status = psa_crypto_init();
    bool hash_active = false;
    if (hash_status == PSA_SUCCESS) {
        hash_status = psa_hash_setup(&operation, PSA_ALG_SHA_256);
        hash_active = hash_status == PSA_SUCCESS;
    }

    uint32_t chunks_since_yield = 0;
    while (hash_status == PSA_SUCCESS) {
        size_t count = fread(sd_hash_buffer, 1, sizeof(sd_hash_buffer), file);
        if (count > 0) {
            hash_status = psa_hash_update(&operation, sd_hash_buffer, count);
            *file_size += count;
            if (++chunks_since_yield == 64U) {
                chunks_since_yield = 0;
                vTaskDelay(1);
            }
        }
        if (count < sizeof(sd_hash_buffer)) {
            if (ferror(file)) {
                err = ESP_FAIL;
            }
            break;
        }
    }

    if (err == ESP_OK && hash_status == PSA_SUCCESS) {
        size_t digest_length = 0;
        hash_status = psa_hash_finish(&operation, digest, 32, &digest_length);
        if (hash_status == PSA_SUCCESS) {
            hash_active = false;
        }
        if (hash_status != PSA_SUCCESS || digest_length != 32) {
            err = ESP_FAIL;
        }
    } else if (hash_status != PSA_SUCCESS) {
        err = ESP_FAIL;
    }

    if (hash_active) {
        (void)psa_hash_abort(&operation);
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SHA-256 failed for %s: PSA status=%ld", sd_hash_path, (long)hash_status);
        memset(digest, 0, 32);
    }

    fclose(file);
    release_storage_mutex();
    return err;
}

static esp_err_t ensure_directory_exists(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    ESP_LOGW(TAG, "cannot create %s: errno=%d", path, errno);
    return ESP_FAIL;
}

static bool diagnostic_report_name_is_safe(const char *file_name)
{
    if (file_name == NULL || strlen(file_name) != 12U ||
        (memcmp(file_name, "154_", 4U) != 0 && memcmp(file_name, "WIFI", 4U) != 0 &&
         memcmp(file_name, "WDEF", 4U) != 0) ||
        memcmp(file_name + 8U, ".CSV", 4U) != 0) {
        return false;
    }
    for (uint8_t index = 4U; index < 8U; ++index) {
        if (file_name[index] < '0' || file_name[index] > '9') {
            return false;
        }
    }
    return true;
}

static esp_err_t write_diagnostic_report_with_prefix(
    const char prefix[4],
    size_t maximum_bytes,
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    if (prefix == NULL || data == NULL || data_length == 0U ||
        data_length > maximum_bytes ||
        file_name == NULL || file_name_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    file_name[0] = '\0';

    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for report failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_diagnostic_directory_path,
        sizeof(sd_diagnostic_directory_path),
        "%s/CERBERUS",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_diagnostic_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = ensure_directory_exists(sd_diagnostic_directory_path);
    if (err == ESP_OK) {
        path_length = snprintf(
            sd_diagnostic_directory_path,
            sizeof(sd_diagnostic_directory_path),
            "%s/CERBERUS/LOGS",
            CERBERUS_STORAGE_MOUNT_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(sd_diagnostic_directory_path)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            err = ensure_directory_exists(sd_diagnostic_directory_path);
        }
    }
    if (err != ESP_OK) {
        release_storage_mutex();
        return err;
    }

    uint16_t report_index = 0U;
    struct stat file_stat;
    for (uint16_t candidate = 1U; candidate <= SD_DIAGNOSTIC_FILE_LIMIT; ++candidate) {
        int final_length = snprintf(
            sd_diagnostic_file_path,
            sizeof(sd_diagnostic_file_path),
            "%s/%.4s%04u.CSV",
            sd_diagnostic_directory_path,
            prefix,
            (unsigned)candidate);
        int temp_length = snprintf(
            sd_diagnostic_temp_path,
            sizeof(sd_diagnostic_temp_path),
            "%s/%.4s%04u.TMP",
            sd_diagnostic_directory_path,
            prefix,
            (unsigned)candidate);
        if (final_length < 0 || (size_t)final_length >= sizeof(sd_diagnostic_file_path) ||
            temp_length < 0 || (size_t)temp_length >= sizeof(sd_diagnostic_temp_path)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (stat(sd_diagnostic_file_path, &file_stat) != 0 &&
            stat(sd_diagnostic_temp_path, &file_stat) != 0) {
            report_index = candidate;
            break;
        }
    }
    if (err != ESP_OK || report_index == 0U) {
        release_storage_mutex();
        return err != ESP_OK ? err : ESP_FAIL;
    }

    FILE *report = fopen(sd_diagnostic_temp_path, "wb");
    if (report == NULL) {
        ESP_LOGW(TAG, "cannot open diagnostic report: errno=%d", errno);
        release_storage_mutex();
        return ESP_FAIL;
    }
    size_t written = fwrite(data, 1U, data_length, report);
    bool write_failed = written != data_length;
    if (fflush(report) != 0) {
        write_failed = true;
    }
    if (fclose(report) != 0) {
        write_failed = true;
    }
    if (write_failed) {
        ESP_LOGW(TAG, "diagnostic report write failed: errno=%d", errno);
        (void)remove(sd_diagnostic_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }
    if (rename(sd_diagnostic_temp_path, sd_diagnostic_file_path) != 0) {
        ESP_LOGW(TAG, "diagnostic report finalize failed: errno=%d", errno);
        (void)remove(sd_diagnostic_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }

    int name_length = snprintf(file_name, file_name_capacity, "%.4s%04u.CSV",
        prefix, (unsigned)report_index);
    if (name_length < 0 || (size_t)name_length >= file_name_capacity) {
        file_name[0] = '\0';
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "saved diagnostic report %s", sd_diagnostic_file_path);
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_write_diagnostic_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    return write_diagnostic_report_with_prefix(
        "154_", CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES, data, data_length, file_name, file_name_capacity);
}

esp_err_t cerberus_storage_write_wifi_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    return write_diagnostic_report_with_prefix(
        "WIFI", CERBERUS_STORAGE_WIFI_REPORT_MAX_BYTES, data, data_length, file_name, file_name_capacity);
}

esp_err_t cerberus_storage_write_wifi_defense_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    return write_diagnostic_report_with_prefix(
        "WDEF", CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES, data, data_length, file_name, file_name_capacity);
}

esp_err_t cerberus_storage_write_portal_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    return write_diagnostic_report_with_prefix(
        "PORT", CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES, data, data_length, file_name, file_name_capacity);
}

esp_err_t cerberus_storage_wifi_capture_begin(const uint8_t *header, size_t header_length)
{
    if (header == NULL || header_length == 0U || header_length > 512U)
        return ESP_ERR_INVALID_ARG;
    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for Wi-Fi capture failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (require_firmware_owner_locked() != ESP_OK || sd_wifi_capture_file != NULL ||
        wardrive_storage_busy()) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    char directory[CERBERUS_STORAGE_PATH_MAX];
    int n = snprintf(directory, sizeof(directory), "%s/CERBERUS", CERBERUS_STORAGE_MOUNT_PATH);
    if (n < 0 || (size_t)n >= sizeof(directory)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = ensure_directory_exists(directory);
    if (error == ESP_OK) {
        n = snprintf(directory, sizeof(directory), "%s/CERBERUS/CAPTURES", CERBERUS_STORAGE_MOUNT_PATH);
        error = n < 0 || (size_t)n >= sizeof(directory) ? ESP_ERR_INVALID_SIZE :
            ensure_directory_exists(directory);
    }
    if (error != ESP_OK) {
        release_storage_mutex();
        return error;
    }
    struct stat item;
    unsigned index;
    for (index = 1U; index <= SD_WIFI_CAPTURE_FILE_LIMIT; ++index) {
        int name_n = snprintf(sd_wifi_capture_name, sizeof(sd_wifi_capture_name), "WCAP%04u.PCAPNG", index);
        int final_n = snprintf(sd_wifi_capture_final_path, sizeof(sd_wifi_capture_final_path),
            "%s/%s", directory, sd_wifi_capture_name);
        int temp_n = snprintf(sd_wifi_capture_temp_path, sizeof(sd_wifi_capture_temp_path),
            "%s/WCAP%04u.TMP", directory, index);
        if (name_n < 0 || (size_t)name_n >= sizeof(sd_wifi_capture_name) ||
            final_n < 0 || (size_t)final_n >= sizeof(sd_wifi_capture_final_path) ||
            temp_n < 0 || (size_t)temp_n >= sizeof(sd_wifi_capture_temp_path)) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (stat(sd_wifi_capture_final_path, &item) != 0 &&
            stat(sd_wifi_capture_temp_path, &item) != 0) break;
    }
    if (error == ESP_OK && index > SD_WIFI_CAPTURE_FILE_LIMIT) error = ESP_ERR_NOT_FOUND;
    if (error != ESP_OK) {
        release_storage_mutex();
        return error;
    }
    FILE *file = fopen(sd_wifi_capture_temp_path, "wb");
    if (file == NULL) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    if (fwrite(header, 1U, header_length, file) != header_length) {
        (void)fclose(file);
        (void)remove(sd_wifi_capture_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }
    sd_wifi_capture_file = file;
    sd_wifi_capture_bytes = header_length;
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_wifi_capture_append(const uint8_t *record, size_t record_length)
{
    if (record == NULL || record_length == 0U) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (sd_wifi_capture_file == NULL || require_firmware_owner_locked() != ESP_OK) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (record_length > CERBERUS_STORAGE_WIFI_CAPTURE_MAX_BYTES - sd_wifi_capture_bytes) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    if (fwrite(record, 1U, record_length, sd_wifi_capture_file) != record_length) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    sd_wifi_capture_bytes += record_length;
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_wifi_capture_finish(bool keep,
    char *file_name, size_t file_name_capacity, uint64_t *bytes_written)
{
    if (file_name != NULL && file_name_capacity != 0U) file_name[0] = '\0';
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (sd_wifi_capture_file == NULL) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (keep && (file_name == NULL || file_name_capacity < strlen(sd_wifi_capture_name) + 1U)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = sd_wifi_capture_file;
    uint64_t size = sd_wifi_capture_bytes;
    bool failed = fflush(file) != 0;
    if (fclose(file) != 0) failed = true;
    sd_wifi_capture_file = NULL;
    sd_wifi_capture_bytes = 0U;
    esp_err_t error = failed ? ESP_FAIL : ESP_OK;
    if (keep && error == ESP_OK) {
        if (rename(sd_wifi_capture_temp_path, sd_wifi_capture_final_path) != 0) error = ESP_FAIL;
        else {
            memcpy(file_name, sd_wifi_capture_name, strlen(sd_wifi_capture_name) + 1U);
            if (bytes_written != NULL) *bytes_written = size;
        }
    } else if (!keep) {
        if (remove(sd_wifi_capture_temp_path) != 0) error = ESP_FAIL;
    }
    if (error == ESP_OK && keep)
        ESP_LOGI(TAG, "saved Wi-Fi capture %s (%llu bytes)", sd_wifi_capture_final_path,
            (unsigned long long)size);
    release_storage_mutex();
    return error;
}

static esp_err_t wardrive_stream_begin(wardrive_storage_stream_t *stream,
    const char *prefix, const char *header, size_t header_length)
{
    if (header == NULL || header_length == 0U || header_length > 512U)
        return ESP_ERR_INVALID_ARG;
    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for Wardriving failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (require_firmware_owner_locked() != ESP_OK || stream->file != NULL ||
        sd_wifi_capture_file != NULL) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    char directory[CERBERUS_STORAGE_PATH_MAX];
    int n = snprintf(directory, sizeof(directory), "%s/CERBERUS", CERBERUS_STORAGE_MOUNT_PATH);
    if (n < 0 || (size_t)n >= sizeof(directory)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t error = ensure_directory_exists(directory);
    if (error == ESP_OK) {
        n = snprintf(directory, sizeof(directory), "%s%s",
            CERBERUS_STORAGE_MOUNT_PATH, CERBERUS_STORAGE_WARDRIVE_PATH);
        error = n < 0 || (size_t)n >= sizeof(directory) ? ESP_ERR_INVALID_SIZE :
            ensure_directory_exists(directory);
    }
    if (error != ESP_OK) {
        release_storage_mutex();
        return error;
    }
    struct stat item;
    unsigned index;
    for (index = 1U; index <= SD_WARDRIVE_FILE_LIMIT; ++index) {
        int name_n = snprintf(stream->name, sizeof(stream->name),
            "%s%03u.CSV", prefix, index);
        int final_n = snprintf(stream->final_path, sizeof(stream->final_path),
            "%s/%s", directory, stream->name);
        int temp_n = snprintf(stream->temp_path, sizeof(stream->temp_path),
            "%s/%s%03u.TMP", directory, prefix, index);
        if (name_n < 0 || (size_t)name_n >= sizeof(stream->name) ||
            final_n < 0 || (size_t)final_n >= sizeof(stream->final_path) ||
            temp_n < 0 || (size_t)temp_n >= sizeof(stream->temp_path)) {
            error = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (stat(stream->final_path, &item) != 0 &&
            stat(stream->temp_path, &item) != 0) break;
    }
    if (error == ESP_OK && index > SD_WARDRIVE_FILE_LIMIT) error = ESP_ERR_NOT_FOUND;
    if (error != ESP_OK) {
        release_storage_mutex();
        return error;
    }
    FILE *file = fopen(stream->temp_path, "wb");
    if (file == NULL) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    bool failed = fwrite(header, 1U, header_length, file) != header_length ||
        fflush(file) != 0 || fsync(fileno(file)) != 0;
    if (failed) {
        (void)fclose(file);
        (void)remove(stream->temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }
    stream->file = file;
    stream->bytes = header_length;
    stream->pending_rows = 0U;
    stream->last_sync_us = esp_timer_get_time();
    stream->write_failed = false;
    release_storage_mutex();
    return ESP_OK;
}

static esp_err_t wardrive_stream_sync(wardrive_storage_stream_t *stream)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (stream->file == NULL || require_firmware_owner_locked() != ESP_OK) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (stream->write_failed) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    esp_err_t error = ESP_OK;
    if (stream->pending_rows != 0U &&
        (fflush(stream->file) != 0 || fsync(fileno(stream->file)) != 0))
        error = ESP_FAIL;
    if (error != ESP_OK) stream->write_failed = true;
    if (error == ESP_OK) {
        stream->pending_rows = 0U;
        stream->last_sync_us = esp_timer_get_time();
    }
    release_storage_mutex();
    return error;
}

static esp_err_t wardrive_stream_append(wardrive_storage_stream_t *stream,
    const char *row, size_t row_length)
{
    if (row == NULL || row_length == 0U || row_length > 512U) return ESP_ERR_INVALID_ARG;
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (stream->file == NULL || require_firmware_owner_locked() != ESP_OK) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (stream->write_failed) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    if (row_length > CERBERUS_STORAGE_WARDRIVE_MAX_BYTES - stream->bytes) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    if (fwrite(row, 1U, row_length, stream->file) != row_length) {
        /* The FILE can contain a partial row even if a later flush succeeds. */
        stream->write_failed = true;
        release_storage_mutex();
        return ESP_FAIL;
    }
    stream->bytes += row_length;
    ++stream->pending_rows;
    bool due = stream->pending_rows >= 25U ||
        esp_timer_get_time() - stream->last_sync_us >= 2000000LL;
    esp_err_t error = ESP_OK;
    if (due && (fflush(stream->file) != 0 || fsync(fileno(stream->file)) != 0))
        error = ESP_FAIL;
    if (error != ESP_OK) stream->write_failed = true;
    if (due && error == ESP_OK) {
        stream->pending_rows = 0U;
        stream->last_sync_us = esp_timer_get_time();
    }
    release_storage_mutex();
    return error;
}

static esp_err_t wardrive_stream_finish(wardrive_storage_stream_t *stream, bool keep,
    char *file_name, size_t file_name_capacity, uint64_t *bytes_written)
{
    if (file_name != NULL && file_name_capacity != 0U) file_name[0] = '\0';
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (stream->file == NULL) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }
    if (keep && (file_name == NULL || file_name_capacity < strlen(stream->name) + 1U)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_ARG;
    }
    FILE *file = stream->file;
    uint64_t size = stream->bytes;
    bool failed = stream->write_failed;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) failed = true;
    if (fclose(file) != 0) failed = true;
    stream->file = NULL;
    stream->bytes = 0U;
    stream->pending_rows = 0U;
    esp_err_t error = failed ? ESP_FAIL : ESP_OK;
    if (keep && error == ESP_OK) {
        if (rename(stream->temp_path, stream->final_path) != 0) error = ESP_FAIL;
        else {
            memcpy(file_name, stream->name, strlen(stream->name) + 1U);
            if (bytes_written != NULL) *bytes_written = size;
        }
    } else if (!keep && !failed && remove(stream->temp_path) != 0) {
        error = ESP_FAIL;
    }
    if (error == ESP_OK && keep)
        ESP_LOGI(TAG, "saved Wardriving %s (%llu bytes)",
            stream->final_path, (unsigned long long)size);
    release_storage_mutex();
    return error;
}

/* The session lease also covers the period before either lazy file opens. */
esp_err_t cerberus_storage_wardrive_session_begin(void)
{
    if (!cerberus_storage_is_mounted())
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for Wardriving failed");
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t error = ESP_OK;
    if (require_firmware_owner_locked() != ESP_OK || wardrive_storage_busy() ||
        sd_wifi_capture_file != NULL) error = ESP_ERR_INVALID_STATE;
    else sd_wardrive_session_active = true;
    release_storage_mutex();
    return error;
}

esp_err_t cerberus_storage_wardrive_session_finish(void)
{
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t error = ESP_OK;
    if (sd_wardrive_stream.file != NULL || sd_wigle_stream.file != NULL)
        error = ESP_ERR_INVALID_STATE;
    else sd_wardrive_session_active = false;
    release_storage_mutex();
    return error;
}

esp_err_t cerberus_storage_wardrive_begin(const char *header, size_t length)
{ return wardrive_stream_begin(&sd_wardrive_stream, "UNLOC", header, length); }
esp_err_t cerberus_storage_wardrive_append(const char *row, size_t length)
{ return wardrive_stream_append(&sd_wardrive_stream, row, length); }
esp_err_t cerberus_storage_wardrive_sync(void)
{ return wardrive_stream_sync(&sd_wardrive_stream); }
esp_err_t cerberus_storage_wardrive_finish(bool keep, char *name,
    size_t capacity, uint64_t *bytes)
{ return wardrive_stream_finish(&sd_wardrive_stream, keep, name, capacity, bytes); }
esp_err_t cerberus_storage_wardrive_wigle_begin(const char *header, size_t length)
{ return wardrive_stream_begin(&sd_wigle_stream, "WIGLE", header, length); }
esp_err_t cerberus_storage_wardrive_wigle_append(const char *row, size_t length)
{ return wardrive_stream_append(&sd_wigle_stream, row, length); }
esp_err_t cerberus_storage_wardrive_wigle_sync(void)
{ return wardrive_stream_sync(&sd_wigle_stream); }
esp_err_t cerberus_storage_wardrive_wigle_finish(bool keep, char *name,
    size_t capacity, uint64_t *bytes)
{ return wardrive_stream_finish(&sd_wigle_stream, keep, name, capacity, bytes); }

static void write_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void write_le32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

esp_err_t cerberus_storage_write_qr_bmp(
    const uint8_t *modules,
    uint16_t module_count,
    char *file_name,
    size_t file_name_capacity)
{
    if (modules == NULL || module_count == 0U || module_count > SD_QR_MAX_MODULE_COUNT ||
        file_name == NULL || file_name_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    file_name[0] = '\0';

    uint16_t bounded_modules = module_count + SD_QR_QUIET_ZONE_MODULES * 2U;
    uint16_t scale = SD_QR_TARGET_SIZE / bounded_modules;
    if (scale == 0U) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint16_t image_size = bounded_modules * scale;
    size_t pixel_row_bytes = (size_t)image_size * 3U;
    size_t file_row_bytes = (pixel_row_bytes + 3U) & ~3U;
    if (file_row_bytes > sizeof(sd_qr_row)) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for QR failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_qr_directory_path,
        sizeof(sd_qr_directory_path),
        "%s/CERBERUS",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_qr_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = ensure_directory_exists(sd_qr_directory_path);
    if (err == ESP_OK) {
        path_length = snprintf(
            sd_qr_directory_path,
            sizeof(sd_qr_directory_path),
            "%s/CERBERUS/QR",
            CERBERUS_STORAGE_MOUNT_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(sd_qr_directory_path)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            err = ensure_directory_exists(sd_qr_directory_path);
        }
    }
    if (err != ESP_OK) {
        release_storage_mutex();
        return err;
    }

    uint16_t qr_index = 0U;
    struct stat file_stat;
    for (uint16_t candidate = 1U; candidate <= SD_QR_FILE_LIMIT; ++candidate) {
        int final_length = snprintf(
            sd_qr_file_path,
            sizeof(sd_qr_file_path),
            "%s/QR_%04u.BMP",
            sd_qr_directory_path,
            (unsigned)candidate);
        int temp_length = snprintf(
            sd_qr_temp_path,
            sizeof(sd_qr_temp_path),
            "%s/QR_%04u.TMP",
            sd_qr_directory_path,
            (unsigned)candidate);
        if (final_length < 0 || (size_t)final_length >= sizeof(sd_qr_file_path) ||
            temp_length < 0 || (size_t)temp_length >= sizeof(sd_qr_temp_path)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (stat(sd_qr_file_path, &file_stat) != 0 && stat(sd_qr_temp_path, &file_stat) != 0) {
            qr_index = candidate;
            break;
        }
    }
    if (err != ESP_OK || qr_index == 0U) {
        release_storage_mutex();
        return err != ESP_OK ? err : ESP_FAIL;
    }

    uint32_t bitmap_bytes = (uint32_t)(file_row_bytes * image_size);
    uint8_t header[54] = {0};
    header[0] = 'B';
    header[1] = 'M';
    write_le32(&header[2], (uint32_t)sizeof(header) + bitmap_bytes);
    write_le32(&header[10], sizeof(header));
    write_le32(&header[14], 40U);
    write_le32(&header[18], image_size);
    write_le32(&header[22], image_size);
    write_le16(&header[26], 1U);
    write_le16(&header[28], 24U);
    write_le32(&header[34], bitmap_bytes);

    FILE *bitmap = fopen(sd_qr_temp_path, "wb");
    if (bitmap == NULL) {
        ESP_LOGW(TAG, "cannot open QR bitmap: errno=%d", errno);
        release_storage_mutex();
        return ESP_FAIL;
    }

    bool write_failed = fwrite(header, 1U, sizeof(header), bitmap) != sizeof(header);
    for (uint16_t file_y = 0U; file_y < image_size && !write_failed; ++file_y) {
        memset(sd_qr_row, 0xFF, file_row_bytes);
        uint16_t output_y = image_size - 1U - file_y;
        int module_y = (int)(output_y / scale) - (int)SD_QR_QUIET_ZONE_MODULES;
        if (module_y >= 0 && module_y < module_count) {
            for (uint16_t output_x = 0U; output_x < image_size; ++output_x) {
                int module_x = (int)(output_x / scale) - (int)SD_QR_QUIET_ZONE_MODULES;
                bool black = module_x >= 0 && module_x < module_count &&
                    modules[(size_t)module_y * module_count + (size_t)module_x] != 0U;
                if (black) {
                    size_t pixel_offset = (size_t)output_x * 3U;
                    sd_qr_row[pixel_offset] = 0U;
                    sd_qr_row[pixel_offset + 1U] = 0U;
                    sd_qr_row[pixel_offset + 2U] = 0U;
                }
            }
        }
        write_failed = fwrite(sd_qr_row, 1U, file_row_bytes, bitmap) != file_row_bytes;
    }
    if (fflush(bitmap) != 0) {
        write_failed = true;
    }
    if (fclose(bitmap) != 0) {
        write_failed = true;
    }
    if (write_failed) {
        ESP_LOGW(TAG, "QR bitmap write failed: errno=%d", errno);
        (void)remove(sd_qr_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }
    if (rename(sd_qr_temp_path, sd_qr_file_path) != 0) {
        ESP_LOGW(TAG, "QR bitmap finalize failed: errno=%d", errno);
        (void)remove(sd_qr_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }

    int name_length = snprintf(file_name, file_name_capacity, "QR_%04u.BMP", (unsigned)qr_index);
    if (name_length < 0 || (size_t)name_length >= file_name_capacity) {
        file_name[0] = '\0';
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "saved QR bitmap %s", sd_qr_file_path);
    release_storage_mutex();
    return ESP_OK;
}

esp_err_t cerberus_storage_write_config_backup(
    const uint8_t *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity)
{
    if (data == NULL || data_length == 0U || data_length > SD_CONFIG_MAX_BYTES ||
        file_name == NULL || file_name_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    file_name[0] = '\0';

    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for config backup failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_config_directory_path,
        sizeof(sd_config_directory_path),
        "%s/CERBERUS",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_config_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = ensure_directory_exists(sd_config_directory_path);
    if (err == ESP_OK) {
        path_length = snprintf(
            sd_config_directory_path,
            sizeof(sd_config_directory_path),
            "%s/CERBERUS/CONFIG",
            CERBERUS_STORAGE_MOUNT_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(sd_config_directory_path)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            err = ensure_directory_exists(sd_config_directory_path);
        }
    }
    if (err != ESP_OK) {
        release_storage_mutex();
        return err;
    }

    uint16_t backup_index = 0U;
    struct stat file_stat;
    for (uint16_t candidate = 1U; candidate <= SD_CONFIG_FILE_LIMIT; ++candidate) {
        int final_length = snprintf(
            sd_config_file_path,
            sizeof(sd_config_file_path),
            "%s/CFG_%04u.CBK",
            sd_config_directory_path,
            (unsigned)candidate);
        int temp_length = snprintf(
            sd_config_temp_path,
            sizeof(sd_config_temp_path),
            "%s/CFG_%04u.TMP",
            sd_config_directory_path,
            (unsigned)candidate);
        if (final_length < 0 || (size_t)final_length >= sizeof(sd_config_file_path) ||
            temp_length < 0 || (size_t)temp_length >= sizeof(sd_config_temp_path)) {
            err = ESP_ERR_INVALID_SIZE;
            break;
        }
        if (stat(sd_config_file_path, &file_stat) != 0 &&
            stat(sd_config_temp_path, &file_stat) != 0) {
            backup_index = candidate;
            break;
        }
    }
    if (err != ESP_OK || backup_index == 0U) {
        release_storage_mutex();
        return err != ESP_OK ? err : ESP_FAIL;
    }

    FILE *backup = fopen(sd_config_temp_path, "wb");
    if (backup == NULL) {
        ESP_LOGW(TAG, "cannot open config backup: errno=%d", errno);
        release_storage_mutex();
        return ESP_FAIL;
    }
    bool write_failed = fwrite(data, 1U, data_length, backup) != data_length;
    if (fflush(backup) != 0) {
        write_failed = true;
    }
    if (fclose(backup) != 0) {
        write_failed = true;
    }
    if (write_failed) {
        ESP_LOGW(TAG, "config backup write failed: errno=%d", errno);
        (void)remove(sd_config_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }
    if (rename(sd_config_temp_path, sd_config_file_path) != 0) {
        ESP_LOGW(TAG, "config backup finalize failed: errno=%d", errno);
        (void)remove(sd_config_temp_path);
        release_storage_mutex();
        return ESP_FAIL;
    }

    int name_length = snprintf(
        file_name,
        file_name_capacity,
        "CFG_%04u.CBK",
        (unsigned)backup_index);
    if (name_length < 0 || (size_t)name_length >= file_name_capacity) {
        file_name[0] = '\0';
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    ESP_LOGI(TAG, "saved config backup %s", sd_config_file_path);
    release_storage_mutex();
    return ESP_OK;
}

static bool config_backup_name_index(const char *file_name, uint16_t *backup_index)
{
    if (file_name == NULL || backup_index == NULL || strlen(file_name) != 12U ||
        memcmp(file_name, "CFG_", 4U) != 0 || memcmp(file_name + 8U, ".CBK", 4U) != 0) {
        return false;
    }
    uint16_t value = 0U;
    for (uint8_t index = 4U; index < 8U; ++index) {
        if (file_name[index] < '0' || file_name[index] > '9') {
            return false;
        }
        value = (uint16_t)(value * 10U + (uint16_t)(file_name[index] - '0'));
    }
    if (value == 0U) {
        return false;
    }
    *backup_index = value;
    return true;
}

esp_err_t cerberus_storage_read_latest_config_backup(
    uint8_t *data,
    size_t data_capacity,
    size_t *data_length,
    char *file_name,
    size_t file_name_capacity)
{
    if (data == NULL || data_capacity == 0U || data_length == NULL ||
        file_name == NULL || file_name_capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }
    *data_length = 0U;
    file_name[0] = '\0';

    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for config restore failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_config_directory_path,
        sizeof(sd_config_directory_path),
        "%s/CERBERUS/CONFIG",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_config_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    DIR *directory = opendir(sd_config_directory_path);
    if (directory == NULL) {
        release_storage_mutex();
        return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;
    }
    uint16_t newest_index = 0U;
    char newest_name[CERBERUS_STORAGE_NAME_MAX] = {0};
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        uint16_t candidate_index = 0U;
        if (config_backup_name_index(entry->d_name, &candidate_index) &&
            candidate_index > newest_index) {
            newest_index = candidate_index;
            memcpy(newest_name, entry->d_name, 13U);
        }
    }
    closedir(directory);
    if (newest_index == 0U) {
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }

    path_length = snprintf(
        sd_config_file_path,
        sizeof(sd_config_file_path),
        "%s/%s",
        sd_config_directory_path,
        newest_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_config_file_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    struct stat backup_stat;
    if (stat(sd_config_file_path, &backup_stat) != 0 || !S_ISREG(backup_stat.st_mode)) {
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }
    if (backup_stat.st_size <= 0 || (uint64_t)backup_stat.st_size > data_capacity ||
        (uint64_t)backup_stat.st_size > SD_CONFIG_MAX_BYTES) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *backup = fopen(sd_config_file_path, "rb");
    if (backup == NULL) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    size_t expected = (size_t)backup_stat.st_size;
    size_t bytes_read = fread(data, 1U, expected, backup);
    bool read_failed = bytes_read != expected || ferror(backup) != 0;
    if (fclose(backup) != 0) {
        read_failed = true;
    }
    if (read_failed) {
        memset(data, 0, bytes_read);
        release_storage_mutex();
        return ESP_FAIL;
    }

    int name_length = snprintf(file_name, file_name_capacity, "%s", newest_name);
    if (name_length < 0 || (size_t)name_length >= file_name_capacity) {
        memset(data, 0, expected);
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    *data_length = expected;
    ESP_LOGI(TAG, "loaded config backup candidate %s", sd_config_file_path);
    release_storage_mutex();
    return ESP_OK;
}

static bool append_csv_field(
    char *line,
    size_t line_capacity,
    size_t *line_length,
    const char *value)
{
    if (*line_length >= line_capacity - 1U) {
        return false;
    }
    line[(*line_length)++] = '"';
    for (size_t index = 0U; value[index] != '\0'; ++index) {
        char character = value[index];
        if (character == '\r' || character == '\n') {
            character = ' ';
        } else if ((uint8_t)character < 32U || (uint8_t)character > 126U) {
            character = '?';
        }
        size_t required = character == '"' ? 2U : 1U;
        if (*line_length > line_capacity - 2U || required > line_capacity - *line_length - 2U) {
            return false;
        }
        if (character == '"') {
            line[(*line_length)++] = '"';
        }
        line[(*line_length)++] = character;
    }
    if (*line_length >= line_capacity - 1U) {
        return false;
    }
    line[(*line_length)++] = '"';
    line[*line_length] = '\0';
    return true;
}

static esp_err_t rotate_system_logs(void)
{
    struct stat file_stat;
    for (int rotation = (int)SD_SYSTEM_LOG_ROTATIONS; rotation >= 1; --rotation) {
        int source_length = rotation == 1
            ? snprintf(
                sd_system_log_rotate_source,
                sizeof(sd_system_log_rotate_source),
                "%s/SYSTEM.CSV",
                sd_system_log_directory_path)
            : snprintf(
                sd_system_log_rotate_source,
                sizeof(sd_system_log_rotate_source),
                "%s/SYSTEM.%d.CSV",
                sd_system_log_directory_path,
                rotation - 1);
        int target_length = snprintf(
            sd_system_log_rotate_target,
            sizeof(sd_system_log_rotate_target),
            "%s/SYSTEM.%d.CSV",
            sd_system_log_directory_path,
            rotation);
        if (source_length < 0 || (size_t)source_length >= sizeof(sd_system_log_rotate_source) ||
            target_length < 0 || (size_t)target_length >= sizeof(sd_system_log_rotate_target)) {
            return ESP_ERR_INVALID_SIZE;
        }
        (void)remove(sd_system_log_rotate_target);
        if (stat(sd_system_log_rotate_source, &file_stat) == 0 &&
            rename(sd_system_log_rotate_source, sd_system_log_rotate_target) != 0) {
            ESP_LOGW(TAG, "system log rotation failed: errno=%d", errno);
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

esp_err_t cerberus_storage_log_event(
    cerberus_log_level_t level,
    const char *component,
    const char *event,
    const char *detail)
{
    if (level < CERBERUS_LOG_INFO || level > CERBERUS_LOG_ERROR ||
        component == NULL || component[0] == '\0' ||
        event == NULL || event[0] == '\0' || detail == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!cerberus_storage_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }

    const char *level_text = level == CERBERUS_LOG_INFO
        ? "INFO"
        : (level == CERBERUS_LOG_WARNING ? "WARN" : "ERROR");
    int prefix_length = snprintf(
        sd_system_log_line,
        sizeof(sd_system_log_line),
        "%llu,",
        (unsigned long long)(esp_timer_get_time() / 1000));
    if (prefix_length < 0 || (size_t)prefix_length >= sizeof(sd_system_log_line)) {
        return ESP_ERR_INVALID_SIZE;
    }
    size_t line_length = (size_t)prefix_length;
    if (
        !append_csv_field(sd_system_log_line, sizeof(sd_system_log_line), &line_length, level_text)) {
        return ESP_ERR_INVALID_SIZE;
    }
    const char *fields[] = {component, event, detail};
    for (size_t index = 0U; index < sizeof(fields) / sizeof(fields[0]); ++index) {
        if (line_length >= sizeof(sd_system_log_line) - 2U) {
            return ESP_ERR_INVALID_SIZE;
        }
        sd_system_log_line[line_length++] = ',';
        if (!append_csv_field(
                sd_system_log_line,
                sizeof(sd_system_log_line),
                &line_length,
                fields[index])) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    if (line_length > sizeof(sd_system_log_line) - 3U) {
        return ESP_ERR_INVALID_SIZE;
    }
    sd_system_log_line[line_length++] = '\r';
    sd_system_log_line[line_length++] = '\n';
    sd_system_log_line[line_length] = '\0';

    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_system_log_directory_path,
        sizeof(sd_system_log_directory_path),
        "%s/CERBERUS",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_system_log_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = ensure_directory_exists(sd_system_log_directory_path);
    if (err == ESP_OK) {
        path_length = snprintf(
            sd_system_log_directory_path,
            sizeof(sd_system_log_directory_path),
            "%s/CERBERUS/LOGS",
            CERBERUS_STORAGE_MOUNT_PATH);
        if (path_length < 0 || (size_t)path_length >= sizeof(sd_system_log_directory_path)) {
            err = ESP_ERR_INVALID_SIZE;
        } else {
            err = ensure_directory_exists(sd_system_log_directory_path);
        }
    }
    if (err != ESP_OK) {
        release_storage_mutex();
        return err;
    }

    path_length = snprintf(
        sd_system_log_path,
        sizeof(sd_system_log_path),
        "%s/SYSTEM.CSV",
        sd_system_log_directory_path);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_system_log_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat log_stat;
    bool log_exists = stat(sd_system_log_path, &log_stat) == 0 &&
        S_ISREG(log_stat.st_mode) && log_stat.st_size > 0;
    static const char header[] = "uptime_ms,level,component,event,detail\r\n";
    uint64_t pending_bytes = line_length + (log_exists ? 0U : sizeof(header) - 1U);
    if (log_exists && (uint64_t)log_stat.st_size + pending_bytes > SD_SYSTEM_LOG_MAX_BYTES) {
        err = rotate_system_logs();
        if (err != ESP_OK) {
            release_storage_mutex();
            return err;
        }
        log_exists = false;
    }

    FILE *log_file = fopen(sd_system_log_path, "ab");
    if (log_file == NULL) {
        release_storage_mutex();
        return ESP_FAIL;
    }
    bool write_failed = false;
    if (!log_exists) {
        write_failed = fwrite(header, 1U, sizeof(header) - 1U, log_file) != sizeof(header) - 1U;
    }
    if (!write_failed) {
        write_failed = fwrite(sd_system_log_line, 1U, line_length, log_file) != line_length;
    }
    if (fflush(log_file) != 0) {
        write_failed = true;
    }
    if (fclose(log_file) != 0) {
        write_failed = true;
    }
    release_storage_mutex();
    return write_failed ? ESP_FAIL : ESP_OK;
}

esp_err_t cerberus_storage_delete_diagnostic_report(const char *file_name)
{
    if (!diagnostic_report_name_is_safe(file_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_diagnostic_file_path,
        sizeof(sd_diagnostic_file_path),
        "%s/CERBERUS/LOGS/%s",
        CERBERUS_STORAGE_MOUNT_PATH,
        file_name);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_diagnostic_file_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    struct stat file_stat;
    if (stat(sd_diagnostic_file_path, &file_stat) != 0 || !S_ISREG(file_stat.st_mode)) {
        release_storage_mutex();
        return ESP_ERR_NOT_FOUND;
    }
    if (remove(sd_diagnostic_file_path) != 0) {
        ESP_LOGW(TAG, "cannot delete diagnostic report: errno=%d", errno);
        release_storage_mutex();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "deleted diagnostic report %s", file_name);
    release_storage_mutex();
    return ESP_OK;
}

static uint8_t benchmark_pattern_byte(uint32_t offset)
{
    return (uint8_t)(((offset * 33U) + 17U) ^ (offset >> 8U));
}

esp_err_t cerberus_storage_run_benchmark(cerberus_storage_benchmark_result_t *result)
{
    if (result == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(result, 0, sizeof(*result));

    if (!cerberus_storage_is_mounted()) {
        ESP_RETURN_ON_ERROR(cerberus_storage_mount(), TAG, "SD mount for benchmark failed");
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    if (!sd_mounted) {
        release_storage_mutex();
        return ESP_ERR_INVALID_STATE;
    }

    int path_length = snprintf(
        sd_diagnostic_directory_path,
        sizeof(sd_diagnostic_directory_path),
        "%s/CERBERUS",
        CERBERUS_STORAGE_MOUNT_PATH);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_diagnostic_directory_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }
    esp_err_t err = ensure_directory_exists(sd_diagnostic_directory_path);
    if (err != ESP_OK) {
        release_storage_mutex();
        return err;
    }

    path_length = snprintf(
        sd_diagnostic_temp_path,
        sizeof(sd_diagnostic_temp_path),
        "%s/%s",
        sd_diagnostic_directory_path,
        SD_BENCHMARK_FILE_NAME);
    if (path_length < 0 || (size_t)path_length >= sizeof(sd_diagnostic_temp_path)) {
        release_storage_mutex();
        return ESP_ERR_INVALID_SIZE;
    }

    /* This exact firmware-owned temporary path is the only file touched. A
       prior interrupted benchmark may leave it behind, so discard it first. */
    (void)remove(sd_diagnostic_temp_path);
    FILE *file = fopen(sd_diagnostic_temp_path, "wb");
    if (file == NULL) {
        ESP_LOGW(TAG, "cannot open SD benchmark file for writing: errno=%d", errno);
        release_storage_mutex();
        return ESP_FAIL;
    }

    int64_t started_us = esp_timer_get_time();
    for (uint32_t offset = 0U; offset < CERBERUS_STORAGE_BENCHMARK_BYTES;) {
        size_t block_size = CERBERUS_STORAGE_BENCHMARK_BYTES - offset;
        if (block_size > sizeof(sd_hash_buffer)) {
            block_size = sizeof(sd_hash_buffer);
        }
        for (size_t index = 0U; index < block_size; ++index) {
            sd_hash_buffer[index] = benchmark_pattern_byte(offset + (uint32_t)index);
        }
        size_t written = fwrite(sd_hash_buffer, 1U, block_size, file);
        result->bytes_written += (uint32_t)written;
        if (written != block_size) {
            err = ESP_FAIL;
            break;
        }
        offset += (uint32_t)block_size;
    }
    if (fflush(file) != 0) {
        err = ESP_FAIL;
    }
    if (fclose(file) != 0) {
        err = ESP_FAIL;
    }
    result->write_time_us = (uint64_t)(esp_timer_get_time() - started_us);

    if (err == ESP_OK) {
        file = fopen(sd_diagnostic_temp_path, "rb");
        if (file == NULL) {
            ESP_LOGW(TAG, "cannot open SD benchmark file for reading: errno=%d", errno);
            err = ESP_FAIL;
        } else {
            bool verified = true;
            started_us = esp_timer_get_time();
            for (uint32_t offset = 0U; offset < CERBERUS_STORAGE_BENCHMARK_BYTES;) {
                size_t block_size = CERBERUS_STORAGE_BENCHMARK_BYTES - offset;
                if (block_size > sizeof(sd_hash_buffer)) {
                    block_size = sizeof(sd_hash_buffer);
                }
                size_t read = fread(sd_hash_buffer, 1U, block_size, file);
                result->bytes_read += (uint32_t)read;
                if (read != block_size) {
                    err = ESP_FAIL;
                    verified = false;
                    break;
                }
                for (size_t index = 0U; index < block_size; ++index) {
                    if (sd_hash_buffer[index] != benchmark_pattern_byte(offset + (uint32_t)index)) {
                        err = ESP_ERR_INVALID_CRC;
                        verified = false;
                        break;
                    }
                }
                if (!verified) {
                    break;
                }
                offset += (uint32_t)block_size;
            }
            if (fclose(file) != 0 && err == ESP_OK) {
                err = ESP_FAIL;
            }
            result->read_time_us = (uint64_t)(esp_timer_get_time() - started_us);
            result->data_verified = verified && err == ESP_OK &&
                result->bytes_read == CERBERUS_STORAGE_BENCHMARK_BYTES;
        }
    }

    if (remove(sd_diagnostic_temp_path) != 0) {
        ESP_LOGW(TAG, "cannot remove SD benchmark file: errno=%d", errno);
        if (err == ESP_OK) {
            err = ESP_FAIL;
        }
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_create_directory(const char *relative_path, const char *name)
{
    if (!relative_path_is_safe(relative_path) || !file_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_destination_path, sizeof(sd_operation_destination_path), relative_path, name);
    }
    struct stat entry_stat;
    if (err == ESP_OK && stat(sd_operation_destination_path, &entry_stat) == 0) {
        err = ESP_ERR_INVALID_STATE;
    } else if (err == ESP_OK && mkdir(sd_operation_destination_path, 0775) != 0) {
        err = ESP_FAIL;
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_create_empty_file(const char *relative_path, const char *name)
{
    if (!relative_path_is_safe(relative_path) || !file_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_destination_path, sizeof(sd_operation_destination_path), relative_path, name);
    }
    struct stat entry_stat;
    if (err == ESP_OK && stat(sd_operation_destination_path, &entry_stat) == 0) {
        err = ESP_ERR_INVALID_STATE;
    }
    FILE *file = NULL;
    if (err == ESP_OK) {
        file = fopen(sd_operation_destination_path, "wb");
        if (file == NULL) {
            err = ESP_FAIL;
        }
    }
    if (file != NULL) {
        if (fflush(file) != 0 || fsync(fileno(file)) != 0) {
            err = ESP_FAIL;
        }
        fclose(file);
        if (err != ESP_OK) {
            (void)remove(sd_operation_destination_path);
        }
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_rename_entry(const char *relative_path, const char *old_name, const char *new_name)
{
    return cerberus_storage_move_entry(relative_path, old_name, relative_path, new_name);
}

esp_err_t cerberus_storage_copy_file(
    const char *source_path,
    const char *source_name,
    const char *destination_path,
    const char *destination_name)
{
    if (!relative_path_is_safe(source_path) || !file_name_is_safe(source_name) ||
        !relative_path_is_safe(destination_path) || !file_name_is_safe(destination_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_source_path, sizeof(sd_operation_source_path), source_path, source_name);
    }
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_destination_path, sizeof(sd_operation_destination_path), destination_path, destination_name);
    }
    int temp_length = 0;
    if (err == ESP_OK) {
        temp_length = snprintf(sd_operation_temp_path, sizeof(sd_operation_temp_path), "%s.CERBTMP", sd_operation_destination_path);
        if (temp_length < 0 || (size_t)temp_length >= sizeof(sd_operation_temp_path)) {
            err = ESP_ERR_INVALID_SIZE;
        }
    }
    struct stat source_stat;
    struct stat destination_stat;
    if (err == ESP_OK && (stat(sd_operation_source_path, &source_stat) != 0 || !S_ISREG(source_stat.st_mode))) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK && (stat(sd_operation_destination_path, &destination_stat) == 0 ||
        stat(sd_operation_temp_path, &destination_stat) == 0)) {
        err = ESP_ERR_INVALID_STATE;
    }
    FILE *source = NULL;
    FILE *destination = NULL;
    bool temp_created = false;
    if (err == ESP_OK) {
        source = fopen(sd_operation_source_path, "rb");
        destination = fopen(sd_operation_temp_path, "wb");
        temp_created = destination != NULL;
        if (source == NULL || destination == NULL) {
            err = ESP_FAIL;
        }
    }
    while (err == ESP_OK) {
        size_t count = fread(sd_hash_buffer, 1, sizeof(sd_hash_buffer), source);
        if (count > 0U && fwrite(sd_hash_buffer, 1, count, destination) != count) {
            err = ESP_FAIL;
            break;
        }
        if (count < sizeof(sd_hash_buffer)) {
            if (ferror(source)) {
                err = ESP_FAIL;
            }
            break;
        }
        vTaskDelay(1);
    }
    if (destination != NULL && err == ESP_OK &&
        (fflush(destination) != 0 || fsync(fileno(destination)) != 0)) {
        err = ESP_FAIL;
    }
    if (source != NULL) {
        fclose(source);
    }
    if (destination != NULL) {
        fclose(destination);
    }
    if (err == ESP_OK && rename(sd_operation_temp_path, sd_operation_destination_path) != 0) {
        err = ESP_FAIL;
    }
    if (err != ESP_OK && temp_created) {
        (void)remove(sd_operation_temp_path);
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_move_entry(
    const char *source_path,
    const char *source_name,
    const char *destination_path,
    const char *destination_name)
{
    if (!relative_path_is_safe(source_path) || !file_name_is_safe(source_name) ||
        !relative_path_is_safe(destination_path) || !file_name_is_safe(destination_name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (entry_is_protected_system_file(source_path, source_name)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_source_path, sizeof(sd_operation_source_path), source_path, source_name);
    }
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_destination_path, sizeof(sd_operation_destination_path), destination_path, destination_name);
    }
    struct stat entry_stat;
    if (err == ESP_OK && stat(sd_operation_source_path, &entry_stat) != 0) {
        err = ESP_ERR_NOT_FOUND;
    }
    if (err == ESP_OK && S_ISDIR(entry_stat.st_mode)) {
        size_t source_length = strlen(sd_operation_source_path);
        if (strncmp(sd_operation_destination_path, sd_operation_source_path, source_length) == 0 &&
            sd_operation_destination_path[source_length] == '/') {
            err = ESP_ERR_INVALID_ARG;
        }
    }
    if (err == ESP_OK && stat(sd_operation_destination_path, &entry_stat) == 0) {
        err = ESP_ERR_INVALID_STATE;
    } else if (err == ESP_OK && rename(sd_operation_source_path, sd_operation_destination_path) != 0) {
        err = ESP_FAIL;
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_delete_entry(const char *relative_path, const char *name)
{
    if (!relative_path_is_safe(relative_path) || !file_name_is_safe(name)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (entry_is_protected_system_file(relative_path, name)) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_source_path, sizeof(sd_operation_source_path), relative_path, name);
    }
    struct stat entry_stat;
    if (err == ESP_OK && stat(sd_operation_source_path, &entry_stat) != 0) {
        err = ESP_ERR_NOT_FOUND;
    } else if (err == ESP_OK) {
        int result = S_ISDIR(entry_stat.st_mode) ? rmdir(sd_operation_source_path) : remove(sd_operation_source_path);
        if (result != 0) {
            err = errno == ENOTEMPTY ? ESP_ERR_INVALID_STATE : ESP_FAIL;
        }
    }
    release_storage_mutex();
    return err;
}

esp_err_t cerberus_storage_inspect_firmware_package(
    const char *file_name,
    cerberus_firmware_package_info_t *info)
{
    if (!file_name_is_safe(file_name) || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *extension = strrchr(file_name, '.');
    if (extension == NULL || (strcasecmp(extension, ".bin") != 0 && strcasecmp(extension, ".cerb") != 0)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    memset(info, 0, sizeof(*info));
    size_t bytes_read = 0U;
    uint8_t header[4] = {0};
    ESP_RETURN_ON_ERROR(
        cerberus_storage_read_file_chunk(CERBERUS_STORAGE_FIRMWARE_PATH, file_name, 0U,
            header, sizeof(header), &bytes_read, &info->size_bytes),
        TAG,
        "firmware header read failed");
    if (bytes_read >= sizeof(header) && header[0] == 0xE9U && header[1] > 0U && header[1] <= 16U) {
        info->esp_image = true;
        info->segment_count = header[1];
        info->spi_mode = header[2];
        info->spi_speed_size = header[3];
    }
    uint64_t hashed_size = 0U;
    esp_err_t err = cerberus_storage_sha256_file(
        CERBERUS_STORAGE_FIRMWARE_PATH, file_name, info->sha256, &hashed_size);
    if (err == ESP_OK && hashed_size != info->size_bytes) {
        return ESP_ERR_INVALID_SIZE;
    }
    return err;
}

esp_err_t cerberus_storage_write_firmware_package(
    const char *file_name,
    const uint8_t *data,
    size_t data_length,
    cerberus_firmware_package_info_t *info)
{
    if (!file_name_is_safe(file_name) || data == NULL || data_length == 0U ||
        data_length > CERBERUS_STORAGE_FIRMWARE_MAX_BYTES || info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *extension = strrchr(file_name, '.');
    if (extension == NULL || (strcasecmp(extension, ".bin") != 0 && strcasecmp(extension, ".cerb") != 0)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    ESP_RETURN_ON_ERROR(take_storage_mutex(), TAG, "storage lock failed");
    esp_err_t err = require_firmware_owner_locked();
    if (err == ESP_OK) {
        err = build_entry_path(sd_operation_destination_path, sizeof(sd_operation_destination_path),
            CERBERUS_STORAGE_FIRMWARE_PATH, file_name);
    }
    if (err == ESP_OK) {
        int length = snprintf(sd_operation_temp_path, sizeof(sd_operation_temp_path), "%s.CERBTMP", sd_operation_destination_path);
        if (length < 0 || (size_t)length >= sizeof(sd_operation_temp_path)) err = ESP_ERR_INVALID_SIZE;
    }
    struct stat entry_stat;
    if (err == ESP_OK && (stat(sd_operation_destination_path, &entry_stat) == 0 || stat(sd_operation_temp_path, &entry_stat) == 0)) {
        err = ESP_ERR_INVALID_STATE;
    }
    FILE *file = NULL;
    if (err == ESP_OK) {
        file = fopen(sd_operation_temp_path, "wb");
        if (file == NULL) err = ESP_FAIL;
    }
    if (file != NULL) {
        if (fwrite(data, 1U, data_length, file) != data_length || fflush(file) != 0 || fsync(fileno(file)) != 0) err = ESP_FAIL;
        if (fclose(file) != 0 && err == ESP_OK) err = ESP_FAIL;
    }
    if (err == ESP_OK && rename(sd_operation_temp_path, sd_operation_destination_path) != 0) err = ESP_FAIL;
    if (err != ESP_OK && file != NULL) (void)remove(sd_operation_temp_path);
    release_storage_mutex();
    if (err != ESP_OK) return err;
    err = cerberus_storage_inspect_firmware_package(file_name, info);
    if (err != ESP_OK || !info->esp_image) {
        (void)cerberus_storage_delete_entry(CERBERUS_STORAGE_FIRMWARE_PATH, file_name);
        return err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE;
    }
    return ESP_OK;
}
