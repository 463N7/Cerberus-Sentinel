#include "settings.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SETTINGS_NAMESPACE "cerberus"
#define SETTINGS_BLOB_KEY "config"
#define SENTINEL_STYLE_KEY "sentinel_style"
#define SENTINEL_STYLE_VERSION 2U
#define BLE_FAVORITES_BLOB_KEY "ble_favs"
#define WIFI_TRUSTED_BLOB_KEY "wifi_trust"
#define RESTORE_PENDING_BLOB_KEY "restore_v1"
#define TOUCH_CALIBRATION_BLOB_KEY "touch_cal"
#define SETTINGS_MAGIC 0x43455242UL
#define SETTINGS_VERSION 3U
#define BLE_FAVORITES_MAGIC 0x424c4546UL
#define BLE_FAVORITES_VERSION 1U
#define WIFI_TRUSTED_MAGIC 0x57494654UL
#define WIFI_TRUSTED_VERSION 1U
#define SCREEN_TIMEOUT_OPTION_COUNT 5U
#define SETTINGS_BACKUP_HEADER_SIZE 24U
#define SETTINGS_BACKUP_VERSION 2U
#define TOUCH_CALIBRATION_MAGIC 0x5443414cUL
#define TOUCH_CALIBRATION_VERSION 1U

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t brightness_percent;
    uint8_t screen_timeout_index;
    uint8_t theme_id;
    uint8_t accessibility_size;
    uint8_t privacy_mask_addresses;
    uint8_t reserved;
} settings_record_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t brightness_percent;
    uint8_t screen_timeout_index;
    uint8_t theme_id;
    uint8_t accessibility_size;
    uint8_t reserved[2];
} settings_record_v2_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t brightness_percent;
    uint8_t screen_timeout_index;
} settings_record_v1_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
} touch_calibration_record_t;

typedef struct __attribute__((packed)) {
    uint8_t address[6];
    uint8_t address_type;
    char name[CERBERUS_BLE_FAVORITE_NAME_MAX + 1U];
} ble_favorite_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t reserved;
    ble_favorite_entry_t entries[CERBERUS_BLE_FAVORITE_MAX];
} ble_favorites_record_t;

typedef struct __attribute__((packed)) {
    uint8_t bssid[6];
    uint8_t security;
    char ssid[CERBERUS_WIFI_TRUST_SSID_MAX + 1U];
} wifi_trusted_entry_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    uint8_t reserved;
    wifi_trusted_entry_t entries[CERBERUS_WIFI_TRUST_MAX];
} wifi_trusted_record_t;

typedef struct {
    settings_record_t settings;
    ble_favorites_record_t favorites;
    wifi_trusted_record_t trusted;
} settings_backup_parsed_t;

static const char *TAG = "settings";
static cerberus_settings_t current_settings = {
    .brightness_percent = CERBERUS_SETTINGS_DEFAULT_BRIGHTNESS,
    .screen_timeout_seconds = CERBERUS_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS,
    .theme_id = CERBERUS_THEME_CERBERUS,
    .accessibility_size = 0U,
    .privacy_mask_addresses = false,
};
static nvs_handle_t settings_handle;
static bool settings_ready;
static bool sentinel_style_needs_save;
static ble_favorites_record_t ble_favorites = {
    .magic = BLE_FAVORITES_MAGIC,
    .version = BLE_FAVORITES_VERSION,
};
static wifi_trusted_record_t wifi_trusted = {
    .magic = WIFI_TRUSTED_MAGIC,
    .version = WIFI_TRUSTED_VERSION,
};
static uint8_t restore_pending_buffer[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
static cerberus_touch_calibration_t touch_calibration;

static esp_err_t parse_settings_backup(
    const uint8_t *data,
    size_t data_length,
    settings_backup_parsed_t *parsed,
    cerberus_settings_backup_info_t *info);
static esp_err_t apply_backup_records(const settings_backup_parsed_t *parsed);
static esp_err_t recover_pending_restore(void);

static bool touch_calibration_values_valid(
    uint16_t x_min,
    uint16_t x_max,
    uint16_t y_min,
    uint16_t y_max)
{
    return x_min < x_max && y_min < y_max && x_max - x_min >= 200U && y_max - y_min >= 300U;
}

static esp_err_t load_touch_calibration(void)
{
    touch_calibration_record_t record = {0};
    size_t size = sizeof(record);
    esp_err_t err = nvs_get_blob(
        settings_handle,
        TOUCH_CALIBRATION_BLOB_KEY,
        &record,
        &size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&touch_calibration, 0, sizeof(touch_calibration));
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }
    if (size != sizeof(record) || record.magic != TOUCH_CALIBRATION_MAGIC ||
        record.version != TOUCH_CALIBRATION_VERSION ||
        !touch_calibration_values_valid(
            record.x_min, record.x_max, record.y_min, record.y_max)) {
        return ESP_ERR_INVALID_ARG;
    }
    touch_calibration = (cerberus_touch_calibration_t){
        .valid = true,
        .x_min = record.x_min,
        .x_max = record.x_max,
        .y_min = record.y_min,
        .y_max = record.y_max,
    };
    return ESP_OK;
}

static const uint16_t screen_timeout_options[SCREEN_TIMEOUT_OPTION_COUNT] = {
    0U,
    30U,
    60U,
    120U,
    300U,
};

