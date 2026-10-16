#include "usb_mass_storage.h"

#include "storage.h"
#include "usb_console.h"

#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"
#include "tinyusb_msc.h"

#include <string.h>

#define USB_MSC_APP_MOUNT_PATH "/usb_sd"

static const char *TAG = "usb_msc";
static tinyusb_msc_storage_handle_t storage_handle;
static sdmmc_card_t *usb_card;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;
static cerberus_usb_mass_storage_status_t mass_storage_status;

static void update_state(bool attached, bool safe)
{
    portENTER_CRITICAL(&status_lock);
    mass_storage_status.host_attached = attached;
    mass_storage_status.safe_to_release = safe;
    portEXIT_CRITICAL(&status_lock);
}

static void storage_event(
    tinyusb_msc_storage_handle_t handle,
    tinyusb_msc_event_t *event,
    void *argument)
{
    (void)handle;
    (void)argument;
    if (event == NULL) {
        return;
    }
    if (event->id == TINYUSB_MSC_EVENT_MOUNT_START &&
        event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_USB) {
        portENTER_CRITICAL(&status_lock);
        mass_storage_status.safe_to_release = false;
        portEXIT_CRITICAL(&status_lock);
    } else if (event->id == TINYUSB_MSC_EVENT_MOUNT_COMPLETE &&
        event->mount_point == TINYUSB_MSC_STORAGE_MOUNT_APP) {
        portENTER_CRITICAL(&status_lock);
        mass_storage_status.safe_to_release = true;
        portEXIT_CRITICAL(&status_lock);
        ESP_LOGI(TAG, "host released microSD safely");
    }
}

static void usb_event(tinyusb_event_t *event, void *argument)
{
    (void)argument;
    if (event == NULL) {
        return;
    }
    if (event->id == TINYUSB_EVENT_ATTACHED) {
        update_state(true, false);
        ESP_LOGI(TAG, "USB mass-storage host attached");
    } else if (event->id == TINYUSB_EVENT_DETACHED) {
        update_state(false, true);
        ESP_LOGI(TAG, "USB mass-storage host detached");
    }
}

static void restore_normal_usb(void)
{
    (void)cerberus_storage_release_usb_card();
    (void)cerberus_storage_release_from_usb();
    (void)cerberus_storage_mount();
    (void)cerberus_usb_console_start();
}

esp_err_t cerberus_usb_mass_storage_start(void)
{
    portENTER_CRITICAL(&status_lock);
    bool already_active = mass_storage_status.active;
    portEXIT_CRITICAL(&status_lock);
    if (already_active) {
        return ESP_OK;
    }

    esp_err_t err = cerberus_usb_console_stop();
    if (err != ESP_OK) {
        return err;
    }
    err = cerberus_storage_reserve_for_usb();
    if (err != ESP_OK) {
        (void)cerberus_usb_console_start();
        return err;
    }
    err = cerberus_storage_acquire_usb_card(&usb_card);
    if (err != ESP_OK) {
        restore_normal_usb();
        return err;
    }

    tinyusb_msc_driver_config_t driver_config = {
        .callback = storage_event,
    };
    err = tinyusb_msc_install_driver(&driver_config);
    if (err != ESP_OK) {
        restore_normal_usb();
        return err;
    }

    tinyusb_msc_storage_config_t storage_config = {
        .medium.card = usb_card,
        .fat_fs = {
            .base_path = USB_MSC_APP_MOUNT_PATH,
            .config = {
                .format_if_mount_failed = false,
                .max_files = 4,
                .allocation_unit_size = 16 * 1024,
            },
            .do_not_format = true,
            .format_flags = 0,
        },
        .mount_point = TINYUSB_MSC_STORAGE_MOUNT_USB,
    };
    err = tinyusb_msc_new_storage_sdmmc(&storage_config, &storage_handle);
    if (err != ESP_OK) {
        (void)tinyusb_msc_uninstall_driver();
        restore_normal_usb();
        return err;
    }

    tinyusb_config_t usb_config = TINYUSB_DEFAULT_CONFIG();
    usb_config.event_cb = usb_event;
    err = tinyusb_driver_install(&usb_config);
    if (err != ESP_OK) {
        (void)tinyusb_msc_delete_storage(storage_handle);
        storage_handle = NULL;
        (void)tinyusb_msc_uninstall_driver();
        restore_normal_usb();
        return err;
    }

    portENTER_CRITICAL(&status_lock);
    mass_storage_status.active = true;
    mass_storage_status.host_attached = false;
    mass_storage_status.safe_to_release = true;
    mass_storage_status.last_error = ESP_OK;
    portEXIT_CRITICAL(&status_lock);
    ESP_LOGI(TAG, "microSD exposed as exclusive USB mass storage");
    return ESP_OK;
}

esp_err_t cerberus_usb_mass_storage_stop(void)
{
    cerberus_usb_mass_storage_status_t status;
    cerberus_usb_mass_storage_get_status(&status);
    if (!status.active) {
        return ESP_OK;
    }
    if (!status.safe_to_release) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = tinyusb_driver_uninstall();
    if (err == ESP_OK) {
        err = tinyusb_msc_delete_storage(storage_handle);
    }
    if (err == ESP_OK) {
        storage_handle = NULL;
        err = tinyusb_msc_uninstall_driver();
    }
    if (err == ESP_OK) {
        err = cerberus_storage_release_usb_card();
        usb_card = NULL;
    }
    if (err == ESP_OK) {
        err = cerberus_storage_release_from_usb();
    }
    if (err == ESP_OK) {
        err = cerberus_storage_mount();
    }
    if (err == ESP_OK) {
        err = cerberus_usb_console_start();
    }

    portENTER_CRITICAL(&status_lock);
    mass_storage_status.active = err != ESP_OK;
    mass_storage_status.host_attached = false;
    mass_storage_status.safe_to_release = err == ESP_OK;
    mass_storage_status.last_error = err;
    portEXIT_CRITICAL(&status_lock);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "microSD returned to firmware ownership");
    }
    return err;
}

void cerberus_usb_mass_storage_get_status(cerberus_usb_mass_storage_status_t *status)
{
    if (status == NULL) {
        return;
    }
    portENTER_CRITICAL(&status_lock);
    memcpy(status, &mass_storage_status, sizeof(*status));
    portEXIT_CRITICAL(&status_lock);
}
