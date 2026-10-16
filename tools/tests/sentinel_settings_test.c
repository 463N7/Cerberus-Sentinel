#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Include the implementation unchanged so a fixture can model a reboot and
   write the exact historical record layouts without production test hooks. */
#include "../../s3_main/main/settings.c"

typedef struct {
    char key[20];
    size_t size;
    uint8_t value[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
} slot_t;
static slot_t durable[8], staged[8];
static const char *fail_set_key;
static bool fail_marker_read;
static bool fail_commit;
static unsigned commits;
static unsigned full_erases;
static const uint8_t favorite_address[6] = {1, 2, 3, 4, 5, 6};
static const uint8_t trusted_bssid[6] = {6, 5, 4, 3, 2, 1};

static slot_t *find_slot(slot_t *slots, const char *key, bool create)
{
    for (size_t i = 0; i < 8; ++i) {
        if (strcmp(slots[i].key, key) == 0) return &slots[i];
    }
    if (create) {
        for (size_t i = 0; i < 8; ++i) {
            if (slots[i].key[0] == '\0') {
                assert(strlen(key) < sizeof(slots[i].key));
                strcpy(slots[i].key, key);
                return &slots[i];
            }
        }
    }
    return NULL;
}

static void seed(const char *key, const void *value, size_t size)
{
    slot_t *slot = find_slot(durable, key, true);
    assert(slot != NULL && size <= sizeof(slot->value));
    memcpy(slot->value, value, size);
    slot->size = size;
    memcpy(staged, durable, sizeof(staged));
}

const char *esp_err_to_name(esp_err_t error) { (void)error; return "test error"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_erase(void) { ++full_erases; memset(durable, 0, sizeof(durable)); return ESP_OK; }
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle)
{
    assert(strcmp(name, "cerberus") == 0 && mode == NVS_READWRITE);
    memcpy(staged, durable, sizeof(staged));
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); memcpy(staged, durable, sizeof(staged)); }
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *data, size_t *length)
{
    assert(handle == 1);
    slot_t *slot = find_slot(staged, key, false);
    if (slot == NULL) return ESP_ERR_NVS_NOT_FOUND;
    if (data != NULL && *length < slot->size) return ESP_ERR_INVALID_SIZE;
    if (data != NULL) memcpy(data, slot->value, slot->size);
    *length = slot->size;
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *data, size_t length)
{
    assert(handle == 1);
    if (fail_set_key != NULL && strcmp(fail_set_key, key) == 0) return ESP_FAIL;
    slot_t *slot = find_slot(staged, key, true);
    assert(slot != NULL && length <= sizeof(slot->value));
    memcpy(slot->value, data, length);
    slot->size = length;
    return ESP_OK;
}
esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *value)
{
    if (fail_marker_read) return ESP_FAIL;
    size_t size = 1;
    return nvs_get_blob(handle, key, value, &size);
}
esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{ return nvs_set_blob(handle, key, &value, 1); }
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    ++commits;
    if (fail_commit) return ESP_FAIL;
    memcpy(durable, staged, sizeof(durable));
    return ESP_OK;
}
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    assert(handle == 1);
    slot_t *slot = find_slot(staged, key, false);
    if (slot == NULL) return ESP_ERR_NVS_NOT_FOUND;
    memset(slot, 0, sizeof(*slot));
    return ESP_OK;
}
esp_err_t nvs_erase_all(nvs_handle_t handle)
{ assert(handle == 1); ++full_erases; memset(staged, 0, sizeof(staged)); return ESP_OK; }

static void reboot(void)
{
    settings_ready = false;
    sentinel_style_needs_save = false;
    current_settings = (cerberus_settings_t){
        .brightness_percent = CERBERUS_SETTINGS_DEFAULT_BRIGHTNESS,
        .theme_id = CERBERUS_THEME_CERBERUS,
    };
    reset_favorites();
    reset_wifi_trusted();
    memset(&touch_calibration, 0, sizeof(touch_calibration));
    memset(restore_pending_buffer, 0, sizeof(restore_pending_buffer));
    memcpy(staged, durable, sizeof(staged));
}