static int screen_timeout_index_for_seconds(uint16_t seconds)
{
    for (uint8_t index = 0U; index < SCREEN_TIMEOUT_OPTION_COUNT; ++index) {
        if (screen_timeout_options[index] == seconds) {
            return index;
        }
    }
    return -1;
}

static settings_record_t make_record(void)
{
    int timeout_index = screen_timeout_index_for_seconds(current_settings.screen_timeout_seconds);
    settings_record_t record = {
        .magic = SETTINGS_MAGIC,
        .version = SETTINGS_VERSION,
        .brightness_percent = current_settings.brightness_percent,
        .screen_timeout_index = timeout_index >= 0 ? (uint8_t)timeout_index : 0U,
        .theme_id = CERBERUS_THEME_CERBERUS,
        .accessibility_size = current_settings.accessibility_size,
        .privacy_mask_addresses = current_settings.privacy_mask_addresses ? 1U : 0U,
    };
    return record;
}

static bool record_is_valid(const settings_record_t *record)
{
    return record != NULL && record->magic == SETTINGS_MAGIC && record->version == SETTINGS_VERSION &&
        record->brightness_percent >= CERBERUS_SETTINGS_MIN_BRIGHTNESS &&
        record->brightness_percent <= CERBERUS_SETTINGS_MAX_BRIGHTNESS &&
        record->screen_timeout_index < SCREEN_TIMEOUT_OPTION_COUNT &&
        record->theme_id < CERBERUS_SETTINGS_THEME_COUNT &&
        record->accessibility_size <= 1U &&
        record->privacy_mask_addresses <= 1U;
}

static bool record_v2_is_valid(const settings_record_v2_t *record)
{
    return record != NULL && record->magic == SETTINGS_MAGIC && record->version == 2U &&
        record->brightness_percent >= CERBERUS_SETTINGS_MIN_BRIGHTNESS &&
        record->brightness_percent <= CERBERUS_SETTINGS_MAX_BRIGHTNESS &&
        record->screen_timeout_index < SCREEN_TIMEOUT_OPTION_COUNT &&
        record->theme_id < CERBERUS_SETTINGS_THEME_COUNT &&
        record->accessibility_size <= 1U;
}

static bool record_v1_is_valid(const settings_record_v1_t *record)
{
    return record != NULL && record->magic == SETTINGS_MAGIC && record->version == 1U &&
        record->brightness_percent >= CERBERUS_SETTINGS_MIN_BRIGHTNESS &&
        record->brightness_percent <= CERBERUS_SETTINGS_MAX_BRIGHTNESS &&
        record->screen_timeout_index < SCREEN_TIMEOUT_OPTION_COUNT;
}

static esp_err_t save_record(void)
{
    settings_record_t record = make_record();
    esp_err_t err = nvs_set_blob(settings_handle, SETTINGS_BLOB_KEY, &record, sizeof(record));
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(settings_handle);
    if (err == ESP_OK) {
        sentinel_style_needs_save = false;
    }
    return err;
}

static bool favorite_entry_is_valid(const ble_favorite_entry_t *entry)
{
    return entry != NULL && entry->address_type <= 3U &&
        memchr(entry->name, '\0', sizeof(entry->name)) != NULL;
}

static bool favorites_record_is_valid(const ble_favorites_record_t *record)
{
    if (record == NULL || record->magic != BLE_FAVORITES_MAGIC ||
        record->version != BLE_FAVORITES_VERSION || record->count > CERBERUS_BLE_FAVORITE_MAX) {
        return false;
    }
    for (uint8_t index = 0; index < record->count; ++index) {
        if (!favorite_entry_is_valid(&record->entries[index])) {
            return false;
        }
        for (uint8_t prior = 0; prior < index; ++prior) {
            if (record->entries[prior].address_type == record->entries[index].address_type &&
                memcmp(record->entries[prior].address, record->entries[index].address, 6U) == 0) {
                return false;
            }
        }
    }
    return true;
}

static void reset_favorites(void)
{
    memset(&ble_favorites, 0, sizeof(ble_favorites));
    ble_favorites.magic = BLE_FAVORITES_MAGIC;
    ble_favorites.version = BLE_FAVORITES_VERSION;
}

static esp_err_t save_favorites(void)
{
    esp_err_t err = nvs_set_blob(
        settings_handle,
        BLE_FAVORITES_BLOB_KEY,
        &ble_favorites,
        sizeof(ble_favorites));
    return err == ESP_OK ? nvs_commit(settings_handle) : err;
}

static esp_err_t load_favorites(void)
{
    ble_favorites_record_t stored;
    size_t stored_size = sizeof(stored);
    esp_err_t err = nvs_get_blob(
        settings_handle,
        BLE_FAVORITES_BLOB_KEY,
        &stored,
        &stored_size);
    if (err == ESP_OK && stored_size == sizeof(stored) && favorites_record_is_valid(&stored)) {
        ble_favorites = stored;
        ESP_LOGI(TAG, "loaded %u BLE favorites", ble_favorites.count);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
        return err;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored BLE favorites record is invalid");
    }
    reset_favorites();
    return save_favorites();
}

static bool wifi_trusted_entry_is_valid(const wifi_trusted_entry_t *entry)
{
    return entry != NULL && entry->ssid[0] != '\0' && entry->security <= 15U &&
        memchr(entry->ssid, '\0', sizeof(entry->ssid)) != NULL;
}

