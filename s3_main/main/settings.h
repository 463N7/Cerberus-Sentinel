#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CERBERUS_SETTINGS_DEFAULT_BRIGHTNESS 75U
#define CERBERUS_SETTINGS_MIN_BRIGHTNESS 10U
#define CERBERUS_SETTINGS_MAX_BRIGHTNESS 100U
#define CERBERUS_SETTINGS_DEFAULT_SCREEN_TIMEOUT_SECONDS 0U
/* Legacy IDs remain valid when reading existing NVS records and backups. */
#define CERBERUS_SETTINGS_THEME_COUNT 8U
#define CERBERUS_BLE_FAVORITE_MAX 16U
#define CERBERUS_BLE_FAVORITE_NAME_MAX 32U
#define CERBERUS_WIFI_TRUST_MAX 16U
#define CERBERUS_WIFI_TRUST_SSID_MAX 32U
#define CERBERUS_SETTINGS_BACKUP_MAX_BYTES 1536U

typedef enum {
    CERBERUS_WIFI_TRUST_NONE = 0,
    CERBERUS_WIFI_TRUST_EXACT = 1,
    CERBERUS_WIFI_TRUST_MISMATCH = 2,
} cerberus_wifi_trust_state_t;

typedef enum {
    CERBERUS_THEME_CERBERUS = 0,
    CERBERUS_THEME_HIGH_CONTRAST = 1,
    CERBERUS_THEME_AMBER = 2,
    CERBERUS_THEME_HACKER = 3,
    CERBERUS_THEME_CYBERPUNK = 4,
    CERBERUS_THEME_SYNTHWAVE = 5,
    CERBERUS_THEME_ICE = 6,
    CERBERUS_THEME_RED_ALERT = 7,
} cerberus_theme_t;

typedef struct {
    uint8_t brightness_percent;
    uint16_t screen_timeout_seconds;
    uint8_t theme_id;
    uint8_t accessibility_size;
    bool privacy_mask_addresses;
} cerberus_settings_t;

typedef struct {
    uint8_t brightness_percent;
    uint16_t screen_timeout_seconds;
    uint8_t theme_id;
    uint8_t accessibility_size;
    uint8_t ble_favorite_count;
    uint8_t wifi_trusted_count;
    bool privacy_mask_addresses;
} cerberus_settings_backup_info_t;

typedef struct {
    bool valid;
    uint16_t x_min;
    uint16_t x_max;
    uint16_t y_min;
    uint16_t y_max;
} cerberus_touch_calibration_t;

esp_err_t cerberus_settings_init(void);
const cerberus_settings_t *cerberus_settings_get(void);
esp_err_t cerberus_settings_set_brightness(uint8_t percent);
esp_err_t cerberus_settings_set_screen_timeout(uint16_t seconds);
/* Call after settings init to persist the fixed Sentinel palette for older
   records while preserving all other settings. Runtime and restored settings
   always use Sentinel, including when this persistence fails. */
esp_err_t cerberus_settings_apply_sentinel_style(void);
esp_err_t cerberus_settings_set_accessibility_size(uint8_t size);
esp_err_t cerberus_settings_set_privacy_mask_addresses(bool enabled);
const cerberus_touch_calibration_t *cerberus_settings_get_touch_calibration(void);
esp_err_t cerberus_settings_set_touch_calibration(
    const cerberus_touch_calibration_t *calibration);
esp_err_t cerberus_settings_clear_touch_calibration(void);
esp_err_t cerberus_settings_factory_reset(void);
esp_err_t cerberus_settings_export_backup(
    uint8_t *data,
    size_t data_capacity,
    size_t *data_length);
esp_err_t cerberus_settings_validate_backup(
    const uint8_t *data,
    size_t data_length,
    cerberus_settings_backup_info_t *info);
esp_err_t cerberus_settings_import_backup(const uint8_t *data, size_t data_length);
size_t cerberus_settings_ble_favorite_count(void);
bool cerberus_settings_ble_is_favorite(const uint8_t address[6], uint8_t address_type);
esp_err_t cerberus_settings_toggle_ble_favorite(
    const uint8_t address[6],
    uint8_t address_type,
    const char *name,
    bool *is_favorite);
size_t cerberus_settings_wifi_trusted_count(void);
cerberus_wifi_trust_state_t cerberus_settings_wifi_trust_state(
    const char *ssid,
    const uint8_t bssid[6],
    uint8_t security);
esp_err_t cerberus_settings_toggle_wifi_trusted(
    const char *ssid,
    const uint8_t bssid[6],
    uint8_t security,
    bool *is_trusted);

#ifdef __cplusplus
}
#endif