static void seed_record(uint16_t version, uint8_t theme, uint8_t marker)
{
    memset(durable, 0, sizeof(durable));
    memset(staged, 0, sizeof(staged));
    commits = full_erases = 0;
    fail_set_key = NULL;
    fail_marker_read = fail_commit = false;
    reboot();
    settings_record_t record = {
        .magic = SETTINGS_MAGIC, .version = version, .brightness_percent = 38,
        .screen_timeout_index = 3, .theme_id = theme, .accessibility_size = 1,
        .privacy_mask_addresses = 1,
    };
    seed(SETTINGS_BLOB_KEY, &record, version == 1 ? sizeof(settings_record_v1_t) : sizeof(record));
    seed(SENTINEL_STYLE_KEY, &marker, 1);
    reset_favorites();
    ble_favorites.count = 1;
    memcpy(ble_favorites.entries[0].address, favorite_address, 6);
    ble_favorites.entries[0].address_type = 1;
    strcpy(ble_favorites.entries[0].name, "Preserved favorite");
    seed(BLE_FAVORITES_BLOB_KEY, &ble_favorites, sizeof(ble_favorites));
    reset_wifi_trusted();
    wifi_trusted.count = 1;
    memcpy(wifi_trusted.entries[0].bssid, trusted_bssid, 6);
    wifi_trusted.entries[0].security = 3;
    strcpy(wifi_trusted.entries[0].ssid, "Preserved network");
    seed(WIFI_TRUSTED_BLOB_KEY, &wifi_trusted, sizeof(wifi_trusted));
    touch_calibration_record_t calibration = {
        .magic = TOUCH_CALIBRATION_MAGIC, .version = TOUCH_CALIBRATION_VERSION,
        .x_min = 100, .x_max = 3800, .y_min = 200, .y_max = 3700,
    };
    seed(TOUCH_CALIBRATION_BLOB_KEY, &calibration, sizeof(calibration));
    reboot();
}

static void assert_preserved(bool privacy, uint8_t text_size)
{
    const cerberus_settings_t *settings = cerberus_settings_get();
    assert(settings->theme_id == CERBERUS_THEME_CERBERUS);
    assert(settings->brightness_percent == 38 && settings->screen_timeout_seconds == 120);
    assert(settings->accessibility_size == text_size && settings->privacy_mask_addresses == privacy);
    assert(cerberus_settings_ble_favorite_count() == 1);
    assert(cerberus_settings_ble_is_favorite(favorite_address, 1));
    assert(strcmp(ble_favorites.entries[0].name, "Preserved favorite") == 0);
    assert(cerberus_settings_wifi_trusted_count() == 1);
    assert(cerberus_settings_wifi_trust_state("Preserved network", trusted_bssid, 3) == CERBERUS_WIFI_TRUST_EXACT);
    const cerberus_touch_calibration_t *cal = cerberus_settings_get_touch_calibration();
    assert(cal->valid && cal->x_min == 100 && cal->x_max == 3800 && cal->y_min == 200 && cal->y_max == 3700);
    assert(full_erases == 0);
}

static void assert_saved_style(void)
{
    const slot_t *slot = find_slot(durable, SETTINGS_BLOB_KEY, false);
    settings_record_t record;
    assert(slot != NULL && slot->size == sizeof(record));
    memcpy(&record, slot->value, sizeof(record));
    assert(record.theme_id == CERBERUS_THEME_CERBERUS && record.version == SETTINGS_VERSION);
    assert(record.brightness_percent == 38 && record.screen_timeout_index == 3);
}

static void migration_tests(void)
{
    for (uint16_t version = 1; version <= 3; ++version) {
        for (uint8_t theme = 0; theme < 8; ++theme) {
            for (uint8_t marker = 1; marker <= 2; ++marker) {
                seed_record(version, theme, marker);
                assert(cerberus_settings_init() == ESP_OK);
                assert_preserved(version >= 3, version >= 2 ? 1 : 0);
                assert(cerberus_settings_apply_sentinel_style() == ESP_OK);
                assert_saved_style();
                unsigned before = commits;
                assert(cerberus_settings_apply_sentinel_style() == ESP_OK && commits == before);
                reboot();
                assert(cerberus_settings_init() == ESP_OK);
                assert_preserved(version >= 3, version >= 2 ? 1 : 0);
                assert(cerberus_settings_apply_sentinel_style() == ESP_OK && commits == before);
            }
        }
    }
}

