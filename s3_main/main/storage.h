#pragma once

#include "esp_err.h"
#include "sdmmc_cmd.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_STORAGE_MOUNT_PATH "/sdcard"
#define CERBERUS_STORAGE_NAME_MAX 256
#define CERBERUS_STORAGE_PATH_MAX 512
#define CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES 4096
#define CERBERUS_STORAGE_BENCHMARK_BYTES (512U * 1024U)
#define CERBERUS_STORAGE_FIRMWARE_PATH "/CERBERUS/FIRMWARE"
#define CERBERUS_STORAGE_SCRIPTS_PATH "/CERBERUS/SCRIPTS"
#define CERBERUS_STORAGE_FIRMWARE_MAX_BYTES (4U * 1024U * 1024U)

typedef struct {
    bool mounted;
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t sector_size;
    char card_name[8];
} cerberus_storage_status_t;

typedef struct {
    char name[CERBERUS_STORAGE_NAME_MAX];
    uint64_t size_bytes;
    bool is_directory;
} cerberus_storage_entry_t;

typedef struct {
    uint32_t bytes_written;
    uint32_t bytes_read;
    uint64_t write_time_us;
    uint64_t read_time_us;
    bool data_verified;
} cerberus_storage_benchmark_result_t;

typedef enum {
    CERBERUS_LOG_INFO,
    CERBERUS_LOG_WARNING,
    CERBERUS_LOG_ERROR,
} cerberus_log_level_t;

typedef enum {
    CERBERUS_STORAGE_OWNER_NONE,
    CERBERUS_STORAGE_OWNER_FIRMWARE,
    CERBERUS_STORAGE_OWNER_USB,
} cerberus_storage_owner_t;

typedef struct {
    uint64_t size_bytes;
    uint8_t sha256[32];
    bool esp_image;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed_size;
} cerberus_firmware_package_info_t;

esp_err_t cerberus_storage_mount(void);
esp_err_t cerberus_storage_unmount(void);
bool cerberus_storage_is_mounted(void);
cerberus_storage_owner_t cerberus_storage_get_owner(void);
esp_err_t cerberus_storage_reserve_for_usb(void);
esp_err_t cerberus_storage_release_from_usb(void);
esp_err_t cerberus_storage_acquire_usb_card(sdmmc_card_t **card);
esp_err_t cerberus_storage_release_usb_card(void);
esp_err_t cerberus_storage_get_status(cerberus_storage_status_t *status);
esp_err_t cerberus_storage_list_directory(
    const char *relative_path,
    cerberus_storage_entry_t *entries,
    size_t entry_capacity,
    size_t *entry_count);
esp_err_t cerberus_storage_read_file_chunk(
    const char *relative_path,
    const char *file_name,
    uint64_t offset,
    uint8_t *data,
    size_t data_capacity,
    size_t *bytes_read,
    uint64_t *file_size);
esp_err_t cerberus_storage_sha256_file(
    const char *relative_path,
    const char *file_name,
    uint8_t digest[32],
    uint64_t *file_size);
esp_err_t cerberus_storage_write_diagnostic_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_write_wifi_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_write_wifi_defense_report(
    const char *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_write_qr_bmp(
    const uint8_t *modules,
    uint16_t module_count,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_write_config_backup(
    const uint8_t *data,
    size_t data_length,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_read_latest_config_backup(
    uint8_t *data,
    size_t data_capacity,
    size_t *data_length,
    char *file_name,
    size_t file_name_capacity);
esp_err_t cerberus_storage_log_event(
    cerberus_log_level_t level,
    const char *component,
    const char *event,
    const char *detail);
esp_err_t cerberus_storage_delete_diagnostic_report(const char *file_name);
esp_err_t cerberus_storage_run_benchmark(cerberus_storage_benchmark_result_t *result);
esp_err_t cerberus_storage_create_directory(const char *relative_path, const char *name);
esp_err_t cerberus_storage_create_empty_file(const char *relative_path, const char *name);
esp_err_t cerberus_storage_rename_entry(const char *relative_path, const char *old_name, const char *new_name);
esp_err_t cerberus_storage_copy_file(
    const char *source_path,
    const char *source_name,
    const char *destination_path,
    const char *destination_name);
esp_err_t cerberus_storage_move_entry(
    const char *source_path,
    const char *source_name,
    const char *destination_path,
    const char *destination_name);
esp_err_t cerberus_storage_delete_entry(const char *relative_path, const char *name);
esp_err_t cerberus_storage_inspect_firmware_package(
    const char *file_name,
    cerberus_firmware_package_info_t *info);
esp_err_t cerberus_storage_write_firmware_package(
    const char *file_name,
    const uint8_t *data,
    size_t data_length,
    cerberus_firmware_package_info_t *info);

#ifdef __cplusplus
}
#endif