static bool wifi_trusted_record_is_valid(const wifi_trusted_record_t *record)
{
    if (record == NULL || record->magic != WIFI_TRUSTED_MAGIC ||
        record->version != WIFI_TRUSTED_VERSION || record->count > CERBERUS_WIFI_TRUST_MAX) {
        return false;
    }
    for (uint8_t index = 0U; index < record->count; ++index) {
        if (!wifi_trusted_entry_is_valid(&record->entries[index])) {
            return false;
        }
        for (uint8_t prior = 0U; prior < index; ++prior) {
            if (memcmp(record->entries[prior].bssid, record->entries[index].bssid, 6U) == 0) {
                return false;
            }
        }
    }
    return true;
}

static void reset_wifi_trusted(void)
{
    memset(&wifi_trusted, 0, sizeof(wifi_trusted));
    wifi_trusted.magic = WIFI_TRUSTED_MAGIC;
    wifi_trusted.version = WIFI_TRUSTED_VERSION;
}

static esp_err_t save_wifi_trusted(void)
{
    esp_err_t err = nvs_set_blob(
        settings_handle,
        WIFI_TRUSTED_BLOB_KEY,
        &wifi_trusted,
        sizeof(wifi_trusted));
    return err == ESP_OK ? nvs_commit(settings_handle) : err;
}

static esp_err_t load_wifi_trusted(void)
{
    wifi_trusted_record_t stored;
    size_t stored_size = sizeof(stored);
    esp_err_t err = nvs_get_blob(
        settings_handle,
        WIFI_TRUSTED_BLOB_KEY,
        &stored,
        &stored_size);
    if (err == ESP_OK && stored_size == sizeof(stored) && wifi_trusted_record_is_valid(&stored)) {
        wifi_trusted = stored;
        ESP_LOGI(TAG, "loaded %u trusted Wi-Fi baselines", wifi_trusted.count);
        return ESP_OK;
    }
    if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
        return err;
    }
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "stored Wi-Fi baseline record is invalid");
    }
    reset_wifi_trusted();
    return save_wifi_trusted();
}

esp_err_t cerberus_settings_init(void)
{
    if (settings_ready) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS requires recovery: %s", esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &settings_handle);
    if (err != ESP_OK) {
        return err;
    }
    settings_ready = true;

    err = recover_pending_restore();
    if (err != ESP_OK) {
        settings_ready = false;
        nvs_close(settings_handle);
        return err;
    }

    settings_record_t record = {0};
    size_t record_size = sizeof(record);
    err = nvs_get_blob(settings_handle, SETTINGS_BLOB_KEY, &record, &record_size);
    if (err == ESP_OK && record_size == sizeof(record) && record_is_valid(&record)) {
        current_settings.brightness_percent = record.brightness_percent;
        current_settings.screen_timeout_seconds = screen_timeout_options[record.screen_timeout_index];
        current_settings.theme_id = CERBERUS_THEME_CERBERUS;
        sentinel_style_needs_save = record.theme_id != CERBERUS_THEME_CERBERUS;
        current_settings.accessibility_size = record.accessibility_size;
        current_settings.privacy_mask_addresses = record.privacy_mask_addresses != 0U;
        ESP_LOGI(
            TAG,
            "loaded brightness=%u%% screen_timeout=%us theme=%u",
            current_settings.brightness_percent,
            current_settings.screen_timeout_seconds,
            current_settings.theme_id);
    } else if (err == ESP_OK && record_size == sizeof(settings_record_v2_t) &&
        record_v2_is_valid((const settings_record_v2_t *)&record)) {
        const settings_record_v2_t *old_record = (const settings_record_v2_t *)&record;
        current_settings.brightness_percent = old_record->brightness_percent;
        current_settings.screen_timeout_seconds =
            screen_timeout_options[old_record->screen_timeout_index];
        current_settings.theme_id = CERBERUS_THEME_CERBERUS;
        current_settings.accessibility_size = old_record->accessibility_size;
        current_settings.privacy_mask_addresses = false;
        err = save_record();
        if (err != ESP_OK) {
            settings_ready = false;
            nvs_close(settings_handle);
            return err;
        }
        ESP_LOGI(TAG, "migrated version 2 settings with full address display");
    } else if (err == ESP_OK && record_size == sizeof(settings_record_v1_t) &&
        record_v1_is_valid((const settings_record_v1_t *)&record)) {
        const settings_record_v1_t *old_record = (const settings_record_v1_t *)&record;
        current_settings.brightness_percent = old_record->brightness_percent;
        current_settings.screen_timeout_seconds =
            screen_timeout_options[old_record->screen_timeout_index];
        current_settings.theme_id = CERBERUS_THEME_CERBERUS;
        current_settings.accessibility_size = 0U;
        current_settings.privacy_mask_addresses = false;
        err = save_record();
        if (err != ESP_OK) {
            settings_ready = false;
            nvs_close(settings_handle);
            return err;
        }
        ESP_LOGI(TAG, "migrated version 1 settings with Cerberus theme");
    } else {
        if (err != ESP_ERR_NVS_NOT_FOUND && err != ESP_OK) {
            ESP_LOGW(TAG, "stored settings unreadable: %s", esp_err_to_name(err));
        } else if (err == ESP_OK) {
            ESP_LOGW(TAG, "stored settings record is invalid");
        }

        current_settings.brightness_percent = CERBERUS_SETTINGS_DEFAULT_BRIGHTNESS;
        current_settings.screen_timeout_seconds = CERBERUS_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS;
        current_settings.theme_id = CERBERUS_THEME_CERBERUS;
        current_settings.accessibility_size = 0U;
        current_settings.privacy_mask_addresses = false;
        err = save_record();
        if (err != ESP_OK) {
            settings_ready = false;
            nvs_close(settings_handle);
            return err;
        }
        ESP_LOGI(TAG, "created default settings");
    }

    err = load_favorites();
    if (err == ESP_OK) {
        err = load_wifi_trusted();
    }
    if (err == ESP_OK) {
        err = load_touch_calibration();
    }
    if (err == ESP_OK) {
        return ESP_OK;
    }
    settings_ready = false;
    nvs_close(settings_handle);
    return err;
}