static void backup_tests(void)
{
    uint8_t original[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
    size_t length = 0;
    seed_record(3, 0, 2);
    assert(cerberus_settings_init() == ESP_OK);
    assert(cerberus_settings_export_backup(original, sizeof(original), &length) == ESP_OK);
    for (uint16_t version = 1; version <= 2; ++version) {
        for (uint8_t theme = 0; theme < 8; ++theme) {
            uint8_t backup[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
            memcpy(backup, original, length);
            backup_write_le16(&backup[4], version);
            backup[17] = theme;
            backup_write_le32(&backup[12], backup_crc32(&backup[16], length - 16));
            cerberus_settings_backup_info_t info;
            assert(cerberus_settings_validate_backup(backup, length, &info) == ESP_OK);
            assert(info.theme_id == CERBERUS_THEME_CERBERUS && info.brightness_percent == 38);
            assert(info.accessibility_size == 1 && info.screen_timeout_seconds == 120);
            assert(info.ble_favorite_count == 1 && info.wifi_trusted_count == 1);
            assert(info.privacy_mask_addresses == (version >= 2));
            assert(cerberus_settings_set_brightness(80) == ESP_OK);
            assert(cerberus_settings_import_backup(backup, length) == ESP_OK);
            assert_preserved(version >= 2, 1);
            assert_saved_style();
            uint8_t exported[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
            size_t exported_length = 0;
            assert(cerberus_settings_export_backup(exported, sizeof(exported), &exported_length) == ESP_OK);
            assert(exported[17] == CERBERUS_THEME_CERBERUS && exported_length == length);
            /* A restore interrupted after its journal commit is replayed at boot. */
            assert(cerberus_settings_set_brightness(80) == ESP_OK);
            seed(RESTORE_PENDING_BLOB_KEY, backup, length);
            reboot();
            assert(cerberus_settings_init() == ESP_OK);
            assert_preserved(version >= 2, 1);
            assert_saved_style();
            assert(find_slot(durable, RESTORE_PENDING_BLOB_KEY, false) == NULL);
        }
    }
    original[17] = 8;
    backup_write_le32(&original[12], backup_crc32(&original[16], length - 16));
    assert(cerberus_settings_import_backup(original, length) == ESP_ERR_INVALID_ARG);
    assert_preserved(true, 1);
}

static void failure_tests(void)
{
    seed_record(3, 7, 2);
    assert(cerberus_settings_init() == ESP_OK);
    fail_marker_read = true;
    assert(cerberus_settings_apply_sentinel_style() == ESP_FAIL);
    assert_preserved(true, 1);
    fail_marker_read = false;
    fail_set_key = SETTINGS_BLOB_KEY;
    assert(cerberus_settings_apply_sentinel_style() == ESP_FAIL);
    assert_preserved(true, 1);
    fail_set_key = NULL;
    fail_commit = true;
    assert(cerberus_settings_apply_sentinel_style() == ESP_FAIL);
    assert_preserved(true, 1);
    fail_commit = false;
    reboot();
    assert(cerberus_settings_init() == ESP_OK);
    assert(cerberus_settings_apply_sentinel_style() == ESP_OK);
    assert_preserved(true, 1);
    assert_saved_style();

    seed_record(3, 3, 1);
    assert(cerberus_settings_init() == ESP_OK);
    fail_set_key = SENTINEL_STYLE_KEY;
    assert(cerberus_settings_apply_sentinel_style() == ESP_FAIL);
    assert_preserved(true, 1);
    assert_saved_style();
    fail_set_key = NULL;
    reboot();
    assert(cerberus_settings_init() == ESP_OK);
    assert(cerberus_settings_apply_sentinel_style() == ESP_OK);
    assert_preserved(true, 1);
    assert(find_slot(durable, SENTINEL_STYLE_KEY, false)->value[0] == 2);
}

int main(void)
{
    migration_tests();
    backup_tests();
    failure_tests();
    puts("Settings: 48 legacy migrations, 16 backup restores and recovery replays, fixed-palette persistence failures, and unrelated-setting preservation passed.");
    return 0;
}
