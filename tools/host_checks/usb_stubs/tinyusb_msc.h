#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "sdmmc_cmd.h"
typedef void *tinyusb_msc_storage_handle_t;
enum { TINYUSB_MSC_EVENT_MOUNT_START, TINYUSB_MSC_EVENT_MOUNT_COMPLETE };
enum { TINYUSB_MSC_STORAGE_MOUNT_USB, TINYUSB_MSC_STORAGE_MOUNT_APP };
typedef struct { int id, mount_point; } tinyusb_msc_event_t;
typedef struct { void (*callback)(tinyusb_msc_storage_handle_t, tinyusb_msc_event_t *, void *); } tinyusb_msc_driver_config_t;
typedef struct {
    struct { sdmmc_card_t *card; } medium;
    struct {
        const char *base_path;
        struct { bool format_if_mount_failed; int max_files, allocation_unit_size; } config;
        bool do_not_format; int format_flags;
    } fat_fs;
    int mount_point;
} tinyusb_msc_storage_config_t;
esp_err_t tinyusb_msc_install_driver(const tinyusb_msc_driver_config_t *config);
esp_err_t tinyusb_msc_uninstall_driver(void);
esp_err_t tinyusb_msc_new_storage_sdmmc(const tinyusb_msc_storage_config_t *config, tinyusb_msc_storage_handle_t *handle);
esp_err_t tinyusb_msc_delete_storage(tinyusb_msc_storage_handle_t handle);