const cerberus_settings_t *cerberus_settings_get(void)
{
    return &current_settings;
}

esp_err_t cerberus_settings_set_brightness(uint8_t percent)
{
    if (percent < CERBERUS_SETTINGS_MIN_BRIGHTNESS || percent > CERBERUS_SETTINGS_MAX_BRIGHTNESS) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current_settings.brightness_percent == percent) {
        return ESP_OK;
    }

    uint8_t previous = current_settings.brightness_percent;
    current_settings.brightness_percent = percent;
    esp_err_t err = save_record();
    if (err != ESP_OK) {
        current_settings.brightness_percent = previous;
        return err;
    }
    ESP_LOGI(TAG, "saved brightness=%u%%", percent);
    return ESP_OK;
}

esp_err_t cerberus_settings_set_screen_timeout(uint16_t seconds)
{
    if (screen_timeout_index_for_seconds(seconds) < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current_settings.screen_timeout_seconds == seconds) {
        return ESP_OK;
    }

    uint16_t previous = current_settings.screen_timeout_seconds;
    current_settings.screen_timeout_seconds = seconds;
    esp_err_t err = save_record();
    if (err != ESP_OK) {
        current_settings.screen_timeout_seconds = previous;
        return err;
    }
    ESP_LOGI(TAG, "saved screen timeout=%us", seconds);
    return ESP_OK;
}

esp_err_t cerberus_settings_apply_sentinel_style(void)
{
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    current_settings.theme_id = CERBERUS_THEME_CERBERUS;
    uint8_t applied_version = 0U;
    esp_err_t err = nvs_get_u8(settings_handle, SENTINEL_STYLE_KEY, &applied_version);
    if (err == ESP_OK && applied_version >= SENTINEL_STYLE_VERSION &&
        !sentinel_style_needs_save) {
        return ESP_OK;
    }

    /* This migration changes only the theme. Keep the Sentinel style active
       for this boot even if persistence fails; return the error to the caller
       so it can report that the migration will be retried next time. */
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        return err;
    }

    /* Persist the full, already-loaded config before marking the migration.
       A reset between commits repeats this idempotent theme-only change;
       the marker can never acknowledge a theme that failed to save. */
    err = save_record();
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(settings_handle, SENTINEL_STYLE_KEY, SENTINEL_STYLE_VERSION);
    if (err == ESP_OK) {
        err = nvs_commit(settings_handle);
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "applied Sentinel style migration %u", SENTINEL_STYLE_VERSION);
    }
    return err;
}

esp_err_t cerberus_settings_set_accessibility_size(uint8_t size)
{
    if (size > 1U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current_settings.accessibility_size == size) {
        return ESP_OK;
    }

    uint8_t previous = current_settings.accessibility_size;
    current_settings.accessibility_size = size;
    esp_err_t err = save_record();
    if (err != ESP_OK) {
        current_settings.accessibility_size = previous;
        return err;
    }
    ESP_LOGI(TAG, "saved accessibility size=%u", size);
    return ESP_OK;
}

esp_err_t cerberus_settings_set_privacy_mask_addresses(bool enabled)
{
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (current_settings.privacy_mask_addresses == enabled) {
        return ESP_OK;
    }

    bool previous = current_settings.privacy_mask_addresses;
    current_settings.privacy_mask_addresses = enabled;
    esp_err_t err = save_record();
    if (err != ESP_OK) {
        current_settings.privacy_mask_addresses = previous;
        return err;
    }
    ESP_LOGI(TAG, "saved address privacy=%s", enabled ? "masked" : "full");
    return ESP_OK;
}

const cerberus_touch_calibration_t *cerberus_settings_get_touch_calibration(void)
{
    return &touch_calibration;
}

esp_err_t cerberus_settings_set_touch_calibration(
    const cerberus_touch_calibration_t *calibration)
{
    if (calibration == NULL || !calibration->valid ||
        !touch_calibration_values_valid(
            calibration->x_min,
            calibration->x_max,
            calibration->y_min,
            calibration->y_max)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    touch_calibration_record_t record = {
        .magic = TOUCH_CALIBRATION_MAGIC,
        .version = TOUCH_CALIBRATION_VERSION,
        .x_min = calibration->x_min,
        .x_max = calibration->x_max,
        .y_min = calibration->y_min,
        .y_max = calibration->y_max,
    };
    esp_err_t err = nvs_set_blob(
        settings_handle,
        TOUCH_CALIBRATION_BLOB_KEY,
        &record,
        sizeof(record));
    if (err == ESP_OK) {
        err = nvs_commit(settings_handle);
    }
    if (err == ESP_OK) {
        touch_calibration = *calibration;
    }
    return err;
}

esp_err_t cerberus_settings_clear_touch_calibration(void)
{
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = nvs_erase_key(settings_handle, TOUCH_CALIBRATION_BLOB_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_OK;
    }
    if (err == ESP_OK) {
        err = nvs_commit(settings_handle);
    }
    if (err == ESP_OK) {
        memset(&touch_calibration, 0, sizeof(touch_calibration));
    }
    return err;
}

esp_err_t cerberus_settings_factory_reset(void)
{
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = nvs_erase_all(settings_handle);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_commit(settings_handle);
    if (err != ESP_OK) {
        return err;
    }

    current_settings.brightness_percent = CERBERUS_SETTINGS_DEFAULT_BRIGHTNESS;
    current_settings.screen_timeout_seconds = CERBERUS_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS;
    current_settings.theme_id = CERBERUS_THEME_CERBERUS;
    current_settings.accessibility_size = 0U;
    current_settings.privacy_mask_addresses = false;
    reset_favorites();
    reset_wifi_trusted();
    memset(&touch_calibration, 0, sizeof(touch_calibration));
    ESP_LOGW(TAG, "Cerberus settings namespace erased; defaults restored in memory");
    return ESP_OK;
}

static void backup_write_le16(uint8_t *destination, uint16_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
}

static void backup_write_le32(uint8_t *destination, uint32_t value)
{
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static uint32_t backup_crc32(const uint8_t *data, size_t data_length)
{
    uint32_t crc = UINT32_MAX;
    for (size_t index = 0U; index < data_length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U) ^ ((crc & 1U) != 0U ? 0xEDB88320UL : 0U);
        }
    }
    return crc ^ UINT32_MAX;
}

static bool backup_append(
    uint8_t *data,
    size_t data_capacity,
    size_t *offset,
    const void *value,
    size_t value_length)
{
    if (*offset > data_capacity || value_length > data_capacity - *offset) {
        return false;
    }
    memcpy(&data[*offset], value, value_length);
    *offset += value_length;
    return true;
}

esp_err_t cerberus_settings_export_backup(
    uint8_t *data,
    size_t data_capacity,
    size_t *data_length)
{
    if (data == NULL || data_length == NULL || data_capacity < SETTINGS_BACKUP_HEADER_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    *data_length = 0U;
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    memset(data, 0, SETTINGS_BACKUP_HEADER_SIZE);
    data[0] = 'C';
    data[1] = 'B';
    data[2] = 'K';
    data[3] = '1';
    backup_write_le16(&data[4], SETTINGS_BACKUP_VERSION);
    backup_write_le16(&data[6], SETTINGS_BACKUP_HEADER_SIZE);
    data[16] = current_settings.brightness_percent;
    data[17] = CERBERUS_THEME_CERBERUS;
    backup_write_le16(&data[18], current_settings.screen_timeout_seconds);
    data[20] = ble_favorites.count;
    data[21] = wifi_trusted.count;
    data[22] = current_settings.accessibility_size;
    data[23] = current_settings.privacy_mask_addresses ? 1U : 0U;

    size_t offset = SETTINGS_BACKUP_HEADER_SIZE;
    for (uint8_t index = 0U; index < ble_favorites.count; ++index) {
        const ble_favorite_entry_t *entry = &ble_favorites.entries[index];
        uint8_t name_length = (uint8_t)strnlen(entry->name, CERBERUS_BLE_FAVORITE_NAME_MAX);
        if (!backup_append(data, data_capacity, &offset, entry->address, sizeof(entry->address)) ||
            !backup_append(data, data_capacity, &offset, &entry->address_type, 1U) ||
            !backup_append(data, data_capacity, &offset, &name_length, 1U) ||
            !backup_append(data, data_capacity, &offset, entry->name, name_length)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }
    for (uint8_t index = 0U; index < wifi_trusted.count; ++index) {
        const wifi_trusted_entry_t *entry = &wifi_trusted.entries[index];
        uint8_t ssid_length = (uint8_t)strnlen(entry->ssid, CERBERUS_WIFI_TRUST_SSID_MAX);
        if (!backup_append(data, data_capacity, &offset, entry->bssid, sizeof(entry->bssid)) ||
            !backup_append(data, data_capacity, &offset, &entry->security, 1U) ||
            !backup_append(data, data_capacity, &offset, &ssid_length, 1U) ||
            !backup_append(data, data_capacity, &offset, entry->ssid, ssid_length)) {
            return ESP_ERR_INVALID_SIZE;
        }
    }

    backup_write_le32(&data[8], (uint32_t)offset);
    backup_write_le32(
        &data[12],
        backup_crc32(&data[16], offset - 16U));
    *data_length = offset;
    ESP_LOGI(
        TAG,
        "exported settings backup: %u bytes, %u BLE favorites, %u Wi-Fi baselines",
        (unsigned)offset,
        ble_favorites.count,
        wifi_trusted.count);
    return ESP_OK;
}

static uint16_t backup_read_le16(const uint8_t *source)
{
    return (uint16_t)source[0] | ((uint16_t)source[1] << 8U);
}

static uint32_t backup_read_le32(const uint8_t *source)
{
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) |
        ((uint32_t)source[2] << 16U) | ((uint32_t)source[3] << 24U);
}

static bool backup_take(
    const uint8_t *data,
    size_t data_length,
    size_t *offset,
    void *value,
    size_t value_length)
{
    if (*offset > data_length || value_length > data_length - *offset) {
        return false;
    }
    memcpy(value, &data[*offset], value_length);
    *offset += value_length;
    return true;
}

static esp_err_t parse_settings_backup(
    const uint8_t *data,
    size_t data_length,
    settings_backup_parsed_t *parsed,
    cerberus_settings_backup_info_t *info)
{
    if (data == NULL || parsed == NULL || data_length < SETTINGS_BACKUP_HEADER_SIZE ||
        data_length > CERBERUS_SETTINGS_BACKUP_MAX_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }
    uint16_t backup_version = backup_read_le16(&data[4]);
    if (memcmp(data, "CBK1", 4U) != 0 ||
        (backup_version != 1U && backup_version != SETTINGS_BACKUP_VERSION) ||
        backup_read_le16(&data[6]) != SETTINGS_BACKUP_HEADER_SIZE) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (backup_read_le32(&data[8]) != data_length) {
        return ESP_ERR_INVALID_SIZE;
    }
    uint32_t expected_crc = backup_read_le32(&data[12]);
    uint32_t actual_crc = backup_crc32(&data[16], data_length - 16U);
    if (expected_crc != actual_crc) {
        return ESP_ERR_INVALID_CRC;
    }

    uint8_t brightness = data[16];
    uint8_t theme_id = data[17];
    uint16_t timeout_seconds = backup_read_le16(&data[18]);
    uint8_t favorite_count = data[20];
    uint8_t trusted_count = data[21];
    uint8_t accessibility_size = data[22];
    uint8_t privacy_mask_addresses = backup_version >= 2U ? data[23] : 0U;
    int timeout_index = screen_timeout_index_for_seconds(timeout_seconds);
    if (brightness < CERBERUS_SETTINGS_MIN_BRIGHTNESS ||
        brightness > CERBERUS_SETTINGS_MAX_BRIGHTNESS || timeout_index < 0 ||
        theme_id >= CERBERUS_SETTINGS_THEME_COUNT ||
        accessibility_size > 1U || privacy_mask_addresses > 1U ||
        favorite_count > CERBERUS_BLE_FAVORITE_MAX || trusted_count > CERBERUS_WIFI_TRUST_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(parsed, 0, sizeof(*parsed));
    parsed->settings.magic = SETTINGS_MAGIC;
    parsed->settings.version = SETTINGS_VERSION;
    parsed->settings.brightness_percent = brightness;
    parsed->settings.screen_timeout_index = (uint8_t)timeout_index;
    /* Accept all old theme IDs, but never restore a selectable palette. */
    parsed->settings.theme_id = CERBERUS_THEME_CERBERUS;
    parsed->settings.accessibility_size = accessibility_size;
    parsed->settings.privacy_mask_addresses = privacy_mask_addresses;
    parsed->favorites.magic = BLE_FAVORITES_MAGIC;
    parsed->favorites.version = BLE_FAVORITES_VERSION;
    parsed->favorites.count = favorite_count;
    parsed->trusted.magic = WIFI_TRUSTED_MAGIC;
    parsed->trusted.version = WIFI_TRUSTED_VERSION;
    parsed->trusted.count = trusted_count;

    size_t offset = SETTINGS_BACKUP_HEADER_SIZE;
    for (uint8_t index = 0U; index < favorite_count; ++index) {
        ble_favorite_entry_t *entry = &parsed->favorites.entries[index];
        uint8_t name_length = 0U;
        if (!backup_take(data, data_length, &offset, entry->address, sizeof(entry->address)) ||
            !backup_take(data, data_length, &offset, &entry->address_type, 1U) ||
            !backup_take(data, data_length, &offset, &name_length, 1U) ||
            name_length > CERBERUS_BLE_FAVORITE_NAME_MAX ||
            !backup_take(data, data_length, &offset, entry->name, name_length)) {
            return ESP_ERR_INVALID_SIZE;
        }
        entry->name[name_length] = '\0';
    }
    for (uint8_t index = 0U; index < trusted_count; ++index) {
        wifi_trusted_entry_t *entry = &parsed->trusted.entries[index];
        uint8_t ssid_length = 0U;
        if (!backup_take(data, data_length, &offset, entry->bssid, sizeof(entry->bssid)) ||
            !backup_take(data, data_length, &offset, &entry->security, 1U) ||
            !backup_take(data, data_length, &offset, &ssid_length, 1U) ||
            ssid_length == 0U || ssid_length > CERBERUS_WIFI_TRUST_SSID_MAX ||
            !backup_take(data, data_length, &offset, entry->ssid, ssid_length)) {
            return ESP_ERR_INVALID_SIZE;
        }
        entry->ssid[ssid_length] = '\0';
    }
    if (offset != data_length || !record_is_valid(&parsed->settings) ||
        !favorites_record_is_valid(&parsed->favorites) ||
        !wifi_trusted_record_is_valid(&parsed->trusted)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (info != NULL) {
        info->brightness_percent = brightness;
        info->screen_timeout_seconds = timeout_seconds;
        info->theme_id = CERBERUS_THEME_CERBERUS;
        info->accessibility_size = accessibility_size;
        info->ble_favorite_count = favorite_count;
        info->wifi_trusted_count = trusted_count;
        info->privacy_mask_addresses = privacy_mask_addresses != 0U;
    }
    return ESP_OK;
}

static esp_err_t apply_backup_records(const settings_backup_parsed_t *parsed)
{
    esp_err_t err = nvs_set_blob(
        settings_handle,
        SETTINGS_BLOB_KEY,
        &parsed->settings,
        sizeof(parsed->settings));
    if (err == ESP_OK) {
        err = nvs_set_blob(
            settings_handle,
            BLE_FAVORITES_BLOB_KEY,
            &parsed->favorites,
            sizeof(parsed->favorites));
    }
    if (err == ESP_OK) {
        err = nvs_set_blob(
            settings_handle,
            WIFI_TRUSTED_BLOB_KEY,
            &parsed->trusted,
            sizeof(parsed->trusted));
    }
    if (err == ESP_OK) {
        err = nvs_commit(settings_handle);
    }
    if (err != ESP_OK) {
        return err;
    }

    current_settings.brightness_percent = parsed->settings.brightness_percent;
    current_settings.screen_timeout_seconds =
        screen_timeout_options[parsed->settings.screen_timeout_index];
    current_settings.theme_id = CERBERUS_THEME_CERBERUS;
    sentinel_style_needs_save = false;
    current_settings.accessibility_size = parsed->settings.accessibility_size;
    current_settings.privacy_mask_addresses =
        parsed->settings.privacy_mask_addresses != 0U;
    ble_favorites = parsed->favorites;
    wifi_trusted = parsed->trusted;
    return ESP_OK;
}

static esp_err_t recover_pending_restore(void)
{
    size_t pending_length = sizeof(restore_pending_buffer);
    esp_err_t err = nvs_get_blob(
        settings_handle,
        RESTORE_PENDING_BLOB_KEY,
        restore_pending_buffer,
        &pending_length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    settings_backup_parsed_t parsed;
    err = parse_settings_backup(restore_pending_buffer, pending_length, &parsed, NULL);
    if (err == ESP_OK) {
        ESP_LOGW(TAG, "completing interrupted configuration restore");
        err = apply_backup_records(&parsed);
    } else {
        ESP_LOGE(TAG, "discarding invalid pending restore: %s", esp_err_to_name(err));
    }
    memset(restore_pending_buffer, 0, sizeof(restore_pending_buffer));
    if (err != ESP_OK && err != ESP_ERR_INVALID_ARG && err != ESP_ERR_INVALID_SIZE &&
        err != ESP_ERR_INVALID_CRC && err != ESP_ERR_NOT_SUPPORTED) {
        return err;
    }

    esp_err_t erase_err = nvs_erase_key(settings_handle, RESTORE_PENDING_BLOB_KEY);
    if (erase_err == ESP_OK || erase_err == ESP_ERR_NVS_NOT_FOUND) {
        erase_err = nvs_commit(settings_handle);
    }
    return erase_err;
}

esp_err_t cerberus_settings_validate_backup(
    const uint8_t *data,
    size_t data_length,
    cerberus_settings_backup_info_t *info)
{
    settings_backup_parsed_t parsed;
    return parse_settings_backup(data, data_length, &parsed, info);
}

esp_err_t cerberus_settings_import_backup(const uint8_t *data, size_t data_length)
{
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    settings_backup_parsed_t parsed;
    esp_err_t err = parse_settings_backup(data, data_length, &parsed, NULL);
    if (err != ESP_OK) {
        return err;
    }

    err = nvs_set_blob(settings_handle, RESTORE_PENDING_BLOB_KEY, data, data_length);
    if (err == ESP_OK) {
        err = nvs_commit(settings_handle);
    }
    if (err != ESP_OK) {
        return err;
    }

    err = apply_backup_records(&parsed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "restore staged but apply failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_err_t erase_err = nvs_erase_key(settings_handle, RESTORE_PENDING_BLOB_KEY);
    if (erase_err == ESP_OK || erase_err == ESP_ERR_NVS_NOT_FOUND) {
        erase_err = nvs_commit(settings_handle);
    }
    if (erase_err != ESP_OK) {
        ESP_LOGW(TAG, "restore applied; pending marker cleanup deferred: %s", esp_err_to_name(erase_err));
    }
    ESP_LOGW(TAG, "configuration restored from validated backup");
    return ESP_OK;
}

size_t cerberus_settings_ble_favorite_count(void)
{
    return settings_ready ? ble_favorites.count : 0U;
}

static int find_ble_favorite(const uint8_t address[6], uint8_t address_type)
{
    if (address == NULL) {
        return -1;
    }
    for (uint8_t index = 0; index < ble_favorites.count; ++index) {
        if (ble_favorites.entries[index].address_type == address_type &&
            memcmp(ble_favorites.entries[index].address, address, 6U) == 0) {
            return index;
        }
    }
    return -1;
}

bool cerberus_settings_ble_is_favorite(const uint8_t address[6], uint8_t address_type)
{
    return settings_ready && find_ble_favorite(address, address_type) >= 0;
}

esp_err_t cerberus_settings_toggle_ble_favorite(
    const uint8_t address[6],
    uint8_t address_type,
    const char *name,
    bool *is_favorite)
{
    if (address == NULL || address_type > 3U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    ble_favorites_record_t previous = ble_favorites;
    int existing = find_ble_favorite(address, address_type);
    bool new_state = existing < 0;
    if (existing >= 0) {
        size_t remaining = (size_t)ble_favorites.count - (size_t)existing - 1U;
        if (remaining > 0U) {
            memmove(
                &ble_favorites.entries[existing],
                &ble_favorites.entries[existing + 1],
                remaining * sizeof(ble_favorites.entries[0]));
        }
        --ble_favorites.count;
        memset(&ble_favorites.entries[ble_favorites.count], 0, sizeof(ble_favorites.entries[0]));
    } else {
        if (ble_favorites.count >= CERBERUS_BLE_FAVORITE_MAX) {
            return ESP_ERR_NO_MEM;
        }
        ble_favorite_entry_t *entry = &ble_favorites.entries[ble_favorites.count++];
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->address, address, sizeof(entry->address));
        entry->address_type = address_type;
        if (name != NULL) {
            size_t name_length = strnlen(name, CERBERUS_BLE_FAVORITE_NAME_MAX);
            memcpy(entry->name, name, name_length);
            entry->name[name_length] = '\0';
        }
    }

    esp_err_t err = save_favorites();
    if (err != ESP_OK) {
        ble_favorites = previous;
        return err;
    }
    if (is_favorite != NULL) {
        *is_favorite = new_state;
    }
    ESP_LOGI(
        TAG,
        "BLE favorite %s; count=%u",
        new_state ? "added" : "removed",
        ble_favorites.count);
    return ESP_OK;
}

size_t cerberus_settings_wifi_trusted_count(void)
{
    return settings_ready ? wifi_trusted.count : 0U;
}

static int find_wifi_trusted_bssid(const uint8_t bssid[6])
{
    if (bssid == NULL) {
        return -1;
    }
    for (uint8_t index = 0U; index < wifi_trusted.count; ++index) {
        if (memcmp(wifi_trusted.entries[index].bssid, bssid, 6U) == 0) {
            return index;
        }
    }
    return -1;
}

cerberus_wifi_trust_state_t cerberus_settings_wifi_trust_state(
    const char *ssid,
    const uint8_t bssid[6],
    uint8_t security)
{
    if (!settings_ready || ssid == NULL || ssid[0] == '\0' || bssid == NULL) {
        return CERBERUS_WIFI_TRUST_NONE;
    }

    bool related_baseline = false;
    for (uint8_t index = 0U; index < wifi_trusted.count; ++index) {
        const wifi_trusted_entry_t *entry = &wifi_trusted.entries[index];
        bool bssid_matches = memcmp(entry->bssid, bssid, 6U) == 0;
        bool ssid_matches = strcmp(entry->ssid, ssid) == 0;
        if (bssid_matches && ssid_matches && entry->security == security) {
            return CERBERUS_WIFI_TRUST_EXACT;
        }
        if (bssid_matches || ssid_matches) {
            related_baseline = true;
        }
    }
    return related_baseline ? CERBERUS_WIFI_TRUST_MISMATCH : CERBERUS_WIFI_TRUST_NONE;
}

esp_err_t cerberus_settings_toggle_wifi_trusted(
    const char *ssid,
    const uint8_t bssid[6],
    uint8_t security,
    bool *is_trusted)
{
    if (ssid == NULL || ssid[0] == '\0' ||
        strnlen(ssid, CERBERUS_WIFI_TRUST_SSID_MAX + 1U) > CERBERUS_WIFI_TRUST_SSID_MAX ||
        bssid == NULL || security > 15U) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!settings_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_trusted_record_t previous = wifi_trusted;
    int existing = find_wifi_trusted_bssid(bssid);
    bool exact = existing >= 0 &&
        strcmp(wifi_trusted.entries[existing].ssid, ssid) == 0 &&
        wifi_trusted.entries[existing].security == security;
    bool new_state = !exact;
    if (exact) {
        size_t remaining = (size_t)wifi_trusted.count - (size_t)existing - 1U;
        if (remaining > 0U) {
            memmove(
                &wifi_trusted.entries[existing],
                &wifi_trusted.entries[existing + 1],
                remaining * sizeof(wifi_trusted.entries[0]));
        }
        --wifi_trusted.count;
        memset(&wifi_trusted.entries[wifi_trusted.count], 0, sizeof(wifi_trusted.entries[0]));
    } else {
        wifi_trusted_entry_t *entry;
        if (existing >= 0) {
            entry = &wifi_trusted.entries[existing];
        } else {
            if (wifi_trusted.count >= CERBERUS_WIFI_TRUST_MAX) {
                return ESP_ERR_NO_MEM;
            }
            entry = &wifi_trusted.entries[wifi_trusted.count++];
        }
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->bssid, bssid, sizeof(entry->bssid));
        entry->security = security;
        size_t ssid_length = strnlen(ssid, CERBERUS_WIFI_TRUST_SSID_MAX);
        memcpy(entry->ssid, ssid, ssid_length);
        entry->ssid[ssid_length] = '\0';
    }

    esp_err_t err = save_wifi_trusted();
    if (err != ESP_OK) {
        wifi_trusted = previous;
        return err;
    }
    if (is_trusted != NULL) {
        *is_trusted = new_state;
    }
    ESP_LOGI(
        TAG,
        "Wi-Fi baseline %s; count=%u",
        new_state ? "saved" : "removed",
        wifi_trusted.count);
    return ESP_OK;
}
