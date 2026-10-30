#include "ui.h"
#include "field_survey.h"
#include "recon_store.h"
#include "field_survey_view.h"
#include "field_menu.h"
#include "wardrive_service.h"
#include "wifi_target_view.h"
#include "wifi_packet_monitor_core.h"
#include "csi_link.h"
#include "csi_ui.h"
#include "ble_test.h"
#include "s3_ble_attack.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "s3_ble_central.h"
#include "s3_ble_connection_ui.h"
#include "s3_findmy_ui_route.h"
#include "s3_findmy_monitor.h"
#include "s3_flipper_hint_view.h"
#include "s3_meta_hint_view.h"
#include "s3_ibeacon_hint_view.h"
#include "s3_flock_hint_view.h"
#include "s3_hc_hint_view.h"
#include "ble_rssi.h"
#include "sdkconfig.h"
#include "s3_wifi_receiver.h"
#include "s3_portal.h"
#include "wifi_capture_session.h"
#include "s3_wifi_owner.h"
#include "ui_widgets.h"
#include "ui_time_service.h"
#include "battery.h"
#include "sentinel_battery_view.h"
#include "sentinel_dashboard.h"
#include "sentinel_watch.h"
#include "sentinel_watch_view.h"
#include "sentinel_baseline.h"
#include "sentinel_guide.h"
#include "sentinel_hub.h"
#include "sentinel_tools.h"
#include "sentinel_scan_session.h"
#include "sentinel_scan_view.h"

#include "bmp_viewer.h"
#include "c5_spi_link.h"
#include "display.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "event_bus.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "keyboard.h"
#include "privacy.h"
#include "qr_renderer.h"
#include "recovery.h"
#include "script_engine.h"
#include "settings.h"
#include "storage.h"
#include "system_health.h"
#include "touch.h"
#include "usb_mass_storage.h"
#include "web_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#define UI_DEFAULT_MUTED 0x84B5

#define UI_HEADER_HEIGHT 48
#define UI_BACK_Y 420
#define UI_BACK_HEIGHT 44
#define UI_C5_REFRESH_MS 5000
#define UI_STATUS_REFRESH_MS 1000
#define UI_HEALTH_REFRESH_MS 5000
#define UI_WIFI_REFRESH_MS 500
#define UI_BLE_REFRESH_MS 500
#define UI_IEEE802154_REFRESH_MS 250
#define UI_TOUCH_STARTUP_GUARD_MS 800
#define UI_TOUCH_REARM_MS 180
#define UI_FILE_ENTRY_CAPACITY 32
#define UI_FILE_VISIBLE_ROWS 7
#define UI_FILE_LIST_Y 100
#define UI_FILE_ROW_HEIGHT 36
#define UI_FILE_CONTROLS_Y 360
#define UI_TEXT_VIEWER_Y 84
#define UI_SCRIPT_ENTRY_CAPACITY 16U
#define UI_SCRIPT_VISIBLE_ROWS 5U
#define UI_SCRIPT_LIST_Y 82U
#define UI_SCRIPT_ROW_HEIGHT 54U
#define UI_TEXT_VIEWER_COLUMNS 48
#define UI_TEXT_VIEWER_LINES 22
#define UI_TEXT_VIEWER_CHUNK_BYTES (UI_TEXT_VIEWER_COLUMNS * UI_TEXT_VIEWER_LINES)
#define UI_TEXT_VIEWER_HISTORY 64
#define UI_FILE_INFO_HASH_Y 340
#define UI_FILE_INFO_HASH_HEIGHT 58
#define UI_LOG_PATH "/CERBERUS/LOGS"
#define UI_QR_PAYLOAD_CAPACITY 320
#define UI_QR_CUSTOM_CAPACITY 181
#define UI_QR_WIFI_SSID_CAPACITY 33
#define UI_QR_WIFI_PASSWORD_CAPACITY 64
#define UI_WIFI_VISIBLE_ROWS 6
#define UI_WIFI_HISTORY_SAMPLES 6U
#define UI_WIFI_LIST_Y 84
#define UI_WIFI_ROW_HEIGHT 44
#define UI_WIFI_CONTROLS_Y 356
#define UI_BLE_HISTORY_CAPACITY CERBERUS_BLE_SCAN_MAX_RESULTS
#define UI_BLE_HISTORY_SAMPLES 6U
#define UI_BLE_SERVICE_VISIBLE_ROWS 6U
#define UI_BLE_CHARACTERISTIC_VISIBLE_ROWS 6U
#define UI_IEEE802154_BEACON_VISIBLE_ROWS 6U

typedef enum {
    UI_PAGE_MAIN,
    UI_PAGE_FIELD_MENU,
    UI_PAGE_RADIO_LAB,
    UI_PAGE_WATCH,
    UI_PAGE_TOOLS,
    UI_PAGE_GUIDE,
    UI_PAGE_DASHBOARD,
    UI_PAGE_WIFI,
    UI_PAGE_BLE,
    UI_PAGE_BLE_ATTACK,
    UI_PAGE_BLE_SOUND,
    UI_PAGE_WARDRIVE,
    UI_PAGE_IEEE802154,
    UI_PAGE_FILES,
    UI_PAGE_QR,
    UI_PAGE_WEB,
    UI_PAGE_USB,
    UI_PAGE_SCRIPTS,
    UI_PAGE_LOGS,
    UI_PAGE_SELF_TEST,
    UI_PAGE_SETTINGS,
    UI_PAGE_REMOTE_ID,
    UI_PAGE_MAC_MONITOR,
} ui_page_t;

typedef struct {
    const char *label;
    ui_page_t page;
} ui_menu_item_t;

typedef enum {
    UI_FILE_MODE_LIST,
    UI_FILE_MODE_TEXT,
    UI_FILE_MODE_INFO,
    UI_FILE_MODE_IMAGE,
    UI_FILE_MODE_TOOLS,
    UI_FILE_MODE_SELECT,
    UI_FILE_MODE_ACTIONS,
    UI_FILE_MODE_NAME_INPUT,
    UI_FILE_MODE_DELETE_CONFIRM,
    UI_FILE_MODE_PACKAGE_INFO,
} ui_file_mode_t;

typedef enum {
    UI_FILE_OPERATION_NONE,
    UI_FILE_OPERATION_CREATE_DIRECTORY,
    UI_FILE_OPERATION_CREATE_FILE,
    UI_FILE_OPERATION_RENAME,
    UI_FILE_OPERATION_COPY,
    UI_FILE_OPERATION_MOVE,
} ui_file_operation_t;

typedef enum {
    UI_FILE_HASH_NOT_RUN,
    UI_FILE_HASH_RUNNING,
    UI_FILE_HASH_READY,
    UI_FILE_HASH_ERROR,
} ui_file_hash_state_t;

typedef enum {
    UI_LOG_MODE_LIST,
    UI_LOG_MODE_TEXT,
    UI_LOG_MODE_INFO,
    UI_LOG_MODE_QR,
    UI_LOG_MODE_DELETE_CONFIRM,
} ui_log_mode_t;

typedef enum {
    UI_QR_MODE_MENU,
    UI_QR_MODE_INPUT,
    UI_QR_MODE_CUSTOM,
    UI_QR_MODE_WIFI_SECURITY,
    UI_QR_MODE_WIFI_SSID_INPUT,
    UI_QR_MODE_WIFI_PASSWORD_INPUT,
    UI_QR_MODE_WIFI_CODE,
} ui_qr_mode_t;

typedef enum {
    UI_QR_WIFI_SECURITY_WPA,
    UI_QR_WIFI_SECURITY_WEP,
    UI_QR_WIFI_SECURITY_OPEN,
} ui_qr_wifi_security_t;

typedef enum {
    UI_WIFI_MODE_MENU,
    UI_WIFI_MODE_FIELD_SURVEY,
    UI_WIFI_MODE_LIST,
    UI_WIFI_MODE_STATS,
    UI_WIFI_MODE_CHANNELS_24,
    UI_WIFI_MODE_CHANNELS_5,
    UI_WIFI_MODE_PACKET_MONITOR,
    UI_WIFI_MODE_CSI,
    UI_WIFI_MODE_DEFENSE,
    UI_WIFI_MODE_DETAIL,
    UI_WIFI_MODE_TARGET,
    UI_WIFI_MODE_PASSWORD,
    UI_WIFI_MODE_CONNECTION,
    UI_WIFI_MODE_NETWORK_SCAN,
    UI_WIFI_MODE_STATIONS,
    UI_WIFI_MODE_OBSERVED_CLIENTS,
    UI_WIFI_MODE_AP_JOB,
    UI_WIFI_MODE_PORTAL,
    UI_WIFI_MODE_CAPTURE,
    UI_WIFI_MODE_MULTISSID,
    UI_WIFI_MODE_PINESCAN,
    UI_WIFI_MODE_DIAGNOSTICS,
    UI_WIFI_MODE_MDNS,
    UI_WIFI_MODE_BASELINE,
} ui_wifi_mode_t;

typedef struct {
    uint8_t total_bssids;
    uint16_t security_mask;
    uint8_t count_24_ghz;
    uint8_t count_5_ghz;
    int8_t strongest_rssi;
    int8_t weakest_rssi;
    uint8_t peer_count;
    cerberus_c5_wifi_result_t peers[4];
} ui_wifi_ssid_analysis_t;

typedef enum {
    UI_BLE_MODE_MENU,
    UI_BLE_MODE_LIST,
    UI_BLE_MODE_DETAIL,
    UI_BLE_MODE_FOX_HUNT,
    UI_BLE_MODE_ANALYZER,
    UI_BLE_MODE_FINDMY_MONITOR,
    UI_BLE_MODE_ENVIRONMENT,
    UI_BLE_MODE_SERVICES,
    UI_BLE_MODE_CHARACTERISTICS,
    UI_BLE_MODE_VALUE,
} ui_ble_mode_t;

#define UI_BLE_ENVIRONMENT_READING_MAX 3U

typedef struct {
    cerberus_c5_ble_characteristic_t characteristic;
    char value[64];
    bool complete;
    bool valid;
} ui_ble_environment_reading_t;

typedef enum {
    UI_IEEE802154_MODE_MENU,
    UI_IEEE802154_MODE_ENERGY,
    UI_IEEE802154_MODE_BEACONS,
    UI_IEEE802154_MODE_ACTIVITY,
    UI_IEEE802154_MODE_ASSESSMENT,
} ui_ieee802154_mode_t;

typedef enum {
    UI_SELF_TEST_MODE_SUMMARY,
    UI_SELF_TEST_MODE_TOUCH,
} ui_self_test_mode_t;

typedef enum {
    UI_SCRIPT_MODE_LIST,
    UI_SCRIPT_MODE_DETAIL,
    UI_SCRIPT_MODE_RUNNING,
} ui_script_mode_t;

typedef enum {
    UI_DASHBOARD_MODE_SUMMARY,
    UI_DASHBOARD_MODE_CRASH_RECORD,
    UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM,
} ui_dashboard_mode_t;

typedef enum {
    UI_USB_MODE_SUMMARY,
    UI_USB_MODE_RECOVERY_CONFIRM,
} ui_usb_mode_t;

typedef enum {
    UI_SETTINGS_MODE_DISPLAY,
    UI_SETTINGS_MODE_CLOCK_TIMER,
    UI_SETTINGS_MODE_NOTIFICATIONS,
    UI_SETTINGS_MODE_ACCESSIBILITY,
    UI_SETTINGS_MODE_DATA,
    UI_SETTINGS_MODE_TOUCH_CALIBRATION,
} ui_settings_mode_t;

typedef enum {
    UI_CAP_DISPLAY = 1UL << 0,
    UI_CAP_TOUCH = 1UL << 1,
    UI_CAP_SETTINGS = 1UL << 2,
    UI_CAP_STORAGE = 1UL << 3,
    UI_CAP_USB_HOST = 1UL << 4,
    UI_CAP_C5_LINK = 1UL << 5,
    UI_CAP_C5_PROTOCOL_V2 = 1UL << 6,
    UI_CAP_WIFI_2_4 = 1UL << 7,
    UI_CAP_WIFI_5 = 1UL << 8,
    UI_CAP_BLE = 1UL << 9,
    UI_CAP_BLE_GATT = 1UL << 10,
    UI_CAP_IEEE802154 = 1UL << 11,
} ui_capability_t;

typedef struct {
    bool used;
    uint8_t address[6];
    uint8_t address_type;
    int8_t samples[UI_BLE_HISTORY_SAMPLES];
    uint8_t sample_count;
    uint8_t next_sample;
    uint8_t last_generation;
    uint32_t last_seen_sequence;
} ui_ble_history_t;

static const char *TAG = "ui";
static esp_err_t draw_battery_indicator(void);

/* Commit only the finished UI state. The driver retains failed regions so a
 * transient transfer failure can be repaired on the next iteration. */
static void present_ui_frame(void)
{
    static unsigned failures;
    esp_err_t err = draw_battery_indicator();
    esp_err_t present_error = cerberus_display_present();
    if (err == ESP_OK) err = present_error;
    if (err == ESP_OK) {
        failures = 0;
    } else if (++failures == 1U || failures % 50U == 0U) {
        ESP_LOGW(TAG, "display transfer will retry: %s", esp_err_to_name(err));
    }
}
static const cerberus_ui_widget_theme_t ui_colors = {
    0x0062, 0x08A4, 0x3F1B, 0xE77E, 0x84B5,
};

#define UI_BACKGROUND (ui_colors.background)
#define UI_PANEL (ui_colors.panel)
#define UI_BORDER (ui_colors.border)
#define UI_TEXT (ui_colors.text)
#define UI_MUTED (ui_colors.muted)
#define UI_GOOD 0x3F1B
#define UI_BAD 0xF471
#define UI_WIDGET_THEME (&ui_colors)

static esp_err_t draw_detail_page(void);
static esp_err_t draw_live_scan(void);
static bool live_is_view(void);
static void live_scan_tick(uint32_t now, bool asleep);
static void handle_live_scan_tap(uint16_t x, uint16_t y);
static esp_err_t live_connect_selected_wifi(const char *password);
static bool live_scan_observation_valid;
static void live_reset_observations(void);
static bool live_scan_paused(void);
static const char *live_scan_action_label(void);
static void live_toggle_pause(void);
static sentinel_scan_tool_t live_page_tool(void);
static bool live_has_snapshot(sentinel_scan_tool_t tool);
static uint16_t live_snapshot_count(sentinel_scan_tool_t tool);
static bool live_connection_ready(void);
static bool log_file_name_is_system(const char *name);
static void open_tool_page(ui_page_t page);
static sentinel_scan_tool_t watch_requested_tool(void);
static esp_err_t watch_draw(uint32_t now, bool full);
static void watch_open(uint32_t now);
static void watch_ui_tick(uint32_t now);
static void watch_handle_tap(uint16_t x, uint16_t y);
static bool watch_keeps_awake(void);
static bool recon_sessions_idle_for_takeover(void);
static esp_err_t remote_id_draw(bool full);
static void remote_id_open(void);
static void remote_id_handle_tap(uint16_t x, uint16_t y);
static void remote_id_tick(uint32_t now, bool asleep);
static bool remote_id_keeps_awake(void);
static esp_err_t mac_monitor_draw(bool full);
static void mac_monitor_open(void);
static void mac_monitor_handle_tap(uint16_t x, uint16_t y);
static void mac_monitor_tick(uint32_t now, bool asleep);
static bool mac_monitor_keeps_awake(void);
static void open_learning_lesson(uint8_t lesson);

static const ui_menu_item_t menu_items[] = {
    {"Radio lab", UI_PAGE_RADIO_LAB},
    {"System health", UI_PAGE_DASHBOARD},
    {"Wi-Fi Watch", UI_PAGE_WIFI},
    {"Bluetooth Scout", UI_PAGE_BLE},
    {"Bluetooth attack", UI_PAGE_BLE_ATTACK},
    {"Find My sound", UI_PAGE_BLE_SOUND},
    {"Wardriving", UI_PAGE_WARDRIVE},
    {"Remote ID", UI_PAGE_REMOTE_ID},
    {"MAC Monitor", UI_PAGE_MAC_MONITOR},
    {"Radio Sense", UI_PAGE_IEEE802154},
    {"SD files", UI_PAGE_FILES},
    {"QR maker", UI_PAGE_QR},
    {"Local web", UI_PAGE_WEB},
    {"USB support", UI_PAGE_USB},
    {"Local scripts", UI_PAGE_SCRIPTS},
    {"Logs + reports", UI_PAGE_LOGS},
    {"Hardware checks", UI_PAGE_SELF_TEST},
    {"Settings", UI_PAGE_SETTINGS},
};

static bool wardrive_include_expansions;
static bool wardrive_gps_enabled; /* OFF on entry: GPS is not fitted on this unit. */
static bool wardrive_start_pending;
static bool wardrive_return_pending;
static uint32_t wardrive_start_requested_ms;
static uint32_t wardrive_last_start_attempt_ms;
static uint32_t wardrive_last_draw_ms;
static esp_err_t wardrive_start_error = ESP_OK;
static char wardrive_blocked_reason[48];
static cerberus_ble_attack_mode_t ble_attack_mode = CERBERUS_BLE_ATTACK_SOUR_APPLE;
static cerberus_ble_attack_capture_t ble_attack_capture;
static bool ble_attack_capture_valid;
static bool ble_attack_selecting;
static bool ble_attack_start_pending;
static bool ble_attack_return_pending;
static uint16_t ble_attack_job_id;
static uint16_t ble_attack_next_job_id;
static uint32_t ble_attack_start_requested_ms;
static uint32_t ble_attack_last_start_attempt_ms;
static uint32_t ble_attack_last_draw_ms;
static uint32_t ble_attack_last_touch_ms;
static esp_err_t ble_attack_start_error = ESP_OK;
static char ble_attack_capture_message[48];

static sentinel_tools_group_t tools_group = SENTINEL_TOOLS_MAIN;

static cerberus_ui_config_t ui_config;
static uint16_t ui_screen_timeout_seconds;
static uint8_t ui_accessibility_size;
static ui_settings_mode_t settings_mode = UI_SETTINGS_MODE_DISPLAY;
static uint32_t settings_timer_seconds = 60U;
static uint8_t touch_calibration_step;
static cerberus_touch_point_t touch_calibration_points[4];
static esp_err_t touch_calibration_error = ESP_OK;
static bool settings_reset_confirmation;
static esp_err_t settings_reset_error = ESP_OK;
static uint8_t settings_backup_buffer[CERBERUS_SETTINGS_BACKUP_MAX_BYTES];
static bool settings_backup_checked;
static esp_err_t settings_backup_error = ESP_ERR_INVALID_STATE;
static char settings_backup_file_name[CERBERUS_STORAGE_NAME_MAX];
static bool settings_restore_confirmation;
static bool settings_restore_checked;
static esp_err_t settings_restore_error = ESP_ERR_INVALID_STATE;
static size_t settings_restore_length;
static char settings_restore_file_name[CERBERUS_STORAGE_NAME_MAX];
static cerberus_settings_backup_info_t settings_restore_info;

static bool privacy_addresses_masked(void)
{
    return cerberus_settings_get()->privacy_mask_addresses;
}
static ui_page_t current_page = UI_PAGE_MAIN;
static cerberus_field_menu_t field_menu;
static ui_page_t tool_parent_page = UI_PAGE_MAIN;
static bool learning_tool_active;
static uint32_t sentinel_frame;
static esp_err_t web_action_error = ESP_OK;
static cerberus_c5_status_t c5_status;
static bool c5_online;
static cerberus_storage_entry_t file_entries[UI_FILE_ENTRY_CAPACITY];
static cerberus_storage_status_t storage_status;
static char file_path[CERBERUS_STORAGE_PATH_MAX] = "/";
static size_t file_entry_count;
static size_t file_page_offset;
static esp_err_t storage_last_error = ESP_ERR_INVALID_STATE;
static bool storage_checked;
static bool file_listing_checked;
static bool storage_safely_removed;
static bool storage_benchmark_running;
static ui_script_mode_t script_mode = UI_SCRIPT_MODE_LIST;
static cerberus_storage_entry_t script_entries[UI_SCRIPT_ENTRY_CAPACITY];
static size_t script_entry_count;
static size_t script_page_offset;
static bool script_listing_checked;
static esp_err_t script_listing_error = ESP_ERR_INVALID_STATE;
static cerberus_script_t loaded_script;
static cerberus_script_error_t script_parse_error;
static esp_err_t script_load_error = ESP_ERR_INVALID_STATE;
static size_t script_action_index;
static int64_t script_next_action_ms;
static bool script_running;
static bool script_complete;
static cerberus_script_action_type_t script_display_type;
static char script_display_text[CERBERUS_SCRIPT_TEXT_MAX + 1U];
static bool storage_benchmark_checked;
static esp_err_t storage_benchmark_error = ESP_ERR_INVALID_STATE;
static cerberus_storage_benchmark_result_t storage_benchmark_result;
static ui_dashboard_mode_t dashboard_mode = UI_DASHBOARD_MODE_SUMMARY;
static ui_usb_mode_t usb_mode = UI_USB_MODE_SUMMARY;
static esp_err_t usb_recovery_error = ESP_OK;
static cerberus_crash_record_t dashboard_crash_record;
static char dashboard_crash_action[48];
static uint16_t dashboard_crash_action_color = UI_DEFAULT_MUTED;
static ui_self_test_mode_t self_test_mode = UI_SELF_TEST_MODE_SUMMARY;
static uint8_t touch_test_corner_mask;
static bool touch_test_has_sample;
static uint16_t touch_test_last_x;
static uint16_t touch_test_last_y;
static uint16_t touch_test_min_x;
static uint16_t touch_test_max_x;
static uint16_t touch_test_min_y;
static uint16_t touch_test_max_y;
static ui_file_mode_t file_mode = UI_FILE_MODE_LIST;
static char viewed_file_name[CERBERUS_STORAGE_NAME_MAX];
static uint8_t viewed_file_data[UI_TEXT_VIEWER_CHUNK_BYTES + 1U];
static uint64_t viewed_file_offset;
static uint64_t viewed_file_size;
static uint64_t viewed_file_history[UI_TEXT_VIEWER_HISTORY];
static size_t viewed_file_history_count;
static size_t viewed_file_bytes;
static size_t viewed_file_advance;
static esp_err_t viewed_file_error = ESP_ERR_INVALID_STATE;
static uint8_t viewed_file_sha256[32];
static ui_file_hash_state_t viewed_file_hash_state = UI_FILE_HASH_NOT_RUN;
static esp_err_t viewed_file_hash_error = ESP_ERR_INVALID_STATE;
static cerberus_keyboard_t file_keyboard;
static ui_file_operation_t file_operation;
static char file_name_input[CERBERUS_STORAGE_NAME_MAX];
static cerberus_storage_entry_t file_selected_entry;
static char file_selected_path[CERBERUS_STORAGE_PATH_MAX];
static bool file_clipboard_active;
static bool file_clipboard_move;
static char file_clipboard_path[CERBERUS_STORAGE_PATH_MAX];
static char file_clipboard_name[CERBERUS_STORAGE_NAME_MAX];
static bool file_clipboard_is_directory;
static char file_action_message[80];
static uint16_t file_action_color = UI_DEFAULT_MUTED;
static cerberus_firmware_package_info_t file_package_info;
static esp_err_t file_package_error = ESP_ERR_INVALID_STATE;
static cerberus_storage_entry_t log_entries[UI_FILE_ENTRY_CAPACITY];
static size_t log_entry_count;
static size_t log_page_offset;
static esp_err_t log_error = ESP_ERR_INVALID_STATE;
static bool log_checked;
static ui_log_mode_t log_mode = UI_LOG_MODE_LIST;
static char log_action_message[64];
static uint16_t log_action_color = UI_DEFAULT_MUTED;
static char log_qr_payload[160];
static ui_qr_mode_t qr_mode = UI_QR_MODE_MENU;
static char qr_payload[UI_QR_PAYLOAD_CAPACITY];
static cerberus_keyboard_t qr_keyboard;
static bool qr_custom_is_url;
static char qr_wifi_ssid[UI_QR_WIFI_SSID_CAPACITY];
static char qr_wifi_password[UI_QR_WIFI_PASSWORD_CAPACITY];
static ui_qr_wifi_security_t qr_wifi_security = UI_QR_WIFI_SECURITY_WPA;
static uint8_t qr_save_modules[CERBERUS_QR_MAX_BITMAP_BYTES];
static bool qr_save_checked;
static esp_err_t qr_save_error = ESP_ERR_INVALID_STATE;
static char qr_save_file_name[CERBERUS_STORAGE_NAME_MAX];
static cerberus_wifi_scan_status_payload_t wifi_scan_status;
static cerberus_c5_wifi_result_t wifi_results[UI_WIFI_VISIBLE_ROWS];
static size_t wifi_result_count;
static size_t wifi_page_offset;
static esp_err_t wifi_scan_error = ESP_ERR_INVALID_STATE;
static bool wifi_scan_checked;
static uint32_t wifi_observed_at_seconds;
static uint8_t wifi_observed_generation;
static bool wifi_observation_valid;
static sentinel_baseline_t wifi_baseline;
static ui_wifi_mode_t wifi_mode = UI_WIFI_MODE_LIST;
static cerberus_wifi_radio_stats_payload_t wifi_radio_stats;
static esp_err_t wifi_radio_stats_error = ESP_ERR_INVALID_STATE;
static bool wifi_radio_stats_checked;
static char wifi_report_buffer[CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES];
static char wifi_report_file_name[CERBERUS_STORAGE_NAME_MAX];
static char wifi_export_message[64];
static uint16_t wifi_export_color = UI_DEFAULT_MUTED;
static cerberus_wifi_defense_status_payload_t wifi_defense_status;
static cerberus_wifi_defense_result_payload_t
    wifi_defense_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static size_t wifi_defense_loaded_count;
static size_t wifi_defense_page_offset;
static esp_err_t wifi_defense_error = ESP_ERR_INVALID_STATE;
static bool wifi_defense_checked;
static uint8_t wifi_selected_index;
static char wifi_connection_password[CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 1U];
static cerberus_keyboard_t wifi_keyboard;
static cerberus_wifi_connection_status_payload_t wifi_connection_status;
static cerberus_wifi_dns_status_payload_t wifi_dns_status;
static esp_err_t wifi_connection_error = ESP_ERR_INVALID_STATE;
static esp_err_t wifi_dns_error = ESP_ERR_INVALID_STATE;
static cerberus_network_diagnostics_status_payload_t network_diagnostics_status;
static esp_err_t network_diagnostics_error = ESP_ERR_INVALID_STATE;
static cerberus_mdns_scan_status_payload_t mdns_scan_status;
static cerberus_mdns_result_payload_t mdns_results[UI_WIFI_VISIBLE_ROWS];
static size_t mdns_result_count;
static size_t mdns_page_offset;
static esp_err_t mdns_scan_error = ESP_ERR_INVALID_STATE;
static uint8_t wifi_channel_counts[256];
static int8_t wifi_channel_strongest[256];
static uint8_t wifi_channel_generation;
static esp_err_t wifi_channel_error = ESP_ERR_INVALID_STATE;
static uint8_t wifi_channel_history_counts[UI_WIFI_HISTORY_SAMPLES][256];
static int8_t wifi_channel_history_strongest[UI_WIFI_HISTORY_SAMPLES][256];
static uint8_t wifi_channel_history_sample_count;
static uint8_t wifi_channel_history_next_sample;
static uint8_t wifi_channel_history_last_generation;
static cerberus_wifi_channel_survey_status_payload_t wifi_channel_survey_status;
static cerberus_wifi_channel_survey_result_payload_t
    wifi_channel_survey_rows[UI_WIFI_VISIBLE_ROWS];
static uint8_t wifi_channel_survey_page;
static bool wifi_channel_survey_attempted;
static esp_err_t wifi_channel_survey_error = ESP_ERR_INVALID_STATE;
static uint32_t wifi_channel_survey_last_poll;
static cerberus_wifi_packet_monitor_status_payload_t packet_monitor_status;
static cerberus_wifi_packet_monitor_sample_payload_t
    packet_monitor_samples[CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
static uint16_t packet_monitor_latest_fetched;
static uint8_t packet_monitor_channel = 1U;
static bool packet_monitor_have_fetched;
static bool packet_monitor_attempted;
static bool packet_monitor_from_target;
static esp_err_t packet_monitor_error = ESP_ERR_INVALID_STATE;
static uint32_t packet_monitor_last_poll;
static cerberus_c5_wifi_result_t wifi_selected_result;
static bool wifi_selection_valid;
static cerberus_c5_wifi_result_t wifi_connection_target;
static bool wifi_connection_target_valid;
static ui_wifi_ssid_analysis_t wifi_ssid_analysis;
static uint8_t wifi_analysis_generation;
static esp_err_t wifi_analysis_error = ESP_ERR_INVALID_STATE;
static esp_err_t wifi_trust_error = ESP_OK;
static cerberus_ble_scan_status_payload_t ble_scan_status;
static cerberus_c5_ble_result_t ble_results[UI_WIFI_VISIBLE_ROWS];
static uint8_t ble_result_indexes[UI_WIFI_VISIBLE_ROWS];
static uint8_t ble_focus_bits[CERBERUS_BLE_SCAN_MAX_RESULTS];
static size_t ble_result_count;
static size_t ble_page_offset;
static size_t ble_filtered_count;
static cerberus_s3_ble_filter_t ble_filter = CERBERUS_S3_BLE_FILTER_ALL;
static esp_err_t ble_scan_error = ESP_ERR_INVALID_STATE;
static bool ble_scan_checked;
static ui_ble_mode_t ble_mode = UI_BLE_MODE_LIST;
static bool ble_selection_valid;
static cerberus_c5_ble_result_t ble_selected_result;
static cerberus_s3_ble_focus_t ble_selected_focus;
static cerberus_findmy_profile_t ble_selected_sound_profile;
static uint8_t ble_selected_scan_index;
static uint32_t ble_selected_scan_ms, ble_sound_selected_scan_ms;
static cerberus_findmy_monitor_t *ble_findmy_monitor;
static cerberus_findmy_monitor_record_t ble_findmy_selected;
static bool ble_selected_from_monitor, ble_selected_raw_frozen;
static bool ble_selected_original_time64_valid;
static uint64_t ble_selected_original_ms64;
static uint64_t ble_selected_source_ceiling_ms64;
static bool ble_findmy_running, ble_findmy_scan_owned, ble_findmy_stopping;
static bool ble_findmy_exit_pending, ble_findmy_select_pending;
static uint8_t ble_findmy_generation;
static uint32_t ble_findmy_last_tick, ble_findmy_last_draw, ble_findmy_wait_since;
static uint32_t ble_findmy_sweeps;
static size_t ble_findmy_page;
static esp_err_t ble_findmy_error;
static esp_err_t draw_ble_findmy_monitor(void);
static cerberus_findmy_queue_t *ble_sound_queue; /* PSRAM only while Beep All is open. */
static cerberus_findmy_selection_t ble_sound_selection;
static cerberus_c5_ble_result_t ble_sound_peer;
static cerberus_findmy_profile_t ble_sound_profile;
static cerberus_s3_ble_sound_status_t ble_sound_status;
static ui_page_t ble_sound_return_page = UI_PAGE_FIELD_MENU;
static bool ble_sound_all, ble_sound_running, ble_sound_scan_owned;
static bool ble_sound_attempt_owned, ble_sound_selected_started;
static bool ble_sound_stop_pending, ble_sound_exit_pending;
static uint8_t ble_sound_scan_generation;
static uint8_t ble_sound_action_generation;
static uint32_t ble_sound_last_scan_start_ms, ble_sound_last_tick_ms;
static uint32_t ble_sound_busy_since_ms;
static uint32_t ble_sound_last_draw_ms, ble_sound_stop_started_ms;
static uint32_t ble_sound_sweeps, ble_sound_attempts, ble_sound_accepted;
static uint32_t ble_sound_responses, ble_sound_reported, ble_sound_errors;
static esp_err_t ble_sound_error = ESP_OK;
static bool ble_fox_hunt_exit_pending;
static bool ble_fox_hunt_retry_requested;
static esp_err_t ble_fox_hunt_error = ESP_OK;
static uint32_t ble_fox_hunt_last_tick_ms, ble_fox_hunt_last_draw_ms;
static uint32_t ble_fox_hunt_exit_started_ms;
static ui_page_t ble_session_exit_page = UI_PAGE_BLE;
static bool ble_analyzer_start_pending, ble_analyzer_started;
static bool ble_analyzer_exit_pending, ble_analyzer_retry_requested;
static esp_err_t ble_analyzer_error = ESP_OK;
static uint32_t ble_analyzer_last_tick_ms, ble_analyzer_last_draw_ms;
static uint32_t ble_analyzer_exit_started_ms, ble_analyzer_start_requested_ms;
static cerberus_ble_connection_status_payload_t ble_connection_status;
static esp_err_t ble_connection_error = ESP_ERR_INVALID_STATE;
static bool ble_connection_checked;
static cerberus_ble_gatt_service_status_payload_t ble_service_status;
static cerberus_c5_ble_service_t ble_services[UI_BLE_SERVICE_VISIBLE_ROWS];
static size_t ble_service_count;
static size_t ble_service_page_offset;
static esp_err_t ble_service_error = ESP_ERR_INVALID_STATE;
static bool ble_service_checked;
static cerberus_c5_ble_service_t ble_selected_service;
static cerberus_ble_gatt_characteristic_status_payload_t ble_characteristic_status;
static cerberus_c5_ble_characteristic_t
    ble_characteristics[UI_BLE_CHARACTERISTIC_VISIBLE_ROWS];
static size_t ble_characteristic_count;
static size_t ble_characteristic_page_offset;
static esp_err_t ble_characteristic_error = ESP_ERR_INVALID_STATE;
static bool ble_characteristic_checked;
static cerberus_c5_ble_characteristic_t ble_selected_characteristic;
static cerberus_ble_gatt_read_status_payload_t ble_read_status;
static uint8_t ble_read_value[CERBERUS_BLE_GATT_READ_MAX_LENGTH];
static size_t ble_read_value_length;
static esp_err_t ble_read_error = ESP_ERR_INVALID_STATE;
static bool ble_read_checked;
static ui_ble_environment_reading_t
    ble_environment_readings[UI_BLE_ENVIRONMENT_READING_MAX];
static size_t ble_environment_reading_count;
static size_t ble_environment_reading_index;
static bool ble_environment_service_search_complete;
static bool ble_environment_service_found;
static bool ble_environment_characteristics_loaded;
static bool ble_environment_refresh_active;
static esp_err_t ble_environment_error = ESP_ERR_INVALID_STATE;
static char ble_action_message[48];
static uint16_t ble_action_color = UI_DEFAULT_MUTED;
static ui_ble_history_t ble_history[UI_BLE_HISTORY_CAPACITY];
static uint32_t ble_history_sequence;
static cerberus_ieee802154_energy_scan_status_payload_t ieee802154_scan_status;
static cerberus_ieee802154_energy_scan_result_payload_t ieee802154_scan_result;
static esp_err_t ieee802154_scan_error = ESP_ERR_INVALID_STATE;
static bool ieee802154_scan_checked;
static ui_ieee802154_mode_t ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
static cerberus_ieee802154_beacon_scan_status_payload_t ieee802154_beacon_status;
static cerberus_ieee802154_beacon_scan_result_payload_t
    ieee802154_beacon_results[UI_IEEE802154_BEACON_VISIBLE_ROWS];
static size_t ieee802154_beacon_result_count;
static size_t ieee802154_beacon_page_offset;
static esp_err_t ieee802154_beacon_error = ESP_ERR_INVALID_STATE;
static bool ieee802154_beacon_checked;
static cerberus_ieee802154_activity_scan_status_payload_t ieee802154_activity_status;
static uint8_t ieee802154_activity_counts[CERBERUS_IEEE802154_CHANNEL_COUNT]
                                             [CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT];
static esp_err_t ieee802154_activity_error = ESP_ERR_INVALID_STATE;
static bool ieee802154_activity_checked;
static char ieee802154_report_buffer[CERBERUS_STORAGE_DIAGNOSTIC_MAX_BYTES];
static char ieee802154_report_file_name[CERBERUS_STORAGE_NAME_MAX];
static char ieee802154_export_message[64];
static uint16_t ieee802154_export_color = UI_DEFAULT_MUTED;
static uint8_t c5_status_failures;
static uint32_t ui_capability_map;
static bool ui_capability_blocked;
static const char *ui_capability_blocked_reason;

static uint16_t text_width(const char *text, uint8_t scale)
{
    return (uint16_t)(strlen(text) * 6U * scale);
}

static esp_err_t draw_centered(uint16_t y, const char *text, uint16_t color, uint8_t scale)
{
    uint16_t width = text_width(text, scale);
    uint16_t x = width < CERBERUS_LCD_WIDTH ? (CERBERUS_LCD_WIDTH - width) / 2U : 0;
    return cerberus_display_draw_text(x, y, text, color, UI_BACKGROUND, scale);
}

static esp_err_t draw_header(const char *title)
{
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(0, 0, CERBERUS_LCD_WIDTH, UI_HEADER_HEIGHT, UI_BACKGROUND),
        TAG,
        "header clear failed");
    uint16_t accent = current_page == UI_PAGE_BLE ? 0xACDF :
        current_page == UI_PAGE_IEEE802154 ? 0xFDEA : 0x3F1B;
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, 7, 3, 28, accent), TAG, "header rail");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(20, 5, "CERBERUS / SENTINEL", UI_MUTED, UI_BACKGROUND, 1), TAG, "header brand");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(20, 20, title, UI_TEXT, UI_BACKGROUND, 2), TAG, "header title");
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, 44, 304, 1, UI_PANEL), TAG, "header divider");
    return cerberus_display_fill_rect(8, 44, 46, 1, accent);
}

static esp_err_t draw_button(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *label)
{
    return cerberus_ui_widget_draw_button(
        (cerberus_ui_rect_t){x, y, width, height},
        label,
        2U,
        true,
        UI_WIDGET_THEME);
}

static esp_err_t draw_small_button(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const char *label)
{
    return cerberus_ui_widget_draw_button(
        (cerberus_ui_rect_t){x, y, width, height},
        label,
        1U,
        true,
        UI_WIDGET_THEME);
}

static void refresh_capability_map(void)
{
    uint32_t capabilities = UI_CAP_DISPLAY;
    cerberus_usb_mass_storage_status_t usb_msc_status;
    cerberus_usb_mass_storage_get_status(&usb_msc_status);
    if (ui_config.touch_available) {
        capabilities |= UI_CAP_TOUCH;
    }
    if (ui_config.settings_available) {
        capabilities |= UI_CAP_SETTINGS;
    }
    if (cerberus_storage_is_mounted()) {
        capabilities |= UI_CAP_STORAGE;
    }
    if (usb_serial_jtag_is_connected() || usb_msc_status.host_attached) {
        capabilities |= UI_CAP_USB_HOST;
    }
    if (c5_online) {
        capabilities |= UI_CAP_C5_LINK;
        if (c5_status.protocol_version >= CERBERUS_PROTOCOL_VERSION) {
            capabilities |= UI_CAP_C5_PROTOCOL_V2;
        }
        if ((c5_status.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ) != 0U) {
            capabilities |= UI_CAP_WIFI_2_4;
        }
        if ((c5_status.capabilities & CERBERUS_C5_CAP_WIFI_5_GHZ) != 0U) {
            capabilities |= UI_CAP_WIFI_5;
        }
        if ((c5_status.capabilities & CERBERUS_C5_CAP_IEEE802154) != 0U) {
            capabilities |= UI_CAP_IEEE802154;
        }
    }
#ifdef CONFIG_BT_NIMBLE_ENABLED
    capabilities |= UI_CAP_BLE;
#endif
#ifdef CONFIG_BT_NIMBLE_ROLE_CENTRAL
    capabilities |= UI_CAP_BLE_GATT;
#endif
    ui_capability_map = capabilities;
}

static const char *page_capability_issue(ui_page_t page)
{
    if (page != UI_PAGE_WIFI && page != UI_PAGE_BLE && page != UI_PAGE_IEEE802154) {
        return NULL;
    }
    if (page == UI_PAGE_BLE) {
        return (ui_capability_map & UI_CAP_BLE) != 0U ? NULL : "S3 BLE OBSERVER IS UNAVAILABLE";
    }
    if (!ui_config.c5_link_available) {
        return "C5 LINK INITIALIZATION FAILED";
    }
    if ((ui_capability_map & UI_CAP_C5_LINK) == 0U) {
        return "C5 IS NOT RESPONDING";
    }
    if ((ui_capability_map & UI_CAP_C5_PROTOCOL_V2) == 0U) {
        return "C5 PROTOCOL V2 IS REQUIRED";
    }
    if (c5_status.firmware_version < CERBERUS_C5_FIRMWARE_VERSION) {
        return "INSTALL MATCHED S3 AND C5 FIRMWARE";
    }
    if (page == UI_PAGE_WIFI &&
        (ui_capability_map & (UI_CAP_WIFI_2_4 | UI_CAP_WIFI_5)) == 0U) {
        return "C5 WIFI CAPABILITY IS MISSING";
    }
    if (page == UI_PAGE_IEEE802154 && (ui_capability_map & UI_CAP_IEEE802154) == 0U) {
        return "C5 802.15.4 CAPABILITY IS MISSING";
    }
    return NULL;
}

static const char *tools_group_title(void)
{
    if (tools_group == SENTINEL_TOOLS_CARE) return "DEVICE CARE";
    if (tools_group == SENTINEL_TOOLS_EXTRAS) return "EXTRAS";
    return "DEVICE TOOLS";
}

static esp_err_t draw_tools_menu(void)
{
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, UI_BACKGROUND),
        TAG, "tools clear failed");
    ESP_RETURN_ON_ERROR(draw_header(tools_group_title()), TAG, "tools header failed");
    return sentinel_tools_draw(tools_group, UI_WIDGET_THEME);
}

static sentinel_dashboard_status_t sentinel_status(void)
{
    sentinel_dashboard_status_t status = {
        .c5_online = c5_online,
        .touch_online = ui_config.touch_available,
        .privacy_masked = privacy_addresses_masked(),
        .wifi_count = live_snapshot_count(SENTINEL_SCAN_WIFI),
        .ble_count = live_snapshot_count(SENTINEL_SCAN_BLE),
        .lessons_complete = sentinel_guide_completed_count(),
        .radio_state = c5_online ? c5_status.radio_state : 0U,
        .wifi_observed = live_has_snapshot(SENTINEL_SCAN_WIFI),
        .ble_observed = live_has_snapshot(SENTINEL_SCAN_BLE),
        .uptime_seconds = (uint32_t)(esp_timer_get_time() / 1000000LL),
    };
    return status;
}

static esp_err_t draw_main_menu(void)
{
    sentinel_dashboard_status_t status = sentinel_status();
    return sentinel_dashboard_draw(&status, sentinel_frame);
}

static const ui_menu_item_t *find_item(ui_page_t page)
{
    for (size_t i = 0; i < sizeof(menu_items) / sizeof(menu_items[0]); ++i) {
        if (menu_items[i].page == page) {
            return &menu_items[i];
        }
    }
    return NULL;
}

static esp_err_t draw_dashboard_summary(void)
{
    char line[64];
    refresh_capability_map();
    cerberus_system_health_t s3_health;
    cerberus_system_health_snapshot(&s3_health);

    snprintf(line, sizeof(line), "SENTINEL %s", esp_app_get_description()->version);
    ESP_RETURN_ON_ERROR(draw_centered(68, line, UI_MUTED, 1), TAG, "dashboard subtitle failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(22, 108, "S3 DISPLAY", UI_TEXT, UI_BACKGROUND, 2), TAG, "dashboard S3 failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(218, 108, "READY", UI_GOOD, UI_BACKGROUND, 2), TAG, "dashboard S3 state failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(22, 146, "TOUCH", UI_TEXT, UI_BACKGROUND, 2), TAG, "dashboard touch failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_draw_text(218, 146, ui_config.touch_available ? "READY" : "OFFLINE", ui_config.touch_available ? UI_GOOD : UI_BAD, UI_BACKGROUND, 2),
        TAG,
        "dashboard touch state failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(22, 184, "C5 LINK", UI_TEXT, UI_BACKGROUND, 2), TAG, "dashboard C5 failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_draw_text(218, 184, c5_online ? "ONLINE" : "OFFLINE", c5_online ? UI_GOOD : UI_BAD, UI_BACKGROUND, 2),
        TAG,
        "dashboard C5 state failed");

    snprintf(line, sizeof(line), "S3 RESET  %s", cerberus_reset_reason_label(s3_health.reset_reason));
    ESP_RETURN_ON_ERROR(
        draw_centered(235, line, s3_health.previous_reset_fault ? UI_BAD : UI_GOOD, 1),
        TAG,
        "dashboard S3 reset failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(247, "TAP RESET LINE FOR CRASH RECORD", UI_MUTED, 1),
        TAG,
        "dashboard crash record hint failed");

    snprintf(line, sizeof(line), "S3 HEAP  FREE %uK  LOW %uK",
        (unsigned)(s3_health.free_internal_bytes / 1024U),
        (unsigned)(s3_health.minimum_internal_bytes / 1024U));
    ESP_RETURN_ON_ERROR(
        draw_centered(
            259,
            line,
            s3_health.minimum_internal_bytes < CERBERUS_HEAP_WARNING_BYTES ? UI_BAD : UI_TEXT,
            1),
        TAG,
        "dashboard S3 heap failed");

    snprintf(line, sizeof(line), "S3 STACK LOW %u B  UP %llu S",
        (unsigned)s3_health.main_task_minimum_stack_bytes,
        (unsigned long long)(s3_health.uptime_ms / 1000U));
    ESP_RETURN_ON_ERROR(
        draw_centered(
            283,
            line,
            s3_health.main_task_minimum_stack_bytes < CERBERUS_MAIN_STACK_WARNING_BYTES
                ? UI_BAD
                : UI_MUTED,
            1),
        TAG,
        "dashboard S3 stack failed");

    if (c5_online) {
        snprintf(line, sizeof(line), "C5 FW %lu  BOOT %u  HEAP %uK",
            (unsigned long)c5_status.firmware_version,
            (unsigned)c5_status.boot_count,
            (unsigned)c5_status.free_heap_kib);
        ESP_RETURN_ON_ERROR(draw_centered(313, line, UI_MUTED, 1), TAG, "dashboard C5 version failed");
        snprintf(line, sizeof(line), "C5 HEAP LOW %uK  UP %lu S",
            (unsigned)c5_status.minimum_free_heap_kib,
            (unsigned long)(c5_status.uptime_ms / 1000U));
        ESP_RETURN_ON_ERROR(draw_centered(335, line, UI_MUTED, 1), TAG, "dashboard C5 heap failed");
        snprintf(line, sizeof(line), "C5 RESET %s",
            cerberus_reset_reason_label((esp_reset_reason_t)c5_status.reset_reason));
        ESP_RETURN_ON_ERROR(
            draw_centered(
                357,
                line,
                cerberus_reset_reason_is_fault((esp_reset_reason_t)c5_status.reset_reason) ? UI_BAD : UI_MUTED,
                1),
            TAG,
            "dashboard C5 reset failed");
        snprintf(line, sizeof(line), "RADIO W:%c B:%c G:%c 15:%c",
            (c5_status.radio_state & CERBERUS_C5_RADIO_WIFI_SCAN) != 0U ? '*' : '-',
            (c5_status.radio_state & CERBERUS_C5_RADIO_BLE_SCAN) != 0U ? '*' : '-',
            (c5_status.radio_state & CERBERUS_C5_RADIO_BLE_CONNECTION) != 0U ? '*' : '-',
            (c5_status.radio_state & CERBERUS_C5_RADIO_IEEE802154_SCAN) != 0U ? '*' : '-');
        ESP_RETURN_ON_ERROR(draw_centered(379, line, UI_GOOD, 1), TAG, "dashboard C5 radio failed");
        snprintf(line, sizeof(line), "P%u 2G:%c 5G:%c B:%c 15:%c SD:%c U:%c",
            c5_status.protocol_version,
            (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ) != 0U ? 'Y' : '-',
            (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_5_GHZ) != 0U ? 'Y' : '-',
            (c5_status.capabilities & CERBERUS_C5_CAP_BLE) != 0U ? 'Y' : '-',
            (c5_status.capabilities & CERBERUS_C5_CAP_IEEE802154) != 0U ? 'Y' : '-',
            (ui_capability_map & UI_CAP_STORAGE) != 0U ? 'Y' : '-',
            (ui_capability_map & UI_CAP_USB_HOST) != 0U ? 'Y' : '-');
        ESP_RETURN_ON_ERROR(draw_centered(401, line, UI_MUTED, 1), TAG, "dashboard C5 protocol failed");
    }

    return ESP_OK;
}

static esp_err_t draw_dashboard_crash_record(void)
{
    char line[72];
    ESP_RETURN_ON_ERROR(
        draw_centered(68, "PERSISTENT CRASH RECORD", UI_MUTED, 2),
        TAG,
        "crash record title failed");

    if (!dashboard_crash_record.present) {
        ESP_RETURN_ON_ERROR(
            draw_centered(132, "NO STORED CRASH DUMP", UI_GOOD, 2),
            TAG,
            "crash record empty failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(184, "CREATED ONLY AFTER A PANIC OR WATCHDOG", UI_TEXT, 1),
            TAG,
            "crash record empty scope failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(210, "NORMAL RESETS DO NOT CREATE A RECORD", UI_MUTED, 1),
            TAG,
            "crash record normal reset failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(258, "RESERVED 256K IN INTERNAL FLASH", UI_MUTED, 1),
            TAG,
            "crash record partition failed");
        if (dashboard_crash_action[0] != '\0') {
            ESP_RETURN_ON_ERROR(
                draw_centered(310, dashboard_crash_action, dashboard_crash_action_color, 1),
                TAG,
                "crash record action failed");
        }
        return draw_button(40, 350, 240, 58, "REFRESH");
    }

    if (!dashboard_crash_record.valid) {
        ESP_RETURN_ON_ERROR(
            draw_centered(112, "CRASH DATA PRESENT", UI_BAD, 2),
            TAG,
            "crash record invalid title failed");
        snprintf(line, sizeof(line), "SIZE %u BYTES", (unsigned)dashboard_crash_record.size_bytes);
        ESP_RETURN_ON_ERROR(draw_centered(160, line, UI_TEXT, 1), TAG, "crash record invalid size failed");
        snprintf(line, sizeof(line), "INTEGRITY ERROR %s", esp_err_to_name(dashboard_crash_record.status));
        ESP_RETURN_ON_ERROR(draw_centered(192, line, UI_BAD, 1), TAG, "crash record invalid error failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(244, "METADATA CANNOT BE TRUSTED", UI_MUTED, 1),
            TAG,
            "crash record invalid warning failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(276, "CLEAR BEFORE CAPTURING ANOTHER CRASH", UI_TEXT, 1),
            TAG,
            "crash record invalid help failed");
        if (dashboard_crash_action[0] != '\0') {
            ESP_RETURN_ON_ERROR(
                draw_centered(316, dashboard_crash_action, dashboard_crash_action_color, 1),
                TAG,
                "crash record invalid action failed");
        }
        return draw_button(40, 350, 240, 58, "CLEAR RECORD");
    }

    ESP_RETURN_ON_ERROR(draw_centered(94, "VALID CORE DUMP", UI_BAD, 2), TAG, "crash record valid failed");
    snprintf(line, sizeof(line), "SIZE %u BYTES  TASK %.15s",
        (unsigned)dashboard_crash_record.size_bytes,
        dashboard_crash_record.task_name[0] != '\0' ? dashboard_crash_record.task_name : "UNKNOWN");
    ESP_RETURN_ON_ERROR(draw_centered(128, line, UI_TEXT, 1), TAG, "crash record task failed");
    snprintf(line, sizeof(line), "PC 0x%08lX", (unsigned long)dashboard_crash_record.exception_pc);
    ESP_RETURN_ON_ERROR(draw_centered(154, line, UI_TEXT, 1), TAG, "crash record PC failed");
    snprintf(line, sizeof(line), "CAUSE 0x%08lX  ADDR 0x%08lX",
        (unsigned long)dashboard_crash_record.exception_cause,
        (unsigned long)dashboard_crash_record.exception_address);
    ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_TEXT, 1), TAG, "crash record cause failed");
    snprintf(line, sizeof(line), "BACKTRACE %lu FRAME%s  %s",
        (unsigned long)dashboard_crash_record.backtrace_depth,
        dashboard_crash_record.backtrace_depth == 1U ? "" : "S",
        dashboard_crash_record.backtrace_corrupted ? "CORRUPT" : "VALID");
    ESP_RETURN_ON_ERROR(
        draw_centered(206, line, dashboard_crash_record.backtrace_corrupted ? UI_BAD : UI_GOOD, 1),
        TAG,
        "crash record backtrace failed");
    if (dashboard_crash_record.backtrace_depth > 0U) {
        snprintf(line, sizeof(line), "FIRST PC 0x%08lX",
            (unsigned long)dashboard_crash_record.backtrace[0]);
        ESP_RETURN_ON_ERROR(draw_centered(232, line, UI_MUTED, 1), TAG, "crash record first PC failed");
    }
    snprintf(line, sizeof(line), "APP ELF %.16s", dashboard_crash_record.app_elf_sha256_prefix);
    ESP_RETURN_ON_ERROR(draw_centered(258, line, UI_MUTED, 1), TAG, "crash record ELF hash failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(296, "RAW STACK IS NOT SHOWN OR EXPORTED", UI_GOOD, 1),
        TAG,
        "crash record privacy failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(320, "RETAINED UNTIL EXPLICITLY CLEARED", UI_MUTED, 1),
        TAG,
        "crash record retention failed");
    if (dashboard_crash_action[0] != '\0') {
        ESP_RETURN_ON_ERROR(
            draw_centered(334, dashboard_crash_action, dashboard_crash_action_color, 1),
            TAG,
            "crash record valid action failed");
    }
    return draw_button(40, 350, 240, 58, "CLEAR RECORD");
}

static esp_err_t draw_dashboard_crash_clear_confirmation(void)
{
    char line[64];
    snprintf(line, sizeof(line), "STORED SIZE %u BYTES", (unsigned)dashboard_crash_record.size_bytes);
    const char *lines[] = {
        line,
        "ERASES THE INTERNAL TASK AND STACK DUMP",
        "THE RECORD CANNOT BE RECOVERED",
        "NO MICROSD FILE IS CREATED",
    };
    ESP_RETURN_ON_ERROR(
        cerberus_ui_widget_draw_dialog(
            (cerberus_ui_rect_t){8U, 64U, 304U, 246U},
            "CLEAR CRASH RECORD?",
            lines,
            sizeof(lines) / sizeof(lines[0]),
            UI_BAD,
            UI_WIDGET_THEME),
        TAG,
        "crash clear dialog failed");
    ESP_RETURN_ON_ERROR(draw_button(20, 330, 132, 58, "CANCEL"), TAG, "crash clear cancel failed");
    return draw_button(168, 330, 132, 58, "CLEAR");
}

static esp_err_t draw_dashboard(void)
{
    if (dashboard_mode == UI_DASHBOARD_MODE_CRASH_RECORD) {
        return draw_dashboard_crash_record();
    }
    if (dashboard_mode == UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM) {
        return draw_dashboard_crash_clear_confirmation();
    }
    return draw_dashboard_summary();
}

static const char *wifi_security_text(uint8_t security)
{
    switch (security) {
    case CERBERUS_WIFI_SECURITY_OPEN:
        return "OPEN";
    case CERBERUS_WIFI_SECURITY_WEP:
        return "WEP";
    case CERBERUS_WIFI_SECURITY_WPA:
        return "WPA";
    case CERBERUS_WIFI_SECURITY_WPA2:
        return "WPA2";
    case CERBERUS_WIFI_SECURITY_WPA_WPA2:
        return "WPA/2";
    case CERBERUS_WIFI_SECURITY_WPA3:
        return "WPA3";
    case CERBERUS_WIFI_SECURITY_WPA2_WPA3:
        return "WPA2/3";
    case CERBERUS_WIFI_SECURITY_OWE:
        return "OWE";
    case CERBERUS_WIFI_SECURITY_ENTERPRISE:
        return "EAP";
    default:
        return "OTHER";
    }
}

static void make_ssid_display_safe(char *ssid)
{
    for (size_t index = 0; ssid[index] != '\0'; ++index) {
        uint8_t value = (uint8_t)ssid[index];
        if (value < 32U || value > 126U) {
            ssid[index] = '?';
        }
    }
}

static esp_err_t load_wifi_results(void)
{
    wifi_result_count = 0;
    if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (wifi_page_offset >= wifi_scan_status.stored_count) {
        wifi_page_offset = wifi_scan_status.stored_count == 0U
            ? 0U
            : ((wifi_scan_status.stored_count - 1U) / UI_WIFI_VISIBLE_ROWS) * UI_WIFI_VISIBLE_ROWS;
    }

    for (size_t row = 0; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        size_t index = wifi_page_offset + row;
        if (index >= wifi_scan_status.stored_count) {
            break;
        }
        esp_err_t err = c5_spi_link_get_wifi_scan_result(
            wifi_scan_status.generation,
            (uint8_t)index,
            &wifi_results[wifi_result_count]);
        if (err != ESP_OK) {
            return err;
        }
        make_ssid_display_safe(wifi_results[wifi_result_count].ssid);
        ++wifi_result_count;
    }
    return ESP_OK;
}

static void record_wifi_channel_history(void)
{
    if (wifi_channel_history_sample_count > 0U &&
        wifi_channel_history_last_generation == wifi_scan_status.generation) {
        return;
    }

    memcpy(
        wifi_channel_history_counts[wifi_channel_history_next_sample],
        wifi_channel_counts,
        sizeof(wifi_channel_counts));
    memcpy(
        wifi_channel_history_strongest[wifi_channel_history_next_sample],
        wifi_channel_strongest,
        sizeof(wifi_channel_strongest));
    wifi_channel_history_last_generation = wifi_scan_status.generation;
    wifi_channel_history_next_sample =
        (uint8_t)((wifi_channel_history_next_sample + 1U) % UI_WIFI_HISTORY_SAMPLES);
    if (wifi_channel_history_sample_count < UI_WIFI_HISTORY_SAMPLES) {
        ++wifi_channel_history_sample_count;
    }
}

static void clear_wifi_channel_history(void)
{
    memset(wifi_channel_history_counts, 0, sizeof(wifi_channel_history_counts));
    memset(wifi_channel_history_strongest, INT8_MIN, sizeof(wifi_channel_history_strongest));
    wifi_channel_history_sample_count = 0U;
    wifi_channel_history_next_sample = 0U;
    wifi_channel_history_last_generation = 0U;
}

static void reset_wifi_channel_history_to_current(void)
{
    if (wifi_channel_error != ESP_OK || wifi_channel_history_sample_count == 0) return;
    clear_wifi_channel_history();
    memcpy(wifi_channel_history_counts[0], wifi_channel_counts, sizeof(wifi_channel_counts));
    memcpy(wifi_channel_history_strongest[0], wifi_channel_strongest, sizeof(wifi_channel_strongest));
    wifi_channel_history_sample_count = 1;
    wifi_channel_history_next_sample = 1;
    wifi_channel_history_last_generation = wifi_channel_generation;
}

static uint16_t wifi_channel_history_total(uint8_t channel)
{
    uint16_t total = 0U;
    for (uint8_t sample = 0U; sample < wifi_channel_history_sample_count; ++sample) {
        total += wifi_channel_history_counts[sample][channel];
    }
    return total;
}

static uint8_t wifi_channel_history_seen(uint8_t channel)
{
    uint8_t seen = 0U;
    for (uint8_t sample = 0U; sample < wifi_channel_history_sample_count; ++sample) {
        if (wifi_channel_history_counts[sample][channel] > 0U) {
            ++seen;
        }
    }
    return seen;
}

static int8_t wifi_channel_history_best_rssi(uint8_t channel)
{
    int8_t best = INT8_MIN;
    for (uint8_t sample = 0U; sample < wifi_channel_history_sample_count; ++sample) {
        if (wifi_channel_history_strongest[sample][channel] > best) {
            best = wifi_channel_history_strongest[sample][channel];
        }
    }
    return best;
}

static esp_err_t rebuild_wifi_channel_summary(void)
{
    memset(wifi_channel_counts, 0, sizeof(wifi_channel_counts));
    for (size_t channel = 0; channel < sizeof(wifi_channel_strongest); ++channel) {
        wifi_channel_strongest[channel] = INT8_MIN;
    }
    wifi_channel_generation = 0;
    if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t index = 0; index < wifi_scan_status.stored_count; ++index) {
        cerberus_c5_wifi_result_t result;
        esp_err_t err = c5_spi_link_get_wifi_scan_metadata(
            wifi_scan_status.generation,
            index,
            &result);
        if (err != ESP_OK) {
            return err;
        }
        uint8_t channel = result.channel;
        if (wifi_channel_counts[channel] < UINT8_MAX) {
            ++wifi_channel_counts[channel];
        }
        if (result.rssi > wifi_channel_strongest[channel]) {
            wifi_channel_strongest[channel] = result.rssi;
        }
    }
    wifi_channel_generation = wifi_scan_status.generation;
    record_wifi_channel_history();
    return ESP_OK;
}

static bool wifi_bssid_matches(const uint8_t left[6], const uint8_t right[6])
{
    return memcmp(left, right, 6U) == 0;
}

static esp_err_t analyze_selected_wifi_ssid(void)
{
    memset(&wifi_ssid_analysis, 0, sizeof(wifi_ssid_analysis));
    wifi_ssid_analysis.strongest_rssi = INT8_MIN;
    wifi_ssid_analysis.weakest_rssi = INT8_MAX;
    wifi_analysis_generation = 0;
    if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }

    if (wifi_selected_result.ssid[0] == '\0') {
        wifi_ssid_analysis.total_bssids = 1;
        wifi_ssid_analysis.security_mask = (uint16_t)(1U << wifi_selected_result.security);
        wifi_ssid_analysis.count_24_ghz = wifi_selected_result.channel <= 14U ? 1U : 0U;
        wifi_ssid_analysis.count_5_ghz = wifi_selected_result.channel > 14U ? 1U : 0U;
        wifi_ssid_analysis.strongest_rssi = wifi_selected_result.rssi;
        wifi_ssid_analysis.weakest_rssi = wifi_selected_result.rssi;
        wifi_analysis_generation = wifi_scan_status.generation;
        return ESP_OK;
    }

    for (uint8_t index = 0; index < wifi_scan_status.stored_count; ++index) {
        cerberus_c5_wifi_result_t result;
        esp_err_t err = c5_spi_link_get_wifi_scan_result(
            wifi_scan_status.generation,
            index,
            &result);
        if (err != ESP_OK) {
            return err;
        }
        make_ssid_display_safe(result.ssid);
        if (strcmp(result.ssid, wifi_selected_result.ssid) != 0) {
            continue;
        }
        if (wifi_ssid_analysis.total_bssids < UINT8_MAX) {
            ++wifi_ssid_analysis.total_bssids;
        }
        if (result.security < 16U) {
            wifi_ssid_analysis.security_mask |= (uint16_t)(1U << result.security);
        }
        if (result.channel <= 14U) {
            if (wifi_ssid_analysis.count_24_ghz < UINT8_MAX) {
                ++wifi_ssid_analysis.count_24_ghz;
            }
        } else if (wifi_ssid_analysis.count_5_ghz < UINT8_MAX) {
            ++wifi_ssid_analysis.count_5_ghz;
        }
        if (result.rssi > wifi_ssid_analysis.strongest_rssi) {
            wifi_ssid_analysis.strongest_rssi = result.rssi;
        }
        if (result.rssi < wifi_ssid_analysis.weakest_rssi) {
            wifi_ssid_analysis.weakest_rssi = result.rssi;
        }
        if (!wifi_bssid_matches(result.bssid, wifi_selected_result.bssid) &&
            wifi_ssid_analysis.peer_count <
                sizeof(wifi_ssid_analysis.peers) / sizeof(wifi_ssid_analysis.peers[0])) {
            wifi_ssid_analysis.peers[wifi_ssid_analysis.peer_count++] = result;
        }
    }
    wifi_analysis_generation = wifi_scan_status.generation;
    return wifi_ssid_analysis.total_bssids > 0U ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static uint8_t count_wifi_security_modes(uint16_t mask)
{
    uint8_t count = 0;
    while (mask != 0U) {
        count += mask & 1U;
        mask >>= 1U;
    }
    return count;
}

static esp_err_t refresh_wifi_radio_stats(void)
{
    wifi_radio_stats_checked = true;
    if ((c5_status.capabilities & CERBERUS_C5_CAP_WIFI_RADIO_STATS) == 0U) {
        wifi_radio_stats_error = ESP_ERR_NOT_SUPPORTED;
        return wifi_radio_stats_error;
    }
    wifi_radio_stats_error = c5_spi_link_get_wifi_radio_stats(&wifi_radio_stats);
    if (wifi_radio_stats_error == ESP_OK &&
        wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE &&
        wifi_radio_stats.generation != wifi_scan_status.generation) {
        wifi_radio_stats_error = ESP_ERR_INVALID_RESPONSE;
    }
    return wifi_radio_stats_error;
}

static esp_err_t refresh_wifi_scan_status(void)
{
    wifi_scan_checked = true;
    wifi_scan_error = c5_spi_link_get_wifi_scan_status(&wifi_scan_status);
    if (wifi_scan_error == ESP_OK && wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE) {
        wifi_scan_error = load_wifi_results();
        if (wifi_scan_error == ESP_OK &&
            wifi_channel_generation != wifi_scan_status.generation) {
            wifi_channel_error = rebuild_wifi_channel_summary();
        }
        if (wifi_scan_error == ESP_OK &&
            (!wifi_radio_stats_checked ||
             wifi_radio_stats.generation != wifi_scan_status.generation)) {
            (void)refresh_wifi_radio_stats();
        }
        if (wifi_scan_error == ESP_OK && (!wifi_observation_valid ||
            wifi_observed_generation != wifi_scan_status.generation)) {
            wifi_observed_at_seconds = (uint32_t)(esp_timer_get_time() / 1000000LL);
            wifi_observed_generation = wifi_scan_status.generation;
            wifi_observation_valid = true;
            wifi_baseline.checked = false;
        }
    }
    return wifi_scan_error;
}

static uint8_t wifi_top_channel(bool five_ghz)
{
    uint8_t best = 0U;
    for (uint16_t channel = 1U; channel < 256U; ++channel) {
        if ((channel > 14U) != five_ghz || wifi_channel_counts[channel] == 0U) continue;
        if (best == 0U || wifi_channel_counts[channel] > wifi_channel_counts[best] ||
            (wifi_channel_counts[channel] == wifi_channel_counts[best] &&
             wifi_channel_strongest[channel] > wifi_channel_strongest[best])) {
            best = (uint8_t)channel;
        }
    }
    return best;
}

static esp_err_t append_wifi_report(size_t *length, const char *format, ...)
{
    if (length == NULL || format == NULL || *length >= sizeof(wifi_report_buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(
        wifi_report_buffer + *length,
        sizeof(wifi_report_buffer) - *length,
        format,
        args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(wifi_report_buffer) - *length) {
        return ESP_ERR_INVALID_SIZE;
    }
    *length += (size_t)written;
    return ESP_OK;
}

static esp_err_t export_wifi_radio_stats(void)
{
    /* Export the coherent completed snapshot, even while a new sweep runs or
       after the user pauses it. The live C5 generation is a different record. */
    if (!wifi_observation_valid || !wifi_radio_stats_checked ||
        wifi_radio_stats_error != ESP_OK ||
        wifi_channel_error != ESP_OK ||
        wifi_radio_stats.generation != wifi_observed_generation ||
        wifi_channel_generation != wifi_radio_stats.generation) {
        snprintf(wifi_export_message, sizeof(wifi_export_message), "COMPLETE A WI-FI SCAN FIRST");
        wifi_export_color = UI_BAD;
        return ESP_ERR_INVALID_STATE;
    }

    size_t length = 0U;
    wifi_report_buffer[0] = '\0';
    esp_err_t err = append_wifi_report(
        &length,
        "# Cerberus Wi-Fi radio statistics\r\n"
        "# format_version=1\r\n"
        "# scope=passive aggregate scan metadata; no SSIDs or BSSIDs\r\n"
        "# s3_uptime_ms=%llu\r\n"
        "# c5_uptime_ms=%lu\r\n"
        "# c5_firmware=%lu\r\n"
        "# generation=%u\r\n"
        "# observation_uptime_seconds=%lu\r\n"
        "# observation_age_seconds=%lu\r\n"
        "# duration_ms=%lu\r\n"
        "# total_heard=%u\r\n"
        "# analyzed=%u\r\n"
        "# truncated=%u\r\n"
        "# band_24_count=%u\r\n"
        "# band_5_count=%u\r\n"
        "# open_count=%u\r\n"
        "# secured_count=%u\r\n"
        "# hidden_count=%u\r\n"
        "# successful_scans=%u\r\n"
        "# failed_scans=%u\r\n"
        "channel,band,ap_count,strongest_rssi_dbm\r\n",
        (unsigned long long)(esp_timer_get_time() / 1000LL),
        (unsigned long)c5_status.uptime_ms,
        (unsigned long)c5_status.firmware_version,
        (unsigned)wifi_radio_stats.generation,
        (unsigned long)wifi_observed_at_seconds,
        (unsigned long)((uint32_t)(esp_timer_get_time() / 1000000LL) - wifi_observed_at_seconds),
        (unsigned long)wifi_radio_stats.scan_duration_ms,
        (unsigned)wifi_radio_stats.total_count,
        (unsigned)wifi_radio_stats.analyzed_count,
        (unsigned)wifi_radio_stats.truncated,
        (unsigned)wifi_radio_stats.count_24_ghz,
        (unsigned)wifi_radio_stats.count_5_ghz,
        (unsigned)wifi_radio_stats.open_count,
        (unsigned)wifi_radio_stats.secured_count,
        (unsigned)wifi_radio_stats.hidden_count,
        (unsigned)wifi_radio_stats.successful_scans,
        (unsigned)wifi_radio_stats.failed_scans);
    for (uint16_t channel = 1U; err == ESP_OK && channel < 256U; ++channel) {
        if (wifi_channel_counts[channel] == 0U) continue;
        err = append_wifi_report(
            &length,
            "%u,%s,%u,%d\r\n",
            (unsigned)channel,
            channel <= 14U ? "2.4GHz" : "5GHz",
            (unsigned)wifi_channel_counts[channel],
            wifi_channel_strongest[channel]);
    }
    if (err == ESP_OK) {
        err = cerberus_storage_write_wifi_report(
            wifi_report_buffer,
            length,
            wifi_report_file_name,
            sizeof(wifi_report_file_name));
    }
    if (err == ESP_OK) {
        storage_safely_removed = false;
        storage_checked = false;
        file_listing_checked = false;
        log_checked = false;
        snprintf(wifi_export_message, sizeof(wifi_export_message),
            "SAVED /CERBERUS/LOGS/%.12s", wifi_report_file_name);
        wifi_export_color = UI_GOOD;
        (void)cerberus_storage_log_event(
            CERBERUS_LOG_INFO, "wifi", "export", "aggregate radio statistics saved");
    } else {
        snprintf(wifi_export_message, sizeof(wifi_export_message),
            "EXPORT FAILED %s", esp_err_to_name(err));
        wifi_export_color = UI_BAD;
    }
    return err;
}

static esp_err_t refresh_wifi_defense_survey(void)
{
    wifi_defense_checked = true;
    if ((c5_status.capabilities & CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY) == 0U) {
        wifi_defense_error = ESP_ERR_NOT_SUPPORTED;
        return wifi_defense_error;
    }
    cerberus_wifi_defense_status_payload_t previous = wifi_defense_status;
    wifi_defense_error = c5_spi_link_get_wifi_defense_status(&wifi_defense_status);
    if (wifi_defense_error != ESP_OK) return wifi_defense_error;
    if (previous.generation != wifi_defense_status.generation) {
        wifi_defense_loaded_count = 0U;
        wifi_defense_page_offset = 0U;
        memset(wifi_defense_results, 0, sizeof(wifi_defense_results));
    }
    while (wifi_defense_loaded_count < wifi_defense_status.completed_count) {
        size_t index = wifi_defense_loaded_count;
        esp_err_t err = c5_spi_link_get_wifi_defense_result(
            wifi_defense_status.generation,
            (uint8_t)index,
            &wifi_defense_results[index]);
        if (err != ESP_OK) {
            wifi_defense_error = err;
            return err;
        }
        ++wifi_defense_loaded_count;
    }
    return ESP_OK;
}

static esp_err_t export_wifi_defense_survey(void)
{
    if (wifi_defense_status.state != CERBERUS_WIFI_DEFENSE_COMPLETE ||
        wifi_defense_error != ESP_OK ||
        wifi_defense_loaded_count != wifi_defense_status.completed_count) {
        snprintf(wifi_export_message, sizeof(wifi_export_message), "COMPLETE THE SURVEY FIRST");
        wifi_export_color = UI_BAD;
        return ESP_ERR_INVALID_STATE;
    }

    size_t length = 0U;
    wifi_report_buffer[0] = '\0';
    esp_err_t err = append_wifi_report(
        &length,
        "# Cerberus defensive Wi-Fi observation survey\r\n"
        "# format_version=1\r\n"
        "# scope=aggregate 802.11 frame metadata only; no payloads, SSIDs, BSSIDs, or addresses\r\n"
        "# warning=heuristic indicators are not proof of an attack or jammer\r\n"
        "# generation=%u\r\n"
        "# elapsed_ms=%lu\r\n"
        "# channels=%u\r\n"
        "# flagged_channels=%u\r\n"
        "channel,flags,management,control,data,beacon,deauth,disassoc,avg_noise_dbm,strongest_rssi_dbm,dwell_ms,beacon_rate_per_sec\r\n",
        (unsigned)wifi_defense_status.generation,
        (unsigned long)wifi_defense_status.elapsed_ms,
        (unsigned)wifi_defense_status.completed_count,
        (unsigned)wifi_defense_status.flagged_count);
    for (size_t index = 0U; err == ESP_OK && index < wifi_defense_loaded_count; ++index) {
        const cerberus_wifi_defense_result_payload_t *result = &wifi_defense_results[index];
        err = append_wifi_report(
            &length,
            "%u,0x%02X,%u,%u,%u,%u,%u,%u,%d,%d,%u,%u\r\n",
            (unsigned)result->channel,
            (unsigned)result->assessment_flags,
            (unsigned)result->management_frames,
            (unsigned)result->control_frames,
            (unsigned)result->data_frames,
            (unsigned)result->beacon_frames,
            (unsigned)result->deauth_frames,
            (unsigned)result->disassoc_frames,
            result->average_noise_floor,
            result->strongest_rssi,
            (unsigned)result->dwell_ms,
            (unsigned)result->beacon_rate_per_sec);
    }
    if (err == ESP_OK) {
        err = cerberus_storage_write_wifi_defense_report(
            wifi_report_buffer, length, wifi_report_file_name, sizeof(wifi_report_file_name));
    }
    if (err == ESP_OK) {
        storage_safely_removed = false;
        storage_checked = false;
        file_listing_checked = false;
        log_checked = false;
        snprintf(wifi_export_message, sizeof(wifi_export_message),
            "SAVED /CERBERUS/LOGS/%.12s", wifi_report_file_name);
        wifi_export_color = UI_GOOD;
        (void)cerberus_storage_log_event(
            CERBERUS_LOG_INFO, "wifi", "defense", "aggregate defensive survey saved");
    } else {
        snprintf(wifi_export_message, sizeof(wifi_export_message),
            "EXPORT FAILED %s", esp_err_to_name(err));
        wifi_export_color = UI_BAD;
    }
    return err;
}

static esp_err_t draw_wifi_defense_survey(void)
{
    char line[80];
    if (!wifi_defense_checked) (void)refresh_wifi_defense_survey();
    if (wifi_defense_error != ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_centered(140, "DEFENSIVE SURVEY UNAVAILABLE", UI_BAD, 1),
            TAG, "Wi-Fi defense unavailable failed");
        snprintf(line, sizeof(line), "C5 LINK %s", esp_err_to_name(wifi_defense_error));
        ESP_RETURN_ON_ERROR(draw_centered(174, line, UI_MUTED, 1), TAG, "Wi-Fi defense error failed");
    } else {
        const char *state = wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING
            ? "RUNNING"
            : (wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_COMPLETE
                ? "COMPLETE"
                : (wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_ERROR ? "ERROR" : "READY"));
        snprintf(line, sizeof(line), "%s  %u/%u CH  FLAGGED %u  %lus",
            state,
            (unsigned)wifi_defense_status.completed_count,
            (unsigned)wifi_defense_status.result_count,
            (unsigned)wifi_defense_status.flagged_count,
            (unsigned long)(wifi_defense_status.elapsed_ms / 1000U));
        ESP_RETURN_ON_ERROR(draw_centered(54, line,
            wifi_defense_status.flagged_count > 0U ? UI_BAD : UI_GOOD, 1),
            TAG, "Wi-Fi defense summary failed");
        ESP_RETURN_ON_ERROR(draw_centered(72, "D=DEAUTH  B=BEACON  N=NOISE  A=ACTIVITY", UI_MUTED, 1),
            TAG, "Wi-Fi defense legend failed");
        if (wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_ERROR) {
            snprintf(line, sizeof(line), "SURVEY ERROR %s",
                esp_err_to_name((esp_err_t)wifi_defense_status.error_code));
            ESP_RETURN_ON_ERROR(draw_centered(82, line, UI_BAD, 1),
                TAG, "Wi-Fi defense C5 error failed");
        }

        size_t visible = 0U;
        for (size_t row = 0U; row < UI_WIFI_VISIBLE_ROWS; ++row) {
            size_t index = wifi_defense_page_offset + row;
            if (index >= wifi_defense_loaded_count) break;
            ++visible;
            const cerberus_wifi_defense_result_payload_t *result = &wifi_defense_results[index];
            char flags[5] = {
                (result->assessment_flags & CERBERUS_WIFI_DEFENSE_FLAG_DEAUTH_ACTIVITY) ? 'D' : '-',
                (result->assessment_flags & CERBERUS_WIFI_DEFENSE_FLAG_HIGH_BEACON_RATE) ? 'B' : '-',
                (result->assessment_flags & CERBERUS_WIFI_DEFENSE_FLAG_ELEVATED_NOISE) ? 'N' : '-',
                (result->assessment_flags & CERBERUS_WIFI_DEFENSE_FLAG_HIGH_ACTIVITY) ? 'A' : '-',
                '\0',
            };
            uint16_t y = 92U + (uint16_t)row * 40U;
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304, 36, UI_PANEL),
                TAG, "Wi-Fi defense row failed");
            snprintf(line, sizeof(line), "CH%-3u [%s]  MGMT %u  DATA %u",
                (unsigned)result->channel, flags,
                (unsigned)result->management_frames, (unsigned)result->data_frames);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 4U, line,
                result->assessment_flags != 0U ? UI_BAD : UI_TEXT, UI_PANEL, 1),
                TAG, "Wi-Fi defense row title failed");
            snprintf(line, sizeof(line), "BEACON %u/s  DEAUTH %u  DISASSOC %u  NF %d",
                (unsigned)result->beacon_rate_per_sec,
                (unsigned)result->deauth_frames,
                (unsigned)result->disassoc_frames,
                result->average_noise_floor);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 20U, line, UI_MUTED, UI_PANEL, 1),
                TAG, "Wi-Fi defense row detail failed");
        }
        if (visible == 0U) {
            if (wifi_defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING) {
                snprintf(line, sizeof(line), "OBSERVING CHANNEL %u",
                    (unsigned)wifi_defense_status.current_channel);
                ESP_RETURN_ON_ERROR(draw_centered(175, line, UI_GOOD, 2),
                    TAG, "Wi-Fi defense current channel failed");
            } else {
                ESP_RETURN_ON_ERROR(draw_centered(155, "RUN A WI-FI SCAN, THEN START SURVEY", UI_TEXT, 1),
                    TAG, "Wi-Fi defense empty failed");
            }
        }
        ESP_RETURN_ON_ERROR(draw_centered(338, "INDICATORS ONLY - NOT PROOF OF AN ATTACK", UI_MUTED, 1),
            TAG, "Wi-Fi defense disclaimer failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42, "PREV"), TAG, "Wi-Fi defense prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42, "NEXT"), TAG, "Wi-Fi defense next failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42, "EXPORT"), TAG, "Wi-Fi defense export failed");
    return draw_small_button(236, UI_WIFI_CONTROLS_Y, 76, 42,
        live_scan_action_label());
}

static esp_err_t draw_wifi_radio_stats(void)
{
    char line[72];
    if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE &&
        (!wifi_radio_stats_checked || wifi_radio_stats_error != ESP_OK ||
         wifi_radio_stats.generation != wifi_scan_status.generation)) {
        (void)refresh_wifi_radio_stats();
    }
    bool have_snapshot = wifi_observation_valid && wifi_radio_stats_checked &&
        wifi_radio_stats_error == ESP_OK && wifi_channel_error == ESP_OK &&
        wifi_radio_stats.generation == wifi_observed_generation &&
        wifi_channel_generation == wifi_radio_stats.generation;
    if (!have_snapshot && wifi_radio_stats_error != ESP_OK &&
        wifi_radio_stats_error != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(draw_centered(150, "RADIO STATISTICS UNAVAILABLE", UI_BAD, 1),
            TAG, "Wi-Fi statistics unavailable failed");
        snprintf(line, sizeof(line), "C5 LINK %s", esp_err_to_name(wifi_radio_stats_error));
        ESP_RETURN_ON_ERROR(draw_centered(184, line, UI_MUTED, 1), TAG, "Wi-Fi statistics error failed");
    } else if (!have_snapshot) {
        ESP_RETURN_ON_ERROR(draw_centered(150, "WAITING FOR A COMPLETED SNAPSHOT", UI_MUTED, 1),
            TAG, "Wi-Fi statistics empty failed");
        ESP_RETURN_ON_ERROR(draw_centered(184, live_scan_paused() ?
            "TAP SCAN TO COLLECT A SWEEP" : "ONE SWEEP / RESULTS APPEAR WHEN COMPLETE", UI_TEXT, 1),
            TAG, "Wi-Fi statistics prompt failed");
    } else {
        uint8_t top_24 = wifi_top_channel(false);
        uint8_t top_5 = wifi_top_channel(true);
        snprintf(line, sizeof(line), "SNAPSHOT %u / OBSERVED %lus AGO",
            (unsigned)wifi_radio_stats.generation,
            (unsigned long)((uint32_t)(esp_timer_get_time() / 1000000LL) - wifi_observed_at_seconds));
        ESP_RETURN_ON_ERROR(draw_centered(54, line, UI_GOOD, 1), TAG, "Wi-Fi statistics header failed");
        snprintf(line, sizeof(line), "%lums / OK %u / FAIL %u / %s",
            (unsigned long)wifi_radio_stats.scan_duration_ms,
            (unsigned)wifi_radio_stats.successful_scans,
            (unsigned)wifi_radio_stats.failed_scans,
            wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ? "LISTENING" :
                live_scan_paused() ? "PAUSED" : "READY");
        ESP_RETURN_ON_ERROR(draw_centered(68, line, UI_MUTED, 1), TAG, "Wi-Fi statistics activity failed");

        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(10, 84, 300, 54, UI_PANEL),
            TAG, "Wi-Fi statistics 2.4 panel failed");
        snprintf(line, sizeof(line), "2.4 GHZ  APS %u  ACTIVE CH %u",
            (unsigned)wifi_radio_stats.count_24_ghz,
            (unsigned)wifi_radio_stats.active_channels_24);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, 94, line, UI_TEXT, UI_PANEL, 1),
            TAG, "Wi-Fi statistics 2.4 summary failed");
        snprintf(line, sizeof(line), top_24 == 0U ? "TOP CHANNEL: NONE" : "TOP CHANNEL %u  APS %u  BEST %dDB",
            (unsigned)top_24,
            top_24 == 0U ? 0U : (unsigned)wifi_channel_counts[top_24],
            top_24 == 0U ? -127 : wifi_channel_strongest[top_24]);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, 116, line, UI_MUTED, UI_PANEL, 1),
            TAG, "Wi-Fi statistics 2.4 top failed");

        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(10, 146, 300, 54, UI_PANEL),
            TAG, "Wi-Fi statistics 5 panel failed");
        snprintf(line, sizeof(line), "5 GHZ    APS %u  ACTIVE CH %u",
            (unsigned)wifi_radio_stats.count_5_ghz,
            (unsigned)wifi_radio_stats.active_channels_5);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, 156, line, UI_TEXT, UI_PANEL, 1),
            TAG, "Wi-Fi statistics 5 summary failed");
        snprintf(line, sizeof(line), top_5 == 0U ? "TOP CHANNEL: NONE" : "TOP CHANNEL %u  APS %u  BEST %dDB",
            (unsigned)top_5,
            top_5 == 0U ? 0U : (unsigned)wifi_channel_counts[top_5],
            top_5 == 0U ? -127 : wifi_channel_strongest[top_5]);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, 178, line, UI_MUTED, UI_PANEL, 1),
            TAG, "Wi-Fi statistics 5 top failed");

        snprintf(line, sizeof(line), "HEARD %u  ANALYZED %u%s",
            (unsigned)wifi_radio_stats.total_count,
            (unsigned)wifi_radio_stats.analyzed_count,
            wifi_radio_stats.truncated ? "  TRUNCATED" : "");
        ESP_RETURN_ON_ERROR(draw_centered(222, line, wifi_radio_stats.truncated ? UI_BAD : UI_TEXT, 1),
            TAG, "Wi-Fi statistics totals failed");
        snprintf(line, sizeof(line), "OPEN %u  SECURED %u  HIDDEN %u",
            (unsigned)wifi_radio_stats.open_count,
            (unsigned)wifi_radio_stats.secured_count,
            (unsigned)wifi_radio_stats.hidden_count);
        ESP_RETURN_ON_ERROR(draw_centered(250, line, UI_TEXT, 1), TAG, "Wi-Fi security totals failed");
        snprintf(line, sizeof(line), "STRONGEST CH %u  %dDB",
            (unsigned)wifi_radio_stats.strongest_channel,
            wifi_radio_stats.strongest_rssi);
        ESP_RETURN_ON_ERROR(draw_centered(278, line, UI_GOOD, 1), TAG, "Wi-Fi strongest failed");
        ESP_RETURN_ON_ERROR(draw_centered(304, "AGGREGATE METADATA ONLY - NO PAYLOAD CAPTURE", UI_MUTED, 1),
            TAG, "Wi-Fi statistics scope failed");
        if (wifi_export_message[0] != '\0') {
            ESP_RETURN_ON_ERROR(draw_centered(328, wifi_export_message, wifi_export_color, 1),
                TAG, "Wi-Fi export message failed");
        }
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42, "SURVEY"), TAG, "Wi-Fi defense survey failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42, "CHANNELS"), TAG, "Wi-Fi channels failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42, "EXPORT"), TAG, "Wi-Fi stats export failed");
    return draw_small_button(236, UI_WIFI_CONTROLS_Y, 76, 42, "AP LIST");
}

static bool wifi_channel_in_selected_band(uint16_t channel)
{
    return wifi_mode == UI_WIFI_MODE_CHANNELS_24
        ? channel >= 1U && channel <= 14U
        : channel > 14U;
}

static uint8_t wifi_channel_survey_band(void)
{
    return wifi_mode == UI_WIFI_MODE_CHANNELS_24
        ? CERBERUS_WIFI_CHANNEL_SURVEY_BAND_24
        : CERBERUS_WIFI_CHANNEL_SURVEY_BAND_5;
}

static bool packet_monitor_stop_ui(void)
{
    if (!packet_monitor_attempted) {
        return true;
    }
    if (packet_monitor_error == ESP_ERR_NOT_SUPPORTED) {
        packet_monitor_attempted = false;
        return true;
    }
    if (packet_monitor_status.generation == 0U) {
        packet_monitor_error = c5_spi_link_wifi_packet_monitor_status(
            &packet_monitor_status);
        if (packet_monitor_error != ESP_OK) return false;
        if (packet_monitor_status.state != CERBERUS_WIFI_PACKET_MONITOR_RUNNING ||
            packet_monitor_status.channel != packet_monitor_channel) {
            packet_monitor_attempted = false;
            return true;
        }
    }
    if (packet_monitor_status.generation == 0U) {
        packet_monitor_attempted = false;
        return true;
    }
    if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING ||
        packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_ERROR) {
        packet_monitor_error = c5_spi_link_wifi_packet_monitor_stop(
            packet_monitor_status.generation, &packet_monitor_status);
        if (packet_monitor_error != ESP_OK) return false;
        uint32_t began = (uint32_t)(esp_timer_get_time() / 1000LL);
        while (packet_monitor_status.state ==
                CERBERUS_WIFI_PACKET_MONITOR_RUNNING &&
            (uint32_t)((esp_timer_get_time() / 1000LL) - began) < 1500U) {
            vTaskDelay(pdMS_TO_TICKS(50));
            packet_monitor_error = c5_spi_link_wifi_packet_monitor_status(
                &packet_monitor_status);
            if (packet_monitor_error != ESP_OK) return false;
        }
        if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING) {
            packet_monitor_error = ESP_ERR_TIMEOUT;
            return false;
        }
    }
    packet_monitor_attempted = false;
    return true;
}

static void packet_monitor_start_ui(void)
{
    if (packet_monitor_attempted) return;
    memset(&packet_monitor_status, 0, sizeof(packet_monitor_status));
    if (c5_status.firmware_version < CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE) {
        packet_monitor_attempted = true;
        packet_monitor_error = ESP_ERR_NOT_SUPPORTED;
        return;
    }
    packet_monitor_attempted = true;
    memset(packet_monitor_samples, 0, sizeof(packet_monitor_samples));
    packet_monitor_have_fetched = false;
    packet_monitor_error = c5_spi_link_wifi_packet_monitor_start(
        packet_monitor_channel, &packet_monitor_status);
    packet_monitor_last_poll = (uint32_t)(esp_timer_get_time() / 1000LL);
    if (packet_monitor_error == ESP_ERR_INVALID_STATE) {
        /* A prior AP scan may still be releasing the C5. */
        packet_monitor_attempted = false;
    } else if (packet_monitor_error == ESP_OK) {
        packet_monitor_latest_fetched = (uint16_t)(
            packet_monitor_status.latest_sequence -
            packet_monitor_status.stored_count);
        packet_monitor_have_fetched = true;
    }
}

static void packet_monitor_refresh(uint32_t now_ms)
{
    if (c5_status.firmware_version < CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE)
        return;
    if (!packet_monitor_attempted) {
        if ((uint32_t)(now_ms - packet_monitor_last_poll) < 700U) return;
        packet_monitor_start_ui();
        if (!packet_monitor_attempted) return;
    }
    if ((uint32_t)(now_ms - packet_monitor_last_poll) < 500U) return;
    packet_monitor_last_poll = now_ms;
    packet_monitor_error = c5_spi_link_wifi_packet_monitor_status(
        &packet_monitor_status);
    if (packet_monitor_error != ESP_OK ||
        packet_monitor_status.generation == 0U) return;
    if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_ERROR) {
        packet_monitor_error = packet_monitor_status.error_code != ESP_OK ?
            (esp_err_t)packet_monitor_status.error_code : ESP_FAIL;
        return;
    }
    if (!packet_monitor_have_fetched) {
        packet_monitor_latest_fetched = (uint16_t)(
            packet_monitor_status.latest_sequence -
            packet_monitor_status.stored_count);
        packet_monitor_have_fetched = true;
    }
    uint16_t missing = (uint16_t)(packet_monitor_status.latest_sequence -
        packet_monitor_latest_fetched);
    if (missing > packet_monitor_status.stored_count)
        packet_monitor_latest_fetched = (uint16_t)(
            packet_monitor_status.latest_sequence -
            packet_monitor_status.stored_count);
    missing = (uint16_t)(packet_monitor_status.latest_sequence -
        packet_monitor_latest_fetched);
    if (missing > 4U)
        packet_monitor_latest_fetched = (uint16_t)(
            packet_monitor_status.latest_sequence - 4U);
    for (unsigned fetched = 0U; fetched < 4U &&
        packet_monitor_latest_fetched != packet_monitor_status.latest_sequence;
        ++fetched) {
        uint16_t sequence = (uint16_t)(packet_monitor_latest_fetched + 1U);
        cerberus_wifi_packet_monitor_sample_payload_t sample = {0};
        esp_err_t error = c5_spi_link_wifi_packet_monitor_sample(
            packet_monitor_status.generation, sequence, &sample);
        if (error != ESP_OK) {
            packet_monitor_error = error;
            break;
        }
        packet_monitor_samples[sequence % CERBERUS_WIFI_PACKET_MONITOR_SAMPLES] =
            sample;
        packet_monitor_latest_fetched = sequence;
    }
}

static void packet_monitor_step_channel(int direction)
{
    uint16_t candidate = packet_monitor_channel;
    do {
        candidate = direction > 0 ? candidate == 165U ? 1U : candidate + 1U :
            candidate == 1U ? 165U : candidate - 1U;
    } while (!cerberus_wifi_packet_monitor_channel_valid((uint8_t)candidate));
    if (!packet_monitor_stop_ui()) return;
    packet_monitor_channel = (uint8_t)candidate;
    packet_monitor_last_poll = 0U;
    packet_monitor_start_ui();
}

static bool wifi_channel_survey_stop_ui(void)
{
    if (wifi_channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING) {
        cerberus_wifi_channel_survey_status_payload_t stopped;
        esp_err_t err = c5_spi_link_wifi_channel_survey_stop(&stopped);
        if (err == ESP_OK) wifi_channel_survey_status = stopped;
        else { wifi_channel_survey_error = err; return false; }
    }
    wifi_channel_survey_attempted = false;
    return true;
}

static void wifi_channel_survey_start_ui(void)
{
    if (wifi_channel_survey_attempted) return;
    if (c5_status.firmware_version < CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE) {
        wifi_channel_survey_attempted = true;
        wifi_channel_survey_error = ESP_ERR_NOT_SUPPORTED;
        return;
    }
    wifi_channel_survey_attempted = true;
    wifi_channel_survey_page = 0U;
    memset(&wifi_channel_survey_status, 0, sizeof(wifi_channel_survey_status));
    memset(wifi_channel_survey_rows, 0, sizeof(wifi_channel_survey_rows));
    wifi_channel_survey_error = c5_spi_link_wifi_channel_survey_start(
        wifi_channel_survey_band(), &wifi_channel_survey_status);
    if (wifi_channel_survey_error == ESP_ERR_INVALID_STATE) {
        /* An AP scan or previous-band survey may still be draining after its
         * Stop acknowledgement. The next UI poll retries once it has left. */
        wifi_channel_survey_attempted = false;
        wifi_channel_survey_last_poll = (uint32_t)(esp_timer_get_time() / 1000LL);
        return;
    }
    if (wifi_channel_survey_error == ESP_OK) {
        for (uint8_t row = 0U; row < UI_WIFI_VISIBLE_ROWS &&
            row < wifi_channel_survey_status.result_count; ++row) {
            if (c5_spi_link_wifi_channel_survey_result(
                wifi_channel_survey_status.generation, row,
                &wifi_channel_survey_rows[row]) != ESP_OK) break;
        }
    }
    wifi_channel_survey_last_poll = 0U;
}

static void wifi_channel_survey_refresh(uint32_t now_ms)
{
    if (!wifi_channel_survey_attempted) {
        if (wifi_channel_survey_last_poll &&
            now_ms - wifi_channel_survey_last_poll < 700U) return;
        wifi_channel_survey_start_ui();
        if (!wifi_channel_survey_attempted) return;
    }
    if (now_ms - wifi_channel_survey_last_poll < 700U ||
        (wifi_channel_survey_error != ESP_OK &&
         wifi_channel_survey_status.state != CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING)) return;
    wifi_channel_survey_last_poll = now_ms;
    wifi_channel_survey_error = c5_spi_link_wifi_channel_survey_status(
        &wifi_channel_survey_status);
    if (wifi_channel_survey_error != ESP_OK ||
        wifi_channel_survey_status.generation == 0U) return;
    memset(wifi_channel_survey_rows, 0, sizeof(wifi_channel_survey_rows));
    for (uint8_t row = 0U; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        uint8_t index = (uint8_t)(wifi_channel_survey_page * UI_WIFI_VISIBLE_ROWS + row);
        if (index >= wifi_channel_survey_status.result_count) break;
        wifi_channel_survey_error = c5_spi_link_wifi_channel_survey_result(
            wifi_channel_survey_status.generation, index,
            &wifi_channel_survey_rows[row]);
        if (wifi_channel_survey_error != ESP_OK) break;
    }
}

static uint16_t packet_monitor_count(
    const cerberus_wifi_packet_monitor_sample_payload_t *sample,
    unsigned lane)
{
    return lane == 0U ? sample->beacons : lane == 1U ?
        sample->disconnections : sample->probe_requests;
}

static esp_err_t draw_packet_monitor_lane(unsigned lane)
{
    static const char *const labels[] = {
        "BEACONS", "DEAUTH + DISASSOC", "PROBE REQUESTS",
    };
    static const uint16_t colors[] = {UI_GOOD, UI_BAD, 0x3DDF};
    uint16_t y = (uint16_t)(96U + lane * 75U);
    uint16_t peak = 1U, current = 0U;
    uint16_t latest = packet_monitor_status.latest_sequence;
    for (unsigned column = 0U; column < 32U; ++column) {
        uint16_t sequence = (uint16_t)(latest - 31U + column);
        const cerberus_wifi_packet_monitor_sample_payload_t *sample =
            &packet_monitor_samples[sequence % CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
        if (sample->generation != packet_monitor_status.generation ||
            sample->sequence != sequence) continue;
        uint16_t count = packet_monitor_count(sample, lane);
        if (count > peak) peak = count;
        if (column == 31U) current = count;
    }
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304, 68,
        UI_PANEL), TAG, "packet monitor panel");
    char line[46];
    snprintf(line, sizeof(line), "%s  %u / 200MS  PEAK %u",
        labels[lane], (unsigned)current, (unsigned)peak);
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 4U, line,
        UI_TEXT, UI_PANEL, 1), TAG, "packet monitor lane label");
    for (unsigned column = 0U; column < 32U; ++column) {
        uint16_t sequence = (uint16_t)(latest - 31U + column);
        const cerberus_wifi_packet_monitor_sample_payload_t *sample =
            &packet_monitor_samples[sequence % CERBERUS_WIFI_PACKET_MONITOR_SAMPLES];
        if (sample->generation != packet_monitor_status.generation ||
            sample->sequence != sequence) continue;
        uint16_t count = packet_monitor_count(sample, lane);
        if (count == 0U) continue;
        uint16_t height = (uint16_t)((uint32_t)count * 36U / peak);
        if (height == 0U) height = 1U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
            (uint16_t)(52U + column * 8U),
            (uint16_t)(y + 60U - height), 6U, height, colors[lane]),
            TAG, "packet monitor bar");
    }
    return ESP_OK;
}

static esp_err_t draw_wifi_packet_monitor(void)
{
    char line[72];
    snprintf(line, sizeof(line), "CH %u  %s  PACKET MONITOR",
        (unsigned)packet_monitor_channel,
        packet_monitor_channel <= 14U ? "2.4 GHZ" : "5 GHZ");
    ESP_RETURN_ON_ERROR(draw_centered(51, line, UI_GOOD, 1), TAG,
        "packet monitor heading");
    if (!packet_monitor_attempted &&
        packet_monitor_error == ESP_ERR_INVALID_STATE)
        snprintf(line, sizeof(line), "WAITING FOR C5 RADIO");
    else if (packet_monitor_error != ESP_OK)
        snprintf(line, sizeof(line), "C5: %s", esp_err_to_name(packet_monitor_error));
    else if (packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING)
        snprintf(line, sizeof(line), "%u SAMPLES  %luMS ELAPSED",
            (unsigned)packet_monitor_status.stored_count,
            (unsigned long)packet_monitor_status.elapsed_ms);
    else
        snprintf(line, sizeof(line), "STOPPED / TAP RESTART");
    ESP_RETURN_ON_ERROR(draw_centered(72, line,
        packet_monitor_error == ESP_OK ? UI_MUTED : UI_BAD, 1), TAG,
        "packet monitor status");
    for (unsigned lane = 0U; lane < 3U; ++lane)
        ESP_RETURN_ON_ERROR(draw_packet_monitor_lane(lane), TAG,
            "packet monitor graph");
    ESP_RETURN_ON_ERROR(draw_centered(326,
        "CHANNEL TRAFFIC / NO BSSID FILTER", UI_MUTED, 1), TAG,
        "packet monitor scope");
    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42,
        "CH -"), TAG, "packet monitor previous channel");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42,
        "CH +"), TAG, "packet monitor next channel");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42,
        packet_monitor_status.state == CERBERUS_WIFI_PACKET_MONITOR_RUNNING ?
            "SURVEY" : "RESTART"), TAG, "packet monitor survey/restart");
    return draw_small_button(236, UI_WIFI_CONTROLS_Y, 76, 42,
        packet_monitor_from_target ? "BACK AP" : "AP LIST");
}

static esp_err_t draw_wifi_channel_survey_live(void)
{
    char line[72];
    uint8_t pages = (uint8_t)((wifi_channel_survey_status.result_count +
        UI_WIFI_VISIBLE_ROWS - 1U) / UI_WIFI_VISIBLE_ROWS);
    if (pages == 0U) pages = 1U;
    snprintf(line, sizeof(line), "%s  LIVE  PAGE %u/%u",
        wifi_mode == UI_WIFI_MODE_CHANNELS_24 ? "2.4 GHZ" : "5 GHZ",
        (unsigned)(wifi_channel_survey_page + 1U), (unsigned)pages);
    ESP_RETURN_ON_ERROR(draw_centered(51, line, UI_GOOD, 1), TAG,
        "Wi-Fi live channel heading failed");
    if (!wifi_channel_survey_attempted &&
        wifi_channel_survey_error == ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(draw_centered(65, "WAITING FOR C5 RADIO",
            UI_MUTED, 1), TAG, "Wi-Fi live channel wait failed");
    } else if (wifi_channel_survey_error != ESP_OK) {
        snprintf(line, sizeof(line), "C5 SURVEY %s",
            esp_err_to_name(wifi_channel_survey_error));
        ESP_RETURN_ON_ERROR(draw_centered(65, line, UI_BAD, 1), TAG,
            "Wi-Fi live channel error failed");
    } else if (wifi_channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_ERROR) {
        snprintf(line, sizeof(line), "C5 RADIO %s",
            esp_err_to_name((esp_err_t)wifi_channel_survey_status.error_code));
        ESP_RETURN_ON_ERROR(draw_centered(65, line, UI_BAD, 1), TAG,
            "Wi-Fi live channel radio error failed");
    } else if (wifi_channel_survey_status.state == CERBERUS_WIFI_CHANNEL_SURVEY_IDLE) {
        ESP_RETURN_ON_ERROR(draw_centered(65, "PAUSED / TAP BAND TO RESTART",
            UI_MUTED, 1), TAG, "Wi-Fi live channel paused failed");
    } else {
        snprintf(line, sizeof(line), "100MS PASSIVE  SWEEPS %u  TOTAL %lu",
            (unsigned)wifi_channel_survey_status.total_sweeps,
            (unsigned long)wifi_channel_survey_status.total_packets);
        ESP_RETURN_ON_ERROR(draw_centered(65, line, UI_MUTED, 1), TAG,
            "Wi-Fi live channel summary failed");
    }
    for (uint8_t row = 0U; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        uint8_t index = (uint8_t)(wifi_channel_survey_page * UI_WIFI_VISIBLE_ROWS + row);
        if (index >= wifi_channel_survey_status.result_count) break;
        uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
        const cerberus_wifi_channel_survey_result_payload_t *result =
            &wifi_channel_survey_rows[row];
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304,
            UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL), TAG, "Wi-Fi live channel row failed");
        snprintf(line, sizeof(line), "CH %u", (unsigned)result->channel);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 5U, line,
            UI_TEXT, UI_PANEL, 1), TAG, "Wi-Fi live channel number failed");
        if (result->seen) {
            snprintf(line, sizeof(line), "%u PKT/S  AVG %dDB",
                (unsigned)result->rate_per_sec, result->average_rssi);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(78, y + 5U, line,
                UI_GOOD, UI_PANEL, 1), TAG, "Wi-Fi live channel rate failed");
            snprintf(line, sizeof(line), "TOTAL %lu  LAST %u  BEST %dDB",
                (unsigned long)result->total_packets,
                (unsigned)result->last_packets, result->strongest_rssi);
        } else {
            snprintf(line, sizeof(line), "%s",
                wifi_channel_survey_status.total_sweeps > 0U
                    ? "UNAVAILABLE IN REGION / DRIVER" : "WAITING FOR 100MS DWELL");
        }
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(78, y + 23U, line,
            UI_MUTED, UI_PANEL, 1), TAG, "Wi-Fi live channel count failed");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42,
        "2.4 GHZ"), TAG, "Wi-Fi 2.4 button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42,
        "5 GHZ"), TAG, "Wi-Fi 5 button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42,
        "NEXT"), TAG, "Wi-Fi channel next failed");
    return draw_small_button(236, UI_WIFI_CONTROLS_Y, 76, 42, "AP LIST");
}

static esp_err_t draw_wifi_channel_occupancy(void)
{
    if (c5_status.firmware_version >= CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE)
        return draw_wifi_channel_survey_live();
    char line[64];
    /* Channel history belongs to completed sweeps. A new C5 generation must
       not clear it while scanning or when cancellation returns the C5 to IDLE. */
    if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE &&
        (wifi_channel_generation != wifi_scan_status.generation || wifi_channel_error != ESP_OK)) {
        wifi_channel_error = rebuild_wifi_channel_summary();
    }
    if (wifi_channel_history_sample_count == 0U) {
        ESP_RETURN_ON_ERROR(
            draw_centered(150, "WAITING FOR CHANNEL OBSERVATIONS", UI_MUTED, 1),
            TAG,
            "Wi-Fi channel summary unavailable failed");
        if (wifi_channel_error != ESP_OK && wifi_channel_error != ESP_ERR_INVALID_STATE) {
            snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(wifi_channel_error));
        } else {
            snprintf(line, sizeof(line), "%s", live_scan_paused() ?
                "TAP SCAN TO COLLECT A SWEEP" : "ONE SWEEP / RESULTS APPEAR WHEN COMPLETE");
        }
        ESP_RETURN_ON_ERROR(draw_centered(184, line, UI_MUTED, 1), TAG, "Wi-Fi channel error failed");
    } else {
        uint8_t top_channels[UI_WIFI_VISIBLE_ROWS] = {0};
        uint16_t band_ap_observations = 0;
        uint16_t active_channel_count = 0;
        uint16_t maximum_observations = 0;
        for (uint16_t channel = 1; channel < 256U; ++channel) {
            uint16_t observations = wifi_channel_history_total((uint8_t)channel);
            if (!wifi_channel_in_selected_band(channel) || observations == 0U) {
                continue;
            }
            band_ap_observations += observations;
            ++active_channel_count;
            if (observations > maximum_observations) {
                maximum_observations = observations;
            }
        }
        for (size_t slot = 0; slot < UI_WIFI_VISIBLE_ROWS; ++slot) {
            uint8_t best_channel = 0;
            for (uint16_t channel = 1; channel < 256U; ++channel) {
                uint16_t observations = wifi_channel_history_total((uint8_t)channel);
                if (!wifi_channel_in_selected_band(channel) || observations == 0U) {
                    continue;
                }
                bool already_selected = false;
                for (size_t previous = 0; previous < slot; ++previous) {
                    if (top_channels[previous] == channel) {
                        already_selected = true;
                        break;
                    }
                }
                uint16_t best_observations = wifi_channel_history_total(best_channel);
                if (!already_selected && (best_channel == 0U ||
                    observations > best_observations ||
                    (observations == best_observations &&
                        wifi_channel_history_best_rssi((uint8_t)channel) >
                            wifi_channel_history_best_rssi(best_channel)))) {
                    best_channel = (uint8_t)channel;
                }
            }
            top_channels[slot] = best_channel;
        }

        snprintf(
            line,
            sizeof(line),
            "%s %u/%u AVG %u.%u APS %u CH",
            wifi_mode == UI_WIFI_MODE_CHANNELS_24 ? "2.4 GHZ" : "5 GHZ",
            (unsigned)wifi_channel_history_sample_count,
            (unsigned)UI_WIFI_HISTORY_SAMPLES,
            wifi_channel_history_sample_count == 0U
                ? 0U
                : (unsigned)(band_ap_observations / wifi_channel_history_sample_count),
            wifi_channel_history_sample_count == 0U
                ? 0U
                : (unsigned)((band_ap_observations * 10U / wifi_channel_history_sample_count) % 10U),
            (unsigned)active_channel_count);
        ESP_RETURN_ON_ERROR(draw_centered(54, line, UI_GOOD, 1), TAG, "Wi-Fi channel summary failed");
        snprintf(line, sizeof(line), "LATEST %lus AGO / NOT AIRTIME USE",
            (unsigned long)((uint32_t)(esp_timer_get_time() / 1000000LL) - wifi_observed_at_seconds));
        ESP_RETURN_ON_ERROR(
            draw_centered(68, line, UI_MUTED, 1),
            TAG,
            "Wi-Fi channel scope failed");

        if (active_channel_count == 0U) {
            ESP_RETURN_ON_ERROR(
                draw_centered(180, "NO APS IN THIS BAND", UI_MUTED, 2),
                TAG,
                "Wi-Fi channel empty failed");
        } else {
            for (size_t row = 0; row < UI_WIFI_VISIBLE_ROWS && top_channels[row] != 0U; ++row) {
                uint8_t channel = top_channels[row];
                uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(8, y, 304, UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL),
                    TAG,
                    "Wi-Fi channel row failed");
                snprintf(line, sizeof(line), "CH %u", channel);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(14, y + 5U, line, UI_TEXT, UI_PANEL, 1),
                    TAG,
                    "Wi-Fi channel label failed");
                snprintf(
                    line,
                    sizeof(line),
                    "%u.%u AVG  %u/%u SEEN  BEST %dDB",
                    wifi_channel_history_sample_count == 0U
                        ? 0U
                        : (unsigned)(wifi_channel_history_total(channel) /
                            wifi_channel_history_sample_count),
                    wifi_channel_history_sample_count == 0U
                        ? 0U
                        : (unsigned)((wifi_channel_history_total(channel) * 10U /
                            wifi_channel_history_sample_count) % 10U),
                    (unsigned)wifi_channel_history_seen(channel),
                    (unsigned)wifi_channel_history_sample_count,
                    wifi_channel_history_best_rssi(channel));
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(86, y + 5U, line, UI_MUTED, UI_PANEL, 1),
                    TAG,
                    "Wi-Fi channel metrics failed");
                uint16_t bar_width = maximum_observations == 0U
                    ? 0U
                    : (uint16_t)(286U * wifi_channel_history_total(channel) /
                        maximum_observations);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(14, y + 26U, bar_width, 9U, UI_GOOD),
                    TAG,
                    "Wi-Fi channel bar failed");
            }
        }
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42, "2.4 GHZ"), TAG, "Wi-Fi 2.4 button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42, "5 GHZ"), TAG, "Wi-Fi 5 button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42, "RESET"), TAG, "Wi-Fi history reset failed");
    return draw_small_button(236, UI_WIFI_CONTROLS_Y, 76, 42, "AP LIST");
}

typedef struct {
    bool valid;
    uint8_t address[6];
    uint8_t source;
    int64_t checked_at_ms;
    recon_db_result_t result;
    uint32_t storage_epoch;
    char name[RECON_DB_VENDOR_SIZE];
} ui_selected_vendor_cache_t;

/* Only one selected detail page is rendered at a time. Public BLE types 0/2
 * and Wi-Fi key 4 remain distinct even when their address bytes are equal. */
static ui_selected_vendor_cache_t vendor_cache;

static void refresh_selected_vendor(bool ble, const uint8_t address[6], uint8_t address_type,
    int64_t now_ms)
{
    uint8_t public_mac[6];
    if (address == NULL || (ble && address_type > 3U)) {
        memset(&vendor_cache, 0, sizeof(vendor_cache));
        vendor_cache.result = RECON_DB_INVALID;
        return;
    }
    if (ble) {
        if (!recon_db_ble_public_mac(address, address_type, public_mac)) {
            memset(&vendor_cache, 0, sizeof(vendor_cache));
            vendor_cache.result = RECON_DB_PRIVATE_ADDRESS;
            return;
        }
    } else {
        memcpy(public_mac, address, sizeof(public_mac));
    }
    if (public_mac[0] & 3U) {
        /* Address type/bit rules do not depend on the card or its ownership. */
        memset(&vendor_cache, 0, sizeof(vendor_cache));
        vendor_cache.result = RECON_DB_PRIVATE_ADDRESS;
        return;
    }

    const uint8_t source = ble ? address_type : 4U;
    uint32_t storage_epoch = 0U;
    if (cerberus_storage_try_get_read_epoch(&storage_epoch) != ESP_OK || !storage_epoch) {
        memset(&vendor_cache, 0, sizeof(vendor_cache));
        vendor_cache.result = RECON_DB_UNAVAILABLE;
    } else if (!vendor_cache.valid || vendor_cache.storage_epoch != storage_epoch ||
        vendor_cache.source != source ||
        memcmp(vendor_cache.address, address, sizeof(vendor_cache.address)) != 0 ||
        now_ms < 0 || vendor_cache.checked_at_ms < 0 || now_ms < vendor_cache.checked_at_ms ||
        (uint64_t)now_ms - (uint64_t)vendor_cache.checked_at_ms >= 30000U) {
        vendor_cache.valid = false;
        vendor_cache.name[0] = '\0';
        /* Cold lookup keeps the existing recon-store contract. Only warm
         * detail redraws avoid its header reads and blocking storage locks. */
        vendor_cache.result = recon_store_vendor(public_mac, vendor_cache.name);
        if (vendor_cache.result == RECON_DB_FOUND || vendor_cache.result == RECON_DB_UNKNOWN) {
            uint32_t finished_epoch = 0U;
            if (cerberus_storage_try_get_read_epoch(&finished_epoch) != ESP_OK ||
                finished_epoch != storage_epoch) {
                vendor_cache.result = RECON_DB_UNAVAILABLE;
            } else {
                memcpy(vendor_cache.address, address, sizeof(vendor_cache.address));
                vendor_cache.source = source;
                vendor_cache.checked_at_ms = now_ms;
                vendor_cache.storage_epoch = storage_epoch;
                vendor_cache.valid = now_ms >= 0;
            }
        }
        /* Retry transient/invalid results on the next draw; never keep dirty
         * lookup text or the previous selected device's vendor label. */
        if (vendor_cache.result != RECON_DB_FOUND) vendor_cache.name[0] = '\0';
    }
}

static esp_err_t draw_wifi_detail(void)
{
    char line[72];
    char identifier[24];
    if (wifi_analysis_error != ESP_OK || wifi_analysis_generation != wifi_scan_status.generation) {
        wifi_analysis_error = analyze_selected_wifi_ssid();
    }

    const char *ssid = wifi_selected_result.ssid[0] != '\0'
        ? wifi_selected_result.ssid
        : "<HIDDEN SSID>";
    ESP_RETURN_ON_ERROR(draw_centered(54, ssid, UI_TEXT, 1), TAG, "Wi-Fi detail SSID failed");
    cerberus_privacy_format_wifi_identifier(
        wifi_selected_result.bssid,
        privacy_addresses_masked(),
        identifier,
        sizeof(identifier));
    ESP_RETURN_ON_ERROR(draw_centered(78, identifier, UI_MUTED, 1), TAG, "Wi-Fi detail BSSID failed");
    snprintf(
        line,
        sizeof(line),
        "CH %u  %s  %dDB  %s",
        wifi_selected_result.channel,
        wifi_selected_result.channel > 14U ? "5 GHZ" : "2.4 GHZ",
        wifi_selected_result.rssi,
        wifi_security_text(wifi_selected_result.security));
    ESP_RETURN_ON_ERROR(draw_centered(102, line, UI_GOOD, 1), TAG, "Wi-Fi detail metadata failed");

    cerberus_wifi_trust_state_t trust_state = cerberus_settings_wifi_trust_state(
        wifi_selected_result.ssid,
        wifi_selected_result.bssid,
        wifi_selected_result.security);
    const char *trust_status;
    uint16_t trust_color;
    if (!ui_config.settings_available) {
        trust_status = "TRUST STORAGE UNAVAILABLE";
        trust_color = UI_BAD;
    } else if (wifi_selected_result.ssid[0] == '\0') {
        trust_status = "HIDDEN SSID - BASELINE DISABLED";
        trust_color = UI_MUTED;
    } else if (trust_state == CERBERUS_WIFI_TRUST_EXACT) {
        trust_status = "TRUSTED EXACT BASELINE";
        trust_color = UI_GOOD;
    } else if (trust_state == CERBERUS_WIFI_TRUST_MISMATCH) {
        trust_status = "TRUSTED SSID/BSSID MISMATCH";
        trust_color = UI_BAD;
    } else {
        trust_status = "NOT IN LOCAL TRUST BASELINE";
        trust_color = UI_MUTED;
    }
    ESP_RETURN_ON_ERROR(draw_centered(118, trust_status, trust_color, 1), TAG, "Wi-Fi trust state failed");

    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(8, 130, 304, 188, UI_PANEL),
        TAG,
        "Wi-Fi review panel failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_draw_text(14, 140, "PASSIVE SSID REVIEW", UI_TEXT, UI_PANEL, 1),
        TAG,
        "Wi-Fi review title failed");

    if (wifi_analysis_error != ESP_OK) {
        const char *message = wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ?
            "FINISHING SWEEP / YOUR SELECTION IS HELD" : "RETURN TO LIVE VIEW FOR A FRESH COMPARISON";
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, 190, message, UI_MUTED, UI_PANEL, 1), TAG, "Wi-Fi analysis wait");
    } else if (wifi_selected_result.ssid[0] == '\0') {
        ESP_RETURN_ON_ERROR(
            cerberus_display_draw_text(14, 172, "HIDDEN SSIDS CANNOT BE GROUPED", UI_MUTED, UI_PANEL, 1),
            TAG,
            "Wi-Fi hidden analysis failed");
        ESP_RETURN_ON_ERROR(
            cerberus_display_draw_text(14, 196, "THIS BSSID IS SHOWN INDIVIDUALLY", UI_MUTED, UI_PANEL, 1),
            TAG,
            "Wi-Fi hidden guidance failed");
    } else {
        snprintf(
            line,
            sizeof(line),
            "OBSERVED ON %u BSSID%s  2G:%u  5G:%u",
            wifi_ssid_analysis.total_bssids,
            wifi_ssid_analysis.total_bssids == 1U ? "" : "S",
            wifi_ssid_analysis.count_24_ghz,
            wifi_ssid_analysis.count_5_ghz);
        ESP_RETURN_ON_ERROR(
            cerberus_display_draw_text(14, 164, line, UI_MUTED, UI_PANEL, 1),
            TAG,
            "Wi-Fi BSSID count failed");
        uint8_t security_modes = count_wifi_security_modes(wifi_ssid_analysis.security_mask);
        if (security_modes > 1U) {
            snprintf(
                line,
                sizeof(line),
                "SECURITY MODES DIFFER (%u) - REVIEW",
                (unsigned)security_modes);
        } else {
            snprintf(
                line,
                sizeof(line),
                "SECURITY MODE CONSISTENT (%s)",
                wifi_security_text(wifi_selected_result.security));
        }
        ESP_RETURN_ON_ERROR(
            cerberus_display_draw_text(
                14,
                184,
                line,
                security_modes > 1U ? UI_BAD : UI_GOOD,
                UI_PANEL,
                1),
            TAG,
            "Wi-Fi security review failed");
        snprintf(
            line,
            sizeof(line),
            "SIGNAL RANGE %d TO %dDB",
            wifi_ssid_analysis.weakest_rssi,
            wifi_ssid_analysis.strongest_rssi);
        ESP_RETURN_ON_ERROR(
            cerberus_display_draw_text(14, 204, line, UI_MUTED, UI_PANEL, 1),
            TAG,
            "Wi-Fi signal range failed");

        if (wifi_ssid_analysis.peer_count == 0U) {
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, 236, "NO OTHER BSSID IN STORED RESULTS", UI_MUTED, UI_PANEL, 1),
                TAG,
                "Wi-Fi peer empty failed");
        } else {
            for (uint8_t index = 0; index < wifi_ssid_analysis.peer_count && index < 3U; ++index) {
                const cerberus_c5_wifi_result_t *peer = &wifi_ssid_analysis.peers[index];
                cerberus_privacy_format_wifi_identifier(
                    peer->bssid,
                    privacy_addresses_masked(),
                    identifier,
                    sizeof(identifier));
                snprintf(
                    line,
                    sizeof(line),
                    "%s CH%u %dDB %s",
                    identifier,
                    peer->channel,
                    peer->rssi,
                    wifi_security_text(peer->security));
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(
                        14,
                        228U + (uint16_t)index * 22U,
                        line,
                        UI_TEXT,
                        UI_PANEL,
                        1),
                    TAG,
                    "Wi-Fi peer row failed");
            }
        }
    }

    if (wifi_trust_error != ESP_OK) {
        snprintf(line, sizeof(line), "TRUST UPDATE FAILED %s", esp_err_to_name(wifi_trust_error));
        ESP_RETURN_ON_ERROR(draw_centered(334, line, UI_BAD, 1), TAG, "Wi-Fi trust error failed");
    } else {
        ESP_RETURN_ON_ERROR(
            draw_centered(334, "MULTI-BSSID IS NORMAL ON MESH / ENTERPRISE", UI_MUTED, 1),
            TAG,
            "Wi-Fi review caveat failed");
    }
    refresh_selected_vendor(false, wifi_selected_result.bssid, 0U, esp_timer_get_time() / 1000LL);
    if (vendor_cache.result == RECON_DB_FOUND) snprintf(line, sizeof(line), "VENDOR %.40s", vendor_cache.name);
    else snprintf(line, sizeof(line), "%s", vendor_cache.result == RECON_DB_PRIVATE_ADDRESS ?
        "LOCAL ADDRESS / NO VENDOR ATTRIBUTION" : vendor_cache.result == RECON_DB_UNKNOWN ?
        "VENDOR NOT IN SD DATABASE" : "SD VENDOR DATABASE NOT READY");
    ESP_RETURN_ON_ERROR(draw_centered(354, line, UI_MUTED, 1), TAG, "Wi-Fi vendor");

    const char *trust_label = !ui_config.settings_available
        ? "NVS UNAVAILABLE"
        : (wifi_selected_result.ssid[0] == '\0'
            ? "HIDDEN SSID"
            : (trust_state == CERBERUS_WIFI_TRUST_EXACT
                ? "REMOVE TRUST"
                : (trust_state == CERBERUS_WIFI_TRUST_MISMATCH ? "UPDATE TRUST" : "TRUST THIS AP")));
    ESP_RETURN_ON_ERROR(draw_small_button(8, 370, 144, 42, trust_label), TAG, "Wi-Fi trust button failed");
    bool can_connect = wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE ||
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING;
    return cerberus_ui_widget_draw_button((cerberus_ui_rect_t){168, 370, 144, 42},
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING
            ? "CONNECTION" : can_connect ? "CONNECT" : "FINISHING SWEEP", 1, can_connect, UI_WIDGET_THEME);
}

static void *ui_job_allocate(size_t bytes)
{
    void *p=heap_caps_calloc(1,bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    return p?p:calloc(1,bytes);
}
#define CERBERUS_UI_JOB_ALLOC(n) ui_job_allocate(n)
#include "wifi_targets.h"
#include "network_scan_ui.h"
#include "wifi_station_ui.h"
#include "wifi_ap_ui.h"
#include "radio_lab_ui.h"
#include "wifi_capture_ui.h"
#include "wifi_multissid_ui.h"
#include "wifi_pinescan_ui.h"
#include "csi_live_ui.h"
#include "wifi_relation_ui.h"
#include "wifi_target_ui.h"
#include "wifi_portal_ui.h"

static esp_err_t draw_wifi_connection(void)
{
    char line[72];
    const char *state = "IDLE";
    uint16_t color = UI_MUTED;
    switch (wifi_connection_status.state) {
    case CERBERUS_WIFI_CONNECTION_STAGING: state = "PREPARING"; color = UI_MUTED; break;
    case CERBERUS_WIFI_CONNECTION_CONNECTING: state = "CONNECTING"; color = UI_GOOD; break;
    case CERBERUS_WIFI_CONNECTION_CONNECTED: state = "CONNECTED"; color = UI_GOOD; break;
    case CERBERUS_WIFI_CONNECTION_DISCONNECTING: state = "DISCONNECTING"; color = UI_MUTED; break;
    case CERBERUS_WIFI_CONNECTION_ERROR: state = "CONNECTION FAILED"; color = UI_BAD; break;
    default: break;
    }
    ESP_RETURN_ON_ERROR(draw_centered(76, wifi_connection_target_valid ? wifi_connection_target.ssid : "CURRENT CONNECTION", UI_TEXT, 1), TAG, "Wi-Fi connection SSID failed");
    ESP_RETURN_ON_ERROR(draw_centered(126, state, color, 2), TAG, "Wi-Fi connection state failed");
    if (wifi_connection_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(wifi_connection_error));
        ESP_RETURN_ON_ERROR(draw_centered(172, line, UI_BAD, 1), TAG, "Wi-Fi connection link error failed");
    } else if (wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_ERROR) {
        if (wifi_connection_status.error_code == 202) {
            snprintf(line, sizeof(line), "AUTH REJECTED - PASSWORD OR WPA MODE");
        } else {
            snprintf(line, sizeof(line), "C5 REASON %ld", (long)wifi_connection_status.error_code);
        }
        ESP_RETURN_ON_ERROR(draw_centered(172, line, UI_BAD, 1), TAG, "Wi-Fi connection error failed");
    }
    if (wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED) {
        snprintf(line, sizeof(line), "IP %u.%u.%u.%u",
            wifi_connection_status.ipv4[0], wifi_connection_status.ipv4[1],
            wifi_connection_status.ipv4[2], wifi_connection_status.ipv4[3]);
        ESP_RETURN_ON_ERROR(draw_centered(206, line, UI_GOOD, 1), TAG, "Wi-Fi IP failed");
        snprintf(line, sizeof(line), "MASK %u.%u.%u.%u",
            wifi_connection_status.netmask[0], wifi_connection_status.netmask[1],
            wifi_connection_status.netmask[2], wifi_connection_status.netmask[3]);
        ESP_RETURN_ON_ERROR(draw_centered(228, line, UI_MUTED, 1), TAG, "Wi-Fi mask failed");
        snprintf(line, sizeof(line), "GW %u.%u.%u.%u  CH%u  %dDB",
            wifi_connection_status.gateway[0], wifi_connection_status.gateway[1],
            wifi_connection_status.gateway[2], wifi_connection_status.gateway[3],
            wifi_connection_status.channel, wifi_connection_status.rssi);
        ESP_RETURN_ON_ERROR(draw_centered(250, line, UI_MUTED, 1), TAG, "Wi-Fi gateway failed");
        if (wifi_dns_error == ESP_OK &&
            (wifi_dns_status.valid_flags & CERBERUS_WIFI_DNS_PRIMARY_VALID) != 0U) {
            snprintf(line, sizeof(line), "DNS %u.%u.%u.%u",
                wifi_dns_status.primary[0], wifi_dns_status.primary[1],
                wifi_dns_status.primary[2], wifi_dns_status.primary[3]);
        } else {
            snprintf(line, sizeof(line), "DNS NOT PROVIDED");
        }
        ESP_RETURN_ON_ERROR(draw_centered(272, line, UI_MUTED, 1), TAG, "Wi-Fi DNS failed");
    }
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){8, 302, 304, 40},
        "PORT / SSH / TELNET SCAN", 1,
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED &&
        (c5_status.capabilities & CERBERUS_C5_CAP_NETWORK_SCAN), UI_WIDGET_THEME), TAG, "network scan button");
    ESP_RETURN_ON_ERROR(draw_small_button(8, 360, 144, 48,
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING ? "CANCEL" :
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING
            ? "DISCONNECT" : "BACK TO AP"), TAG, "Wi-Fi disconnect button failed");
    return draw_small_button(168, 360, 144, 48, "NETWORK TESTS");
}

static const char *network_test_result(uint8_t test)
{
    if (network_diagnostics_status.active_test == test) return "RUNNING";
    if ((network_diagnostics_status.completed_mask & test) == 0U) return "WAITING";
    return (network_diagnostics_status.passed_mask & test) != 0U ? "PASS" : "FAIL";
}

static uint16_t network_test_color(uint8_t test)
{
    if (network_diagnostics_status.active_test == test) return UI_GOOD;
    if ((network_diagnostics_status.completed_mask & test) == 0U) return UI_MUTED;
    return (network_diagnostics_status.passed_mask & test) != 0U ? UI_GOOD : UI_BAD;
}

static esp_err_t draw_wifi_diagnostics(void)
{
    char line[80];
    ESP_RETURN_ON_ERROR(draw_centered(66, "BOUNDED CONNECTIVITY TESTS", UI_TEXT, 1), TAG,
        "network diagnostics title failed");
    snprintf(line, sizeof(line), "DNS example.com     %s",
        network_test_result(CERBERUS_NETWORK_TEST_DNS));
    ESP_RETURN_ON_ERROR(draw_centered(108, line,
        network_test_color(CERBERUS_NETWORK_TEST_DNS), 1), TAG, "DNS test failed");
    snprintf(line, sizeof(line), "DEFAULT ROUTE       %s",
        network_test_result(CERBERUS_NETWORK_TEST_ROUTE));
    ESP_RETURN_ON_ERROR(draw_centered(146, line,
        network_test_color(CERBERUS_NETWORK_TEST_ROUTE), 1), TAG, "route test failed");
    snprintf(line, sizeof(line), "GATEWAY PING        %s  %uMS",
        network_test_result(CERBERUS_NETWORK_TEST_GATEWAY_PING),
        network_diagnostics_status.gateway_ping_ms);
    ESP_RETURN_ON_ERROR(draw_centered(184, line,
        network_test_color(CERBERUS_NETWORK_TEST_GATEWAY_PING), 1), TAG, "ping test failed");
    snprintf(line, sizeof(line), "NTP UTC TIME        %s",
        network_test_result(CERBERUS_NETWORK_TEST_NTP));
    ESP_RETURN_ON_ERROR(draw_centered(222, line,
        network_test_color(CERBERUS_NETWORK_TEST_NTP), 1), TAG, "NTP test failed");
    snprintf(line, sizeof(line), "HTTPS CERT + HEAD   %s  HTTP %u",
        network_test_result(CERBERUS_NETWORK_TEST_HTTPS), network_diagnostics_status.https_status);
    ESP_RETURN_ON_ERROR(draw_centered(260, line,
        network_test_color(CERBERUS_NETWORK_TEST_HTTPS), 1), TAG, "HTTPS test failed");
    if (network_diagnostics_error != ESP_OK) {
        snprintf(line, sizeof(line), "C5 LINK %s", esp_err_to_name(network_diagnostics_error));
        ESP_RETURN_ON_ERROR(draw_centered(306, line, UI_BAD, 1), TAG, "diagnostics link error failed");
    } else if (network_diagnostics_status.error_code != ESP_OK) {
        snprintf(line, sizeof(line), "FIRST ERROR %s",
            esp_err_to_name(network_diagnostics_status.error_code));
        ESP_RETURN_ON_ERROR(draw_centered(306, line, UI_BAD, 1), TAG, "diagnostics error failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(306, "NO PAYLOADS OR CREDENTIALS CAPTURED", UI_MUTED, 1),
            TAG, "diagnostics scope failed");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, 360, 144, 48, "BACK"), TAG,
        "diagnostics back failed");
    return draw_small_button(168, 360, 144, 48,
        network_diagnostics_status.state == CERBERUS_NETWORK_DIAGNOSTICS_RUNNING
            ? "TESTING..." : "MDNS SERVICES");
}

static const char *mdns_service_text(uint8_t service_type)
{
    switch (service_type) {
    case CERBERUS_MDNS_SERVICE_HTTP: return "HTTP";
    case CERBERUS_MDNS_SERVICE_HTTPS: return "HTTPS";
    case CERBERUS_MDNS_SERVICE_SSH: return "SSH";
    case CERBERUS_MDNS_SERVICE_WORKSTATION: return "WORKSTATION";
    case CERBERUS_MDNS_SERVICE_PRINTER: return "PRINTER";
    case CERBERUS_MDNS_SERVICE_IPP: return "IPP";
    case CERBERUS_MDNS_SERVICE_AIRPLAY: return "AIRPLAY";
    default: return "SERVICE";
    }
}

static esp_err_t load_mdns_results(void)
{
    mdns_result_count = 0U;
    if (mdns_page_offset >= mdns_scan_status.result_count) {
        mdns_page_offset = mdns_scan_status.result_count == 0U
            ? 0U
            : ((mdns_scan_status.result_count - 1U) / UI_WIFI_VISIBLE_ROWS) * UI_WIFI_VISIBLE_ROWS;
    }
    for (size_t row = 0; row < UI_WIFI_VISIBLE_ROWS; ++row) {
        size_t index = mdns_page_offset + row;
        if (index >= mdns_scan_status.result_count) break;
        esp_err_t err = c5_spi_link_get_mdns_result(mdns_scan_status.generation,
            (uint8_t)index, &mdns_results[mdns_result_count]);
        if (err != ESP_OK) return err;
        mdns_results[mdns_result_count].hostname[CERBERUS_MDNS_HOSTNAME_LENGTH - 1U] = '\0';
        make_ssid_display_safe(mdns_results[mdns_result_count].hostname);
        ++mdns_result_count;
    }
    return ESP_OK;
}

static esp_err_t draw_wifi_mdns(void)
{
    char line[64];
    snprintf(line, sizeof(line), "LOCAL MDNS SERVICES  %u/%u",
        (unsigned)mdns_scan_status.queried_type_count, 7U);
    ESP_RETURN_ON_ERROR(draw_centered(62, line, UI_TEXT, 1), TAG, "mDNS title failed");

    if (mdns_scan_error != ESP_OK) {
        snprintf(line, sizeof(line), "C5 LINK %s", esp_err_to_name(mdns_scan_error));
        ESP_RETURN_ON_ERROR(draw_centered(190, line, UI_BAD, 1), TAG, "mDNS link failed");
    } else if (mdns_scan_status.state == CERBERUS_MDNS_SCAN_RUNNING &&
        mdns_scan_status.result_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(180, "DISCOVERING ON LOCAL NETWORK", UI_GOOD, 1),
            TAG, "mDNS running failed");
        ESP_RETURN_ON_ERROR(draw_centered(210, "BOUNDED TO COMMON SERVICE TYPES", UI_MUTED, 1),
            TAG, "mDNS scope failed");
    } else if (mdns_scan_status.state == CERBERUS_MDNS_SCAN_ERROR) {
        snprintf(line, sizeof(line), "DISCOVERY ERROR %s",
            esp_err_to_name((esp_err_t)mdns_scan_status.error_code));
        ESP_RETURN_ON_ERROR(draw_centered(190, line, UI_BAD, 1), TAG, "mDNS error failed");
    } else if (mdns_scan_status.result_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(180, "NO COMMON SERVICES ANSWERED", UI_MUTED, 1),
            TAG, "mDNS empty failed");
    } else {
        for (size_t row = 0; row < mdns_result_count; ++row) {
            const cerberus_mdns_result_payload_t *result = &mdns_results[row];
            uint16_t y = (uint16_t)(82U + row * 44U);
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(8, y, 304, 42, UI_PANEL),
                TAG, "mDNS row failed");
            snprintf(line, sizeof(line), "%s  %.12s:%u", mdns_service_text(result->service_type),
                result->hostname[0] != '\0' ? result->hostname : "<UNNAMED>", result->port);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 5U, line,
                UI_TEXT, UI_PANEL, 1), TAG, "mDNS service failed");
            snprintf(line, sizeof(line), "%u.%u.%u.%u  LOCAL RESPONSE",
                result->ipv4[0], result->ipv4[1], result->ipv4[2], result->ipv4[3]);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(14, y + 22U, line,
                UI_MUTED, UI_PANEL, 1), TAG, "mDNS address failed");
        }
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, 360, 68, 48, "BACK"), TAG, "mDNS back failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 360, 68, 48, "PREV"), TAG, "mDNS prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, 360, 68, 48, "NEXT"), TAG, "mDNS next failed");
    return draw_small_button(236, 360, 76, 48,
        mdns_scan_status.state == CERBERUS_MDNS_SCAN_RUNNING ? "SCANNING" : "RESCAN");
}

static esp_err_t refresh_wifi_baseline(void)
{
    (void)refresh_wifi_scan_status();
    esp_err_t err = sentinel_baseline_compare(&wifi_baseline);
    if (err == ESP_OK && (!wifi_observation_valid ||
        wifi_baseline.generation != wifi_observed_generation)) {
        wifi_observed_at_seconds = (uint32_t)(esp_timer_get_time() / 1000000LL);
        wifi_observed_generation = wifi_baseline.generation;
        wifi_observation_valid = true;
    }
    return err;
}

static esp_err_t draw_baseline_metric(uint16_t x, uint16_t y, const char *label,
    unsigned value, uint16_t color)
{
    char text[16];
    snprintf(text, sizeof(text), "%u", value);
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(x, y, 144, 66, UI_PANEL), TAG, "metric panel");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(x + 10, y + 9, label,
        UI_MUTED, UI_PANEL, 1), TAG, "metric label");
    return cerberus_display_draw_text(x + 10, y + 28, text, color, UI_PANEL, 3);
}

static esp_err_t export_wifi_baseline(void)
{
    if (!wifi_baseline.checked || wifi_baseline.error != ESP_OK) return ESP_ERR_INVALID_STATE;
    size_t length = 0U;
    ESP_RETURN_ON_ERROR(append_wifi_report(&length,
        "report,cerberus_sentinel_baseline\r\n"
        "firmware,%s\r\n"
        "observation_uptime_seconds,%lu\r\n"
        "saved_baselines,%u\r\ncompared_stored_records,%u\r\n"
        "exact_matches,%u\r\nsame_name_differences,%u\r\nother_records,%u\r\n"
        "scope,Only stored access points in this completed scan\r\n"
        "interpretation,A difference is a review clue and is not proof of an attack\r\n"
        "privacy,No network names or device identifiers included\r\n",
        esp_app_get_description()->version,
        (unsigned long)wifi_observed_at_seconds, (unsigned)wifi_baseline.saved,
        wifi_baseline.compared, wifi_baseline.exact, wifi_baseline.review, wifi_baseline.other),
        TAG, "baseline report");
    esp_err_t err = cerberus_storage_write_wifi_report(wifi_report_buffer, length,
        wifi_report_file_name, sizeof(wifi_report_file_name));
    snprintf(wifi_export_message, sizeof(wifi_export_message),
        err == ESP_OK ? "SAVED: %.42s" : "SAVE FAILED: %.40s",
        err == ESP_OK ? wifi_report_file_name : esp_err_to_name(err));
    wifi_export_color = err == ESP_OK ? UI_GOOD : UI_BAD;
    return err;
}

static esp_err_t draw_wifi_baseline(void)
{
    if (!wifi_baseline.checked) (void)refresh_wifi_baseline();
    ESP_RETURN_ON_ERROR(draw_centered(55, "TRUSTED BASELINE / PASSIVE COMPARISON", UI_MUTED, 1),
        TAG, "baseline subtitle");
    if (wifi_baseline.error == ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_baseline_metric(12, 79, "SAVED BY YOU", (unsigned)wifi_baseline.saved,
            UI_TEXT), TAG, "saved");
        ESP_RETURN_ON_ERROR(draw_baseline_metric(164, 79, "EXACT MATCHES", wifi_baseline.exact,
            UI_GOOD), TAG, "exact");
        ESP_RETURN_ON_ERROR(draw_baseline_metric(12, 153, "REVIEW CLUES", wifi_baseline.review,
            wifi_baseline.review > 0 ? UI_BAD : UI_TEXT), TAG, "review");
        ESP_RETURN_ON_ERROR(draw_baseline_metric(164, 153, "OTHER NETWORKS", wifi_baseline.other,
            UI_TEXT), TAG, "other");
        char line[64];
        uint32_t age = (uint32_t)(esp_timer_get_time() / 1000000LL) - wifi_observed_at_seconds;
        snprintf(line, sizeof(line), "%u STORED RECORDS / OBSERVED %lus AGO",
            wifi_baseline.compared, (unsigned long)age);
        ESP_RETURN_ON_ERROR(draw_centered(232, line, UI_MUTED, 1), TAG, "baseline scope");
        const char *lines[] = {
            "A matching name with different radio ID or",
            "security deserves a look. Mesh nodes and",
            "router changes can explain a difference.",
            "Other networks are not automatically unsafe.",
            "An unseen saved network is not proof of loss.",
        };
        for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); ++i) {
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, 258 + (uint16_t)i * 15,
                lines[i], UI_MUTED, UI_BACKGROUND, 1), TAG, "baseline explanation");
        }
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(125, "START WITH A SCAN", UI_TEXT, 2), TAG, "baseline empty");
        ESP_RETURN_ON_ERROR(draw_centered(178, "Open Networks and tap SCAN.", UI_MUTED, 1), TAG, "baseline step1");
        ESP_RETURN_ON_ERROR(draw_centered(206, "Select your access point and save it", UI_MUTED, 1), TAG, "baseline step2");
        ESP_RETURN_ON_ERROR(draw_centered(226, "as trusted, then return here to compare.", UI_MUTED, 1), TAG, "baseline step3");
        if (wifi_baseline.error != ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(draw_centered(278, esp_err_to_name(wifi_baseline.error),
                UI_BAD, 1), TAG, "baseline error");
        }
    }
    if (wifi_export_message[0] != '\0') {
        ESP_RETURN_ON_ERROR(draw_centered(340, wifi_export_message, wifi_export_color, 1), TAG, "baseline export");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, 364, 68, 42, "NETWORKS"), TAG, "baseline networks");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 364, 68, 42, "COMPARE"), TAG, "baseline compare");
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button((cerberus_ui_rect_t){160, 364, 68, 42},
        "SAVE", 1, wifi_baseline.error == ESP_OK, UI_WIDGET_THEME), TAG, "baseline save");
    return draw_small_button(236, 364, 76, 42, "LEARN");
}

static esp_err_t draw_wifi(void)
{
    if (wifi_mode == UI_WIFI_MODE_TARGET) return draw_wifi_target();
    if (wifi_mode == UI_WIFI_MODE_FIELD_SURVEY) return field_survey_view_draw(UI_WIDGET_THEME);
    if (wifi_mode == UI_WIFI_MODE_NETWORK_SCAN) return net_draw();
    if (wifi_mode == UI_WIFI_MODE_STATIONS) return station_draw();
    if (wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS) return wifi_relation_draw();
    if (wifi_mode == UI_WIFI_MODE_AP_JOB) return ap_draw();
    if (wifi_mode == UI_WIFI_MODE_PORTAL) return portal_draw();
    if (wifi_mode == UI_WIFI_MODE_CAPTURE) return capture_draw();
    if (wifi_mode == UI_WIFI_MODE_MULTISSID) return multissid_draw();
    if (wifi_mode == UI_WIFI_MODE_PINESCAN) return pinescan_draw();
    if (wifi_mode == UI_WIFI_MODE_PACKET_MONITOR) return draw_wifi_packet_monitor();
    if (wifi_mode == UI_WIFI_MODE_MENU) return sentinel_hub_draw(SENTINEL_HUB_WIFI, UI_WIDGET_THEME);
    if (live_is_view()) return draw_live_scan();
    char line[64];
    bool wifi_capable = c5_online && c5_status.protocol_version == CERBERUS_PROTOCOL_VERSION &&
        (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_2_4_GHZ) != 0U;

    if (!wifi_capable) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "C5 WI-FI UNAVAILABLE", UI_BAD, 2), TAG, "Wi-Fi unavailable failed");
        return draw_centered(205, "C5 PROTOCOL V2 REQUIRED", UI_MUTED, 1);
    }

    if (wifi_mode == UI_WIFI_MODE_MENU) {
        return sentinel_hub_draw(SENTINEL_HUB_WIFI, UI_WIDGET_THEME);
    }
    if (wifi_mode == UI_WIFI_MODE_BASELINE) {
        return draw_wifi_baseline();
    }

    if (wifi_mode == UI_WIFI_MODE_PASSWORD) {
        return cerberus_keyboard_draw(&wifi_keyboard, "NETWORK PASSWORD");
    }
    if (wifi_mode == UI_WIFI_MODE_CONNECTION) {
        return draw_wifi_connection();
    }
    if (wifi_mode == UI_WIFI_MODE_DIAGNOSTICS) {
        return draw_wifi_diagnostics();
    }
    if (wifi_mode == UI_WIFI_MODE_MDNS) {
        return draw_wifi_mdns();
    }
    if (wifi_mode == UI_WIFI_MODE_STATS) {
        return draw_wifi_radio_stats();
    }
    if (wifi_mode == UI_WIFI_MODE_DEFENSE) {
        return draw_wifi_defense_survey();
    }

    if (!wifi_scan_checked) {
        (void)refresh_wifi_scan_status();
    }
    if (wifi_selection_valid && wifi_mode == UI_WIFI_MODE_DETAIL) {
        return draw_wifi_detail();
    }
    if (wifi_mode == UI_WIFI_MODE_CHANNELS_24 || wifi_mode == UI_WIFI_MODE_CHANNELS_5) {
        return draw_wifi_channel_occupancy();
    }
    if (wifi_scan_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(wifi_scan_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "SCAN SERVICE UNAVAILABLE", UI_BAD, 2), TAG, "Wi-Fi service failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "Wi-Fi link error failed");
    } else if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_ERROR) {
        snprintf(line, sizeof(line), "C5 ERROR %s", esp_err_to_name((esp_err_t)wifi_scan_status.error_code));
        ESP_RETURN_ON_ERROR(draw_centered(145, "PASSIVE SCAN FAILED", UI_BAD, 2), TAG, "Wi-Fi scan error failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "Wi-Fi C5 error failed");
    } else if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "PASSIVE SCAN RUNNING", UI_GOOD, 2), TAG, "Wi-Fi running failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, "LISTENING ON 2.4 + 5 GHZ", UI_MUTED, 1), TAG, "Wi-Fi bands failed");
    } else if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE) {
        snprintf(
            line,
            sizeof(line),
            "FOUND %u  STORED %u  HIDDEN %u  TRUST %u",
            (unsigned)wifi_scan_status.total_count,
            (unsigned)wifi_scan_status.stored_count,
            (unsigned)wifi_scan_status.hidden_count,
            (unsigned)cerberus_settings_wifi_trusted_count());
        ESP_RETURN_ON_ERROR(draw_centered(56, line, UI_GOOD, 1), TAG, "Wi-Fi summary failed");

        for (size_t row = 0; row < wifi_result_count; ++row) {
            uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
            const cerberus_c5_wifi_result_t *result = &wifi_results[row];
            cerberus_wifi_trust_state_t trust_state = cerberus_settings_wifi_trust_state(
                result->ssid,
                result->bssid,
                result->security);
            ESP_RETURN_ON_ERROR(
                cerberus_display_fill_rect(8, y, 304, UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL),
                TAG,
                "Wi-Fi row failed");

            const char *ssid = result->ssid[0] != '\0' ? result->ssid : "<HIDDEN>";
            const char *trust_mark = trust_state == CERBERUS_WIFI_TRUST_EXACT
                ? "[T]"
                : (trust_state == CERBERUS_WIFI_TRUST_MISMATCH ? "[!]" : "[ ]");
            snprintf(line, sizeof(line), "%s %.22s  %s", trust_mark, ssid, wifi_security_text(result->security));
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(
                    14,
                    y + 6U,
                    line,
                    trust_state == CERBERUS_WIFI_TRUST_EXACT
                        ? UI_GOOD
                        : (trust_state == CERBERUS_WIFI_TRUST_MISMATCH ? UI_BAD : UI_TEXT),
                    UI_PANEL,
                    1),
                TAG,
                "Wi-Fi SSID failed");
            char identifier[24];
            cerberus_privacy_format_wifi_identifier(
                result->bssid,
                privacy_addresses_masked(),
                identifier,
                sizeof(identifier));
            snprintf(
                line,
                sizeof(line),
                "%s  CH%u %dDB %s",
                identifier,
                result->channel,
                result->rssi,
                result->channel > 14U ? "5G" : "2G");
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 23U, line, UI_MUTED, UI_PANEL, 1),
                TAG,
                "Wi-Fi metadata failed");
        }
        if (wifi_scan_status.stored_count == 0U) {
            ESP_RETURN_ON_ERROR(draw_centered(190, "NO ACCESS POINTS HEARD", UI_MUTED, 2), TAG, "Wi-Fi empty failed");
        }
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(145, "PASSIVE WI-FI VISIBILITY", UI_MUTED, 2), TAG, "Wi-Fi idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, "TAP SCAN TO LIST NEARBY APS", UI_TEXT, 1), TAG, "Wi-Fi prompt failed");
        ESP_RETURN_ON_ERROR(draw_centered(225, "NO CONNECTION OR CAPTURE", UI_GOOD, 1), TAG, "Wi-Fi scope failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42, "PREV"), TAG, "Wi-Fi prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42, "NEXT"), TAG, "Wi-Fi next failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 68, 42, "STATS"), TAG, "Wi-Fi statistics failed");
    return draw_small_button(
        236,
        UI_WIFI_CONTROLS_Y,
        76,
        42,
        wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ? "STOP" : "SCAN");
}

static void make_ble_name_safe(char *name)
{
    for (size_t index = 0; name[index] != '\0'; ++index) {
        uint8_t value = (uint8_t)name[index];
        if (value < 32U || value > 126U) {
            name[index] = '?';
        }
    }
}

static ui_ble_history_t *find_ble_history(const uint8_t address[6], uint8_t address_type)
{
    for (size_t index = 0; index < UI_BLE_HISTORY_CAPACITY; ++index) {
        if (ble_history[index].used && ble_history[index].address_type == address_type &&
            memcmp(ble_history[index].address, address, 6U) == 0) {
            return &ble_history[index];
        }
    }
    return NULL;
}

static ui_ble_history_t *allocate_ble_history(const uint8_t address[6], uint8_t address_type)
{
    ui_ble_history_t *entry = find_ble_history(address, address_type);
    if (entry != NULL) {
        return entry;
    }

    ui_ble_history_t *slot = NULL;
    for (size_t index = 0; index < UI_BLE_HISTORY_CAPACITY; ++index) {
        if (!ble_history[index].used) {
            slot = &ble_history[index];
            break;
        }
        if (slot == NULL || ble_history[index].last_seen_sequence < slot->last_seen_sequence) {
            slot = &ble_history[index];
        }
    }
    if (slot == NULL) {
        return NULL;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = true;
    memcpy(slot->address, address, sizeof(slot->address));
    slot->address_type = address_type;
    return slot;
}

static void record_ble_rssi(const cerberus_c5_ble_result_t *result, uint8_t generation)
{
    if (!cerberus_ble_rssi_valid(result->rssi)) return;
    ui_ble_history_t *history = allocate_ble_history(result->address, result->address_type);
    if (history == NULL ||
        (history->sample_count > 0U && history->last_generation == generation)) {
        return;
    }

    history->samples[history->next_sample] = result->rssi;
    history->next_sample = (history->next_sample + 1U) % UI_BLE_HISTORY_SAMPLES;
    if (history->sample_count < UI_BLE_HISTORY_SAMPLES) {
        ++history->sample_count;
    }
    history->last_generation = generation;
    history->last_seen_sequence = ++ble_history_sequence;
}

static char ble_rssi_trend(const cerberus_c5_ble_result_t *result)
{
    const ui_ble_history_t *history = find_ble_history(result->address, result->address_type);
    if (history == NULL || history->sample_count < 2U) {
        return '?';
    }
    size_t newest = (history->next_sample + UI_BLE_HISTORY_SAMPLES - 1U) % UI_BLE_HISTORY_SAMPLES;
    size_t previous = (history->next_sample + UI_BLE_HISTORY_SAMPLES - 2U) % UI_BLE_HISTORY_SAMPLES;
    int difference = history->samples[newest] - history->samples[previous];
    return difference >= 4 ? '+' : (difference <= -4 ? '-' : '=');
}

static uint16_t ble_rssi_color(int8_t rssi)
{
    return rssi >= -65 ? UI_GOOD : (rssi >= -85 ? UI_MUTED : UI_BAD);
}

static esp_err_t draw_ble_rssi_history(uint16_t row_y, const cerberus_c5_ble_result_t *result)
{
    const ui_ble_history_t *history = find_ble_history(result->address, result->address_type);
    if (history == NULL || history->sample_count == 0U) {
        return ESP_OK;
    }

    size_t oldest = history->sample_count < UI_BLE_HISTORY_SAMPLES ? 0U : history->next_sample;
    int16_t values[UI_BLE_HISTORY_SAMPLES] = {0};
    uint16_t colors[UI_BLE_HISTORY_SAMPLES] = {0};
    for (uint8_t sample = 0; sample < history->sample_count; ++sample) {
        int8_t rssi = history->samples[(oldest + sample) % UI_BLE_HISTORY_SAMPLES];
        values[sample] = rssi;
        colors[sample] = ble_rssi_color(rssi);
    }
    return cerberus_ui_widget_draw_bar_graph(
        (cerberus_ui_rect_t){
            264U,
            (uint16_t)(row_y + 23U),
            (uint16_t)(history->sample_count * 7U),
            15U,
        },
        values,
        colors,
        history->sample_count,
        -100,
        -40,
        UI_MUTED,
        UI_WIDGET_THEME);
}

static const char *ble_filter_name(cerberus_s3_ble_filter_t filter)
{
    switch (filter) {
    case CERBERUS_S3_BLE_FILTER_IBEACON: return "IBEACON";
    case CERBERUS_S3_BLE_FILTER_FIND_MY: return "FIND MY";
    case CERBERUS_S3_BLE_FILTER_FLIPPER: return "FLIPPER";
    case CERBERUS_S3_BLE_FILTER_FLOCK: return "FLOCK";
    case CERBERUS_S3_BLE_FILTER_META_HINT: return "META ID HINT";
    case CERBERUS_S3_BLE_FILTER_HC_NAME_HINT: return "HC NAME HINT";
    default: return "ALL";
    }
}

static esp_err_t load_ble_results(void)
{
    ble_result_count = 0;
    ble_filtered_count = 0U;
    if (ble_scan_status.state != CERBERUS_BLE_SCAN_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_scan_status.stored_count > CERBERUS_BLE_SCAN_MAX_RESULTS)
        return ESP_ERR_INVALID_SIZE;
    for (uint8_t index = 0U; index < ble_scan_status.stored_count; ++index) {
        cerberus_s3_ble_focus_t focus = {0};
        esp_err_t error = cerberus_s3_ble_scanner_get_focus(
            ble_scan_status.generation, index, &focus);
        if (error != ESP_OK) return error;
        ble_focus_bits[index] = focus.category_bits;
    }
    ble_filtered_count = cerberus_s3_ble_focus_page(ble_focus_bits,
        ble_scan_status.stored_count, ble_filter, 0U, NULL, 0U);
    if (ble_page_offset >= ble_filtered_count) {
        ble_page_offset = ble_filtered_count == 0U
            ? 0U
            : ((ble_filtered_count - 1U) / UI_WIFI_VISIBLE_ROWS) * UI_WIFI_VISIBLE_ROWS;
    }
    (void)cerberus_s3_ble_focus_page(ble_focus_bits,
        ble_scan_status.stored_count, ble_filter, ble_page_offset,
        ble_result_indexes, UI_WIFI_VISIBLE_ROWS);
    size_t visible = ble_filtered_count > ble_page_offset
        ? ble_filtered_count - ble_page_offset : 0U;
    if (visible > UI_WIFI_VISIBLE_ROWS) visible = UI_WIFI_VISIBLE_ROWS;
    for (size_t row = 0; row < visible; ++row) {
        uint8_t index = ble_result_indexes[row];
        esp_err_t err = cerberus_s3_ble_scanner_get_result(
            ble_scan_status.generation,
            index,
            &ble_results[ble_result_count]);
        if (err != ESP_OK) {
            return err;
        }
        make_ble_name_safe(ble_results[ble_result_count].name);
        record_ble_rssi(&ble_results[ble_result_count], ble_scan_status.generation);
        ++ble_result_count;
    }
    return ESP_OK;
}

static esp_err_t refresh_ble_scan_status(void)
{
    ble_scan_checked = true;
    ble_scan_error = cerberus_s3_ble_scanner_get_status(&ble_scan_status);
    if (ble_scan_error == ESP_OK && ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE) {
        ble_scan_error = load_ble_results();
    }
    return ble_scan_error;
}

static esp_err_t refresh_ble_connection_status(void)
{
    ble_connection_checked = true;
    ble_connection_error = cerberus_s3_ble_central_get_status(&ble_connection_status);
    return ble_connection_error;
}

static bool ble_connection_is_active(void)
{
    return ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        ble_connection_status.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
}

static bool ble_connection_matches_selected(void)
{
    return ble_connection_status.address_type == ble_selected_result.address_type &&
        memcmp(ble_connection_status.address, ble_selected_result.address, 6U) == 0;
}

static void set_ble_action_message(const char *message, uint16_t color)
{
    snprintf(ble_action_message, sizeof(ble_action_message), "%s", message != NULL ? message : "");
    ble_action_color = color;
}

/* BEGIN BLE CONNECTION CONTROL: production tap route used by host checks. */
static cerberus_ble_link_control_t ble_connection_control(void)
{
    return cerberus_ble_link_control(
        (ui_capability_map & UI_CAP_BLE_GATT) && ble_connection_checked &&
            ble_connection_error == ESP_OK,
        &ble_connection_status, ble_selected_result.address,
        ble_selected_result.address_type,
        (ble_selected_result.flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U);
}

static void ble_handle_connection_action(void)
{
    if (ui_capability_map & UI_CAP_BLE_GATT) (void)refresh_ble_connection_status();
    const cerberus_ble_link_control_t control = ble_connection_control();
    if (control.action == CERBERUS_BLE_LINK_CANCEL ||
        control.action == CERBERUS_BLE_LINK_DISCONNECT ||
        control.action == CERBERUS_BLE_LINK_RETRY_CLOSE) {
        ble_connection_error = cerberus_s3_ble_central_disconnect(&ble_connection_status);
        ble_connection_checked = true;
        ble_service_checked = ble_characteristic_checked = ble_read_checked = false;
        const char *message = control.action == CERBERUS_BLE_LINK_CANCEL ? "CANCEL REQUESTED" :
            control.action == CERBERUS_BLE_LINK_RETRY_CLOSE ? "CLOSE RETRY REQUESTED" : "DISCONNECT REQUESTED";
        set_ble_action_message(ble_connection_error == ESP_OK ? message : "CLOSE COMMAND FAILED",
            ble_connection_error == ESP_OK ? UI_MUTED : UI_BAD);
    } else if (control.action == CERBERUS_BLE_LINK_CONNECT) {
        ble_connection_error = cerberus_s3_ble_central_connect(&ble_selected_result,
            &ble_connection_status);
        ble_connection_checked = true;
        ble_service_checked = ble_characteristic_checked = ble_read_checked = false;
        if (ble_connection_error != ESP_OK)
            set_ble_action_message("CONNECT COMMAND FAILED", UI_BAD);
        else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTING)
            set_ble_action_message("CONNECT REQUEST SENT", UI_GOOD);
        else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED)
            set_ble_action_message("CONNECTED - DISCOVER SERVICES", UI_GOOD);
        else set_ble_action_message("S3 REJECTED CONNECT REQUEST", UI_BAD);
    } else if (!(ui_capability_map & UI_CAP_BLE_GATT) || ble_connection_error != ESP_OK)
        set_ble_action_message("S3 CONNECTION SERVICE UNAVAILABLE", UI_BAD);
    else if (ble_connection_is_active() && !ble_connection_matches_selected())
        set_ble_action_message("DISCONNECT THE ACTIVE DEVICE FIRST", UI_MUTED);
    else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_DISCONNECTING)
        set_ble_action_message("WAITING FOR CONNECTION TO CLOSE", UI_MUTED);
    else if (ble_connection_status.state != CERBERUS_BLE_CONNECTION_IDLE &&
        ble_connection_status.state != CERBERUS_BLE_CONNECTION_ERROR)
        set_ble_action_message("CONNECTION STATUS UNAVAILABLE", UI_BAD);
    else set_ble_action_message("DEVICE IS ADVERTISEMENT ONLY", UI_MUTED);
}
/* END BLE CONNECTION CONTROL. */

static esp_err_t load_ble_services(void)
{
    ble_service_count = 0;
    if (ble_service_status.state != CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_service_page_offset >= ble_service_status.stored_count) {
        ble_service_page_offset = ble_service_status.stored_count == 0U
            ? 0U
            : ((ble_service_status.stored_count - 1U) / UI_BLE_SERVICE_VISIBLE_ROWS) *
                UI_BLE_SERVICE_VISIBLE_ROWS;
    }
    for (size_t row = 0; row < UI_BLE_SERVICE_VISIBLE_ROWS; ++row) {
        size_t index = ble_service_page_offset + row;
        if (index >= ble_service_status.stored_count) {
            break;
        }
        esp_err_t err = cerberus_s3_ble_central_get_service_result(
            ble_service_status.generation,
            (uint8_t)index,
            &ble_services[ble_service_count]);
        if (err != ESP_OK) {
            return err;
        }
        ++ble_service_count;
    }
    return ESP_OK;
}

static esp_err_t refresh_ble_service_status(void)
{
    ble_service_checked = true;
    ble_service_error = cerberus_s3_ble_central_get_service_status(&ble_service_status);
    return ble_service_error;
}

static esp_err_t load_ble_characteristics(void)
{
    ble_characteristic_count = 0;
    if (ble_characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
        return ESP_ERR_INVALID_STATE;
    }
    if (ble_characteristic_page_offset >= ble_characteristic_status.stored_count) {
        ble_characteristic_page_offset = ble_characteristic_status.stored_count == 0U
            ? 0U
            : ((ble_characteristic_status.stored_count - 1U) /
                UI_BLE_CHARACTERISTIC_VISIBLE_ROWS) * UI_BLE_CHARACTERISTIC_VISIBLE_ROWS;
    }
    for (size_t row = 0; row < UI_BLE_CHARACTERISTIC_VISIBLE_ROWS; ++row) {
        size_t index = ble_characteristic_page_offset + row;
        if (index >= ble_characteristic_status.stored_count) {
            break;
        }
        esp_err_t err = cerberus_s3_ble_central_get_characteristic_result(
            ble_characteristic_status.generation,
            (uint8_t)index,
            &ble_characteristics[ble_characteristic_count]);
        if (err != ESP_OK) {
            return err;
        }
        ++ble_characteristic_count;
    }
    return ESP_OK;
}

static esp_err_t refresh_ble_characteristic_status(void)
{
    ble_characteristic_checked = true;
    ble_characteristic_error = cerberus_s3_ble_central_get_characteristic_status(
        &ble_characteristic_status);
    if (ble_characteristic_error == ESP_OK &&
        ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
        ble_characteristic_error = load_ble_characteristics();
    }
    return ble_characteristic_error;
}

static esp_err_t load_ble_read_value(void)
{
    ble_read_value_length = 0;
    memset(ble_read_value, 0, sizeof(ble_read_value));
    if (ble_read_status.state != CERBERUS_BLE_GATT_READ_COMPLETE ||
        ble_read_status.stored_length > sizeof(ble_read_value)) {
        return ESP_ERR_INVALID_STATE;
    }
    return cerberus_s3_ble_central_get_read_value(ble_read_status.generation,
        ble_read_value, sizeof(ble_read_value), &ble_read_value_length);
}

static esp_err_t refresh_ble_read_status(void)
{
    ble_read_checked = true;
    ble_read_error = cerberus_s3_ble_central_get_read_status(&ble_read_status);
    if (ble_read_error == ESP_OK &&
        ble_read_status.state == CERBERUS_BLE_GATT_READ_COMPLETE) {
        ble_read_error = load_ble_read_value();
    }
    return ble_read_error;
}

static void begin_ble_characteristic_read(void)
{
    ble_read_value_length = 0;
    memset(ble_read_value, 0, sizeof(ble_read_value));
    ble_read_error = cerberus_s3_ble_central_start_read(
        &ble_selected_characteristic,
        &ble_read_status);
    ble_read_checked = true;
    if (ble_read_error == ESP_OK &&
        ble_read_status.state == CERBERUS_BLE_GATT_READ_COMPLETE) {
        ble_read_error = load_ble_read_value();
    } else if (ble_read_error == ESP_OK &&
        ble_read_status.state != CERBERUS_BLE_GATT_READ_RUNNING) {
        ble_read_error = ESP_ERR_INVALID_STATE;
    }
}

static void format_ble_uuid(
    const uint8_t *uuid,
    uint8_t uuid_length,
    char *text,
    size_t text_size)
{
    if (uuid_length == 2U) {
        uint16_t value = (uint16_t)uuid[0] | ((uint16_t)uuid[1] << 8U);
        snprintf(text, text_size, "0x%04X", value);
    } else if (uuid_length == 4U) {
        uint32_t value = (uint32_t)uuid[0] |
            ((uint32_t)uuid[1] << 8U) |
            ((uint32_t)uuid[2] << 16U) |
            ((uint32_t)uuid[3] << 24U);
        snprintf(text, text_size, "0x%08lX", (unsigned long)value);
    } else if (uuid_length == 16U) {
        snprintf(
            text,
            text_size,
            "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
            uuid[15], uuid[14], uuid[13], uuid[12],
            uuid[11], uuid[10], uuid[9], uuid[8],
            uuid[7], uuid[6], uuid[5], uuid[4],
            uuid[3], uuid[2], uuid[1], uuid[0]);
    } else {
        text[0] = '\0';
    }
}

static void format_ble_service(
    const cerberus_c5_ble_result_t *result,
    char *text,
    size_t text_size)
{
    format_ble_uuid(result->uuid, result->uuid_length, text, text_size);
}

static const char *ble_standard_service_name(const cerberus_c5_ble_service_t *service)
{
    if (service->uuid_length != 2U) {
        return "PRIMARY SERVICE";
    }
    uint16_t uuid = (uint16_t)service->uuid[0] | ((uint16_t)service->uuid[1] << 8U);
    switch (uuid) {
    case 0x1800:
        return "GENERIC ACCESS";
    case 0x1801:
        return "GENERIC ATTRIBUTE";
    case 0x180A:
        return "DEVICE INFORMATION";
    case 0x180D:
        return "HEART RATE";
    case 0x180F:
        return "BATTERY SERVICE";
    case 0x1812:
        return "HUMAN INTERFACE DEVICE";
    case 0x1816:
        return "CYCLING SPEED/CADENCE";
    case 0x181A:
        return "ENVIRONMENTAL SENSING";
    default:
        return "PRIMARY SERVICE";
    }
}

static const char *ble_standard_characteristic_name(
    const cerberus_c5_ble_characteristic_t *characteristic)
{
    if (characteristic->uuid_length != 2U) {
        return "CHARACTERISTIC";
    }
    uint16_t uuid = (uint16_t)characteristic->uuid[0] |
        ((uint16_t)characteristic->uuid[1] << 8U);
    switch (uuid) {
    case 0x2A00:
        return "DEVICE NAME";
    case 0x2A01:
        return "APPEARANCE";
    case 0x2A05:
        return "SERVICE CHANGED";
    case 0x2A19:
        return "BATTERY LEVEL";
    case 0x2A24:
        return "MODEL NUMBER";
    case 0x2A25:
        return "SERIAL NUMBER";
    case 0x2A26:
        return "FIRMWARE REVISION";
    case 0x2A27:
        return "HARDWARE REVISION";
    case 0x2A28:
        return "SOFTWARE REVISION";
    case 0x2A29:
        return "MANUFACTURER NAME";
    case 0x2A6D:
        return "PRESSURE";
    case 0x2A6E:
        return "TEMPERATURE";
    case 0x2A6F:
        return "HUMIDITY";
    default:
        return "CHARACTERISTIC";
    }
}

static bool format_ble_standard_value(char *text, size_t text_size)
{
    if (text == NULL || text_size == 0U ||
        ble_selected_characteristic.uuid_length != 2U || ble_read_value_length == 0U) {
        return false;
    }
    uint16_t uuid = (uint16_t)ble_selected_characteristic.uuid[0] |
        ((uint16_t)ble_selected_characteristic.uuid[1] << 8U);
    if (uuid == 0x2A19U) {
        if (ble_read_value[0] <= 100U) {
            snprintf(text, text_size, "BATTERY LEVEL %u%%", ble_read_value[0]);
        } else {
            snprintf(text, text_size, "BATTERY LEVEL UNKNOWN (%u)", ble_read_value[0]);
        }
        return true;
    }
    if (uuid == 0x2A6EU && ble_read_value_length >= 2U) {
        int32_t centi_celsius = (int16_t)((uint16_t)ble_read_value[0] |
            ((uint16_t)ble_read_value[1] << 8U));
        bool negative = centi_celsius < 0;
        uint32_t magnitude = (uint32_t)(negative ? -centi_celsius : centi_celsius);
        snprintf(
            text,
            text_size,
            "TEMPERATURE %s%lu.%02lu C",
            negative ? "-" : "",
            (unsigned long)(magnitude / 100U),
            (unsigned long)(magnitude % 100U));
        return true;
    }
    if (uuid == 0x2A6FU && ble_read_value_length >= 2U) {
        uint32_t centi_percent = (uint16_t)ble_read_value[0] |
            ((uint16_t)ble_read_value[1] << 8U);
        snprintf(
            text,
            text_size,
            "HUMIDITY %lu.%02lu%%",
            (unsigned long)(centi_percent / 100U),
            (unsigned long)(centi_percent % 100U));
        return true;
    }
    if (uuid == 0x2A6DU && ble_read_value_length >= 4U) {
        uint32_t deci_pascal = (uint32_t)ble_read_value[0] |
            ((uint32_t)ble_read_value[1] << 8U) |
            ((uint32_t)ble_read_value[2] << 16U) |
            ((uint32_t)ble_read_value[3] << 24U);
        snprintf(
            text,
            text_size,
            "PRESSURE %lu.%01lu PA",
            (unsigned long)(deci_pascal / 10U),
            (unsigned long)(deci_pascal % 10U));
        return true;
    }
    if (uuid == 0x2A01U && ble_read_value_length >= 2U) {
        uint16_t appearance = (uint16_t)ble_read_value[0] |
            ((uint16_t)ble_read_value[1] << 8U);
        snprintf(text, text_size, "APPEARANCE 0x%04X", appearance);
        return true;
    }

    const char *label = NULL;
    switch (uuid) {
    case 0x2A00:
        label = "DEVICE NAME";
        break;
    case 0x2A24:
        label = "MODEL";
        break;
    case 0x2A25:
        label = "SERIAL";
        break;
    case 0x2A26:
        label = "FIRMWARE";
        break;
    case 0x2A27:
        label = "HARDWARE";
        break;
    case 0x2A28:
        label = "SOFTWARE";
        break;
    case 0x2A29:
        label = "MANUFACTURER";
        break;
    default:
        return false;
    }

    char safe_value[44];
    size_t label_length = strlen(label);
    size_t max_value = 52U > label_length + 1U ? 52U - label_length - 1U : 1U;
    if (max_value >= sizeof(safe_value)) {
        max_value = sizeof(safe_value) - 1U;
    }
    size_t value_length = 0;
    while (value_length < ble_read_value_length && value_length < max_value &&
        ble_read_value[value_length] != '\0') {
        uint8_t value = ble_read_value[value_length];
        safe_value[value_length] = value >= 32U && value <= 126U ? (char)value : '.';
        ++value_length;
    }
    safe_value[value_length] = '\0';
    snprintf(text, text_size, "%s %s", label, safe_value);
    return true;
}

static bool ble_uuid16_is(const uint8_t *uuid, uint8_t uuid_length, uint16_t expected)
{
    return uuid_length == 2U && uuid[0] == (uint8_t)expected &&
        uuid[1] == (uint8_t)(expected >> 8U);
}

static bool ble_environment_characteristic_is_supported(
    const cerberus_c5_ble_characteristic_t *characteristic)
{
    if (characteristic == NULL || (characteristic->properties & 0x02U) == 0U) {
        return false;
    }
    return ble_uuid16_is(characteristic->uuid, characteristic->uuid_length, 0x2A6DU) ||
        ble_uuid16_is(characteristic->uuid, characteristic->uuid_length, 0x2A6EU) ||
        ble_uuid16_is(characteristic->uuid, characteristic->uuid_length, 0x2A6FU);
}

static void reset_ble_environment_viewer(void)
{
    memset(ble_environment_readings, 0, sizeof(ble_environment_readings));
    ble_environment_reading_count = 0U;
    ble_environment_reading_index = 0U;
    ble_environment_service_search_complete = false;
    ble_environment_service_found = false;
    ble_environment_characteristics_loaded = false;
    ble_environment_refresh_active = false;
    ble_environment_error = ESP_ERR_INVALID_STATE;
}

static esp_err_t load_ble_environment_characteristics(void)
{
    memset(ble_environment_readings, 0, sizeof(ble_environment_readings));
    ble_environment_reading_count = 0U;
    ble_environment_reading_index = 0U;
    ble_environment_characteristics_loaded = false;

    if (ble_characteristic_error != ESP_OK ||
        ble_characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
        return ble_characteristic_error == ESP_OK ? ESP_ERR_INVALID_STATE : ble_characteristic_error;
    }

    for (uint8_t index = 0U;
        index < ble_characteristic_status.stored_count &&
            ble_environment_reading_count < UI_BLE_ENVIRONMENT_READING_MAX;
        ++index) {
        cerberus_c5_ble_characteristic_t characteristic;
        esp_err_t err = cerberus_s3_ble_central_get_characteristic_result(
            ble_characteristic_status.generation,
            index,
            &characteristic);
        if (err != ESP_OK) {
            return err;
        }
        if (!ble_environment_characteristic_is_supported(&characteristic)) {
            continue;
        }
        bool duplicate = false;
        for (size_t prior = 0U; prior < ble_environment_reading_count; ++prior) {
            const cerberus_c5_ble_characteristic_t *stored =
                &ble_environment_readings[prior].characteristic;
            if (stored->uuid_length == characteristic.uuid_length &&
                memcmp(stored->uuid, characteristic.uuid, characteristic.uuid_length) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) {
            continue;
        }
        ui_ble_environment_reading_t *reading =
            &ble_environment_readings[ble_environment_reading_count++];
        reading->characteristic = characteristic;
        snprintf(reading->value, sizeof(reading->value), "TAP REFRESH TO READ");
    }
    ble_environment_characteristics_loaded = true;
    return ESP_OK;
}

static esp_err_t find_ble_environment_service(void)
{
    ble_environment_service_search_complete = true;
    ble_environment_service_found = false;
    if (ble_service_error != ESP_OK ||
        ble_service_status.state != CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        return ble_service_error == ESP_OK ? ESP_ERR_INVALID_STATE : ble_service_error;
    }

    for (uint8_t index = 0U; index < ble_service_status.stored_count; ++index) {
        cerberus_c5_ble_service_t service;
        esp_err_t err = cerberus_s3_ble_central_get_service_result(
            ble_service_status.generation,
            index,
            &service);
        if (err != ESP_OK) {
            return err;
        }
        if (!ble_uuid16_is(service.uuid, service.uuid_length, 0x181AU)) {
            continue;
        }
        ble_selected_service = service;
        ble_environment_service_found = true;
        ble_characteristic_checked = true;
        ble_characteristic_error = cerberus_s3_ble_central_start_characteristic_discovery(
            &ble_selected_service,
            &ble_characteristic_status);
        if (ble_characteristic_error == ESP_OK &&
            ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
            ble_characteristic_error = load_ble_environment_characteristics();
        } else if (ble_characteristic_error == ESP_OK &&
            ble_characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING) {
            ble_characteristic_error = ESP_ERR_INVALID_STATE;
        }
        return ble_characteristic_error;
    }
    return ESP_ERR_NOT_FOUND;
}

static void begin_ble_environment_discovery(void)
{
    reset_ble_environment_viewer();
    bool current_services = ble_service_checked && ble_service_error == ESP_OK &&
        ble_service_status.connection_generation == ble_connection_status.generation;
    if (current_services &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        ble_environment_error = find_ble_environment_service();
        return;
    }
    if (current_services &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING) {
        ble_environment_error = ESP_OK;
        return;
    }

    ble_service_checked = true;
    ble_characteristic_checked = false;
    ble_read_checked = false;
    ble_service_error = cerberus_s3_ble_central_start_service_discovery(&ble_service_status);
    ble_environment_error = ble_service_error;
    if (ble_service_error == ESP_OK &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        ble_environment_error = find_ble_environment_service();
    } else if (ble_service_error == ESP_OK &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING) {
        ble_environment_error = ESP_OK;
    } else if (ble_service_error == ESP_OK) {
        ble_environment_error = ESP_ERR_INVALID_STATE;
    }
}

static void continue_ble_environment_reads(void)
{
    while (ble_environment_refresh_active &&
        ble_read_status.state != CERBERUS_BLE_GATT_READ_RUNNING) {
        ui_ble_environment_reading_t *reading =
            &ble_environment_readings[ble_environment_reading_index];
        reading->complete = true;
        reading->valid = ble_read_error == ESP_OK &&
            ble_read_status.state == CERBERUS_BLE_GATT_READ_COMPLETE &&
            format_ble_standard_value(reading->value, sizeof(reading->value));
        if (!reading->valid) {
            snprintf(reading->value, sizeof(reading->value), "READ UNAVAILABLE");
        }

        ++ble_environment_reading_index;
        if (ble_environment_reading_index >= ble_environment_reading_count) {
            ble_environment_refresh_active = false;
            return;
        }
        ble_selected_characteristic =
            ble_environment_readings[ble_environment_reading_index].characteristic;
        begin_ble_characteristic_read();
    }
}

static void begin_ble_environment_reads(void)
{
    if (!ble_environment_characteristics_loaded || ble_environment_reading_count == 0U ||
        ble_environment_refresh_active) {
        return;
    }
    for (size_t index = 0U; index < ble_environment_reading_count; ++index) {
        ble_environment_readings[index].complete = false;
        ble_environment_readings[index].valid = false;
        snprintf(ble_environment_readings[index].value,
            sizeof(ble_environment_readings[index].value), "WAITING");
    }
    ble_environment_reading_index = 0U;
    ble_environment_refresh_active = true;
    ble_selected_characteristic = ble_environment_readings[0].characteristic;
    begin_ble_characteristic_read();
    continue_ble_environment_reads();
}

static void format_ble_characteristic_properties(
    uint8_t properties,
    char *text,
    size_t text_size)
{
    size_t used = 0;
    text[0] = '\0';
    const struct {
        uint8_t flag;
        const char *label;
    } labels[] = {
        {0x02U, "R"},
        {0x04U, "WNR"},
        {0x08U, "W"},
        {0x10U, "N"},
        {0x20U, "I"},
        {0x01U, "B"},
        {0x40U, "SW"},
        {0x80U, "EXT"},
    };
    for (size_t index = 0; index < sizeof(labels) / sizeof(labels[0]); ++index) {
        if ((properties & labels[index].flag) == 0U || used >= text_size) {
            continue;
        }
        int written = snprintf(
            text + used,
            text_size - used,
            "%s%s",
            used == 0U ? "" : " ",
            labels[index].label);
        if (written < 0 || (size_t)written >= text_size - used) {
            break;
        }
        used += (size_t)written;
    }
    if (used == 0U) {
        snprintf(text, text_size, "NONE");
    }
}

static esp_err_t draw_ble_services(void)
{
    char line[64];
    snprintf(
        line,
        sizeof(line),
        "%u SERVICES  TAP ONE",
        (unsigned)ble_service_status.total_count);
    ESP_RETURN_ON_ERROR(draw_centered(56, line, UI_GOOD, 1), TAG, "BLE service summary failed");

    if (ble_service_error != ESP_OK ||
        ble_service_status.state != CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        ESP_RETURN_ON_ERROR(draw_centered(170, "SERVICE RESULTS UNAVAILABLE", UI_BAD, 2), TAG, "BLE services unavailable failed");
    } else if (ble_service_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(180, "NO PRIMARY SERVICES", UI_MUTED, 2), TAG, "BLE services empty failed");
    } else {
        for (size_t row = 0; row < ble_service_count; ++row) {
            uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
            const cerberus_c5_ble_service_t *service = &ble_services[row];
            char uuid[40];
            format_ble_uuid(service->uuid, service->uuid_length, uuid, sizeof(uuid));
            ESP_RETURN_ON_ERROR(
                cerberus_display_fill_rect(8, y, 304, UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL),
                TAG,
                "BLE service row failed");
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(
                    14,
                    y + 5U,
                    ble_standard_service_name(service),
                    UI_TEXT,
                    UI_PANEL,
                    1),
                TAG,
                "BLE service name failed");
            snprintf(
                line,
                sizeof(line),
                "%s  %04X-%04X",
                uuid,
                service->start_handle,
                service->end_handle);
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 22U, line, UI_MUTED, UI_PANEL, 1),
                TAG,
                "BLE service UUID failed");
        }
    }

    ESP_RETURN_ON_ERROR(
        draw_small_button(8, UI_WIFI_CONTROLS_Y, 148, 42, "PREV SERVICES"),
        TAG,
        "BLE services prev failed");
    return draw_small_button(164, UI_WIFI_CONTROLS_Y, 148, 42, "NEXT SERVICES");
}

static esp_err_t draw_ble_characteristics(void)
{
    char line[64];
    char service_uuid[40];
    format_ble_uuid(
        ble_selected_service.uuid,
        ble_selected_service.uuid_length,
        service_uuid,
        sizeof(service_uuid));
    snprintf(
        line,
        sizeof(line),
        "%u CHARACTERISTICS  READ ONLY",
        (unsigned)ble_characteristic_status.total_count);
    ESP_RETURN_ON_ERROR(draw_centered(54, line, UI_GOOD, 1), TAG, "BLE characteristic summary failed");
    snprintf(line, sizeof(line), "SERVICE %s", service_uuid);
    ESP_RETURN_ON_ERROR(draw_centered(68, line, UI_MUTED, 1), TAG, "BLE characteristic service failed");

    if (ble_characteristic_error != ESP_OK) {
        ESP_RETURN_ON_ERROR(
            draw_centered(170, "CHARACTERISTICS UNAVAILABLE", UI_BAD, 2),
            TAG,
            "BLE characteristics unavailable failed");
    } else if (ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING) {
        ESP_RETURN_ON_ERROR(
            draw_centered(170, "DISCOVERING CHARACTERISTICS", UI_GOOD, 1),
            TAG,
            "BLE characteristics running failed");
    } else if (ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_ERROR) {
        snprintf(line, sizeof(line), "C5 ERROR %ld", (long)ble_characteristic_status.error_code);
        ESP_RETURN_ON_ERROR(draw_centered(160, "DISCOVERY FAILED", UI_BAD, 2), TAG, "BLE characteristics failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "BLE characteristic error failed");
    } else if (ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE &&
        ble_characteristic_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(180, "NO CHARACTERISTICS", UI_MUTED, 2), TAG, "BLE characteristics empty failed");
    } else if (ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
        for (size_t row = 0; row < ble_characteristic_count; ++row) {
            uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
            const cerberus_c5_ble_characteristic_t *characteristic = &ble_characteristics[row];
            char uuid[40];
            char properties[32];
            format_ble_uuid(characteristic->uuid, characteristic->uuid_length, uuid, sizeof(uuid));
            format_ble_characteristic_properties(
                characteristic->properties,
                properties,
                sizeof(properties));
            ESP_RETURN_ON_ERROR(
                cerberus_display_fill_rect(8, y, 304, UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL),
                TAG,
                "BLE characteristic row failed");
            snprintf(
                line,
                sizeof(line),
                "%s  %s",
                ble_standard_characteristic_name(characteristic),
                uuid);
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 5U, line, UI_TEXT, UI_PANEL, 1),
                TAG,
                "BLE characteristic name failed");
            snprintf(
                line,
                sizeof(line),
                "DEF %04X  VAL %04X  %s",
                characteristic->definition_handle,
                characteristic->value_handle,
                properties);
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 22U, line, UI_MUTED, UI_PANEL, 1),
                TAG,
                "BLE characteristic properties failed");
        }
    }

    ESP_RETURN_ON_ERROR(
        draw_small_button(8, UI_WIFI_CONTROLS_Y, 148, 42, "PREV CHARS"),
        TAG,
        "BLE characteristic prev failed");
    return draw_small_button(164, UI_WIFI_CONTROLS_Y, 148, 42, "NEXT CHARS");
}

static esp_err_t draw_ble_characteristic_value(void)
{
    char line[72];
    char uuid[40];
    char properties[32];
    bool readable = (ble_selected_characteristic.properties & 0x02U) != 0U;
    format_ble_uuid(
        ble_selected_characteristic.uuid,
        ble_selected_characteristic.uuid_length,
        uuid,
        sizeof(uuid));
    format_ble_characteristic_properties(
        ble_selected_characteristic.properties,
        properties,
        sizeof(properties));

    ESP_RETURN_ON_ERROR(
        draw_centered(
            52,
            ble_standard_characteristic_name(&ble_selected_characteristic),
            UI_TEXT,
            1),
        TAG,
        "BLE value name failed");
    ESP_RETURN_ON_ERROR(draw_centered(68, uuid, UI_GOOD, 1), TAG, "BLE value UUID failed");
    snprintf(
        line,
        sizeof(line),
        "DEF %04X  VAL %04X  %s",
        ble_selected_characteristic.definition_handle,
        ble_selected_characteristic.value_handle,
        properties);
    ESP_RETURN_ON_ERROR(draw_centered(86, line, UI_MUTED, 1), TAG, "BLE value metadata failed");

    if (!readable) {
        ESP_RETURN_ON_ERROR(draw_centered(150, "READ NOT ADVERTISED", UI_MUTED, 2), TAG, "BLE not readable failed");
        return draw_centered(196, "NO REQUEST WAS SENT", UI_MUTED, 1);
    }
    if (ble_read_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(ble_read_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "READ SERVICE UNAVAILABLE", UI_BAD, 1), TAG, "BLE read unavailable failed");
        ESP_RETURN_ON_ERROR(draw_centered(175, line, UI_MUTED, 1), TAG, "BLE read link error failed");
    } else if (ble_read_status.state == CERBERUS_BLE_GATT_READ_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(150, "READING VALUE", UI_GOOD, 2), TAG, "BLE read running failed");
    } else if (ble_read_status.state == CERBERUS_BLE_GATT_READ_ERROR) {
        snprintf(line, sizeof(line), "ATT / C5 ERROR %ld", (long)ble_read_status.error_code);
        ESP_RETURN_ON_ERROR(draw_centered(140, "READ REJECTED OR LOCKED", UI_BAD, 1), TAG, "BLE read failed");
        ESP_RETURN_ON_ERROR(draw_centered(170, line, UI_MUTED, 1), TAG, "BLE read error failed");
        ESP_RETURN_ON_ERROR(draw_centered(196, "PAIRING IS NEVER ATTEMPTED", UI_MUTED, 1), TAG, "BLE pairing policy failed");
    } else if (ble_read_status.state == CERBERUS_BLE_GATT_READ_COMPLETE) {
        char decoded[64];
        bool decoded_available = format_ble_standard_value(decoded, sizeof(decoded));
        snprintf(
            line,
            sizeof(line),
            "LENGTH %u%s",
            (unsigned)ble_read_status.total_length,
            ble_read_status.total_length > ble_read_status.stored_length ? "  PREVIEW TRUNCATED" : "");
        ESP_RETURN_ON_ERROR(draw_centered(112, line, UI_GOOD, 1), TAG, "BLE value length failed");
        if (ble_read_value_length == 0U) {
            ESP_RETURN_ON_ERROR(draw_centered(185, "EMPTY VALUE", UI_MUTED, 2), TAG, "BLE empty value failed");
        } else {
            if (decoded_available) {
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(12, 128, "DECODED VALUE", UI_MUTED, UI_BACKGROUND, 1),
                    TAG,
                    "BLE decoded label failed");
                ESP_RETURN_ON_ERROR(
                    draw_centered(144, decoded, UI_GOOD, 1),
                    TAG,
                    "BLE decoded value failed");
            }
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(12, 166, "TEXT PREVIEW", UI_MUTED, UI_BACKGROUND, 1),
                TAG,
                "BLE text preview label failed");
            size_t text_length = ble_read_value_length < 120U ? ble_read_value_length : 120U;
            for (size_t row = 0; row < 3U; ++row) {
                size_t offset = row * 40U;
                if (offset >= text_length) {
                    break;
                }
                size_t count = text_length - offset < 40U ? text_length - offset : 40U;
                for (size_t index = 0; index < count; ++index) {
                    uint8_t value = ble_read_value[offset + index];
                    line[index] = value >= 32U && value <= 126U ? (char)value : '.';
                }
                line[count] = '\0';
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(
                        12,
                        182U + (uint16_t)row * 16U,
                        line,
                        UI_TEXT,
                        UI_BACKGROUND,
                        1),
                    TAG,
                    "BLE text preview failed");
            }

            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(12, 234, "HEX PREVIEW", UI_MUTED, UI_BACKGROUND, 1),
                TAG,
                "BLE hex preview label failed");
            size_t hex_length = ble_read_value_length < 48U ? ble_read_value_length : 48U;
            for (size_t row = 0; row < 3U; ++row) {
                size_t offset = row * 16U;
                if (offset >= hex_length) {
                    break;
                }
                size_t count = hex_length - offset < 16U ? hex_length - offset : 16U;
                size_t used = 0;
                for (size_t index = 0; index < count; ++index) {
                    int written = snprintf(
                        line + used,
                        sizeof(line) - used,
                        "%02X%s",
                        ble_read_value[offset + index],
                        index + 1U == count ? "" : " ");
                    if (written < 0 || (size_t)written >= sizeof(line) - used) {
                        break;
                    }
                    used += (size_t)written;
                }
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(
                        12,
                        250U + (uint16_t)row * 16U,
                        line,
                        UI_TEXT,
                        UI_BACKGROUND,
                        1),
                    TAG,
                    "BLE hex preview failed");
            }
        }
    }

    return draw_button(40, 342, 240, 46, "READ AGAIN");
}

static esp_err_t draw_ble_environment(void)
{
    char line[72];
    const char *name = ble_selected_result.name[0] != '\0'
        ? ble_selected_result.name
        : "<UNNAMED DEVICE>";
    ESP_RETURN_ON_ERROR(draw_centered(54, "ENVIRONMENTAL SENSOR", UI_TEXT, 2), TAG,
        "BLE environment title failed");
    snprintf(line, sizeof(line), "%.30s", name);
    ESP_RETURN_ON_ERROR(draw_centered(80, line, UI_MUTED, 1), TAG,
        "BLE environment device failed");

    bool connected = ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        ble_connection_matches_selected();
    if (!connected) {
        ESP_RETURN_ON_ERROR(draw_centered(172, "SELECTED DEVICE IS NOT CONNECTED", UI_BAD, 1), TAG,
            "BLE environment disconnected failed");
        ESP_RETURN_ON_ERROR(draw_centered(204, "CONNECT IT FROM SELECTED DEVICE", UI_MUTED, 1), TAG,
            "BLE environment connection guidance failed");
        return draw_button(40, 330, 240, 52, "OPEN SELECTED DEVICE");
    }
    if (ble_service_checked &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(176, "DISCOVERING GATT SERVICES", UI_GOOD, 2), TAG,
            "BLE environment service discovery failed");
        return draw_centered(212, "READ-ONLY DISCOVERY", UI_MUTED, 1);
    }
    if (ble_service_checked &&
        ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_ERROR) {
        snprintf(line, sizeof(line), "SERVICE DISCOVERY ERROR %ld",
            (long)ble_service_status.error_code);
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_BAD, 1), TAG,
            "BLE environment service error failed");
        return draw_button(40, 330, 240, 52, "TRY AGAIN");
    }
    if (ble_environment_service_search_complete && !ble_environment_service_found) {
        ESP_RETURN_ON_ERROR(draw_centered(166, "NO STANDARD ENVIRONMENT SERVICE", UI_MUTED, 1), TAG,
            "BLE environment absent failed");
        ESP_RETURN_ON_ERROR(draw_centered(194, "EXPECTED SERVICE UUID 0x181A", UI_MUTED, 1), TAG,
            "BLE environment UUID guidance failed");
        ESP_RETURN_ON_ERROR(draw_centered(222, "VENDOR-SPECIFIC SENSORS MAY NOT APPEAR", UI_MUTED, 1), TAG,
            "BLE environment vendor guidance failed");
        return draw_button(40, 330, 240, 52, "DISCOVER AGAIN");
    }
    if (ble_characteristic_checked &&
        ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(176, "FINDING SENSOR VALUES", UI_GOOD, 2), TAG,
            "BLE environment characteristic discovery failed");
        return draw_centered(212, "TEMPERATURE / HUMIDITY / PRESSURE", UI_MUTED, 1);
    }
    if (ble_characteristic_checked &&
        ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_ERROR) {
        snprintf(line, sizeof(line), "SENSOR DISCOVERY ERROR %ld",
            (long)ble_characteristic_status.error_code);
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_BAD, 1), TAG,
            "BLE environment characteristic error failed");
        return draw_button(40, 330, 240, 52, "TRY AGAIN");
    }
    if (ble_environment_error != ESP_OK &&
        ble_environment_error != ESP_ERR_NOT_FOUND) {
        snprintf(line, sizeof(line), "DISCOVERY ERROR %s", esp_err_to_name(ble_environment_error));
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_BAD, 1), TAG,
            "BLE environment error failed");
        return draw_button(40, 330, 240, 52, "TRY AGAIN");
    }
    if (ble_environment_characteristics_loaded && ble_environment_reading_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(174, "NO READABLE STANDARD SENSOR VALUES", UI_MUTED, 1), TAG,
            "BLE environment empty failed");
        ESP_RETURN_ON_ERROR(draw_centered(206, "GATT WRITES AND PAIRING ARE NOT ATTEMPTED", UI_MUTED, 1), TAG,
            "BLE environment policy failed");
        return draw_button(40, 330, 240, 52, "DISCOVER AGAIN");
    }

    for (size_t index = 0U; index < ble_environment_reading_count; ++index) {
        const ui_ble_environment_reading_t *reading = &ble_environment_readings[index];
        uint16_t y = 116U + (uint16_t)index * 64U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(16, y, 288, 52, UI_PANEL), TAG,
            "BLE environment row failed");
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(
            24, y + 7U, ble_standard_characteristic_name(&reading->characteristic),
            UI_TEXT, UI_PANEL, 1), TAG, "BLE environment label failed");
        const char *value = reading->value[0] != '\0' ? reading->value : "TAP REFRESH TO READ";
        uint16_t color = reading->valid ? UI_GOOD :
            (reading->complete ? UI_BAD : UI_MUTED);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(
            24, y + 27U, value, color, UI_PANEL, 1), TAG,
            "BLE environment value failed");
    }

    snprintf(line, sizeof(line), "%u STANDARD VALUE%s FOUND",
        (unsigned)ble_environment_reading_count,
        ble_environment_reading_count == 1U ? "" : "S");
    ESP_RETURN_ON_ERROR(draw_centered(310, line, UI_MUTED, 1), TAG,
        "BLE environment count failed");
    return draw_button(40, 330, 240, 52,
        ble_environment_refresh_active ? "READING SENSORS" : "REFRESH SENSORS");
}

/* Correct only the age suffix produced by existing hint formatters. The
 * frozen copy ceiling disambiguates a source's low32 clock; it is never a
 * replacement observation time. No packet or action eligibility is changed. */
static void ble_hint_age64_line(char *line, size_t capacity, uint32_t observed_ms,
    uint64_t source_ceiling_ms, int64_t now_ms)
{
    if (!line || !capacity || !memchr(line, '\0', capacity)) return;
    char *suffix = NULL;
    for (char *part = strstr(line, " / "); part; part = strstr(part + 3, " / "))
        suffix = part + 3;
    /* Lines without a matching source may contain a scope slash but no age. */
    if (!suffix || !((*suffix >= '0' && *suffix <= '9') || !strncmp(suffix, "AGE ", 4U)))
        return;
    size_t offset = (size_t)(suffix - line);
    if (offset >= capacity) return;
    uint32_t elapsed_at_copy = (uint32_t)source_ceiling_ms - observed_ms;
    if (now_ms < 0 || (uint64_t)now_ms < source_ceiling_ms || source_ceiling_ms < elapsed_at_copy) {
        snprintf(suffix, capacity - offset, "AGE ?");
        return;
    }
    uint64_t age = (uint64_t)now_ms - (source_ceiling_ms - elapsed_at_copy);
    if (age > UINT32_MAX) snprintf(suffix, capacity - offset, ">49d");
    else snprintf(suffix, capacity - offset, "%lus", (unsigned long)(age / 1000U));
}

static void ble_selected_hint_age_line(char *line, size_t capacity,
    uint32_t observed_ms, int64_t now_ms)
{
    if (ble_selected_original_time64_valid)
        ble_hint_age64_line(line, capacity, observed_ms, ble_selected_source_ceiling_ms64, now_ms);
}

static esp_err_t draw_ble_detail(void)
{
    char line[64];
    char identifier[24];
    char service[20];
    bool favorite = cerberus_settings_ble_is_favorite(
        ble_selected_result.address,
        ble_selected_result.address_type);
    bool connectable = (ble_selected_result.flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U;
    bool gatt_capable = (ui_capability_map & UI_CAP_BLE_GATT) != 0U;

    if (gatt_capable && !ble_connection_checked) {
        (void)refresh_ble_connection_status();
    }
    bool selected_connected = ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        ble_connection_matches_selected();
    if (selected_connected && !ble_service_checked) {
        (void)refresh_ble_service_status();
    }

    const char *name = ble_selected_result.name[0] != '\0'
        ? ble_selected_result.name
        : "<UNNAMED>";
    snprintf(line, sizeof(line), "%.25s", name);
    ESP_RETURN_ON_ERROR(draw_centered(62, line, favorite ? UI_GOOD : UI_TEXT, 2), TAG, "BLE detail name failed");

    cerberus_privacy_format_ble_identifier(
        ble_selected_result.address,
        privacy_addresses_masked(),
        identifier,
        sizeof(identifier));
    ESP_RETURN_ON_ERROR(draw_centered(96, identifier, UI_MUTED, 1), TAG, "BLE detail address failed");

    format_ble_service(&ble_selected_result, service, sizeof(service));
    if (cerberus_ble_rssi_valid(ble_selected_result.rssi)) {
        snprintf(line, sizeof(line), "PEAK RSSI %d dBm / %s / %s",
            ble_selected_result.rssi,
            (ble_selected_result.address_type & 1U) != 0U ? "RANDOM" : "PUBLIC",
            connectable ? "CONNECTABLE" : "ADV ONLY");
    } else {
        snprintf(line, sizeof(line), "PEAK RSSI UNAVAILABLE / %s / %s",
            (ble_selected_result.address_type & 1U) != 0U ? "RANDOM" : "PUBLIC",
            connectable ? "CONNECTABLE" : "ADV ONLY");
    }
    ESP_RETURN_ON_ERROR(draw_centered(124, line, UI_TEXT, 1), TAG, "BLE detail metadata failed");

    if (ble_selected_result.manufacturer_id != CERBERUS_BLE_MANUFACTURER_UNKNOWN) {
        snprintf(line, sizeof(line), "MANUFACTURER %04X  SERVICE %s",
            ble_selected_result.manufacturer_id,
            service[0] != '\0' ? service : "NONE ADVERTISED");
    } else {
        snprintf(line, sizeof(line), "SERVICE %s", service[0] != '\0' ? service : "NONE ADVERTISED");
    }
    ESP_RETURN_ON_ERROR(draw_centered(150, line, UI_MUTED, 1), TAG, "BLE detail service failed");

    int64_t now_ms = esp_timer_get_time() / 1000LL;
    refresh_selected_vendor(true, ble_selected_result.address, ble_selected_result.address_type, now_ms);
    if (vendor_cache.result == RECON_DB_FOUND)
        snprintf(line, sizeof(line), "VENDOR %.40s", vendor_cache.name);
    else if (vendor_cache.result == RECON_DB_PRIVATE_ADDRESS)
        snprintf(line, sizeof(line), "RANDOM/LOCAL ADDRESS: NO OUI");
    else if (vendor_cache.result == RECON_DB_UNKNOWN)
        snprintf(line, sizeof(line), "VENDOR UNKNOWN");
    else
        snprintf(line, sizeof(line), "SD VENDOR DATA UNAVAILABLE");
    ESP_RETURN_ON_ERROR(draw_centered(174, line, UI_MUTED, 1), TAG, "BLE detail vendor failed");

    const char *connection_text = "READY";
    uint16_t connection_color = UI_GOOD;
    if (!gatt_capable || ble_connection_error != ESP_OK) {
        connection_text = "CONNECTION SERVICE UNAVAILABLE";
        connection_color = UI_BAD;
    } else if (ble_connection_is_active() && !ble_connection_matches_selected()) {
        connection_text = "ANOTHER DEVICE IS ACTIVE";
        connection_color = UI_MUTED;
    } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTING) {
        connection_text = "CONNECTING";
    } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED) {
        connection_text = "CONNECTED";
    } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_DISCONNECTING) {
        connection_text = "DISCONNECTING";
        connection_color = UI_MUTED;
        if (ble_connection_status.error_code) {
            snprintf(line, sizeof(line), "CLOSE ERROR %ld", (long)ble_connection_status.error_code);
            connection_text = line;
            connection_color = UI_BAD;
        }
    } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_ERROR) {
        snprintf(line, sizeof(line), "CONNECTION ERROR %ld", (long)ble_connection_status.error_code);
        connection_text = line;
        connection_color = UI_BAD;
    }
    ESP_RETURN_ON_ERROR(draw_centered(198, connection_text, connection_color, 2), TAG, "BLE connection state failed");
    const char *guidance = ble_action_message[0] != '\0'
        ? ble_action_message
        : (connectable ? "TAP CONNECT TO OPEN A LINK" : "ADVERTISEMENT IS NOT CONNECTABLE");
    uint16_t guidance_color = ble_action_message[0] != '\0'
        ? ble_action_color
        : (connectable ? UI_GOOD : UI_MUTED);
    ESP_RETURN_ON_ERROR(draw_centered(228, guidance, guidance_color, 1), TAG, "BLE action guidance failed");
    const cerberus_ble_flock_level_t flock_adv_level = cerberus_ble_flock_hints_level(
        &ble_selected_focus.discovery.flock_advertisement);
    const cerberus_ble_flock_level_t flock_rsp_level = cerberus_ble_flock_hints_level(
        &ble_selected_focus.discovery.flock_scan_response);
    if ((ble_selected_focus.flock_oui_clue || flock_adv_level || flock_rsp_level) &&
        (ble_filter == CERBERUS_S3_BLE_FILTER_FLOCK ||
         (ble_filter == CERBERUS_S3_BLE_FILTER_ALL && !ble_selected_focus.ibeacon_valid &&
          !ble_selected_focus.discovery.findmy_advertisement.bits && !ble_selected_focus.discovery.findmy_scan_response.bits &&
          !ble_selected_focus.discovery.flipper_advertisement.bits && !ble_selected_focus.discovery.flipper_scan_response.bits &&
          ((flock_adv_level >= CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE || flock_rsp_level >= CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE) ||
            (!ble_selected_focus.discovery.hc_advertisement && !ble_selected_focus.discovery.hc_scan_response &&
             !ble_selected_focus.meta_suppressed &&
            !cerberus_ble_meta_evidence_count(&ble_selected_focus.discovery.meta_advertisement) &&
            !cerberus_ble_meta_evidence_count(&ble_selected_focus.discovery.meta_scan_response)))))) {
        cerberus_flock_address_line(ble_selected_result.address, ble_selected_focus.flock_oui_clue,
            privacy_addresses_masked(), line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(240, line, UI_MUTED, 1), TAG, "Flock address scope failed");
        cerberus_flock_hint_line(&ble_selected_focus.discovery.flock_advertisement, false,
            (uint32_t)now_ms - ble_selected_focus.discovery.advertisement_observed_ms, line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.advertisement_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG, "Flock ADV facts failed");
        cerberus_flock_hint_line(&ble_selected_focus.discovery.flock_scan_response, true,
            (uint32_t)now_ms - ble_selected_focus.discovery.scan_response_observed_ms, line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.scan_response_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG, "Flock response facts failed");
    } else if ((ble_filter == CERBERUS_S3_BLE_FILTER_META_HINT ||
         (ble_filter == CERBERUS_S3_BLE_FILTER_ALL && !ble_selected_focus.ibeacon_valid &&
          !(ble_selected_focus.category_bits & (CERBERUS_S3_BLE_FOCUS_FIND_MY |
            CERBERUS_S3_BLE_FOCUS_FLIPPER)))) &&
        ((ble_selected_focus.meta_suppressed &&
          (ble_filter == CERBERUS_S3_BLE_FILTER_META_HINT ||
           (!ble_selected_focus.discovery.hc_advertisement && !ble_selected_focus.discovery.hc_scan_response))) ||
         cerberus_ble_meta_evidence_count(&ble_selected_focus.discovery.meta_advertisement) ||
         cerberus_ble_meta_evidence_count(&ble_selected_focus.discovery.meta_scan_response))) {
        ESP_RETURN_ON_ERROR(draw_centered(240, ble_selected_focus.meta_suppressed ?
            "META FILTER SUPPRESSED FOR THIS SWEEP" : "META IDENTIFIER HINTS / NOT DEVICE IDENTITY",
            UI_MUTED, 1), TAG, "Meta identifier scope failed");
        cerberus_meta_hint_line(&ble_selected_focus.discovery.meta_advertisement, false,
            (uint32_t)now_ms - ble_selected_focus.discovery.advertisement_observed_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.advertisement_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG,
            "Meta original ADV pairs failed");
        cerberus_meta_hint_line(&ble_selected_focus.discovery.meta_scan_response, true,
            (uint32_t)now_ms - ble_selected_focus.discovery.scan_response_observed_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.scan_response_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG,
            "Meta matched response pairs failed");
    } else if ((ble_selected_focus.discovery.flipper_advertisement.bits ||
         ble_selected_focus.discovery.flipper_scan_response.bits) &&
        (ble_filter == CERBERUS_S3_BLE_FILTER_FLIPPER ||
         (ble_filter == CERBERUS_S3_BLE_FILTER_ALL && !ble_selected_focus.ibeacon_valid &&
          !ble_selected_focus.discovery.findmy_advertisement.bits &&
          !ble_selected_focus.discovery.findmy_scan_response.bits))) {
        ESP_RETURN_ON_ERROR(draw_centered(240,
            "+NAME=SAME REPORT / +DATA=SERVICE DATA", UI_MUTED, 1), TAG,
            "Flipper hint scope failed");
        cerberus_flipper_hint_line(&ble_selected_focus.discovery.flipper_advertisement, false,
            ble_selected_focus.discovery.advertisement_observed_ms, (uint32_t)now_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.advertisement_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG,
            "Flipper original ADV hints failed");
        cerberus_flipper_hint_line(&ble_selected_focus.discovery.flipper_scan_response, true,
            ble_selected_focus.discovery.scan_response_observed_ms, (uint32_t)now_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.scan_response_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG,
            "Flipper matched response hints failed");
    } else if ((ble_selected_focus.discovery.findmy_advertisement.bits ||
         ble_selected_focus.discovery.findmy_scan_response.bits) &&
        (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY ||
         (ble_filter == CERBERUS_S3_BLE_FILTER_ALL && !ble_selected_focus.ibeacon_valid))) {
        cerberus_findmy_hint_line(&ble_selected_focus.discovery.findmy_advertisement, false,
            ble_selected_focus.discovery.advertisement_observed_ms, (uint32_t)now_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.advertisement_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG,
            "Find My original ADV hints failed");
        cerberus_findmy_hint_line(&ble_selected_focus.discovery.findmy_scan_response, true,
            ble_selected_focus.discovery.scan_response_observed_ms, (uint32_t)now_ms,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.scan_response_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG,
            "Find My matched response hints failed");
    } else if (ble_selected_focus.ibeacon_valid &&
        ble_filter != CERBERUS_S3_BLE_FILTER_HC_NAME_HINT) {
        const cerberus_ibeacon_t *beacon = &ble_selected_focus.ibeacon;
        cerberus_ibeacon_source_line(ble_selected_focus.ibeacon_scan_response,
            cerberus_s3_ble_ibeacon_is_current(&ble_selected_focus),
            (uint32_t)now_ms - ble_selected_focus.ibeacon_observed_ms, false,
            line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.ibeacon_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(240, line, UI_MUTED, 1), TAG,
            "iBeacon source age failed");
        cerberus_ibeacon_values_line(beacon, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG,
            "iBeacon values failed");
        cerberus_ibeacon_uuid_line(beacon, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG,
            "iBeacon UUID failed");
    } else if ((ble_selected_focus.discovery.hc_advertisement || ble_selected_focus.discovery.hc_scan_response) &&
        (ble_filter == CERBERUS_S3_BLE_FILTER_HC_NAME_HINT || ble_filter == CERBERUS_S3_BLE_FILTER_ALL)) {
        ESP_RETURN_ON_ERROR(draw_centered(240,
            "ADVERTISED NAME HINT / NOT DEVICE IDENTITY", UI_MUTED, 1), TAG, "HC name scope failed");
        cerberus_hc_hint_line(ble_selected_focus.discovery.hc_advertisement, false,
            (uint32_t)now_ms - ble_selected_focus.discovery.advertisement_observed_ms, line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.advertisement_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG, "HC original name failed");
        cerberus_hc_hint_line(ble_selected_focus.discovery.hc_scan_response, true,
            (uint32_t)now_ms - ble_selected_focus.discovery.scan_response_observed_ms, line, sizeof(line));
        ble_selected_hint_age_line(line, sizeof(line), ble_selected_focus.discovery.scan_response_observed_ms, now_ms);
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG, "HC response name failed");
    } else if (ble_selected_focus.category_bits != 0U) {
        snprintf(line, sizeof(line), "HINT: %s%s%s%s%s",
            (ble_selected_focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY) ? "FINDMY " : "",
            (ble_selected_focus.category_bits & CERBERUS_S3_BLE_FOCUS_FLIPPER) ? "FLIPPER " : "",
            (ble_selected_focus.category_bits & CERBERUS_S3_BLE_FOCUS_FLOCK) ? "FLOCK " : "",
            (ble_selected_focus.category_bits & CERBERUS_S3_BLE_FOCUS_META_HINT) ? "META-ID " : "",
            (ble_selected_focus.category_bits & CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT) ? "HC-NAME" : "");
        ESP_RETURN_ON_ERROR(draw_centered(252, line, UI_GOOD, 1), TAG,
            "BLE focused hint failed");
        const char *evidence = ble_selected_focus.findmy_evidence ?
            ble_selected_focus.findmy_evidence :
            ble_selected_focus.flipper_evidence ?
                ble_selected_focus.flipper_evidence :
                ble_selected_focus.flock_evidence ? ble_selected_focus.flock_evidence :
                ble_selected_focus.meta_evidence ? ble_selected_focus.meta_evidence :
                (ble_selected_focus.discovery.hc_advertisement || ble_selected_focus.discovery.hc_scan_response) ?
                    "HC ADVERTISED NAME / PURPOSE UNKNOWN" : NULL;
        if (ble_filter == CERBERUS_S3_BLE_FILTER_META_HINT &&
            ble_selected_focus.meta_evidence)
            evidence = ble_selected_focus.meta_evidence;
        snprintf(line, sizeof(line), "%.43s", evidence ? evidence :
            "UNAUTHENTICATED ADVERTISEMENT");
        ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1), TAG,
            "BLE hint evidence failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(252,
            ble_selected_sound_profile != CERBERUS_FINDMY_PROFILE_NONE ?
                "FIND MY SERVICE HINT / GATT SOUND AVAILABLE" :
                "USER INITIATED / NO PAIRING / NO WRITES", UI_MUTED, 1),
            TAG, "BLE scope failed");
    }

    const char *discovery_label = "CONNECT FIRST";
    if (selected_connected && ble_service_error == ESP_OK &&
        ble_service_status.connection_generation == ble_connection_status.generation) {
        if (ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING) {
            discovery_label = "DISCOVERING";
        } else if (ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
            snprintf(
                line,
                sizeof(line),
                "VIEW %u",
                (unsigned)ble_service_status.total_count);
            discovery_label = line;
        } else if (ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_ERROR) {
            discovery_label = "RETRY";
        } else {
            discovery_label = "DISCOVER";
        }
    } else if (selected_connected) {
        discovery_label = "DISCOVER";
    }
    ESP_RETURN_ON_ERROR(
        draw_button(10, 286, 144, 38, "TRACK RSSI"),
        TAG,
        "BLE tracking button failed");
    ESP_RETURN_ON_ERROR(
        draw_button(166, 286, 144, 38, discovery_label),
        TAG,
        "BLE discovery button failed");

    ESP_RETURN_ON_ERROR(
        draw_button(
            10,
            330,
            144,
            54,
            favorite && ble_connection_is_active() && ble_connection_matches_selected()
                ? "FAV LOCKED"
                : (favorite ? "UNFAVORITE" : "FAVORITE")),
        TAG,
        "BLE favorite button failed");

    const cerberus_ble_link_control_t link_control = ble_connection_control();
    ESP_RETURN_ON_ERROR(cerberus_ui_widget_draw_button(
        (cerberus_ui_rect_t){166, 330, 144, 54}, link_control.label, 2U,
        link_control.action != CERBERUS_BLE_LINK_NONE, UI_WIDGET_THEME),
        TAG, "BLE connection button failed");
    const bool sound_fresh = cerberus_findmy_ui_fresh(ble_selected_scan_ms,
        (uint32_t)now_ms);
    const bool sound_ready = gatt_capable && connectable && sound_fresh &&
        ble_selected_sound_profile != CERBERUS_FINDMY_PROFILE_NONE &&
        !ble_connection_is_active();
    const char *sound_label = !gatt_capable ? "S3 CENTRAL UNAVAILABLE" :
        !connectable ? "SOUND NEEDS CONNECTABLE AD" :
        ble_selected_sound_profile == CERBERUS_FINDMY_PROFILE_NONE ?
            "NO SUPPORTED SOUND PROFILE" :
        !sound_fresh ? "SCAN AGAIN FOR SOUND" :
        ble_connection_is_active() ? "DISCONNECT BEFORE SOUND" :
            "BEEP SELECTED DEVICE";
    return cerberus_ui_widget_draw_button(
        (cerberus_ui_rect_t){24, 390, 272, 24}, sound_label, 1,
        sound_ready, UI_WIDGET_THEME);
}

static esp_err_t draw_ble_fox_hunt(void)
{
    char line[64], identifier[24];
    cerberus_s3_ble_track_status_t track = {0};
    esp_err_t tracking_error = cerberus_s3_ble_scanner_track_status(&track);
    cerberus_privacy_format_ble_identifier(ble_selected_result.address,
        privacy_addresses_masked(), identifier, sizeof(identifier));
    ESP_RETURN_ON_ERROR(draw_centered(68, "FOX HUNT / LIVE BLE RSSI", UI_TEXT, 2),
        TAG, "BLE Fox Hunt title failed");
    snprintf(line, sizeof(line), "%.30s", ble_selected_result.name[0] ?
        ble_selected_result.name : "<UNNAMED DEVICE>");
    ESP_RETURN_ON_ERROR(draw_centered(92, line, UI_TEXT, 1),
        TAG, "BLE Fox Hunt name failed");
    ESP_RETURN_ON_ERROR(draw_centered(110, identifier, UI_MUTED, 1),
        TAG, "BLE Fox Hunt identity failed");
    const char *state = "SEARCHING";
    uint16_t state_color = UI_MUTED;
    if (ble_fox_hunt_exit_pending) state = "STOPPING";
    else if (ble_fox_hunt_error != ESP_OK || tracking_error != ESP_OK) {
        state = "SCAN ERROR";
        state_color = UI_BAD;
    } else if (track.fresh) {
        state = "LIVE";
        state_color = UI_GOOD;
    } else if (track.seen) {
        state = "STALE / TARGET NOT SEEN";
    }
    ESP_RETURN_ON_ERROR(draw_centered(140, state, state_color, 2),
        TAG, "BLE Fox Hunt state failed");
    if (track.seen) {
        snprintf(line, sizeof(line), "LAST RSSI %d dBm / AGE %lu.%lus",
            (int)track.rssi_dbm, (unsigned long)(track.age_ms / 1000U),
            (unsigned long)((track.age_ms % 1000U) / 100U));
        ESP_RETURN_ON_ERROR(draw_centered(176, line, track.fresh ? UI_GOOD : UI_MUTED, 1),
            TAG, "BLE Fox Hunt RSSI failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(176, "WAITING FOR THIS EXACT ADDRESS", UI_MUTED, 1),
            TAG, "BLE Fox Hunt search failed");
    }
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(20, 204, 280, 26, UI_PANEL),
        TAG, "BLE Fox Hunt meter background failed");
    if (track.fresh) {
        int bounded = track.rssi_dbm;
        if (bounded < -100) bounded = -100;
        if (bounded > -30) bounded = -30;
        uint16_t width = (uint16_t)(((bounded + 100) * 280) / 70);
        if (width > 0U)
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(20, 204, width, 26, UI_GOOD),
                TAG, "BLE Fox Hunt meter failed");
    }
    snprintf(line, sizeof(line), "MATCHED REPORTS %lu", (unsigned long)track.reports);
    ESP_RETURN_ON_ERROR(draw_centered(250, line, UI_TEXT, 1),
        TAG, "BLE Fox Hunt reports failed");
    ESP_RETURN_ON_ERROR(draw_centered(280,
        "ADDRESS CHANGES END THIS TRACK", UI_MUTED, 1),
        TAG, "BLE Fox Hunt privacy note failed");
    if (ble_fox_hunt_error != ESP_OK) {
        snprintf(line, sizeof(line), "S3 SCAN ERROR %ld", (long)ble_fox_hunt_error);
        ESP_RETURN_ON_ERROR(draw_centered(308, line, UI_BAD, 1),
            TAG, "BLE Fox Hunt error failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(308,
            "S3 BLE TRACKING / C5 WI-FI AVAILABLE", UI_MUTED, 1),
            TAG, "BLE Fox Hunt roles failed");
    }
    return draw_button(40, 350, 240, 50,
        ble_fox_hunt_exit_pending ? "STOPPING" :
        (ble_fox_hunt_error == ESP_OK ? "STOP TRACKING" : "RETRY SCAN"));
}

static esp_err_t draw_ble_analyzer(void)
{
    cerberus_ble_analyzer_view_t view = {0};
    esp_err_t view_error = cerberus_s3_ble_scanner_analyzer_status(&view);
    const cerberus_ble_flock_level_t flock_level = cerberus_ble_flock_hints_level(&view.last_flock);
    const bool show_flock = view.has_last && view.last_report_valid &&
        (flock_level >= CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE ||
          (!view.last_hc_name && !cerberus_ble_meta_evidence_count(&view.last_meta) &&
          (flock_level != CERBERUS_BLE_FLOCK_LEVEL_NONE || view.last_flock_oui_clue)));
    char line[80];
    ESP_RETURN_ON_ERROR(draw_centered(66, "BLUETOOTH ANALYZER", UI_TEXT, 2),
        TAG, "BLE analyzer title failed");
    const char *state = ble_analyzer_exit_pending ? "STOPPING" :
        ble_analyzer_start_pending ? "WAITING FOR S3 BLE" :
        ble_analyzer_error != ESP_OK ||
            (ble_analyzer_started && view_error != ESP_OK) ? "SCAN ERROR" :
        ble_analyzer_started ? "LISTENING" : "READY";
    uint16_t state_color = ble_analyzer_error != ESP_OK ||
        (ble_analyzer_started && view_error != ESP_OK) ? UI_BAD : UI_GOOD;
    ESP_RETURN_ON_ERROR(draw_centered(94, state, state_color, 1),
        TAG, "BLE analyzer state failed");
    snprintf(line, sizeof(line), "BLE CALLBACKS %s%lu / PARTIAL %s%u",
        view.total_events == UINT32_MAX ? ">=" : "", (unsigned long)view.total_events,
        view.current_events == UINT16_MAX ? ">=" : "", (unsigned)view.current_events);
    ESP_RETURN_ON_ERROR(draw_centered(124, line, UI_TEXT, 1),
        TAG, "BLE analyzer count failed");
    cerberus_ble_analyzer_window_t window;
    bool window_valid = cerberus_ble_analyzer_window(&view, &window);
    if (window_valid && window.buckets) {
        /* Truncate to one decimal: even capped values remain lower bounds. */
        uint32_t mean_tenths = window.events * 10U / window.buckets;
        snprintf(line, sizeof(line), "PEAK %s%u / MEAN %s%lu.%lu / N%u",
            window.capped ? ">=" : "", (unsigned)window.peak, window.capped ? ">=" : "",
            (unsigned long)(mean_tenths / 10U), (unsigned long)(mean_tenths % 10U),
            (unsigned)window.buckets);
    } else {
        snprintf(line, sizeof(line), "PEAK -- / MEAN -- / N0");
    }
    ESP_RETURN_ON_ERROR(draw_centered(137, line, UI_MUTED, 1),
        TAG, "BLE analyzer window failed");
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(20, 150, 280, 105, UI_PANEL),
        TAG, "BLE analyzer graph background failed");
    uint16_t scale = window.peak;
    if (scale == 0U) scale = 1U;
    for (unsigned column = 0U; column < CERBERUS_BLE_ANALYZER_HISTORY;
            ++column) {
        unsigned age = CERBERUS_BLE_ANALYZER_HISTORY - 1U - column;
        uint16_t count = age < window.buckets ? view.history[age] : 0U;
        if (count == 0U) continue;
        uint16_t height = (uint16_t)(((uint32_t)count * 95U) / scale);
        if (height == 0U) height = 1U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
            (uint16_t)(32U + column * 4U), (uint16_t)(250U - height),
            3U, height, UI_GOOD), TAG, "BLE analyzer graph bar failed");
    }
    if (window.peak && window.buckets) {
        uint16_t mean_height = (uint16_t)((window.events * 95U) /
            ((uint32_t)window.buckets * scale));
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(32, 155, 255, 1, UI_GOOD),
            TAG, "BLE analyzer peak line failed");
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(32, (uint16_t)(250U - mean_height),
            255, 1, UI_MUTED), TAG, "BLE analyzer mean line failed");
    }
    snprintf(line, sizeof(line), "N:200MS COMPLETE / BAD %lu / PART %lu",
        (unsigned long)view.malformed_reports, (unsigned long)view.unclassified_reports);
    ESP_RETURN_ON_ERROR(draw_centered(268, line, UI_MUTED, 1),
        TAG, "BLE analyzer scope failed");
    if (view.has_last) {
        char name[CERBERUS_BLE_ANALYZER_NAME_MAX + 1U] = {0};
        size_t length = view.last_name_length < CERBERUS_BLE_ANALYZER_NAME_MAX ?
            view.last_name_length : CERBERUS_BLE_ANALYZER_NAME_MAX;
        memcpy(name, view.last_name, length);
        make_ble_name_safe(name);
        char signal[16];
        if (cerberus_ble_rssi_valid(view.last_rssi_dbm))
            snprintf(signal, sizeof(signal), "%d dBm", (int)view.last_rssi_dbm);
        else
            snprintf(signal, sizeof(signal), "RSSI N/A");
        snprintf(line, sizeof(line), "%.20s%s / %s %s / %lu.%lus",
            length ? name : "<UNNAMED>", length > 20U ? "..." : "",
            view.last_scan_response ? "RSP" : "ADV", signal,
            (unsigned long)(view.last_seen_age_ms / 1000U),
            (unsigned long)((view.last_seen_age_ms % 1000U) / 100U));
        ESP_RETURN_ON_ERROR(draw_centered(290, line,
            view.last_fresh ? UI_TEXT : UI_MUTED, 1),
            TAG, "BLE analyzer last report failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(290,
            "WAITING FOR ADVERTISEMENTS", UI_MUTED, 1),
            TAG, "BLE analyzer wait failed");
    }
    if (view.evidence_label) {
        snprintf(line, sizeof(line), "HINT: %.24s / %s",
            view.evidence_label,
            sentinel_watch_confidence_name(view.evidence_confidence));
        ESP_RETURN_ON_ERROR(draw_centered(312, line, UI_MUTED, 1),
            TAG, "BLE analyzer hint failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(312,
            "NO CLASSIFIED HINT IN LAST REPORT", UI_MUTED, 1),
            TAG, "BLE analyzer no hint failed");
    }
    if (view.has_last && view.last_report_valid && view.last_ibeacon_valid) {
        cerberus_ibeacon_values_line(&view.last_ibeacon, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(326, line, UI_MUTED, 1),
            TAG, "BLE analyzer iBeacon tuple failed");
        cerberus_ibeacon_uuid_line(&view.last_ibeacon, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(338, line, UI_MUTED, 1),
            TAG, "BLE analyzer iBeacon UUID failed");
    } else if (show_flock) {
        cerberus_flock_hint_line(&view.last_flock, view.last_scan_response,
            view.last_seen_age_ms, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(326, line, UI_MUTED, 1),
            TAG, "BLE analyzer Flock facts failed");
        cerberus_flock_address_line(view.last_address, view.last_flock_oui_clue,
            privacy_addresses_masked(), line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(338, line, UI_MUTED, 1),
            TAG, "BLE analyzer Flock address scope failed");
    } else if (view.has_last && view.last_report_valid &&
        cerberus_ble_meta_evidence_count(&view.last_meta)) {
        cerberus_meta_hint_line(&view.last_meta, view.last_scan_response,
            view.last_seen_age_ms, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(326, line, UI_MUTED, 1),
            TAG, "BLE analyzer exact identifier pairs failed");
        ESP_RETURN_ON_ERROR(draw_centered(338,
            cerberus_ble_meta_evidence_blocked(&view.last_meta) ?
                "ID HINT / META FILTER BLOCKED IN THIS REPORT" :
                "IDENTIFIER HINTS ARE NOT DEVICE IDENTITY", UI_MUTED, 1),
            TAG, "BLE analyzer identifier scope failed");
    } else if (view.has_last && view.last_report_valid && view.last_hc_name) {
        cerberus_hc_hint_line(view.last_hc_name, view.last_scan_response,
            view.last_seen_age_ms, line, sizeof(line));
        ESP_RETURN_ON_ERROR(draw_centered(326, line, UI_MUTED, 1),
            TAG, "BLE analyzer HC name failed");
        ESP_RETURN_ON_ERROR(draw_centered(338,
            "ADVERTISED NAME HINT / NOT DEVICE IDENTITY", UI_MUTED, 1),
            TAG, "BLE analyzer HC scope failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(332,
            "BROADCAST HINTS ARE NOT IDENTITY PROOF", UI_MUTED, 1),
            TAG, "BLE analyzer evidence note failed");
    }
    return draw_button(40, 350, 240, 50,
        ble_analyzer_exit_pending ? "STOPPING" :
        (ble_analyzer_error == ESP_OK ? "STOP ANALYZER" : "RETRY ANALYZER"));
}

static esp_err_t draw_ble_feature_menu(void)
{
    return sentinel_hub_draw(SENTINEL_HUB_BLE, UI_WIDGET_THEME);
}

static esp_err_t draw_ble(void)
{
    if (live_is_view()) return draw_live_scan();
    char line[64];
    bool ble_capable = (ui_capability_map & UI_CAP_BLE) != 0U;

    if (ble_mode == UI_BLE_MODE_MENU) {
        return draw_ble_feature_menu();
    }

    if (ble_mode == UI_BLE_MODE_ENVIRONMENT) {
        return draw_ble_environment();
    }

    if (ble_mode == UI_BLE_MODE_VALUE) {
        return draw_ble_characteristic_value();
    }
    if (ble_mode == UI_BLE_MODE_CHARACTERISTICS) {
        return draw_ble_characteristics();
    }
    if (ble_mode == UI_BLE_MODE_SERVICES) {
        return draw_ble_services();
    }
    if (ble_mode == UI_BLE_MODE_DETAIL) {
        return draw_ble_detail();
    }
    if (ble_mode == UI_BLE_MODE_FOX_HUNT) {
        return draw_ble_fox_hunt();
    }
    if (ble_mode == UI_BLE_MODE_ANALYZER) {
        return draw_ble_analyzer();
    }
    if (ble_mode == UI_BLE_MODE_FINDMY_MONITOR) return draw_ble_findmy_monitor();
    if ((ui_capability_map & UI_CAP_BLE_GATT) != 0U && !ble_connection_checked) {
        (void)refresh_ble_connection_status();
    }

    if (!ble_capable) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "S3 BLE UNAVAILABLE", UI_BAD, 2), TAG, "BLE unavailable failed");
        return draw_centered(205, "S3 OBSERVER SERVICE REQUIRED", UI_MUTED, 1);
    }
    if (!ble_scan_checked) {
        (void)refresh_ble_scan_status();
    }
    if (ble_scan_error != ESP_OK) {
        snprintf(line, sizeof(line), "S3 ERROR %s", esp_err_to_name(ble_scan_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "SCAN SERVICE UNAVAILABLE", UI_BAD, 2), TAG, "BLE service failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "BLE link error failed");
    } else if (ble_scan_status.state == CERBERUS_BLE_SCAN_ERROR) {
        snprintf(line, sizeof(line), "S3 ERROR %ld", (long)ble_scan_status.error_code);
        ESP_RETURN_ON_ERROR(draw_centered(145, "BLE DEVICE SCAN FAILED", UI_BAD, 2), TAG, "BLE scan error failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "BLE S3 error failed");
    } else if (ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "BLE DEVICE SCAN RUNNING", UI_GOOD, 2), TAG, "BLE running failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, "LISTENING FOR ADVERTISEMENTS", UI_MUTED, 1), TAG, "BLE mode failed");
    } else if (ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE) {
        snprintf(
            line,
            sizeof(line),
            "FILTER %s  %u/%u  NEAR %u >",
            ble_filter_name(ble_filter),
            (unsigned)ble_filtered_count,
            (unsigned)ble_scan_status.stored_count,
            (unsigned)ble_scan_status.total_count);
        ESP_RETURN_ON_ERROR(draw_small_button(8, 48, 304, 32, line), TAG,
            "BLE focus filter failed");

        for (size_t row = 0; row < ble_result_count; ++row) {
            uint16_t y = UI_WIFI_LIST_Y + (uint16_t)row * UI_WIFI_ROW_HEIGHT;
            const cerberus_c5_ble_result_t *result = &ble_results[row];
            bool favorite = cerberus_settings_ble_is_favorite(
                result->address,
                result->address_type);
            char service[12];
            format_ble_service(result, service, sizeof(service));
            ESP_RETURN_ON_ERROR(
                cerberus_display_fill_rect(8, y, 304, UI_WIFI_ROW_HEIGHT - 2U, UI_PANEL),
                TAG,
                "BLE row failed");
            const char *name = result->name[0] != '\0' ? result->name : "<UNNAMED>";
            if (result->manufacturer_id != CERBERUS_BLE_MANUFACTURER_UNKNOWN) {
                snprintf(
                    line,
                    sizeof(line),
                    "%c %.19s M%04X %s",
                    favorite ? '*' : ' ',
                    name,
                    result->manufacturer_id,
                    service);
            } else {
                snprintf(line, sizeof(line), "%c %.25s %s", favorite ? '*' : ' ', name, service);
            }
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 6U, line, favorite ? UI_GOOD : UI_TEXT, UI_PANEL, 1),
                TAG,
                "BLE name failed");
            char identifier[24];
            cerberus_privacy_format_ble_identifier(
                result->address,
                privacy_addresses_masked(),
                identifier,
                sizeof(identifier));
            char signal[12];
            if (cerberus_ble_rssi_valid(result->rssi))
                snprintf(signal, sizeof(signal), "%dDB", result->rssi);
            else
                snprintf(signal, sizeof(signal), "RSSI N/A");
            snprintf(
                line,
                sizeof(line),
                "%s %s T%c %s %s",
                identifier,
                signal,
                ble_rssi_trend(result),
                (result->address_type & 1U) != 0U ? "RND" : "PUB",
                (result->flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U ? "CON" : "ADV");
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 23U, line, UI_MUTED, UI_PANEL, 1),
                TAG,
                "BLE metadata failed");
            ESP_RETURN_ON_ERROR(
                draw_ble_rssi_history(y, result),
                TAG,
                "BLE RSSI graph failed");
        }
        if (ble_scan_status.stored_count == 0U) {
            ESP_RETURN_ON_ERROR(draw_centered(190, "NO BLE DEVICES HEARD", UI_MUTED, 2), TAG, "BLE empty failed");
        } else if (ble_filtered_count == 0U) {
            snprintf(line, sizeof(line), "NO %s HINTS IN THIS SCAN",
                ble_filter_name(ble_filter));
            ESP_RETURN_ON_ERROR(draw_centered(190, line, UI_MUTED, 1), TAG,
                "BLE filtered empty failed");
        }
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(145, "BLE DEVICE VISIBILITY", UI_MUTED, 2), TAG, "BLE idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, "TAP SCAN FOR NEARBY DEVICES", UI_TEXT, 1), TAG, "BLE prompt failed");
        ESP_RETURN_ON_ERROR(draw_centered(225, "NO PAIRING OR CONNECTIONS", UI_GOOD, 1), TAG, "BLE scope failed");
    }

    if (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY &&
        ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE && !ble_attack_selecting) {
        ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 68, 42, "PREV"), TAG, "BLE prev failed");
        ESP_RETURN_ON_ERROR(draw_small_button(84, UI_WIFI_CONTROLS_Y, 68, 42, "NEXT"), TAG, "BLE next failed");
        ESP_RETURN_ON_ERROR(draw_small_button(160, UI_WIFI_CONTROLS_Y, 76, 42, "MONITOR"), TAG, "BLE monitor failed");
        return draw_small_button(244, UI_WIFI_CONTROLS_Y, 68, 42, "SCAN");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_WIFI_CONTROLS_Y, 84, 42, "PREV"), TAG, "BLE prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(98, UI_WIFI_CONTROLS_Y, 84, 42, "NEXT"), TAG, "BLE next failed");
    const char *scan_label = ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING
        ? "STOP"
        : (ble_connection_checked && ble_connection_is_active() ? "LINKED" : "SCAN");
    return draw_small_button(
        188,
        UI_WIFI_CONTROLS_Y,
        124,
        42,
        scan_label);
}

static esp_err_t draw_qr_menu(void)
{
    ESP_RETURN_ON_ERROR(draw_centered(60, "CREATE A CODE TO SHARE", UI_MUTED, 1), TAG, "QR subtitle failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 94, 272, 54, "WIFI CONNECT"), TAG, "QR Wi-Fi button failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 166, 272, 54, "CUSTOM TEXT"), TAG, "QR text button failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 238, 272, 54, "CUSTOM URL"), TAG, "QR URL button failed");
    ESP_RETURN_ON_ERROR(draw_centered(326, "GENERATED ON YOUR CERBERUS", UI_GOOD, 1), TAG, "QR local help failed");
    return draw_centered(350, "LOCAL WEB HAS ITS OWN CONNECTION CODE", UI_MUTED, 1);
}

static esp_err_t draw_qr_wifi_security(void)
{
    ESP_RETURN_ON_ERROR(draw_centered(70, "WIFI SECURITY", UI_TEXT, 2), TAG, "Wi-Fi QR security title failed");
    ESP_RETURN_ON_ERROR(draw_centered(100, "SELECT THE NETWORK TYPE", UI_MUTED, 1), TAG, "Wi-Fi QR security help failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 134, 272, 54, "WPA / WPA2"), TAG, "Wi-Fi QR WPA button failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 206, 272, 54, "WEP"), TAG, "Wi-Fi QR WEP button failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 278, 272, 54, "OPEN"), TAG, "Wi-Fi QR open button failed");
    return draw_centered(362, "CODE INCLUDES THE WI-FI PASSWORD", UI_GOOD, 1);
}

static esp_err_t draw_web_ui(void)
{
    cerberus_web_ui_status_t status;
    cerberus_web_ui_get_status(&status);
    if (!status.running) {
        ESP_RETURN_ON_ERROR(draw_centered(90, "LOCAL DASHBOARD", UI_TEXT, 2), TAG, "web title");
        ESP_RETURN_ON_ERROR(draw_centered(148, "Start a protected local Wi-Fi network", UI_MUTED, 1), TAG, "web intro");
        ESP_RETURN_ON_ERROR(draw_centered(172, "to view results and download reports.", UI_MUTED, 1), TAG, "web intro2");
        ESP_RETURN_ON_ERROR(draw_centered(222, "Credentials appear here after starting.", UI_MUTED, 1), TAG, "web help");
        ESP_RETURN_ON_ERROR(draw_centered(250, "This transmits Wi-Fi from the S3.", UI_MUTED, 1), TAG, "web radio note");
        if (web_action_error != ESP_OK) {
            ESP_RETURN_ON_ERROR(draw_centered(300, esp_err_to_name(web_action_error), UI_BAD, 1), TAG, "web error");
        }
        if (status.radio_active) {
            ESP_RETURN_ON_ERROR(draw_centered(328, "WI-FI STILL ACTIVE - RETRY STOP", UI_BAD, 1), TAG, "web stop pending");
        }
        return draw_button(24, 360, 272, 48, status.radio_active ? "RETRY STOP" : "START LOCAL WEB");
    }
    char line[96];
    ESP_RETURN_ON_ERROR(draw_centered(54, "LOCAL WI-FI ACCESS POINT ACTIVE", UI_GOOD, 1), TAG, "web active");
    snprintf(line, sizeof(line), "SSID %s", status.ssid);
    ESP_RETURN_ON_ERROR(draw_centered(77, line, UI_TEXT, 1), TAG, "web SSID");
    snprintf(line, sizeof(line), "PASS %s", status.access_point_password);
    ESP_RETURN_ON_ERROR(draw_centered(99, line, UI_TEXT, 1), TAG, "web password");
    snprintf(line, sizeof(line), "%s  CLIENTS %u", status.address, status.connected_clients);
    ESP_RETURN_ON_ERROR(draw_centered(123, line, UI_MUTED, 1), TAG, "web address");
    cerberus_qr_info_t info;
    esp_err_t err = cerberus_qr_render_text(status.address, 0, 142, CERBERUS_LCD_WIDTH, 178, &info);
    if (err != ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_centered(230, "USE THE ADDRESS ABOVE", UI_MUTED, 1), TAG, "web QR fallback");
    }
    ESP_RETURN_ON_ERROR(draw_centered(331, status.password_configured ?
        "Your browser login is required." : "Set your browser password on first visit.",
        UI_MUTED, 1), TAG, "web login");
    return draw_button(24, 360, 272, 48, "STOP LOCAL WEB");
}

static bool qr_mode_is_wifi(ui_qr_mode_t mode)
{
    return mode == UI_QR_MODE_WIFI_SECURITY || mode == UI_QR_MODE_WIFI_SSID_INPUT ||
        mode == UI_QR_MODE_WIFI_PASSWORD_INPUT || mode == UI_QR_MODE_WIFI_CODE;
}

static bool qr_mode_has_code(ui_qr_mode_t mode)
{
    return mode == UI_QR_MODE_CUSTOM || mode == UI_QR_MODE_WIFI_CODE;
}

static void reset_qr_save_status(void)
{
    qr_save_checked = false;
    qr_save_error = ESP_ERR_INVALID_STATE;
    qr_save_file_name[0] = '\0';
}

static void secure_clear_buffer(char *buffer, size_t size)
{
    volatile char *cursor = (volatile char *)buffer;
    while (size-- > 0U) {
        *cursor++ = '\0';
    }
}

static void clear_qr_wifi_credentials(void)
{
    secure_clear_buffer(qr_wifi_ssid, sizeof(qr_wifi_ssid));
    secure_clear_buffer(qr_wifi_password, sizeof(qr_wifi_password));
    secure_clear_buffer(qr_payload, sizeof(qr_payload));
    memset(&qr_keyboard, 0, sizeof(qr_keyboard));
}

static esp_err_t escape_wifi_qr_field(const char *input, char *output, size_t capacity)
{
    if (input == NULL || output == NULL || capacity == 0U) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t output_length = 0U;
    for (size_t index = 0U; input[index] != '\0'; ++index) {
        bool escaped = input[index] == '\\' || input[index] == ';' || input[index] == ',' ||
            input[index] == '"' || input[index] == ':';
        if (output_length + (escaped ? 2U : 1U) >= capacity) {
            output[0] = '\0';
            return ESP_ERR_INVALID_SIZE;
        }
        if (escaped) {
            output[output_length++] = '\\';
        }
        output[output_length++] = input[index];
    }
    output[output_length] = '\0';
    return ESP_OK;
}

static esp_err_t build_wifi_qr_payload(void)
{
    char escaped_ssid[UI_QR_WIFI_SSID_CAPACITY * 2U];
    char escaped_password[UI_QR_WIFI_PASSWORD_CAPACITY * 2U];
    ESP_RETURN_ON_ERROR(
        escape_wifi_qr_field(qr_wifi_ssid, escaped_ssid, sizeof(escaped_ssid)),
        TAG,
        "Wi-Fi QR SSID escaping failed");
    ESP_RETURN_ON_ERROR(
        escape_wifi_qr_field(qr_wifi_password, escaped_password, sizeof(escaped_password)),
        TAG,
        "Wi-Fi QR password escaping failed");

    const char *security = qr_wifi_security == UI_QR_WIFI_SECURITY_WPA
        ? "WPA"
        : (qr_wifi_security == UI_QR_WIFI_SECURITY_WEP ? "WEP" : "nopass");
    int length = snprintf(
        qr_payload,
        sizeof(qr_payload),
        "WIFI:T:%s;S:%s;P:%s;H:false;;",
        security,
        escaped_ssid,
        escaped_password);
    return length > 0 && (size_t)length < sizeof(qr_payload) ? ESP_OK : ESP_ERR_INVALID_SIZE;
}



static esp_err_t validate_http_url(const char *url)
{
    if (url == NULL || (strncmp(url, "http://", 7U) != 0 && strncmp(url, "https://", 8U) != 0)) {
        return ESP_ERR_INVALID_ARG;
    }
    const char *host = url + (url[4] == 's' ? 8U : 7U);
    if (*host == '\0' || strchr(host, ' ') != NULL || strchr(host, '\t') != NULL || strchr(host, '\r') != NULL || strchr(host, '\n') != NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    return strlen(url) < UI_QR_CUSTOM_CAPACITY ? ESP_OK : ESP_ERR_INVALID_SIZE;
}

static esp_err_t prepare_qr_payload(ui_qr_mode_t mode)
{
    if (mode == UI_QR_MODE_CUSTOM) {
        if (qr_payload[0] == '\0') return ESP_ERR_INVALID_ARG;
        return qr_custom_is_url ? validate_http_url(qr_payload) : ESP_OK;
    }
    if (mode == UI_QR_MODE_WIFI_CODE) {
        return qr_payload[0] != '\0' ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t draw_qr_code(void)
{
    const char *title = qr_mode == UI_QR_MODE_WIFI_CODE
        ? "WIFI CONNECTION" : (qr_custom_is_url ? "CUSTOM URL" : "CUSTOM TEXT");
    ESP_RETURN_ON_ERROR(draw_centered(54, title, UI_GOOD, 1), TAG, "QR title failed");

    esp_err_t err = prepare_qr_payload(qr_mode);
    if (err != ESP_OK) {
        char error[48];
        snprintf(error, sizeof(error), "QR ERROR %s", esp_err_to_name(err));
        return draw_centered(210, error, UI_BAD, 1);
    }

    cerberus_qr_info_t info;
    err = cerberus_qr_render_text(qr_payload, 0, 70, CERBERUS_LCD_WIDTH, 278, &info);
    if (err != ESP_OK) {
        char error[48];
        snprintf(error, sizeof(error), "QR ERROR %s", esp_err_to_name(err));
        return draw_centered(210, error, UI_BAD, 1);
    }

    char status[64];
    uint16_t status_color = UI_MUTED;
    if (qr_save_checked && qr_save_error == ESP_OK) {
        snprintf(status, sizeof(status), "SAVED /CERBERUS/QR/%.11s", qr_save_file_name);
        status_color = UI_GOOD;
    } else if (qr_save_checked) {
        snprintf(status, sizeof(status), "SAVE FAILED %s", esp_err_to_name(qr_save_error));
        status_color = UI_BAD;
    } else {
        snprintf(status, sizeof(status), "%u MODULES  %uX SCALE", info.module_count, info.scale);
    }
    ESP_RETURN_ON_ERROR(draw_centered(352, status, status_color, 1), TAG, "QR save status failed");
    return draw_small_button(64, 374, 192, 36, qr_save_checked ? "SAVE ANOTHER BMP" : "SAVE BMP TO SD");
}

static esp_err_t draw_qr_input(void)
{
    const char *prompt = qr_mode == UI_QR_MODE_WIFI_SSID_INPUT
        ? "WIFI SSID"
        : (qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT
            ? "WIFI PASSWORD" : (qr_custom_is_url ? "ENTER URL" : "ENTER TEXT"));
    return cerberus_keyboard_draw(&qr_keyboard, prompt);
}

static esp_err_t draw_touch_test_target(
    uint16_t x,
    uint16_t y,
    const char *label,
    bool complete)
{
    const uint16_t size = 54U;
    uint16_t border = complete ? UI_GOOD : UI_BORDER;
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(x, y, size, size, border),
        TAG,
        "touch target border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(x + 3U, y + 3U, size - 6U, size - 6U, UI_PANEL),
        TAG,
        "touch target fill failed");
    uint16_t width = text_width(label, 2);
    uint16_t label_x = x + (size - width) / 2U;
    return cerberus_display_draw_text(
        label_x, y + 20U, label, complete ? UI_GOOD : UI_TEXT, UI_PANEL, 2);
}

static esp_err_t draw_touch_test(void)
{
    ESP_RETURN_ON_ERROR(
        draw_touch_test_target(4, 52, "1", (touch_test_corner_mask & 0x01U) != 0U),
        TAG,
        "top-left touch target failed");
    ESP_RETURN_ON_ERROR(
        draw_touch_test_target(262, 52, "2", (touch_test_corner_mask & 0x02U) != 0U),
        TAG,
        "top-right touch target failed");
    ESP_RETURN_ON_ERROR(
        draw_touch_test_target(4, 414, "3", (touch_test_corner_mask & 0x04U) != 0U),
        TAG,
        "bottom-left touch target failed");
    ESP_RETURN_ON_ERROR(
        draw_touch_test_target(262, 414, "4", (touch_test_corner_mask & 0x08U) != 0U),
        TAG,
        "bottom-right touch target failed");

    ESP_RETURN_ON_ERROR(draw_centered(126, "TAP TARGETS 1-4", UI_TEXT, 2), TAG, "touch instructions failed");
    ESP_RETURN_ON_ERROR(draw_centered(154, "EACH TAP SHOWS LOGICAL X/Y", UI_MUTED, 1), TAG, "touch coordinate help failed");

    char line[52];
    if (touch_test_has_sample) {
        snprintf(line, sizeof(line), "LAST X=%u Y=%u", touch_test_last_x, touch_test_last_y);
        ESP_RETURN_ON_ERROR(draw_centered(184, line, UI_TEXT, 2), TAG, "touch last point failed");
        snprintf(
            line,
            sizeof(line),
            "RANGE X %u-%u   Y %u-%u",
            touch_test_min_x,
            touch_test_max_x,
            touch_test_min_y,
            touch_test_max_y);
        ESP_RETURN_ON_ERROR(draw_centered(214, line, UI_MUTED, 1), TAG, "touch range failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(184, "WAITING FOR TOUCH", UI_MUTED, 2), TAG, "touch waiting failed");
    }

    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(79, 244, 162, 122, UI_BORDER),
        TAG,
        "touch map border failed");
    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(81, 246, 158, 118, UI_PANEL),
        TAG,
        "touch map fill failed");
    if (touch_test_has_sample) {
        uint16_t dot_x = 81U + (uint16_t)(((uint32_t)touch_test_last_x * 153U) /
            (CERBERUS_LCD_WIDTH - 1U));
        uint16_t dot_y = 246U + (uint16_t)(((uint32_t)touch_test_last_y * 113U) /
            (CERBERUS_LCD_HEIGHT - 1U));
        ESP_RETURN_ON_ERROR(
            cerberus_display_fill_rect(dot_x, dot_y, 5, 5, UI_GOOD),
            TAG,
            "touch map point failed");
    }

    return draw_centered(
        382,
        touch_test_corner_mask == 0x0FU ? "ALL FOUR CORNERS VERIFIED" : "GREEN TARGETS ARE COMPLETE",
        touch_test_corner_mask == 0x0FU ? UI_GOOD : UI_MUTED,
        1);
}

static esp_err_t draw_self_test(void)
{
    if (self_test_mode == UI_SELF_TEST_MODE_TOUCH) return draw_touch_test();

    ESP_RETURN_ON_ERROR(draw_centered(60, "CHECK YOUR TOUCHSCREEN AND MICROSD", UI_MUTED, 1), TAG, "checks subtitle failed");
    ESP_RETURN_ON_ERROR(draw_centered(100, "TOUCHSCREEN", UI_TEXT, 2), TAG, "touch title failed");
    ESP_RETURN_ON_ERROR(draw_centered(130,
        ui_config.touch_available ? "TOUCH SENSOR READY - TRY ALL FOUR CORNERS" : "TOUCH SENSOR UNAVAILABLE",
        ui_config.touch_available ? UI_MUTED : UI_BAD, 1), TAG, "touch readiness failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 158, 272, 48, "TEST TOUCHSCREEN"), TAG, "touch test button failed");

    ESP_RETURN_ON_ERROR(draw_centered(238, "MICROSD", UI_TEXT, 2), TAG, "storage title failed");
    const char *sd_state = cerberus_storage_get_owner() == CERBERUS_STORAGE_OWNER_USB
        ? "USB DRIVE ACTIVE - EJECT IT BEFORE TESTING"
        : (cerberus_storage_is_mounted() ? "CARD MOUNTED" :
            (storage_safely_removed ? "CARD SAFELY REMOVED" : "CARD NOT MOUNTED"));
    ESP_RETURN_ON_ERROR(draw_centered(266, sd_state, UI_MUTED, 1), TAG, "storage readiness failed");
    ESP_RETURN_ON_ERROR(draw_centered(290, "CHECK USES A TEMPORARY READ/WRITE FILE", UI_MUTED, 1), TAG, "benchmark scope failed");
    char line[52];
    uint16_t color = UI_MUTED;
    if (storage_benchmark_running) {
        snprintf(line, sizeof(line), "CHECKING %uK - PLEASE WAIT",
            (unsigned)(CERBERUS_STORAGE_BENCHMARK_BYTES / 1024U));
        color = UI_GOOD;
    } else if (!storage_benchmark_checked) {
        snprintf(line, sizeof(line), "READ/WRITE CHECK HAS NOT RUN");
    } else if (storage_benchmark_error != ESP_OK || !storage_benchmark_result.data_verified) {
        snprintf(line, sizeof(line), "CHECK FAILED %s", esp_err_to_name(storage_benchmark_error));
        color = UI_BAD;
    } else {
        uint32_t write_kib_s = storage_benchmark_result.write_time_us == 0U ? 0U :
            (uint32_t)((uint64_t)storage_benchmark_result.bytes_written * 1000000ULL /
                storage_benchmark_result.write_time_us / 1024ULL);
        uint32_t read_kib_s = storage_benchmark_result.read_time_us == 0U ? 0U :
            (uint32_t)((uint64_t)storage_benchmark_result.bytes_read * 1000000ULL /
                storage_benchmark_result.read_time_us / 1024ULL);
        snprintf(line, sizeof(line), "VERIFIED  W %luK/S  R %luK/S",
            (unsigned long)write_kib_s, (unsigned long)read_kib_s);
        color = UI_GOOD;
    }
    ESP_RETURN_ON_ERROR(draw_centered(320, line, color, 1), TAG, "benchmark result failed");
    return draw_button(24, 350, 272, 58, "CHECK MICROSD");
}

static void handle_self_test_tap(uint16_t x, uint16_t y)
{
    if (self_test_mode == UI_SELF_TEST_MODE_TOUCH) {
        if (x >= 64U && x < 256U && y >= UI_BACK_Y) {
            self_test_mode = UI_SELF_TEST_MODE_SUMMARY;
            ESP_ERROR_CHECK(draw_detail_page());
            return;
        }

        touch_test_has_sample = true;
        touch_test_last_x = x;
        touch_test_last_y = y;
        if (x < touch_test_min_x) {
            touch_test_min_x = x;
        }
        if (x > touch_test_max_x) {
            touch_test_max_x = x;
        }
        if (y < touch_test_min_y) {
            touch_test_min_y = y;
        }
        if (y > touch_test_max_y) {
            touch_test_max_y = y;
        }
        if (x < 64U && y < 112U) {
            touch_test_corner_mask |= 0x01U;
        } else if (x >= 256U && y < 112U) {
            touch_test_corner_mask |= 0x02U;
        } else if (x < 64U && y >= 400U) {
            touch_test_corner_mask |= 0x04U;
        } else if (x >= 256U && y >= 400U) {
            touch_test_corner_mask |= 0x08U;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (storage_benchmark_running || x < 24U || x >= 296U) {
        return;
    }
    if (y >= 158U && y < 206U) {
        self_test_mode = UI_SELF_TEST_MODE_TOUCH;
        touch_test_corner_mask = 0U;
        touch_test_has_sample = false;
        touch_test_min_x = UINT16_MAX;
        touch_test_max_x = 0U;
        touch_test_min_y = UINT16_MAX;
        touch_test_max_y = 0U;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (y < 350U || y >= 408U) {
        return;
    }
    storage_benchmark_running = true;
    ESP_ERROR_CHECK(draw_detail_page());
    present_ui_frame();
    storage_benchmark_error = cerberus_storage_run_benchmark(&storage_benchmark_result);
    storage_benchmark_running = false;
    storage_benchmark_checked = true;
    storage_safely_removed = false;
    storage_checked = true;
    storage_last_error = storage_benchmark_error;
    if (storage_benchmark_error == ESP_OK) {
        storage_last_error = cerberus_storage_get_status(&storage_status);
    }
    file_entry_count = 0U;
    log_checked = false;
    ESP_ERROR_CHECK(draw_detail_page());
}

static esp_err_t refresh_file_listing(bool remount)
{
    storage_checked = true;
    file_listing_checked = true;
    storage_safely_removed = false;
    file_entry_count = 0;

    if (remount && cerberus_storage_is_mounted()) {
        storage_last_error = cerberus_storage_unmount();
        if (storage_last_error != ESP_OK) {
            return storage_last_error;
        }
    }

    storage_last_error = cerberus_storage_mount();
    if (storage_last_error != ESP_OK) {
        return storage_last_error;
    }

    storage_last_error = cerberus_storage_get_status(&storage_status);
    if (storage_last_error != ESP_OK) {
        return storage_last_error;
    }

    storage_last_error = cerberus_storage_list_directory(
        file_path,
        file_entries,
        UI_FILE_ENTRY_CAPACITY,
        &file_entry_count);
    if (storage_last_error != ESP_OK) {
        return storage_last_error;
    }

    if (file_page_offset >= file_entry_count) {
        file_page_offset = file_entry_count == 0
            ? 0
            : ((file_entry_count - 1U) / UI_FILE_VISIBLE_ROWS) * UI_FILE_VISIBLE_ROWS;
    }
    return ESP_OK;
}

static void format_file_size(uint64_t bytes, char *output, size_t output_size)
{
    if (bytes >= 1024U * 1024U) {
        snprintf(output, output_size, "%lluM", (unsigned long long)(bytes / (1024U * 1024U)));
    } else if (bytes >= 1024U) {
        snprintf(output, output_size, "%lluK", (unsigned long long)(bytes / 1024U));
    } else {
        snprintf(output, output_size, "%lluB", (unsigned long long)bytes);
    }
}

static const char *viewed_file_relative_path(void)
{
    return current_page == UI_PAGE_LOGS ? UI_LOG_PATH : file_path;
}

static esp_err_t refresh_text_viewer(void)
{
    viewed_file_bytes = 0;
    viewed_file_advance = 0;
    viewed_file_error = cerberus_storage_read_file_chunk(
        viewed_file_relative_path(),
        viewed_file_name,
        viewed_file_offset,
        viewed_file_data,
        UI_TEXT_VIEWER_CHUNK_BYTES,
        &viewed_file_bytes,
        &viewed_file_size);
    if (viewed_file_error == ESP_OK) {
        viewed_file_data[viewed_file_bytes] = '\0';
    }
    return viewed_file_error;
}

static esp_err_t draw_text_viewer(void)
{
    char status[64];
    snprintf(status, sizeof(status), "%.45s", viewed_file_name);
    ESP_RETURN_ON_ERROR(draw_centered(54, status, UI_GOOD, 1), TAG, "viewer name failed");

    if (viewed_file_error != ESP_OK) {
        snprintf(status, sizeof(status), "READ ERROR %s", esp_err_to_name(viewed_file_error));
        ESP_RETURN_ON_ERROR(draw_centered(160, status, UI_BAD, 1), TAG, "viewer error failed");
        ESP_RETURN_ON_ERROR(draw_centered(210, "FILE MAY HAVE BEEN REMOVED", UI_MUTED, 1), TAG, "viewer help failed");
    } else {
        snprintf(
            status,
            sizeof(status),
            "BYTE %llu / %llu",
            (unsigned long long)viewed_file_offset,
            (unsigned long long)viewed_file_size);
        ESP_RETURN_ON_ERROR(draw_centered(68, status, UI_MUTED, 1), TAG, "viewer position failed");

        if (viewed_file_size == 0) {
            ESP_RETURN_ON_ERROR(draw_centered(180, "EMPTY FILE", UI_MUTED, 2), TAG, "empty file failed");
        } else {
            char line[UI_TEXT_VIEWER_COLUMNS + 1U];
            size_t input = 0;
            size_t column = 0;
            size_t row = 0;

            while (input < viewed_file_bytes && row < UI_TEXT_VIEWER_LINES) {
                uint8_t value = viewed_file_data[input];
                if (value == '\r') {
                    ++input;
                    continue;
                }
                if (value == '\n') {
                    ++input;
                    line[column] = '\0';
                    if (column > 0) {
                        ESP_RETURN_ON_ERROR(
                            cerberus_display_draw_text(10, UI_TEXT_VIEWER_Y + (uint16_t)row * 12U, line, UI_TEXT, UI_BACKGROUND, 1),
                            TAG,
                            "viewer line failed");
                    }
                    column = 0;
                    ++row;
                    continue;
                }
                if (column == UI_TEXT_VIEWER_COLUMNS) {
                    line[column] = '\0';
                    ESP_RETURN_ON_ERROR(
                        cerberus_display_draw_text(10, UI_TEXT_VIEWER_Y + (uint16_t)row * 12U, line, UI_TEXT, UI_BACKGROUND, 1),
                        TAG,
                        "viewer wrapped line failed");
                    column = 0;
                    ++row;
                    continue;
                }

                ++input;
                if (value == '\t') {
                    line[column++] = ' ';
                } else {
                    line[column++] = value >= 32U && value <= 126U ? (char)value : '.';
                }
            }

            if (column > 0 && row < UI_TEXT_VIEWER_LINES) {
                line[column] = '\0';
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(10, UI_TEXT_VIEWER_Y + (uint16_t)row * 12U, line, UI_TEXT, UI_BACKGROUND, 1),
                    TAG,
                    "viewer final line failed");
            }
            viewed_file_advance = input;
        }
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_FILE_CONTROLS_Y, 70, 42, "PREV"), TAG, "viewer prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, UI_FILE_CONTROLS_Y, 70, 42, "NEXT"), TAG, "viewer next failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, UI_FILE_CONTROLS_Y, 70, 42, "TOP"), TAG, "viewer top failed");
    return draw_small_button(236, UI_FILE_CONTROLS_Y, 76, 42, "INFO");
}

static esp_err_t draw_file_info(void)
{
    char line[72];
    char size[20];
    format_file_size(viewed_file_size, size, sizeof(size));

    ESP_RETURN_ON_ERROR(draw_centered(56, "FILE DETAILS", UI_GOOD, 2), TAG, "file info title failed");
    snprintf(line, sizeof(line), "NAME %.43s", viewed_file_name);
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(10, 92, line, UI_TEXT, UI_BACKGROUND, 1), TAG, "file info name failed");
    snprintf(line, sizeof(line), "PATH %.43s", viewed_file_relative_path());
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(10, 112, line, UI_MUTED, UI_BACKGROUND, 1), TAG, "file info path failed");
    snprintf(line, sizeof(line), "SIZE %s  %llu BYTES", size, (unsigned long long)viewed_file_size);
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(10, 132, line, UI_TEXT, UI_BACKGROUND, 1), TAG, "file info size failed");
    ESP_RETURN_ON_ERROR(draw_centered(172, "SHA-256", UI_MUTED, 2), TAG, "file hash title failed");

    if (viewed_file_hash_state == UI_FILE_HASH_READY) {
        static const char hex[] = "0123456789ABCDEF";
        char digest_line[17];
        for (size_t row = 0; row < 4; ++row) {
            for (size_t byte = 0; byte < 8; ++byte) {
                uint8_t value = viewed_file_sha256[row * 8U + byte];
                digest_line[byte * 2U] = hex[value >> 4U];
                digest_line[byte * 2U + 1U] = hex[value & 0x0FU];
            }
            digest_line[16] = '\0';
            ESP_RETURN_ON_ERROR(
                draw_centered(208U + (uint16_t)row * 22U, digest_line, UI_TEXT, 1),
                TAG,
                "file hash line failed");
        }
    } else if (viewed_file_hash_state == UI_FILE_HASH_RUNNING) {
        ESP_RETURN_ON_ERROR(draw_centered(230, "CALCULATING...", UI_GOOD, 2), TAG, "file hash running failed");
    } else if (viewed_file_hash_state == UI_FILE_HASH_ERROR) {
        snprintf(line, sizeof(line), "HASH ERROR %s", esp_err_to_name(viewed_file_hash_error));
        ESP_RETURN_ON_ERROR(draw_centered(230, line, UI_BAD, 1), TAG, "file hash error failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(224, "NOT CALCULATED", UI_MUTED, 2), TAG, "file hash idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(258, "TAP HASH TO START", UI_TEXT, 1), TAG, "file hash prompt failed");
    }

    const char *hash_label = viewed_file_hash_state == UI_FILE_HASH_READY ? "REHASH" : "HASH";
    if (current_page == UI_PAGE_LOGS) {
        ESP_RETURN_ON_ERROR(
            draw_button(8, UI_FILE_INFO_HASH_Y, 96, UI_FILE_INFO_HASH_HEIGHT, hash_label),
            TAG,
            "log hash button failed");
        ESP_RETURN_ON_ERROR(
            draw_button(112, UI_FILE_INFO_HASH_Y, 96, UI_FILE_INFO_HASH_HEIGHT, "QR"),
            TAG,
            "log QR button failed");
        return draw_button(
            216,
            UI_FILE_INFO_HASH_Y,
            96,
            UI_FILE_INFO_HASH_HEIGHT,
            log_file_name_is_system(viewed_file_name) ? "SYSTEM" : "DELETE");
    }
    return draw_button(64, UI_FILE_INFO_HASH_Y, 192, UI_FILE_INFO_HASH_HEIGHT, hash_label);
}

static esp_err_t draw_image_viewer(void)
{
    char line[64];
    snprintf(line, sizeof(line), "%.45s", viewed_file_name);
    ESP_RETURN_ON_ERROR(draw_centered(54, line, UI_GOOD, 1), TAG, "image name failed");
    ESP_RETURN_ON_ERROR(draw_centered(68, "LOADING BMP", UI_MUTED, 1), TAG, "image loading failed");

    cerberus_bmp_info_t image_info;
    esp_err_t err = cerberus_bmp_render_file(
        file_path,
        viewed_file_name,
        0,
        84,
        CERBERUS_LCD_WIDTH,
        312,
        &image_info);

    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 66, CERBERUS_LCD_WIDTH, 14, UI_BACKGROUND), TAG, "image status clear failed");
    if (err != ESP_OK) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 84, CERBERUS_LCD_WIDTH, 312, UI_BACKGROUND), TAG, "image error clear failed");
        ESP_RETURN_ON_ERROR(draw_centered(175, "BMP NOT SUPPORTED", UI_BAD, 2), TAG, "image error title failed");
        snprintf(line, sizeof(line), "ERROR %s", esp_err_to_name(err));
        return draw_centered(220, line, UI_MUTED, 1);
    }

    snprintf(
        line,
        sizeof(line),
        "%lux%lu  DISPLAY %ux%u",
        (unsigned long)image_info.source_width,
        (unsigned long)image_info.source_height,
        image_info.rendered_width,
        image_info.rendered_height);
    return draw_centered(68, line, UI_MUTED, 1);
}

static esp_err_t draw_file_tools(void)
{
    ESP_RETURN_ON_ERROR(draw_centered(66, "FILE TOOLS", UI_GOOD, 2), TAG, "file tools title failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 112, 272, 50, "NEW FOLDER"), TAG, "new folder failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 178, 272, 50, "NEW EMPTY FILE"), TAG, "new file failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 244, 272, 50, "SELECT ENTRY"), TAG, "select entry failed");
    ESP_RETURN_ON_ERROR(draw_button(24, 310, 272, 50, "START USB SD DRIVE"), TAG, "USB drive failed");
    return draw_centered(385,
        file_action_message[0] != '\0' ? file_action_message : "EXCLUSIVE MODE - EJECT BEFORE RETURN",
        file_action_message[0] != '\0' ? file_action_color : UI_MUTED,
        1);
}

static esp_err_t draw_file_actions(void)
{
    char line[64];
    ESP_RETURN_ON_ERROR(draw_centered(58, file_selected_entry.is_directory ? "DIRECTORY ACTIONS" : "FILE ACTIONS", UI_GOOD, 2), TAG, "actions title failed");
    snprintf(line, sizeof(line), "%.46s", file_selected_entry.name);
    ESP_RETURN_ON_ERROR(draw_centered(84, line, UI_TEXT, 1), TAG, "actions name failed");
    ESP_RETURN_ON_ERROR(draw_button(16, 116, 136, 52, "RENAME"), TAG, "rename action failed");
    ESP_RETURN_ON_ERROR(draw_button(168, 116, 136, 52, file_selected_entry.is_directory ? "MOVE" : "COPY"), TAG, "copy action failed");
    if (!file_selected_entry.is_directory) {
        ESP_RETURN_ON_ERROR(draw_button(16, 184, 136, 52, "MOVE"), TAG, "move action failed");
    }
    ESP_RETURN_ON_ERROR(draw_button(168, 184, 136, 52, "DELETE"), TAG, "delete action failed");
    if (!file_selected_entry.is_directory && strcmp(file_selected_path, CERBERUS_STORAGE_FIRMWARE_PATH) == 0) {
        ESP_RETURN_ON_ERROR(draw_button(48, 258, 224, 52, "INSPECT PACKAGE"), TAG, "package action failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(278, "COPY/MOVE: CHOOSE DESTINATION", UI_MUTED, 1), TAG, "actions help failed");
        ESP_RETURN_ON_ERROR(draw_centered(296, "THEN TAP PASTE", UI_MUTED, 1), TAG, "actions paste help failed");
    }
    if (file_action_message[0] != '\0') {
        ESP_RETURN_ON_ERROR(draw_centered(334, file_action_message, file_action_color, 1), TAG, "actions status failed");
    }
    return ESP_OK;
}

static esp_err_t draw_file_delete_confirm(void)
{
    char line[64];
    ESP_RETURN_ON_ERROR(draw_centered(112, "DELETE ENTRY?", UI_BAD, 2), TAG, "delete title failed");
    snprintf(line, sizeof(line), "%.46s", file_selected_entry.name);
    ESP_RETURN_ON_ERROR(draw_centered(158, line, UI_TEXT, 1), TAG, "delete name failed");
    ESP_RETURN_ON_ERROR(draw_centered(194, file_selected_entry.is_directory ? "DIRECTORY MUST BE EMPTY" : "THIS CANNOT BE UNDONE", UI_MUTED, 1), TAG, "delete warning failed");
    return draw_button(48, 258, 224, 62, "CONFIRM DELETE");
}

static esp_err_t draw_file_package_info(void)
{
    char line[72];
    ESP_RETURN_ON_ERROR(draw_centered(58, "FIRMWARE PACKAGE", UI_GOOD, 2), TAG, "package title failed");
    ESP_RETURN_ON_ERROR(draw_centered(84, file_selected_entry.name, UI_TEXT, 1), TAG, "package name failed");
    if (file_package_error != ESP_OK) {
        snprintf(line, sizeof(line), "INSPECTION ERROR %s", esp_err_to_name(file_package_error));
        ESP_RETURN_ON_ERROR(draw_centered(174, line, UI_BAD, 1), TAG, "package error failed");
        return draw_centered(210, "USE .BIN OR .CERB PACKAGE", UI_MUTED, 1);
    }
    snprintf(line, sizeof(line), "SIZE %llu BYTES", (unsigned long long)file_package_info.size_bytes);
    ESP_RETURN_ON_ERROR(draw_centered(122, line, UI_TEXT, 1), TAG, "package size failed");
    ESP_RETURN_ON_ERROR(draw_centered(154, file_package_info.esp_image ? "ESP IMAGE HEADER VALID" : "UNKNOWN IMAGE HEADER", file_package_info.esp_image ? UI_GOOD : UI_BAD, 1), TAG, "package format failed");
    if (file_package_info.esp_image) {
        snprintf(line, sizeof(line), "SEGMENTS %u  SPI MODE %u  CONFIG 0x%02X",
            file_package_info.segment_count, file_package_info.spi_mode, file_package_info.spi_speed_size);
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "package header failed");
    }
    static const char hex[] = "0123456789ABCDEF";
    char digest[33];
    for (size_t index = 0U; index < 16U; ++index) {
        digest[index * 2U] = hex[file_package_info.sha256[index] >> 4U];
        digest[index * 2U + 1U] = hex[file_package_info.sha256[index] & 0x0FU];
    }
    digest[32] = '\0';
    ESP_RETURN_ON_ERROR(draw_centered(226, "SHA-256 (FIRST HALF)", UI_MUTED, 1), TAG, "package hash title failed");
    ESP_RETURN_ON_ERROR(draw_centered(250, digest, UI_TEXT, 1), TAG, "package hash failed");
    ESP_RETURN_ON_ERROR(draw_centered(302, "METADATA ONLY - NO FLASH ACTION", UI_GOOD, 1), TAG, "package safety failed");
    return ESP_OK;
}

static esp_err_t draw_files(void)
{
    char line[64];

    if (storage_safely_removed) {
        bool usb_reserved = cerberus_storage_get_owner() == CERBERUS_STORAGE_OWNER_USB;
        cerberus_usb_mass_storage_status_t msc;
        cerberus_usb_mass_storage_get_status(&msc);
        const char *title = !usb_reserved ? "MICROSD SAFELY UNMOUNTED"
            : (msc.host_attached && !msc.safe_to_release ? "USB SD DRIVE ACTIVE"
                : (msc.host_attached ? "USB DRIVE EJECTED" : "READY FOR USB HOST"));
        const char *detail = !usb_reserved ? "THE CARD CAN NOW BE REMOVED"
            : (msc.safe_to_release ? "SAFE TO RETURN TO FIRMWARE" : "EJECT DRIVE ON HOST BEFORE RETURN");
        ESP_RETURN_ON_ERROR(draw_centered(125, title, UI_GOOD, 2), TAG, "SD safe title failed");
        ESP_RETURN_ON_ERROR(draw_centered(180, detail, msc.safe_to_release ? UI_GOOD : UI_BAD, 1), TAG, "SD safe detail failed");
        ESP_RETURN_ON_ERROR(draw_centered(215, usb_reserved ? "FIRMWARE FILE ACCESS IS LOCKED" : "TAP MOUNT AFTER INSERTING A CARD", UI_MUTED, 1), TAG, "SD safe help failed");
        if (msc.last_error != ESP_OK || storage_last_error != ESP_OK) {
            snprintf(line, sizeof(line), "RETURN ERROR %s", esp_err_to_name(
                msc.last_error != ESP_OK ? msc.last_error : storage_last_error));
            ESP_RETURN_ON_ERROR(draw_centered(260, line, UI_BAD, 1), TAG, "SD return error");
        }
        return draw_button(40, UI_FILE_CONTROLS_Y, 240, 42, usb_reserved ? "RETURN SD TO FIRMWARE" : "MOUNT CARD");
    }

    if (!file_listing_checked || !storage_checked || !cerberus_storage_is_mounted()) {
        (void)refresh_file_listing(false);
    }

    if (storage_last_error != ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_centered(132, "MICROSD NOT AVAILABLE", UI_BAD, 2), TAG, "SD error title failed");
        snprintf(line, sizeof(line), "ERROR %s", esp_err_to_name(storage_last_error));
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "SD error detail failed");
        ESP_RETURN_ON_ERROR(draw_centered(230, "INSERT FAT32 CARD", UI_TEXT, 1), TAG, "SD error help failed");
    } else {
        snprintf(
            line,
            sizeof(line),
            "SD %llu MB  FREE %llu MB",
            (unsigned long long)(storage_status.total_bytes / (1024U * 1024U)),
            (unsigned long long)(storage_status.free_bytes / (1024U * 1024U)));
        ESP_RETURN_ON_ERROR(draw_centered(56, line, UI_GOOD, 1), TAG, "SD capacity failed");

        snprintf(line, sizeof(line), "%s %.40s", file_mode == UI_FILE_MODE_SELECT ? "SELECT IN" : "PATH", file_path);
        ESP_RETURN_ON_ERROR(cerberus_display_draw_text(10, 78, line, UI_MUTED, UI_BACKGROUND, 1), TAG, "SD path failed");

        for (size_t row = 0; row < UI_FILE_VISIBLE_ROWS; ++row) {
            uint16_t y = UI_FILE_LIST_Y + (uint16_t)row * UI_FILE_ROW_HEIGHT;
            size_t index = file_page_offset + row;
            if (index >= file_entry_count) {
                ESP_RETURN_ON_ERROR(
                    cerberus_ui_widget_draw_list_row(
                        (cerberus_ui_rect_t){8U, y, 304U, UI_FILE_ROW_HEIGHT - 2U},
                        NULL,
                        NULL,
                        UI_WIDGET_THEME),
                    TAG,
                    "empty file row failed");
                continue;
            }

            const cerberus_storage_entry_t *entry = &file_entries[index];
            snprintf(line, sizeof(line), "%s %.34s", entry->is_directory ? "[D]" : "[F]", entry->name);
            char size[12];
            if (entry->is_directory) {
                snprintf(size, sizeof(size), "DIR");
            } else {
                format_file_size(entry->size_bytes, size, sizeof(size));
            }
            ESP_RETURN_ON_ERROR(
                cerberus_ui_widget_draw_list_row(
                    (cerberus_ui_rect_t){8U, y, 304U, UI_FILE_ROW_HEIGHT - 2U},
                    line,
                    size,
                    UI_WIDGET_THEME),
                TAG,
                "file row failed");
        }
    }

    if (file_clipboard_active) {
        ESP_RETURN_ON_ERROR(draw_small_button(8, UI_FILE_CONTROLS_Y, 50, 42, strcmp(file_path, "/") == 0 ? "ROOT" : "UP"), TAG, "paste up failed");
        ESP_RETURN_ON_ERROR(draw_small_button(62, UI_FILE_CONTROLS_Y, 52, 42, "PREV"), TAG, "paste prev failed");
        ESP_RETURN_ON_ERROR(draw_small_button(118, UI_FILE_CONTROLS_Y, 52, 42, "NEXT"), TAG, "paste next failed");
        ESP_RETURN_ON_ERROR(draw_small_button(174, UI_FILE_CONTROLS_Y, 52, 42, "CANCEL"), TAG, "paste cancel failed");
        return draw_small_button(230, UI_FILE_CONTROLS_Y, 82, 42, "PASTE");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, UI_FILE_CONTROLS_Y, 64, 42,
        file_mode == UI_FILE_MODE_SELECT ? "CANCEL" : (strcmp(file_path, "/") == 0 ? "EJECT" : "UP")), TAG, "files up/eject failed");
    ESP_RETURN_ON_ERROR(draw_small_button(78, UI_FILE_CONTROLS_Y, 70, 42, "PREV"), TAG, "files prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(154, UI_FILE_CONTROLS_Y, 70, 42, "NEXT"), TAG, "files next failed");
    return draw_small_button(230, UI_FILE_CONTROLS_Y, 82, 42, file_mode == UI_FILE_MODE_SELECT ? "SELECT" : "TOOLS");
}

static bool log_report_name_is_generated(const char *name)
{
    if (name == NULL || strlen(name) != 12U ||
        (memcmp(name, "154_", 4U) != 0 && memcmp(name, "WIFI", 4U) != 0 &&
         memcmp(name, "WDEF", 4U) != 0) ||
        memcmp(name + 8U, ".CSV", 4U) != 0) {
        return false;
    }
    for (uint8_t index = 4U; index < 8U; ++index) {
        if (name[index] < '0' || name[index] > '9') {
            return false;
        }
    }
    return true;
}

static bool log_file_name_is_system(const char *name)
{
    return name != NULL &&
        (strcmp(name, "SYSTEM.CSV") == 0 ||
         strcmp(name, "SYSTEM.1.CSV") == 0 ||
         strcmp(name, "SYSTEM.2.CSV") == 0);
}

static bool log_file_name_is_visible(const char *name)
{
    return log_report_name_is_generated(name) || log_file_name_is_system(name);
}

static esp_err_t refresh_log_listing(bool remount)
{
    log_checked = true;
    log_entry_count = 0U;
    if (remount && cerberus_storage_is_mounted()) {
        log_error = cerberus_storage_unmount();
        if (log_error != ESP_OK) {
            return log_error;
        }
    }
    log_error = cerberus_storage_mount();
    if (log_error != ESP_OK) {
        return log_error;
    }
    if (storage_safely_removed) {
        storage_safely_removed = false;
        storage_checked = false;
    }
    log_error = cerberus_storage_list_directory(
        UI_LOG_PATH,
        log_entries,
        UI_FILE_ENTRY_CAPACITY,
        &log_entry_count);
    if (log_error == ESP_ERR_NOT_FOUND) {
        log_error = ESP_OK;
        log_entry_count = 0U;
    } else if (log_error != ESP_OK) {
        return log_error;
    }

    size_t output = 0U;
    for (size_t input = 0U; input < log_entry_count; ++input) {
        if (!log_entries[input].is_directory &&
            log_file_name_is_visible(log_entries[input].name)) {
            if (output != input) {
                log_entries[output] = log_entries[input];
            }
            ++output;
        }
    }
    log_entry_count = output;
    if (log_page_offset >= log_entry_count) {
        log_page_offset = log_entry_count == 0U
            ? 0U
            : ((log_entry_count - 1U) / UI_FILE_VISIBLE_ROWS) * UI_FILE_VISIBLE_ROWS;
    }
    return ESP_OK;
}

static esp_err_t draw_log_list(void)
{
    char line[64];
    if (!log_checked || !cerberus_storage_is_mounted()) {
        (void)refresh_log_listing(false);
    }
    if (log_error != ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_centered(132, "LOG STORAGE UNAVAILABLE", UI_BAD, 2), TAG, "logs storage failed");
        snprintf(line, sizeof(line), "ERROR %s", esp_err_to_name(log_error));
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "logs storage detail failed");
        ESP_RETURN_ON_ERROR(draw_centered(230, "INSERT FAT32 MICROSD CARD", UI_TEXT, 1), TAG, "logs storage help failed");
    } else {
        snprintf(line, sizeof(line), "%u LOG FILE%s",
            (unsigned)log_entry_count,
            log_entry_count == 1U ? "" : "S");
        ESP_RETURN_ON_ERROR(draw_centered(56, line, UI_GOOD, 1), TAG, "logs count failed");
        ESP_RETURN_ON_ERROR(draw_centered(76, UI_LOG_PATH, UI_MUTED, 1), TAG, "logs path failed");

        if (log_entry_count == 0U) {
            ESP_RETURN_ON_ERROR(draw_centered(175, "NO LOG FILES", UI_MUTED, 2), TAG, "logs empty failed");
            ESP_RETURN_ON_ERROR(draw_centered(220, "SYSTEM EVENTS APPEAR AFTER BOOT", UI_TEXT, 1), TAG, "logs empty help failed");
        }
        for (size_t row = 0U; row < UI_FILE_VISIBLE_ROWS; ++row) {
            uint16_t y = UI_FILE_LIST_Y + (uint16_t)row * UI_FILE_ROW_HEIGHT;
            ESP_RETURN_ON_ERROR(
                cerberus_display_fill_rect(8, y, 304, UI_FILE_ROW_HEIGHT - 2U, UI_PANEL),
                TAG,
                "log row failed");
            size_t index = log_page_offset + row;
            if (index >= log_entry_count) {
                continue;
            }
            const cerberus_storage_entry_t *entry = &log_entries[index];
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(14, y + 10U, entry->name, UI_TEXT, UI_PANEL, 1),
                TAG,
                "log name failed");
            char size[12];
            format_file_size(entry->size_bytes, size, sizeof(size));
            uint16_t width = text_width(size, 1);
            ESP_RETURN_ON_ERROR(
                cerberus_display_draw_text(304U - width, y + 20U, size, UI_MUTED, UI_PANEL, 1),
                TAG,
                "log size failed");
        }
        if (log_action_message[0] != '\0') {
            ESP_RETURN_ON_ERROR(draw_centered(88, log_action_message, log_action_color, 1), TAG, "log action failed");
        }
    }

    ESP_RETURN_ON_ERROR(draw_small_button(28, UI_FILE_CONTROLS_Y, 76, 42, "PREV"), TAG, "logs prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(122, UI_FILE_CONTROLS_Y, 76, 42, "NEXT"), TAG, "logs next failed");
    return draw_small_button(216, UI_FILE_CONTROLS_Y, 76, 42, "RELOAD");
}

static esp_err_t draw_log_delete_confirmation(void)
{
    char size[20];
    format_file_size(viewed_file_size, size, sizeof(size));
    char file_line[64];
    snprintf(file_line, sizeof(file_line), "%.42s  %s", viewed_file_name, size);
    const char *lines[] = {
        file_line,
        "THIS PERMANENTLY REMOVES THE FILE",
        "IT CANNOT BE RECOVERED BY CERBERUS",
    };
    ESP_RETURN_ON_ERROR(
        cerberus_ui_widget_draw_dialog(
            (cerberus_ui_rect_t){8U, 70U, 304U, 230U},
            "DELETE GENERATED REPORT?",
            lines,
            sizeof(lines) / sizeof(lines[0]),
            UI_BAD,
            UI_WIDGET_THEME),
        TAG,
        "log delete dialog failed");
    ESP_RETURN_ON_ERROR(draw_button(20, 330, 132, 58, "CANCEL"), TAG, "log delete cancel failed");
    return draw_button(168, 330, 132, 58, "DELETE");
}

static esp_err_t draw_log_qr(void)
{
    ESP_RETURN_ON_ERROR(draw_centered(54, "REPORT IDENTIFIER", UI_GOOD, 1), TAG, "log QR title failed");
    cerberus_qr_info_t info;
    esp_err_t err = cerberus_qr_render_text(
        log_qr_payload,
        0,
        70,
        CERBERUS_LCD_WIDTH,
        266,
        &info);
    if (err != ESP_OK) {
        char line[48];
        snprintf(line, sizeof(line), "QR ERROR %s", esp_err_to_name(err));
        return draw_centered(200, line, UI_BAD, 1);
    }
    ESP_RETURN_ON_ERROR(draw_centered(342, viewed_file_name, UI_TEXT, 1), TAG, "log QR name failed");
    ESP_RETURN_ON_ERROR(draw_centered(362, "FILENAME + SIZE + SHA-256 ONLY", UI_MUTED, 1), TAG, "log QR scope failed");
    char line[48];
    snprintf(line, sizeof(line), "%u MODULES  %uX SCALE", info.module_count, info.scale);
    return draw_centered(385, line, UI_MUTED, 1);
}

static esp_err_t draw_logs(void)
{
    if (log_mode == UI_LOG_MODE_TEXT) {
        return draw_text_viewer();
    }
    if (log_mode == UI_LOG_MODE_INFO) {
        return draw_file_info();
    }
    if (log_mode == UI_LOG_MODE_QR) {
        return draw_log_qr();
    }
    if (log_mode == UI_LOG_MODE_DELETE_CONFIRM) {
        return draw_log_delete_confirmation();
    }
    return draw_log_list();
}

static void format_hms(uint32_t total_seconds, char *text, size_t text_size)
{
    uint32_t hours = total_seconds / 3600U;
    uint32_t minutes = (total_seconds / 60U) % 60U;
    uint32_t seconds = total_seconds % 60U;
    snprintf(text, text_size, "%02lu:%02lu:%02lu",
        (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);
}

static esp_err_t draw_settings_clock_timer(void)
{
    int64_t now_ms = esp_timer_get_time() / 1000LL;
    char line[48];
    char clock_text[20];
    format_hms(cerberus_ui_uptime_seconds(now_ms), clock_text, sizeof(clock_text));
    ESP_RETURN_ON_ERROR(draw_centered(66, "DEVICE UPTIME", UI_MUTED,
        ui_accessibility_size != 0U ? 2U : 1U), TAG, "clock label failed");
    ESP_RETURN_ON_ERROR(draw_centered(94, clock_text, UI_TEXT, 3), TAG, "clock failed");

    uint32_t remaining = cerberus_ui_timer_remaining(now_ms);
    if (cerberus_ui_timer_is_running()) {
        format_hms(remaining, clock_text, sizeof(clock_text));
        snprintf(line, sizeof(line), "TIMER %s", clock_text);
    } else {
        snprintf(line, sizeof(line), "TIMER READY");
    }
    ESP_RETURN_ON_ERROR(draw_centered(146, line,
        cerberus_ui_timer_is_running() ? UI_GOOD : UI_TEXT, 2), TAG, "timer state failed");
    ESP_RETURN_ON_ERROR(draw_small_button(12, 184, 88, 48, "1 MIN"), TAG, "one minute failed");
    ESP_RETURN_ON_ERROR(draw_small_button(116, 184, 88, 48, "5 MIN"), TAG, "five minute failed");
    ESP_RETURN_ON_ERROR(draw_small_button(220, 184, 88, 48, "10 MIN"), TAG, "ten minute failed");
    snprintf(line, sizeof(line), "SELECTED %lu MINUTE%s",
        (unsigned long)(settings_timer_seconds / 60U),
        settings_timer_seconds == 60U ? "" : "S");
    ESP_RETURN_ON_ERROR(draw_centered(246, line, UI_MUTED, 1), TAG, "timer selection failed");
    ESP_RETURN_ON_ERROR(draw_button(52, 276, 216, 54,
        cerberus_ui_timer_is_running() ? "CANCEL TIMER" : "START TIMER"),
        TAG, "timer action failed");
    snprintf(line, sizeof(line), "LOCAL ALERTS %u",
        (unsigned)cerberus_ui_notification_count());
    return draw_centered(354, line, UI_MUTED,
        ui_accessibility_size != 0U ? 2U : 1U);
}

static esp_err_t draw_settings_notifications(void)
{
    size_t count = cerberus_ui_notification_count();
    char line[64];
    snprintf(line, sizeof(line), "LOCAL NOTIFICATIONS %u", (unsigned)count);
    ESP_RETURN_ON_ERROR(draw_centered(62, line, UI_TEXT, 2), TAG, "notification title failed");
    if (count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(176, "NO NOTIFICATIONS", UI_MUTED,
            ui_accessibility_size != 0U ? 2U : 1U), TAG, "notification empty failed");
    } else {
        size_t visible = count < 6U ? count : 6U;
        for (size_t row = 0U; row < visible; ++row) {
            cerberus_notification_t notification;
            if (!cerberus_ui_notification_get(row, &notification)) {
                continue;
            }
            char timestamp[16];
            format_hms(notification.uptime_seconds, timestamp, sizeof(timestamp));
            snprintf(line, sizeof(line), "%s  %.36s", timestamp, notification.text);
            ESP_RETURN_ON_ERROR(
                cerberus_ui_widget_draw_list_row(
                    (cerberus_ui_rect_t){12, (uint16_t)(98U + row * 40U), 296, 34},
                    line,
                    NULL,
                    UI_WIDGET_THEME),
                TAG,
                "notification row failed");
        }
    }
    return draw_button(52, 350, 216, 54, "CLEAR NOTIFICATIONS");
}

static esp_err_t draw_settings_accessibility(void)
{
    const cerberus_touch_calibration_t *calibration =
        cerberus_settings_get_touch_calibration();
    ESP_RETURN_ON_ERROR(draw_centered(68, "BODY TEXT SIZE", UI_TEXT, 2), TAG,
        "accessibility title failed");
    ESP_RETURN_ON_ERROR(draw_button(52, 104, 216, 54,
        ui_accessibility_size != 0U ? "LARGE" : "STANDARD"), TAG,
        "accessibility size failed");
    ESP_RETURN_ON_ERROR(draw_centered(180,
        ui_accessibility_size != 0U ? "LARGE BODY TEXT" : "STANDARD BODY TEXT",
        UI_MUTED, ui_accessibility_size != 0U ? 2U : 1U), TAG,
        "accessibility preview failed");
    ESP_RETURN_ON_ERROR(draw_centered(232, "TOUCH CALIBRATION", UI_TEXT, 2), TAG,
        "calibration title failed");
    ESP_RETURN_ON_ERROR(draw_centered(262,
        calibration->valid ? "CUSTOM CALIBRATION ACTIVE" : "FACTORY COORDINATES ACTIVE",
        calibration->valid ? UI_GOOD : UI_MUTED, 1), TAG,
        "calibration state failed");
    ESP_RETURN_ON_ERROR(draw_button(30, 292, 260, 52, "RUN 4-POINT CALIBRATION"), TAG,
        "calibration start failed");
    return draw_small_button(72, 360, 176, 42, "RESTORE FACTORY TOUCH");
}

static esp_err_t draw_settings_touch_calibration(void)
{
    static const uint16_t target_x[4] = {12U, 308U, 12U, 308U};
    static const uint16_t target_y[4] = {62U, 62U, 408U, 408U};
    ESP_RETURN_ON_ERROR(draw_centered(190, "TOUCH THE RED TARGET", UI_TEXT, 2), TAG,
        "calibration instruction failed");
    if (touch_calibration_step < 4U) {
        uint16_t x = target_x[touch_calibration_step];
        uint16_t y = target_y[touch_calibration_step];
        uint16_t left = x > 10U ? x - 10U : 0U;
        uint16_t top = y > 10U ? y - 10U : 0U;
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(left, top, 20, 20, UI_BAD), TAG,
            "calibration target failed");
        char line[32];
        snprintf(line, sizeof(line), "POINT %u OF 4", touch_calibration_step + 1U);
        return draw_centered(226, line, UI_MUTED, 1);
    }
    if (touch_calibration_error == ESP_OK) {
        ESP_RETURN_ON_ERROR(draw_centered(226, "CALIBRATION SAVED", UI_GOOD, 2), TAG,
            "calibration saved failed");
        return draw_centered(260, "BACK RETURNS TO ACCESSIBILITY", UI_MUTED, 1);
    }
    char line[48];
    snprintf(line, sizeof(line), "CALIBRATION FAILED %s",
        esp_err_to_name(touch_calibration_error));
    ESP_RETURN_ON_ERROR(draw_centered(226, line, UI_BAD, 1), TAG,
        "calibration error failed");
    return draw_centered(260, "BACK AND TRY AGAIN", UI_MUTED, 1);
}

static esp_err_t draw_settings_data(void)
{
    char status[64];
    uint16_t color = UI_MUTED;
    ESP_RETURN_ON_ERROR(draw_centered(72, "CONFIGURATION DATA", UI_TEXT, 2), TAG,
        "configuration data title failed");
    ESP_RETURN_ON_ERROR(draw_centered(96, "BACKUPS INCLUDE DISPLAY, FAVORITES,", UI_MUTED, 1), TAG,
        "configuration scope one failed");
    ESP_RETURN_ON_ERROR(draw_centered(116, "AND WIFI BASELINES - NO PASSWORDS", UI_MUTED, 1), TAG,
        "configuration scope two failed");
    const char *privacy_label = !ui_config.settings_available
        ? "ADDRESS DISPLAY: UNAVAILABLE"
        : (privacy_addresses_masked()
            ? "ADDRESS DISPLAY: MASKED"
            : "ADDRESS DISPLAY: FULL");
    ESP_RETURN_ON_ERROR(draw_button(52, 140, 216, 44, privacy_label), TAG,
        "address privacy button failed");
    if (settings_restore_checked) {
        snprintf(status, sizeof(status), "RESTORE FAILED %s",
            esp_err_to_name(settings_restore_error));
        color = UI_BAD;
    } else if (settings_backup_checked && settings_backup_error == ESP_OK) {
        snprintf(status, sizeof(status), "SAVED %.12s", settings_backup_file_name);
        color = UI_GOOD;
    } else if (settings_backup_checked) {
        snprintf(status, sizeof(status), "BACKUP FAILED %s",
            esp_err_to_name(settings_backup_error));
        color = UI_BAD;
    } else {
        snprintf(status, sizeof(status), "DISPLAY MASKING DOES NOT CHANGE SAVED TRUST DATA");
    }
    ESP_RETURN_ON_ERROR(draw_centered(204, status, color, 1), TAG,
        "configuration data state failed");
    ESP_RETURN_ON_ERROR(draw_button(28, 232, 124, 54, "BACKUP"), TAG,
        "data backup failed");
    ESP_RETURN_ON_ERROR(draw_button(168, 232, 124, 54, "RESTORE"), TAG,
        "data restore failed");
    ESP_RETURN_ON_ERROR(draw_centered(316, "FACTORY RESET CLEARS NVS ONLY", UI_BAD, 1), TAG,
        "factory reset warning failed");
    return draw_button(52, 344, 216, 58, "FACTORY RESET");
}

static esp_err_t draw_settings(void)
{
    if (settings_reset_confirmation) {
        ESP_RETURN_ON_ERROR(draw_centered(74, "RESET CERBERUS SETTINGS?", UI_BAD, 2), TAG, "reset title failed");
        ESP_RETURN_ON_ERROR(draw_centered(126, "REMOVES DISPLAY PREFERENCES,", UI_TEXT, 1), TAG, "reset preference warning failed");
        ESP_RETURN_ON_ERROR(draw_centered(148, "BLE FAVORITES, AND WIFI BASELINES", UI_TEXT, 1), TAG, "reset list warning failed");
        ESP_RETURN_ON_ERROR(draw_centered(202, "MICROSD FILES ARE UNCHANGED", UI_GOOD, 1), TAG, "reset SD notice failed");
        ESP_RETURN_ON_ERROR(draw_centered(224, "C5 FIRMWARE IS UNCHANGED", UI_GOOD, 1), TAG, "reset C5 notice failed");
        if (settings_reset_error != ESP_OK) {
            char error_line[48];
            snprintf(error_line, sizeof(error_line), "RESET FAILED: %s", esp_err_to_name(settings_reset_error));
            ESP_RETURN_ON_ERROR(draw_centered(278, error_line, UI_BAD, 1), TAG, "reset error failed");
        } else {
            ESP_RETURN_ON_ERROR(draw_centered(278, "THIS CANNOT BE UNDONE", UI_BAD, 1), TAG, "reset final warning failed");
        }
        ESP_RETURN_ON_ERROR(draw_button(20, 330, 132, 58, "CANCEL"), TAG, "reset cancel failed");
        return draw_button(168, 330, 132, 58, "RESET");
    }
    if (settings_restore_confirmation) {
        char line[64];
        ESP_RETURN_ON_ERROR(draw_centered(70, "RESTORE CONFIGURATION?", UI_BAD, 2), TAG, "restore title failed");
        snprintf(line, sizeof(line), "FILE %.12s", settings_restore_file_name);
        ESP_RETURN_ON_ERROR(draw_centered(116, line, UI_TEXT, 1), TAG, "restore file failed");
        snprintf(
            line,
            sizeof(line),
            "BRIGHTNESS %u%%  TIMEOUT %us",
            settings_restore_info.brightness_percent,
            (unsigned)settings_restore_info.screen_timeout_seconds);
        ESP_RETURN_ON_ERROR(draw_centered(146, line, UI_TEXT, 1), TAG, "restore display summary failed");
        snprintf(
            line,
            sizeof(line),
            "BLE FAVORITES %u  WIFI BASELINES %u",
            settings_restore_info.ble_favorite_count,
            settings_restore_info.wifi_trusted_count);
        ESP_RETURN_ON_ERROR(draw_centered(170, line, UI_TEXT, 1), TAG, "restore list summary failed");
        snprintf(
            line,
            sizeof(line),
            "TEXT SIZE %s",
            settings_restore_info.accessibility_size != 0U ? "LARGE" : "STANDARD");
        ESP_RETURN_ON_ERROR(draw_centered(194, line, UI_TEXT, 1), TAG,
            "restore accessibility summary failed");
        snprintf(line, sizeof(line), "ADDRESS IDS %s - REPLACES SETTINGS",
            settings_restore_info.privacy_mask_addresses ? "MASKED" : "FULL");
        ESP_RETURN_ON_ERROR(draw_centered(218, line, UI_BAD, 1), TAG, "restore replacement warning failed");
        ESP_RETURN_ON_ERROR(draw_centered(242, "NO PASSWORDS ARE IMPORTED", UI_GOOD, 1), TAG, "restore password notice failed");
        ESP_RETURN_ON_ERROR(draw_centered(266, "MICROSD AND C5 FIRMWARE ARE UNCHANGED", UI_GOOD, 1), TAG, "restore scope notice failed");
        if (settings_restore_error != ESP_OK) {
            snprintf(line, sizeof(line), "RESTORE FAILED: %s", esp_err_to_name(settings_restore_error));
            ESP_RETURN_ON_ERROR(draw_centered(294, line, UI_BAD, 1), TAG, "restore error failed");
        } else {
            ESP_RETURN_ON_ERROR(draw_centered(294, "CRC AND RECORDS VALIDATED", UI_GOOD, 1), TAG, "restore validation status failed");
        }
        ESP_RETURN_ON_ERROR(draw_button(20, 330, 132, 58, "CANCEL"), TAG, "restore cancel failed");
        return draw_button(168, 330, 132, 58, "RESTORE");
    }

    if (settings_mode == UI_SETTINGS_MODE_CLOCK_TIMER) {
        return draw_settings_clock_timer();
    }
    if (settings_mode == UI_SETTINGS_MODE_NOTIFICATIONS) {
        return draw_settings_notifications();
    }
    if (settings_mode == UI_SETTINGS_MODE_ACCESSIBILITY) {
        return draw_settings_accessibility();
    }
    if (settings_mode == UI_SETTINGS_MODE_DATA) {
        return draw_settings_data();
    }
    if (settings_mode == UI_SETTINGS_MODE_TOUCH_CALIBRATION) {
        return draw_settings_touch_calibration();
    }

    char line[40];
    snprintf(line, sizeof(line), "BRIGHTNESS %u%%", cerberus_display_get_brightness());
    ESP_RETURN_ON_ERROR(draw_centered(58, line, UI_TEXT, 2), TAG, "settings brightness failed");
    ESP_RETURN_ON_ERROR(draw_button(28, 86, 112, 44, "-10%"), TAG, "settings down failed");
    ESP_RETURN_ON_ERROR(draw_button(180, 86, 112, 44, "+10%"), TAG, "settings up failed");

    const char *timeout_label;
    switch (ui_screen_timeout_seconds) {
    case 30U:
        timeout_label = "30 SECONDS";
        break;
    case 60U:
        timeout_label = "1 MINUTE";
        break;
    case 120U:
        timeout_label = "2 MINUTES";
        break;
    case 300U:
        timeout_label = "5 MINUTES";
        break;
    default:
        timeout_label = "DISABLED";
        break;
    }
    ESP_RETURN_ON_ERROR(draw_centered(164, "SCREEN TIMEOUT", UI_TEXT, 2), TAG, "settings timeout title failed");
    ESP_RETURN_ON_ERROR(draw_button(52, 190, 216, 52, timeout_label), TAG, "settings timeout button failed");
    ESP_RETURN_ON_ERROR(draw_centered(260, "FIRST TOUCH ONLY WAKES SCREEN", UI_MUTED, 1), TAG, "settings wake help failed");
    ESP_RETURN_ON_ERROR(draw_centered(
        286,
        ui_config.settings_available ? "SAVED ACROSS REBOOTS" : "SESSION ONLY - NVS ERROR",
        ui_config.settings_available ? UI_GOOD : UI_BAD,
        1), TAG, "settings persistence status failed");

    ESP_RETURN_ON_ERROR(draw_small_button(4, 350, 74, 54, "TIME"), TAG,
        "settings time button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 350, 74, 54, "ALERTS"), TAG,
        "settings alerts button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(164, 350, 74, 54, "ACCESS"), TAG,
        "settings access button failed");
    return draw_small_button(244, 350, 72, 54, "DATA");
}

static esp_err_t refresh_script_listing(void)
{
    script_listing_checked = true;
    script_entry_count = 0U;
    if (!cerberus_storage_is_mounted()) {
        script_listing_error = cerberus_storage_mount();
        if (script_listing_error != ESP_OK) {
            return script_listing_error;
        }
    }
    size_t raw_count = 0U;
    script_listing_error = cerberus_storage_list_directory(
        CERBERUS_STORAGE_SCRIPTS_PATH,
        script_entries,
        UI_SCRIPT_ENTRY_CAPACITY,
        &raw_count);
    if (script_listing_error != ESP_OK) {
        return script_listing_error;
    }
    for (size_t index = 0U; index < raw_count; ++index) {
        if (script_entries[index].is_directory ||
            !cerberus_script_file_name_is_valid(script_entries[index].name)) {
            continue;
        }
        if (script_entry_count != index) {
            script_entries[script_entry_count] = script_entries[index];
        }
        ++script_entry_count;
    }
    if (script_page_offset >= script_entry_count) {
        script_page_offset = script_entry_count == 0U
            ? 0U
            : ((script_entry_count - 1U) / UI_SCRIPT_VISIBLE_ROWS) * UI_SCRIPT_VISIBLE_ROWS;
    }
    return ESP_OK;
}

static esp_err_t draw_scripts_list(void)
{
    char line[72];
    if (!script_listing_checked) {
        (void)refresh_script_listing();
    }
    ESP_RETURN_ON_ERROR(draw_centered(56, "RESTRICTED LOCAL SCRIPTS", UI_TEXT, 2), TAG,
        "scripts title failed");
    if (script_listing_error != ESP_OK) {
        snprintf(line, sizeof(line), "SCRIPTS UNAVAILABLE %s",
            esp_err_to_name(script_listing_error));
        ESP_RETURN_ON_ERROR(draw_centered(174, line, UI_BAD, 1), TAG,
            "scripts unavailable failed");
        ESP_RETURN_ON_ERROR(draw_centered(206, "INSERT OR REMOUNT THE MICROSD CARD", UI_MUTED, 1), TAG,
            "scripts storage guidance failed");
    } else if (script_entry_count == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(160, "NO .CBS SCRIPTS FOUND", UI_MUTED, 2), TAG,
            "scripts empty failed");
        ESP_RETURN_ON_ERROR(draw_centered(198, "/CERBERUS/SCRIPTS", UI_GOOD, 1), TAG,
            "scripts path failed");
        ESP_RETURN_ON_ERROR(draw_centered(226, "FILES MUST USE CERBERUS-SCRIPT 1", UI_MUTED, 1), TAG,
            "scripts format failed");
    } else {
        for (size_t row = 0U; row < UI_SCRIPT_VISIBLE_ROWS; ++row) {
            size_t index = script_page_offset + row;
            if (index >= script_entry_count) {
                break;
            }
            uint16_t y = UI_SCRIPT_LIST_Y + (uint16_t)row * UI_SCRIPT_ROW_HEIGHT;
            ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(10, y, 300, 48, UI_PANEL), TAG,
                "script row failed");
            snprintf(line, sizeof(line), "%.42s", script_entries[index].name);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 8U, line,
                UI_TEXT, UI_PANEL, 1), TAG, "script name failed");
            snprintf(line, sizeof(line), "%lu BYTES  TAP TO VALIDATE",
                (unsigned long)script_entries[index].size_bytes);
            ESP_RETURN_ON_ERROR(cerberus_display_draw_text(18, y + 27U, line,
                UI_MUTED, UI_PANEL, 1), TAG, "script size failed");
        }
    }
    ESP_RETURN_ON_ERROR(draw_small_button(8, 360, 88, 42, "PREV"), TAG,
        "scripts prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(116, 360, 88, 42, "RELOAD"), TAG,
        "scripts reload failed");
    return draw_small_button(224, 360, 88, 42, "NEXT");
}

static esp_err_t draw_script_detail(void)
{
    char line[72];
    ESP_RETURN_ON_ERROR(draw_centered(58, "SCRIPT VALIDATION", UI_TEXT, 2), TAG,
        "script detail title failed");
    snprintf(line, sizeof(line), "%.42s", loaded_script.name);
    ESP_RETURN_ON_ERROR(draw_centered(88, line, UI_GOOD, 1), TAG,
        "script detail name failed");
    if (script_load_error != ESP_OK) {
        snprintf(line, sizeof(line), "REJECTED: %s", esp_err_to_name(script_load_error));
        ESP_RETURN_ON_ERROR(draw_centered(146, line, UI_BAD, 1), TAG,
            "script rejected failed");
        snprintf(line, sizeof(line), "LINE %u: %.48s",
            script_parse_error.line, script_parse_error.message);
        ESP_RETURN_ON_ERROR(draw_centered(178, line, UI_MUTED, 1), TAG,
            "script parse error failed");
        ESP_RETURN_ON_ERROR(draw_centered(236, "NO ACTIONS WERE EXECUTED", UI_GOOD, 1), TAG,
            "script rejection policy failed");
        return draw_button(40, 330, 240, 52, "INVALID SCRIPT");
    }
    snprintf(line, sizeof(line), "%u ACTIONS  MAX WAIT %lu MS",
        loaded_script.action_count, (unsigned long)loaded_script.total_wait_ms);
    ESP_RETURN_ON_ERROR(draw_centered(132, line, UI_GOOD, 1), TAG,
        "script summary failed");
    ESP_RETURN_ON_ERROR(draw_centered(170, "ALLOWED: TEXT DIALOG WAIT TIMER QR", UI_TEXT, 1), TAG,
        "script allowed actions failed");
    ESP_RETURN_ON_ERROR(draw_centered(202, "NO SHELL / HID / RADIO TX / GPIO", UI_MUTED, 1), TAG,
        "script excluded actions failed");
    ESP_RETURN_ON_ERROR(draw_centered(226, "NO CREDENTIAL OR FIRMWARE ACCESS", UI_MUTED, 1), TAG,
        "script excluded access failed");
    ESP_RETURN_ON_ERROR(draw_centered(270, "BACK CANCELS A RUN IMMEDIATELY", UI_MUTED, 1), TAG,
        "script cancel help failed");
    return draw_button(40, 330, 240, 52, "RUN VALIDATED SCRIPT");
}

static esp_err_t draw_script_text_lines(const char *text, uint16_t color, uint16_t start_y)
{
    char line[41];
    size_t length = strlen(text);
    for (size_t row = 0U; row < 4U; ++row) {
        size_t offset = row * 40U;
        if (offset >= length) {
            break;
        }
        size_t count = length - offset < 40U ? length - offset : 40U;
        memcpy(line, text + offset, count);
        line[count] = '\0';
        ESP_RETURN_ON_ERROR(draw_centered(start_y + (uint16_t)row * 24U,
            line, color, 1), TAG, "script text line failed");
    }
    return ESP_OK;
}

static esp_err_t draw_script_running(void)
{
    char line[72];
    snprintf(line, sizeof(line), "%s  %u/%u",
        script_complete ? "SCRIPT COMPLETE" : "SCRIPT RUNNING",
        (unsigned)script_action_index,
        loaded_script.action_count);
    ESP_RETURN_ON_ERROR(draw_centered(56, line, script_complete ? UI_GOOD : UI_TEXT, 1), TAG,
        "script progress failed");
    snprintf(line, sizeof(line), "%.38s", loaded_script.name);
    ESP_RETURN_ON_ERROR(draw_centered(76, line, UI_MUTED, 1), TAG,
        "script running name failed");

    if (script_display_type == CERBERUS_SCRIPT_ACTION_QR && script_display_text[0] != '\0') {
        cerberus_qr_info_t info;
        esp_err_t err = cerberus_qr_render_text(
            script_display_text, 0U, 96U, CERBERUS_LCD_WIDTH, 254U, &info);
        if (err != ESP_OK) {
            snprintf(line, sizeof(line), "QR ERROR %s", esp_err_to_name(err));
            ESP_RETURN_ON_ERROR(draw_centered(190, line, UI_BAD, 1), TAG,
                "script QR error failed");
        }
    } else if (script_display_type == CERBERUS_SCRIPT_ACTION_DIALOG) {
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(18, 126, 284, 150, UI_PANEL), TAG,
            "script dialog panel failed");
        ESP_RETURN_ON_ERROR(draw_centered(146, "SCRIPT MESSAGE", UI_GOOD, 1), TAG,
            "script dialog label failed");
        ESP_RETURN_ON_ERROR(draw_script_text_lines(script_display_text, UI_TEXT, 180), TAG,
            "script dialog text failed");
    } else if (script_display_text[0] != '\0') {
        ESP_RETURN_ON_ERROR(draw_script_text_lines(script_display_text, UI_TEXT, 148), TAG,
            "script text failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(190, "STARTING VALIDATED ACTIONS", UI_MUTED, 1), TAG,
            "script starting failed");
    }
    return draw_centered(374,
        script_complete ? "DONE - BACK RETURNS TO SCRIPT LIST" : "BACK CANCELS THE SCRIPT",
        script_complete ? UI_GOOD : UI_MUTED,
        1);
}

static bool advance_script(int64_t now_ms)
{
    bool changed = false;
    while (script_running && now_ms >= script_next_action_ms) {
        if (script_action_index >= loaded_script.action_count) {
            script_running = false;
            script_complete = true;
            changed = true;
            (void)cerberus_storage_log_event(
                CERBERUS_LOG_INFO, "script", "complete", loaded_script.name);
            break;
        }
        const cerberus_script_action_t *action =
            &loaded_script.actions[script_action_index++];
        changed = true;
        if (action->type == CERBERUS_SCRIPT_ACTION_WAIT) {
            script_next_action_ms = now_ms + action->value;
            break;
        }
        if (action->type == CERBERUS_SCRIPT_ACTION_TIMER) {
            cerberus_ui_timer_start(action->value, now_ms);
            script_display_type = CERBERUS_SCRIPT_ACTION_TEXT;
            snprintf(script_display_text, sizeof(script_display_text),
                "TIMER STARTED FOR %lu SECONDS", (unsigned long)action->value);
        } else {
            script_display_type = action->type;
            snprintf(script_display_text, sizeof(script_display_text), "%s", action->text);
        }
    }
    return changed;
}

static void start_script_run(void)
{
    script_action_index = 0U;
    script_next_action_ms = esp_timer_get_time() / 1000LL;
    script_running = true;
    script_complete = false;
    script_display_type = CERBERUS_SCRIPT_ACTION_TEXT;
    script_display_text[0] = '\0';
    script_mode = UI_SCRIPT_MODE_RUNNING;
    (void)cerberus_storage_log_event(
        CERBERUS_LOG_INFO, "script", "start", loaded_script.name);
    (void)advance_script(script_next_action_ms);
}

static esp_err_t draw_scripts(void)
{
    if (script_mode == UI_SCRIPT_MODE_DETAIL) {
        return draw_script_detail();
    }
    if (script_mode == UI_SCRIPT_MODE_RUNNING) {
        return draw_script_running();
    }
    return draw_scripts_list();
}

static esp_err_t refresh_ieee802154_scan_status(void)
{
    ieee802154_scan_checked = true;
    ieee802154_scan_error =
        c5_spi_link_get_ieee802154_energy_scan_status(&ieee802154_scan_status);
    if (ieee802154_scan_error == ESP_OK &&
        ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE) {
        ieee802154_scan_error = c5_spi_link_get_ieee802154_energy_scan_result(
            ieee802154_scan_status.generation,
            &ieee802154_scan_result);
    }
    return ieee802154_scan_error;
}

static esp_err_t refresh_ieee802154_beacon_status(void)
{
    ieee802154_beacon_checked = true;
    ieee802154_beacon_result_count = 0U;
    ieee802154_beacon_error =
        c5_spi_link_get_ieee802154_beacon_scan_status(&ieee802154_beacon_status);
    if (ieee802154_beacon_error != ESP_OK ||
        ieee802154_beacon_status.state != CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE) {
        return ieee802154_beacon_error;
    }

    if (ieee802154_beacon_status.stored_count == 0U) {
        ieee802154_beacon_page_offset = 0U;
        return ESP_OK;
    }
    if (ieee802154_beacon_page_offset >= ieee802154_beacon_status.stored_count) {
        ieee802154_beacon_page_offset =
            ((ieee802154_beacon_status.stored_count - 1U) /
                UI_IEEE802154_BEACON_VISIBLE_ROWS) *
            UI_IEEE802154_BEACON_VISIBLE_ROWS;
    }

    for (size_t row = 0; row < UI_IEEE802154_BEACON_VISIBLE_ROWS; ++row) {
        size_t index = ieee802154_beacon_page_offset + row;
        if (index >= ieee802154_beacon_status.stored_count) {
            break;
        }
        ieee802154_beacon_error = c5_spi_link_get_ieee802154_beacon_scan_result(
            ieee802154_beacon_status.generation,
            (uint8_t)index,
            &ieee802154_beacon_results[row]);
        if (ieee802154_beacon_error != ESP_OK) {
            ieee802154_beacon_result_count = 0U;
            return ieee802154_beacon_error;
        }
        ++ieee802154_beacon_result_count;
    }
    return ESP_OK;
}

static esp_err_t refresh_ieee802154_activity_status(void)
{
    ieee802154_activity_checked = true;
    ieee802154_activity_error =
        c5_spi_link_get_ieee802154_activity_scan_status(&ieee802154_activity_status);
    if (ieee802154_activity_error != ESP_OK ||
        ieee802154_activity_status.state != CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE) {
        return ieee802154_activity_error;
    }

    memset(ieee802154_activity_counts, 0, sizeof(ieee802154_activity_counts));
    for (uint8_t result_index = 0;
         result_index < CERBERUS_IEEE802154_ACTIVITY_RESULT_COUNT;
         ++result_index) {
        cerberus_ieee802154_activity_scan_result_payload_t result;
        ieee802154_activity_error = c5_spi_link_get_ieee802154_activity_scan_result(
            ieee802154_activity_status.generation,
            result_index,
            &result);
        if (ieee802154_activity_error != ESP_OK) {
            return ieee802154_activity_error;
        }
        uint8_t first_index = result.first_channel - CERBERUS_IEEE802154_FIRST_CHANNEL;
        for (uint8_t channel = 0;
             channel < CERBERUS_IEEE802154_ACTIVITY_CHANNELS_PER_RESULT;
             ++channel) {
            memcpy(
                ieee802154_activity_counts[first_index + channel],
                result.counts[channel],
                CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT);
        }
    }
    return ESP_OK;
}

static uint16_t ieee802154_power_color(int8_t power)
{
    if (power <= -80) {
        return UI_GOOD;
    }
    if (power >= -60) {
        return UI_BAD;
    }
    return UI_MUTED;
}

static uint16_t ieee802154_power_bar_width(int8_t power)
{
    int16_t bounded = power;
    if (bounded < -100) {
        bounded = -100;
    } else if (bounded > -30) {
        bounded = -30;
    }
    return (uint16_t)((bounded + 100) * 58 / 70);
}

static esp_err_t draw_ieee802154_beacons(void)
{
    char line[64];
    if (!ieee802154_beacon_checked) {
        (void)refresh_ieee802154_beacon_status();
    }

    if (ieee802154_beacon_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(ieee802154_beacon_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "BEACON SCAN UNAVAILABLE", UI_BAD, 2), TAG, "beacon link failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "beacon link detail failed");
    } else if (ieee802154_beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_ERROR) {
        ESP_RETURN_ON_ERROR(draw_centered(125, "BEACON SCAN NOT STARTED", UI_BAD, 2), TAG, "beacon scan error failed");
        if (ieee802154_beacon_status.error_code == ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(draw_centered(180, "STOP WI-FI SCAN OR BLE SESSION", UI_MUTED, 1), TAG, "beacon busy failed");
        } else {
            snprintf(line, sizeof(line), "C5 ERROR %s",
                esp_err_to_name((esp_err_t)ieee802154_beacon_status.error_code));
            ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "beacon error detail failed");
        }
    } else if (ieee802154_beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_RUNNING) {
        snprintf(line, sizeof(line), "LISTENING ON CHANNEL %u",
            (unsigned)ieee802154_beacon_status.current_channel);
        ESP_RETURN_ON_ERROR(draw_centered(135, line, UI_GOOD, 2), TAG, "beacon running failed");
        snprintf(line, sizeof(line), "%u OF %u CHANNELS COMPLETE",
            (unsigned)ieee802154_beacon_status.completed_count,
            (unsigned)CERBERUS_IEEE802154_CHANNEL_COUNT);
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "beacon progress failed");
        snprintf(line, sizeof(line), "%u CLASSIC ZIGBEE BEACONS HEARD",
            (unsigned)ieee802154_beacon_status.beacon_count);
        ESP_RETURN_ON_ERROR(draw_centered(205, line, UI_MUTED, 1), TAG, "beacon count failed");
        ESP_RETURN_ON_ERROR(
            cerberus_ui_widget_draw_progress(
                (cerberus_ui_rect_t){32U, 235U, 256U, 18U},
                ieee802154_beacon_status.completed_count,
                CERBERUS_IEEE802154_CHANNEL_COUNT,
                UI_GOOD,
                UI_WIDGET_THEME),
            TAG,
            "beacon progress bar failed");
    } else if (ieee802154_beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_COMPLETE) {
        snprintf(line, sizeof(line), "%u NETWORKS / %u BEACONS",
            (unsigned)ieee802154_beacon_status.stored_count,
            (unsigned)ieee802154_beacon_status.beacon_count);
        ESP_RETURN_ON_ERROR(draw_centered(55, line, UI_GOOD, 1), TAG, "beacon summary failed");

        if (ieee802154_beacon_status.stored_count == 0U) {
            ESP_RETURN_ON_ERROR(draw_centered(145, "NO CLASSIC ZIGBEE BEACONS", UI_TEXT, 2), TAG, "beacon empty failed");
            ESP_RETURN_ON_ERROR(draw_centered(195, "BEACONLESS NETWORKS MAY NOT APPEAR", UI_MUTED, 1), TAG, "beacon empty note failed");
        } else {
            for (size_t row = 0; row < ieee802154_beacon_result_count; ++row) {
                const cerberus_ieee802154_beacon_scan_result_payload_t *result =
                    &ieee802154_beacon_results[row];
                uint16_t y = 76U + (uint16_t)row * 44U;
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(8, y, 304, 40, UI_PANEL),
                    TAG,
                    "beacon result panel failed");
                snprintf(line, sizeof(line), "PAN %04X  CH%u  %dDBM  LQI%u",
                    result->pan_id,
                    (unsigned)result->channel,
                    result->rssi,
                    (unsigned)result->lqi);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(13, y + 4U, line, UI_TEXT, UI_PANEL, 1),
                    TAG,
                    "beacon result heading failed");
                snprintf(line, sizeof(line),
                    "E %02X%02X%02X%02X%02X%02X%02X%02X  J:%c R:%c E:%c",
                    result->extended_pan_id[7], result->extended_pan_id[6],
                    result->extended_pan_id[5], result->extended_pan_id[4],
                    result->extended_pan_id[3], result->extended_pan_id[2],
                    result->extended_pan_id[1], result->extended_pan_id[0],
                    (result->flags & CERBERUS_IEEE802154_BEACON_FLAG_ASSOCIATION_PERMIT) != 0U ? 'Y' : 'N',
                    (result->flags & CERBERUS_IEEE802154_BEACON_FLAG_ROUTER_CAPACITY) != 0U ? 'Y' : 'N',
                    (result->flags & CERBERUS_IEEE802154_BEACON_FLAG_END_DEVICE_CAPACITY) != 0U ? 'Y' : 'N');
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(13, y + 21U, line, UI_MUTED, UI_PANEL, 1),
                    TAG,
                    "beacon result detail failed");
            }
        }
        ESP_RETURN_ON_ERROR(draw_centered(344, "PASSIVE METADATA ONLY - NO JOIN OR TX", UI_MUTED, 1), TAG, "beacon scope failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(135, "ZIGBEE BEACON SURVEY", UI_TEXT, 2), TAG, "beacon idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(180, "LISTENS ON CHANNELS 11-26", UI_MUTED, 1), TAG, "beacon idle detail failed");
        ESP_RETURN_ON_ERROR(draw_centered(205, "NO JOIN, TRANSMIT, OR RAW FRAME STORAGE", UI_MUTED, 1), TAG, "beacon idle scope failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, 364, 68, 42, "PREV"), TAG, "beacon prev failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 364, 68, 42, "NEXT"), TAG, "beacon next failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, 364, 68, 42, "ENERGY"), TAG, "beacon energy failed");
    return draw_small_button(
        236,
        364,
        76,
        42,
        ieee802154_beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_RUNNING
            ? "WAIT"
            : "SCAN");
}

static esp_err_t draw_ieee802154_activity(void)
{
    char line[64];
    if (!ieee802154_activity_checked) {
        (void)refresh_ieee802154_activity_status();
    }

    if (ieee802154_activity_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(ieee802154_activity_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "ACTIVITY SCAN UNAVAILABLE", UI_BAD, 2), TAG, "activity link failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "activity link detail failed");
    } else if (ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_ERROR) {
        ESP_RETURN_ON_ERROR(draw_centered(125, "ACTIVITY SCAN NOT STARTED", UI_BAD, 2), TAG, "activity scan error failed");
        if (ieee802154_activity_status.error_code == ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(draw_centered(180, "STOP WI-FI SCAN OR BLE SESSION", UI_MUTED, 1), TAG, "activity busy failed");
        } else {
            snprintf(line, sizeof(line), "C5 ERROR %s",
                esp_err_to_name((esp_err_t)ieee802154_activity_status.error_code));
            ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "activity error detail failed");
        }
    } else if (ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING) {
        snprintf(line, sizeof(line), "COUNTING ON CHANNEL %u",
            (unsigned)ieee802154_activity_status.current_channel);
        ESP_RETURN_ON_ERROR(draw_centered(135, line, UI_GOOD, 2), TAG, "activity running failed");
        snprintf(line, sizeof(line), "%u OF %u CHANNELS COMPLETE",
            (unsigned)ieee802154_activity_status.completed_count,
            (unsigned)CERBERUS_IEEE802154_CHANNEL_COUNT);
        ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "activity progress failed");
        snprintf(line, sizeof(line), "%lu VALID 802.15.4 FRAMES",
            (unsigned long)ieee802154_activity_status.total_frames);
        ESP_RETURN_ON_ERROR(draw_centered(205, line, UI_MUTED, 1), TAG, "activity total failed");
        ESP_RETURN_ON_ERROR(
            cerberus_ui_widget_draw_progress(
                (cerberus_ui_rect_t){32U, 235U, 256U, 18U},
                ieee802154_activity_status.completed_count,
                CERBERUS_IEEE802154_CHANNEL_COUNT,
                UI_GOOD,
                UI_WIDGET_THEME),
            TAG,
            "activity progress bar failed");
    } else if (ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE) {
        snprintf(line, sizeof(line), "%lu FRAMES  B/D/A/C/O COUNTS",
            (unsigned long)ieee802154_activity_status.total_frames);
        ESP_RETURN_ON_ERROR(draw_centered(55, line, UI_GOOD, 1), TAG, "activity summary failed");

        for (uint8_t row = 0; row < 8U; ++row) {
            for (uint8_t column = 0; column < 2U; ++column) {
                uint8_t index = row + column * 8U;
                uint16_t x = column == 0U ? 8U : 164U;
                uint16_t y = 78U + row * 33U;
                const uint8_t *counts = ieee802154_activity_counts[index];
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(x, y, 148, 29, UI_PANEL),
                    TAG,
                    "activity channel panel failed");
                snprintf(line, sizeof(line), "CH%u B%u D%u A%u",
                    (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + index),
                    (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_BEACON],
                    (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_DATA],
                    (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_ACK]);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(x + 4U, y + 3U, line, UI_TEXT, UI_PANEL, 1),
                    TAG,
                    "activity channel first row failed");
                snprintf(line, sizeof(line), "C%u O%u",
                    (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_MAC_COMMAND],
                    (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_OTHER]);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(x + 4U, y + 16U, line, UI_MUTED, UI_PANEL, 1),
                    TAG,
                    "activity channel second row failed");
            }
        }
        ESP_RETURN_ON_ERROR(draw_centered(344, "COUNTERS SATURATE AT 255 - NO PAYLOADS", UI_MUTED, 1), TAG, "activity scope failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(135, "802.15.4 ACTIVITY SURVEY", UI_TEXT, 2), TAG, "activity idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(180, "COUNTS FRAME TYPES ON CHANNELS 11-26", UI_MUTED, 1), TAG, "activity idle detail failed");
        ESP_RETURN_ON_ERROR(draw_centered(205, "PASSIVE ONLY - NOTHING TRANSMITTED", UI_MUTED, 1), TAG, "activity idle scope failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, 364, 68, 42, "ENERGY"), TAG, "activity energy failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 364, 68, 42, "BEACON"), TAG, "activity beacons failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, 364, 68, 42, "ASSESS"), TAG, "activity assess failed");
    return draw_small_button(
        236,
        364,
        76,
        42,
        ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING
            ? "WAIT"
            : "SCAN");
}

typedef enum {
    UI_IEEE802154_ASSESS_QUIET,
    UI_IEEE802154_ASSESS_PROTOCOL,
    UI_IEEE802154_ASSESS_ENERGY,
    UI_IEEE802154_ASSESS_INTERFERENCE,
} ui_ieee802154_assessment_t;

static uint16_t ieee802154_channel_frame_count(uint8_t channel_index)
{
    uint16_t total = 0U;
    for (uint8_t type = 0; type < CERBERUS_IEEE802154_ACTIVITY_FRAME_TYPE_COUNT; ++type) {
        total += ieee802154_activity_counts[channel_index][type];
    }
    return total;
}

static ui_ieee802154_assessment_t ieee802154_assess_channel(
    int8_t energy_dbm,
    uint16_t frame_count)
{
    if (frame_count > 0U) {
        return UI_IEEE802154_ASSESS_PROTOCOL;
    }
    if (energy_dbm >= -65) {
        return UI_IEEE802154_ASSESS_INTERFERENCE;
    }
    if (energy_dbm >= -82) {
        return UI_IEEE802154_ASSESS_ENERGY;
    }
    return UI_IEEE802154_ASSESS_QUIET;
}

static const char *ieee802154_assessment_label(ui_ieee802154_assessment_t assessment)
{
    switch (assessment) {
    case UI_IEEE802154_ASSESS_PROTOCOL:
        return "802.15.4-active";
    case UI_IEEE802154_ASSESS_ENERGY:
        return "energy-unclassified";
    case UI_IEEE802154_ASSESS_INTERFERENCE:
        return "possible-interference";
    default:
        return "quiet";
    }
}

static esp_err_t append_ieee802154_report(size_t *length, const char *format, ...)
{
    if (length == NULL || format == NULL || *length >= sizeof(ieee802154_report_buffer)) {
        return ESP_ERR_INVALID_ARG;
    }
    va_list args;
    va_start(args, format);
    int written = vsnprintf(
        ieee802154_report_buffer + *length,
        sizeof(ieee802154_report_buffer) - *length,
        format,
        args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(ieee802154_report_buffer) - *length) {
        return ESP_ERR_INVALID_SIZE;
    }
    *length += (size_t)written;
    return ESP_OK;
}

static esp_err_t export_ieee802154_report(void)
{
    bool energy_ready = ieee802154_scan_error == ESP_OK &&
        ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE;
    bool activity_ready = ieee802154_activity_error == ESP_OK &&
        ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE;
    if (!energy_ready || !activity_ready) {
        snprintf(ieee802154_export_message, sizeof(ieee802154_export_message),
            "RUN ENERGY AND ACTIVITY FIRST");
        ieee802154_export_color = UI_BAD;
        return ESP_ERR_INVALID_STATE;
    }

    size_t length = 0U;
    ieee802154_report_buffer[0] = '\0';
    esp_err_t err = append_ieee802154_report(
        &length,
        "# Cerberus 802.15.4 diagnostic report\r\n"
        "# format_version=1\r\n"
        "# s3_uptime_ms=%llu\r\n"
        "# c5_uptime_ms=%lu\r\n"
        "# c5_firmware=%lu\r\n"
        "# c5_boot_count=%u\r\n"
        "# energy_generation=%u\r\n"
        "# activity_generation=%u\r\n"
        "# note=passive separate snapshots; possible-interference is heuristic, not proof\r\n"
        "channel,energy_dbm,total_frames,beacon,data,ack,mac_command,other,assessment\r\n",
        (unsigned long long)(esp_timer_get_time() / 1000),
        (unsigned long)c5_status.uptime_ms,
        (unsigned long)c5_status.firmware_version,
        (unsigned)c5_status.boot_count,
        (unsigned)ieee802154_scan_status.generation,
        (unsigned)ieee802154_activity_status.generation);
    for (uint8_t index = 0;
         err == ESP_OK && index < CERBERUS_IEEE802154_CHANNEL_COUNT;
         ++index) {
        const uint8_t *counts = ieee802154_activity_counts[index];
        uint16_t total = ieee802154_channel_frame_count(index);
        ui_ieee802154_assessment_t assessment = ieee802154_assess_channel(
            ieee802154_scan_result.power_dbm[index], total);
        err = append_ieee802154_report(
            &length,
            "%u,%d,%u,%u,%u,%u,%u,%u,%s\r\n",
            (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + index),
            ieee802154_scan_result.power_dbm[index],
            (unsigned)total,
            (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_BEACON],
            (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_DATA],
            (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_ACK],
            (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_MAC_COMMAND],
            (unsigned)counts[CERBERUS_IEEE802154_ACTIVITY_FRAME_OTHER],
            ieee802154_assessment_label(assessment));
    }
    if (err == ESP_OK) {
        err = cerberus_storage_write_diagnostic_report(
            ieee802154_report_buffer,
            length,
            ieee802154_report_file_name,
            sizeof(ieee802154_report_file_name));
    }
    if (err == ESP_OK) {
        storage_safely_removed = false;
        storage_checked = false;
        snprintf(ieee802154_export_message, sizeof(ieee802154_export_message),
            "SAVED /CERBERUS/LOGS/%.12s", ieee802154_report_file_name);
        ieee802154_export_color = UI_GOOD;
    } else {
        snprintf(ieee802154_export_message, sizeof(ieee802154_export_message),
            "EXPORT FAILED %s", esp_err_to_name(err));
        ieee802154_export_color = UI_BAD;
    }
    return err;
}

static esp_err_t draw_ieee802154_assessment(void)
{
    char line[64];
    bool energy_ready = ieee802154_scan_error == ESP_OK &&
        ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE;
    bool activity_ready = ieee802154_activity_error == ESP_OK &&
        ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_COMPLETE;

    if (!energy_ready || !activity_ready) {
        ESP_RETURN_ON_ERROR(draw_centered(120, "CHANNEL ASSESSMENT NEEDS", UI_TEXT, 2), TAG, "assessment prerequisite failed");
        ESP_RETURN_ON_ERROR(draw_centered(150, "BOTH SURVEYS", UI_TEXT, 2), TAG, "assessment prerequisite detail failed");
        snprintf(line, sizeof(line), "ENERGY SURVEY: %s", energy_ready ? "READY" : "RUN REQUIRED");
        ESP_RETURN_ON_ERROR(draw_centered(205, line, energy_ready ? UI_GOOD : UI_BAD, 1), TAG, "assessment energy state failed");
        snprintf(line, sizeof(line), "ACTIVITY SURVEY: %s", activity_ready ? "READY" : "RUN REQUIRED");
        ESP_RETURN_ON_ERROR(draw_centered(230, line, activity_ready ? UI_GOOD : UI_BAD, 1), TAG, "assessment activity state failed");
        ESP_RETURN_ON_ERROR(draw_centered(280, "SURVEYS ARE PASSIVE AND RUN SEPARATELY", UI_MUTED, 1), TAG, "assessment prerequisite note failed");
        if (ieee802154_export_message[0] != '\0') {
            ESP_RETURN_ON_ERROR(
                draw_centered(320, ieee802154_export_message, ieee802154_export_color, 1),
                TAG,
                "assessment prerequisite export message failed");
        }
    } else {
        uint8_t quiet_count = 0U;
        uint8_t protocol_count = 0U;
        uint8_t interference_count = 0U;
        for (uint8_t index = 0; index < CERBERUS_IEEE802154_CHANNEL_COUNT; ++index) {
            ui_ieee802154_assessment_t assessment = ieee802154_assess_channel(
                ieee802154_scan_result.power_dbm[index],
                ieee802154_channel_frame_count(index));
            if (assessment == UI_IEEE802154_ASSESS_QUIET) {
                ++quiet_count;
            } else if (assessment == UI_IEEE802154_ASSESS_PROTOCOL) {
                ++protocol_count;
            } else if (assessment == UI_IEEE802154_ASSESS_INTERFERENCE) {
                ++interference_count;
            }
        }
        snprintf(line, sizeof(line), "QUIET %u  15.4 %u  INTERF? %u",
            (unsigned)quiet_count,
            (unsigned)protocol_count,
            (unsigned)interference_count);
        ESP_RETURN_ON_ERROR(draw_centered(55, line, UI_GOOD, 1), TAG, "assessment summary failed");

        for (uint8_t row = 0; row < 8U; ++row) {
            for (uint8_t column = 0; column < 2U; ++column) {
                uint8_t index = row + column * 8U;
                uint16_t x = column == 0U ? 8U : 164U;
                uint16_t y = 78U + row * 33U;
                int8_t energy_dbm = ieee802154_scan_result.power_dbm[index];
                uint16_t frame_count = ieee802154_channel_frame_count(index);
                ui_ieee802154_assessment_t assessment =
                    ieee802154_assess_channel(energy_dbm, frame_count);
                const char *label;
                uint16_t color;
                if (assessment == UI_IEEE802154_ASSESS_PROTOCOL) {
                    label = "15.4";
                    color = UI_GOOD;
                } else if (assessment == UI_IEEE802154_ASSESS_INTERFERENCE) {
                    label = "INTERF?";
                    color = UI_BAD;
                } else if (assessment == UI_IEEE802154_ASSESS_ENERGY) {
                    label = "ENERGY";
                    color = UI_MUTED;
                } else {
                    label = "QUIET";
                    color = UI_GOOD;
                }
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(x, y, 148, 29, UI_PANEL),
                    TAG,
                    "assessment channel panel failed");
                if (assessment == UI_IEEE802154_ASSESS_PROTOCOL) {
                    snprintf(line, sizeof(line), "CH%u %d %s:%u",
                        (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + index),
                        energy_dbm,
                        label,
                        (unsigned)frame_count);
                } else {
                    snprintf(line, sizeof(line), "CH%u %d %s",
                        (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + index),
                        energy_dbm,
                        label);
                }
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(x + 4U, y + 10U, line, color, UI_PANEL, 1),
                    TAG,
                    "assessment channel label failed");
            }
        }
        ESP_RETURN_ON_ERROR(
            draw_centered(
                344,
                ieee802154_export_message[0] != '\0'
                    ? ieee802154_export_message
                    : "HEURISTIC SNAPSHOTS - INTERF? IS NOT PROOF",
                ieee802154_export_message[0] != '\0' ? ieee802154_export_color : UI_MUTED,
                1),
            TAG,
            "assessment scope failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, 364, 68, 42, "ENERGY"), TAG, "assessment energy failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 364, 68, 42, "ACTIVITY"), TAG, "assessment activity failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, 364, 68, 42, "BEACON"), TAG, "assessment beacons failed");
    return draw_small_button(236, 364, 76, 42, "EXPORT");
}

static esp_err_t draw_ieee802154_feature_menu(void)
{
    return sentinel_hub_draw(SENTINEL_HUB_RADIO, UI_WIDGET_THEME);
}

static esp_err_t draw_ieee802154(void)
{
    if (live_is_view()) return draw_live_scan();
    char line[64];
    if (!c5_online) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "C5 OFFLINE", UI_BAD, 2), TAG, "802.15.4 offline failed");
        return draw_centered(205, "CHECK ONBOARD SPI LINK", UI_MUTED, 1);
    }
    if (c5_status.protocol_version != CERBERUS_PROTOCOL_VERSION ||
        c5_status.firmware_version < CERBERUS_C5_FIRMWARE_VERSION) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "C5 UPDATE REQUIRED", UI_BAD, 2), TAG, "802.15.4 version failed");
        snprintf(line, sizeof(line), "C5 FIRMWARE %u REQUIRED",
            (unsigned)CERBERUS_C5_FIRMWARE_VERSION);
        return draw_centered(205, line, UI_MUTED, 1);
    }
    if ((c5_status.capabilities & CERBERUS_C5_CAP_IEEE802154) == 0U) {
        ESP_RETURN_ON_ERROR(draw_centered(145, "RADIO UNAVAILABLE", UI_BAD, 2), TAG, "802.15.4 capability failed");
        return draw_centered(205, "C5 DOES NOT REPORT 802.15.4", UI_MUTED, 1);
    }

    if (ieee802154_mode == UI_IEEE802154_MODE_MENU) {
        return draw_ieee802154_feature_menu();
    }

    if (ieee802154_mode == UI_IEEE802154_MODE_BEACONS) {
        return draw_ieee802154_beacons();
    }
    if (ieee802154_mode == UI_IEEE802154_MODE_ACTIVITY) {
        return draw_ieee802154_activity();
    }
    if (ieee802154_mode == UI_IEEE802154_MODE_ASSESSMENT) {
        return draw_ieee802154_assessment();
    }

    if (!ieee802154_scan_checked) {
        (void)refresh_ieee802154_scan_status();
    }
    if (ieee802154_scan_error != ESP_OK) {
        snprintf(line, sizeof(line), "LINK ERROR %s", esp_err_to_name(ieee802154_scan_error));
        ESP_RETURN_ON_ERROR(draw_centered(145, "ENERGY SCAN UNAVAILABLE", UI_BAD, 2), TAG, "802.15.4 link failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, line, UI_MUTED, 1), TAG, "802.15.4 link detail failed");
    } else if (ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_ERROR) {
        ESP_RETURN_ON_ERROR(draw_centered(125, "ENERGY SCAN NOT STARTED", UI_BAD, 2), TAG, "802.15.4 scan error failed");
        if (ieee802154_scan_status.error_code == ESP_ERR_INVALID_STATE) {
            ESP_RETURN_ON_ERROR(draw_centered(180, "STOP WI-FI SCAN OR BLE SESSION", UI_MUTED, 1), TAG, "802.15.4 busy failed");
        } else {
            snprintf(line, sizeof(line), "C5 ERROR %s",
                esp_err_to_name((esp_err_t)ieee802154_scan_status.error_code));
            ESP_RETURN_ON_ERROR(draw_centered(180, line, UI_MUTED, 1), TAG, "802.15.4 error detail failed");
        }
    } else if (ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_RUNNING) {
        snprintf(line, sizeof(line), "MEASURING CHANNEL %u",
            (unsigned)ieee802154_scan_status.current_channel);
        ESP_RETURN_ON_ERROR(draw_centered(145, line, UI_GOOD, 2), TAG, "802.15.4 running failed");
        snprintf(line, sizeof(line), "%u OF %u CHANNELS COMPLETE",
            (unsigned)ieee802154_scan_status.completed_count,
            (unsigned)CERBERUS_IEEE802154_CHANNEL_COUNT);
        ESP_RETURN_ON_ERROR(draw_centered(190, line, UI_MUTED, 1), TAG, "802.15.4 progress failed");
        ESP_RETURN_ON_ERROR(
            cerberus_ui_widget_draw_progress(
                (cerberus_ui_rect_t){32U, 225U, 256U, 18U},
                ieee802154_scan_status.completed_count,
                CERBERUS_IEEE802154_CHANNEL_COUNT,
                UI_GOOD,
                UI_WIDGET_THEME),
            TAG,
            "802.15.4 progress bar failed");
    } else if (ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_COMPLETE) {
        uint8_t quietest_index = 0;
        uint8_t peak_index = 0;
        for (uint8_t index = 1; index < CERBERUS_IEEE802154_CHANNEL_COUNT; ++index) {
            if (ieee802154_scan_result.power_dbm[index] <
                ieee802154_scan_result.power_dbm[quietest_index]) {
                quietest_index = index;
            }
            if (ieee802154_scan_result.power_dbm[index] >
                ieee802154_scan_result.power_dbm[peak_index]) {
                peak_index = index;
            }
        }
        snprintf(line, sizeof(line), "QUIET CH%u %d  PEAK CH%u %d DBM",
            (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + quietest_index),
            ieee802154_scan_result.power_dbm[quietest_index],
            (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + peak_index),
            ieee802154_scan_result.power_dbm[peak_index]);
        ESP_RETURN_ON_ERROR(draw_centered(56, line, UI_GOOD, 1), TAG, "802.15.4 summary failed");

        for (uint8_t row = 0; row < 8U; ++row) {
            for (uint8_t column = 0; column < 2U; ++column) {
                uint8_t index = row + column * 8U;
                uint16_t x = column == 0U ? 8U : 164U;
                uint16_t y = 78U + row * 33U;
                int8_t power = ieee802154_scan_result.power_dbm[index];
                ESP_RETURN_ON_ERROR(
                    cerberus_display_fill_rect(x, y, 148, 29, UI_PANEL),
                    TAG,
                    "802.15.4 channel panel failed");
                snprintf(line, sizeof(line), "CH%u %d",
                    (unsigned)(CERBERUS_IEEE802154_FIRST_CHANNEL + index), power);
                ESP_RETURN_ON_ERROR(
                    cerberus_display_draw_text(x + 5U, y + 5U, line, UI_TEXT, UI_PANEL, 1),
                    TAG,
                    "802.15.4 channel label failed");
                uint16_t bar_width = ieee802154_power_bar_width(power);
                if (bar_width > 0U) {
                    ESP_RETURN_ON_ERROR(
                        cerberus_display_fill_rect(
                            x + 84U, y + 18U, bar_width, 7U,
                            ieee802154_power_color(power)),
                        TAG,
                        "802.15.4 channel bar failed");
                }
            }
        }
        ESP_RETURN_ON_ERROR(draw_centered(344, "ENERGY ONLY - NO FRAME DECODING", UI_MUTED, 1), TAG, "802.15.4 scope failed");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(145, "CHANNEL ENERGY SURVEY", UI_TEXT, 2), TAG, "802.15.4 idle failed");
        ESP_RETURN_ON_ERROR(draw_centered(195, "PASSIVE MEASUREMENT ON CHANNELS 11-26", UI_MUTED, 1), TAG, "802.15.4 idle detail failed");
    }

    ESP_RETURN_ON_ERROR(draw_small_button(8, 364, 68, 42, "SCAN"), TAG, "energy scan button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(84, 364, 68, 42, "BEACON"), TAG, "energy beacons button failed");
    ESP_RETURN_ON_ERROR(draw_small_button(160, 364, 68, 42, "ACTIVITY"), TAG, "energy activity button failed");
    return draw_small_button(236, 364, 76, 42, "ASSESS");
}

static esp_err_t draw_usb(void)
{
    bool driver_ready = usb_serial_jtag_is_driver_installed();
    bool host_connected = usb_serial_jtag_is_connected();
    bool recovery_available = cerberus_recovery_download_mode_available();

    if (usb_mode == UI_USB_MODE_RECOVERY_CONFIRM) {
        ESP_RETURN_ON_ERROR(
            draw_centered(76, "ENTER S3 DOWNLOAD MODE?", UI_BAD, 2),
            TAG,
            "USB recovery title failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(132, "CERBERUS WILL RESTART", UI_TEXT, 1),
            TAG,
            "USB recovery restart failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(164, "USB CONSOLE WILL DISCONNECT", UI_TEXT, 1),
            TAG,
            "USB recovery disconnect failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(210, "ROM LOADER ACCEPTS STANDARD FLASH TOOLS", UI_MUTED, 1),
            TAG,
            "USB recovery loader failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(240, "NO FLASH DATA IS WRITTEN BY THIS ACTION", UI_GOOD, 1),
            TAG,
            "USB recovery write boundary failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(
                286,
                recovery_available ? "DOWNLOAD PATH AVAILABLE" : "BLOCKED BY EFUSE POLICY",
                recovery_available ? UI_GOOD : UI_BAD,
                1),
            TAG,
            "USB recovery availability failed");
        if (usb_recovery_error != ESP_OK) {
            char error_line[64];
            snprintf(
                error_line,
                sizeof(error_line),
                "PREPARATION FAILED %s",
                esp_err_to_name(usb_recovery_error));
            ESP_RETURN_ON_ERROR(
                draw_centered(320, error_line, UI_BAD, 1),
                TAG,
                "USB recovery error failed");
        }
        if (!recovery_available) {
            return ESP_OK;
        }
        return draw_button(38, 350, 244, 58, "ENTER DOWNLOAD MODE");
    }

    ESP_RETURN_ON_ERROR(
        draw_centered(76, "READ-ONLY SERIAL CONSOLE", UI_TEXT, 2),
        TAG,
        "USB console title failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(
            116,
            driver_ready ? "NATIVE USB SERIAL/JTAG READY" : "USB CONSOLE UNAVAILABLE",
            driver_ready ? UI_GOOD : UI_BAD,
            1),
        TAG,
        "USB console state failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(
            144,
            host_connected ? "HOST CONNECTION DETECTED" : "WAITING FOR USB HOST",
            host_connected ? UI_GOOD : UI_MUTED,
            1),
        TAG,
        "USB host state failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(190, "COMMANDS", UI_MUTED, 2),
        TAG,
        "USB command header failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(226, "HELP   INFO   HEALTH", UI_TEXT, 1),
        TAG,
        "USB command row one failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(252, "C5   STORAGE   BATTERY", UI_TEXT, 1),
        TAG,
        "USB command row two failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(292, "NO SHELL, HID, OR RADIO CONTROL", UI_MUTED, 1),
        TAG,
        "USB console boundary one failed");
    ESP_RETURN_ON_ERROR(
        draw_centered(318, "NO REMOTE FIRMWARE WRITES", UI_MUTED, 1),
        TAG,
        "USB console boundary two failed");
    return draw_button(38, 350, 244, 58, "S3 RECOVERY MODE");
}

static const char *wardrive_state_label(cerberus_wardrive_state_t state)
{
    switch (state) {
    case CERBERUS_WARDRIVE_STARTING: return "STARTING";
    case CERBERUS_WARDRIVE_RUNNING: return "RUNNING";
    case CERBERUS_WARDRIVE_STOPPING: return "STOPPING / SAVING";
    case CERBERUS_WARDRIVE_STOPPED: return "STOPPED";
    case CERBERUS_WARDRIVE_ERROR: return "ERROR";
    default: return "IDLE";
    }
}

static const char *ble_attack_mode_label(cerberus_ble_attack_mode_t mode)
{
    switch (mode) {
    case CERBERUS_BLE_ATTACK_SOUR_APPLE: return "SOUR APPLE";
    case CERBERUS_BLE_ATTACK_APPLE_JUICE: return "APPLE JUICE";
    case CERBERUS_BLE_ATTACK_SWIFT_PAIR: return "SWIFT PAIR";
    case CERBERUS_BLE_ATTACK_SAMSUNG: return "SAMSUNG BLE";
    case CERBERUS_BLE_ATTACK_GOOGLE: return "GOOGLE FAST PAIR";
    case CERBERUS_BLE_ATTACK_FLIPPER: return "FLIPPER BLE";
    case CERBERUS_BLE_ATTACK_ALL: return "BLE SPAM ALL";
    case CERBERUS_BLE_ATTACK_SPOOF_AIRTAG: return "SPOOF AIRTAG";
    default: return "UNAVAILABLE MODE";
    }
}

static const char *ble_attack_state_label(cerberus_s3_ble_attack_state_t state)
{
    switch (state) {
    case CERBERUS_S3_BLE_ATTACK_STARTING: return "STARTING";
    case CERBERUS_S3_BLE_ATTACK_RUNNING: return "RUNNING";
    case CERBERUS_S3_BLE_ATTACK_STOPPING: return "STOPPING";
    case CERBERUS_S3_BLE_ATTACK_STOPPED: return "STOPPED";
    case CERBERUS_S3_BLE_ATTACK_LEASE_EXPIRED: return "PAGE LEASE EXPIRED";
    case CERBERUS_S3_BLE_ATTACK_GATT_REQUIRED: return "GATT REQUIRED";
    case CERBERUS_S3_BLE_ATTACK_ERROR: return "ERROR";
    default: return "IDLE";
    }
}

static esp_err_t draw_ble_attack(void)
{
    cerberus_s3_ble_attack_status_t status;
    cerberus_s3_ble_attack_get_status(&status);
    if (status.job_id != ble_attack_job_id || ble_attack_job_id == 0U)
        memset(&status, 0, sizeof(status));
    bool active = status.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
        status.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
        status.state == CERBERUS_S3_BLE_ATTACK_STOPPING;
    char line[64];
    ESP_RETURN_ON_ERROR(draw_centered(60, "S3 BLE ANNOUNCEMENTS / MANUAL STOP", UI_MUTED, 1), TAG, "BLE attack mode");
    ESP_RETURN_ON_ERROR(draw_centered(82, ble_attack_mode_label(ble_attack_mode), UI_TEXT, 2), TAG, "BLE attack title");
    snprintf(line, sizeof(line), "STATE: %s", ble_attack_start_pending ?
        "WAITING FOR S3 RADIO" : ble_attack_state_label(status.state));
    ESP_RETURN_ON_ERROR(draw_centered(118, line,
        status.state == CERBERUS_S3_BLE_ATTACK_ERROR ? UI_BAD :
        active ? UI_GOOD : UI_TEXT, 1), TAG, "BLE attack state");
    snprintf(line, sizeof(line), "CONTROLLER ACCEPTED BURSTS: %lu",
        (unsigned long)status.controller_accepted_bursts);
    ESP_RETURN_ON_ERROR(draw_centered(148, line, UI_GOOD, 1), TAG, "BLE attack accepted");
    snprintf(line, sizeof(line), "REJECTED %lu / COMPLETED %lu / PLANNED %lu",
        (unsigned long)status.controller_rejected_bursts,
        (unsigned long)status.completed_bursts,
        (unsigned long)status.planned_bursts);
    ESP_RETURN_ON_ERROR(draw_centered(172, line, UI_MUTED, 1), TAG, "BLE attack burst counts");
    snprintf(line, sizeof(line), "INTERVAL %u x 0.625MS / HEAP FLOOR %u KiB",
        (unsigned)status.effective_interval_units,
        (unsigned)status.minimum_free_heap_kib);
    ESP_RETURN_ON_ERROR(draw_centered(196, line, UI_MUTED, 1), TAG, "BLE attack controller");
    ESP_RETURN_ON_ERROR(draw_centered(220, "ON-AIR PACKET COUNT UNAVAILABLE", UI_MUTED, 1), TAG, "BLE attack count caveat");
    if (ble_attack_mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG) {
        ESP_RETURN_ON_ERROR(draw_centered(249,
            ble_attack_capture_valid ? "SELECTED STATIC-RANDOM CAPTURE" :
            "SELECT AN OBSERVED BLE DEVICE", ble_attack_capture_valid ? UI_GOOD : UI_MUTED, 1),
            TAG, "BLE attack capture");
    } else if (ble_attack_mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ||
               ble_attack_mode == CERBERUS_BLE_ATTACK_ALL) {
        snprintf(line, sizeof(line), "BLE LINKS: %lu OPENED / %lu CLOSED%s",
            (unsigned long)status.connections_accepted,
            (unsigned long)status.connections_closed,
            status.connection_active ? " / ACTIVE" : "");
        ESP_RETURN_ON_ERROR(draw_centered(249, line, UI_MUTED, 1),
            TAG, "BLE attack connection status");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(249, "NO DEVICE SELECTION REQUIRED", UI_MUTED, 1),
            TAG, "BLE attack selection");
    }
    if (ble_attack_capture_message[0])
        ESP_RETURN_ON_ERROR(draw_centered(278, ble_attack_capture_message, UI_BAD, 1),
            TAG, "BLE attack capture reason");
    else if ((ble_attack_mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ||
              ble_attack_mode == CERBERUS_BLE_ATTACK_ALL) &&
             status.connection_terminate_errors) {
        snprintf(line, sizeof(line), "BLE LINK CLOSE ERRORS: %lu",
            (unsigned long)status.connection_terminate_errors);
        ESP_RETURN_ON_ERROR(draw_centered(278, line, UI_BAD, 1),
            TAG, "BLE attack link close errors");
    }
    esp_err_t error = ble_attack_start_error != ESP_OK ? ble_attack_start_error : status.error;
    if (error != ESP_OK) {
        snprintf(line, sizeof(line), "S3 ERROR: %s / RC %ld", esp_err_to_name(error),
            (long)status.last_controller_rc);
        ESP_RETURN_ON_ERROR(draw_centered(306, line, UI_BAD, 1), TAG, "BLE attack error");
    } else if (ble_attack_return_pending) {
        ESP_RETURN_ON_ERROR(draw_centered(306, "RELEASING S3 BLE BEFORE EXIT", UI_GOOD, 1),
            TAG, "BLE attack exit");
    }
    ESP_RETURN_ON_ERROR(draw_centered(328,
        cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_ADVERTISER &&
            status.state == CERBERUS_S3_BLE_ATTACK_ERROR ?
            "BLE HOST HELD / RESET REQUIRED" :
            "VERIFY RF WITH AN INDEPENDENT RECEIVER", UI_MUTED, 1),
        TAG, "BLE attack verification");
    if (ble_attack_mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG)
        ESP_RETURN_ON_ERROR(draw_small_button(24, 346, 272, 28,
            ble_attack_capture_valid ? "CHANGE BLE DEVICE" : "SELECT BLE DEVICE"),
            TAG, "BLE attack select");
    return draw_button(40, 380, 240, 32,
        active || ble_attack_start_pending ? "STOP" : "START");
}

static const char *ble_sound_profile_name(cerberus_findmy_profile_t profile)
{
    switch (profile) {
    case CERBERUS_FINDMY_PROFILE_AIRTAG: return "APPLE FIND MY HINT";
    case CERBERUS_FINDMY_PROFILE_FMNA: return "FMNA SERVICE HINT";
    case CERBERUS_FINDMY_PROFILE_DULT: return "DULT SERVICE HINT";
    default: return "NO SOUND PROFILE";
    }
}

static const char *ble_sound_stage_name(cerberus_s3_ble_sound_state_t state)
{
    switch (state) {
    case CERBERUS_S3_BLE_SOUND_CONNECTING: return "CONNECTING";
    case CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE: return "FINDING SERVICE";
    case CERBERUS_S3_BLE_SOUND_DISCOVERING_CHARACTERISTIC: return "FINDING CHARACTERISTIC";
    case CERBERUS_S3_BLE_SOUND_DISCOVERING_CCCD: return "FINDING NOTIFICATION";
    case CERBERUS_S3_BLE_SOUND_SUBSCRIBING: return "SUBSCRIBING";
    case CERBERUS_S3_BLE_SOUND_WRITING: return "WRITING COMMAND";
    case CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE: return "WAITING FOR TAG RESPONSE";
    case CERBERUS_S3_BLE_SOUND_STOPPING: return "DISCONNECTING";
    case CERBERUS_S3_BLE_SOUND_COMPLETE: return "COMMAND COMPLETE";
    case CERBERUS_S3_BLE_SOUND_STOPPED: return "STOPPED";
    case CERBERUS_S3_BLE_SOUND_ERROR: return "ERROR";
    default: return "READY";
    }
}

static esp_err_t draw_ble_sound(void)
{
    static const uint8_t infinity_bitmap[16] = {
        0x00, 0x00, 0x38, 0x70, 0x44, 0x88, 0x83, 0x04,
        0x83, 0x04, 0x44, 0x88, 0x38, 0x70, 0x00, 0x00,
    };
    char line[64];
    const char *state = ble_sound_stop_pending ? "STOPPING / RELEASING HOST" :
        ble_sound_attempt_owned ? ble_sound_stage_name(ble_sound_status.state) :
        ble_sound_running ? (ble_sound_scan_owned ? "8S BLE SCAN" :
            "WAITING FOR S3 RADIO") :
        ble_sound_error != ESP_OK ? "ERROR / READY TO RETRY" :
        ble_sound_selected_started ? ble_sound_stage_name(ble_sound_status.state) :
            "READY";
    ESP_RETURN_ON_ERROR(draw_centered(58,
        ble_sound_all ? "BEEP ALL / ENDLESS UNTIL STOP" :
            "ONE SELECTED BLE ADDRESS", UI_MUTED, 1), TAG, "sound scope");
    ESP_RETURN_ON_ERROR(draw_centered(83,
        ble_sound_all ? "FIND MY BEEP ALL" : "FIND MY SOUND",
        UI_TEXT, 2), TAG, "sound title");
    if (ble_sound_all) ESP_RETURN_ON_ERROR(
        cerberus_display_draw_mono_bitmap(264, 84, 16, 8,
            infinity_bitmap, UI_GOOD, UI_BACKGROUND), TAG, "sound infinity");
    snprintf(line, sizeof(line), "STATE: %s", state);
    ESP_RETURN_ON_ERROR(draw_centered(122, line,
        ble_sound_error != ESP_OK ? UI_BAD : ble_sound_running ? UI_GOOD : UI_TEXT,
        1), TAG, "sound state");
    if (ble_sound_all) {
        snprintf(line, sizeof(line), "S3 SWEEPS %lu / CANDIDATES %u OF 32",
            (unsigned long)ble_sound_sweeps,
            ble_sound_queue ? (unsigned)ble_sound_queue->count : 0U);
        ESP_RETURN_ON_ERROR(draw_centered(152, line, UI_MUTED, 1), TAG, "sound candidates");
        snprintf(line, sizeof(line), "ATTEMPTS %lu / ERRORS %lu",
            (unsigned long)ble_sound_attempts, (unsigned long)ble_sound_errors);
        ESP_RETURN_ON_ERROR(draw_centered(177, line, UI_TEXT, 1), TAG, "sound attempts");
        snprintf(line, sizeof(line), "ATT WRITE ACCEPTED %lu",
            (unsigned long)ble_sound_accepted);
        ESP_RETURN_ON_ERROR(draw_centered(202, line, UI_GOOD, 1), TAG, "sound writes");
        snprintf(line, sizeof(line), "TAG RESPONSES %lu / DULT REPORTS %lu",
            (unsigned long)ble_sound_responses,
            (unsigned long)ble_sound_reported);
        ESP_RETURN_ON_ERROR(draw_centered(227, line, UI_MUTED, 1), TAG, "sound responses");
        ESP_RETURN_ON_ERROR(draw_centered(252,
            "EXACT ADDRESSES ONLY / RPA MAY ROTATE", UI_MUTED, 1),
            TAG, "sound identity caveat");
    } else {
        ESP_RETURN_ON_ERROR(draw_centered(152,
            ble_sound_profile_name(ble_sound_profile), UI_GOOD, 1),
            TAG, "selected sound profile");
        snprintf(line, sizeof(line), "ATT WRITE ACCEPTED: %s",
            ble_sound_status.write_accepted ? "YES" : "NO");
        ESP_RETURN_ON_ERROR(draw_centered(182, line,
            ble_sound_status.write_accepted ? UI_GOOD : UI_MUTED, 1),
            TAG, "selected sound write");
        snprintf(line, sizeof(line), "TAG RESPONSE: %s",
            ble_sound_status.response_received ? "RECEIVED" : "NONE");
        ESP_RETURN_ON_ERROR(draw_centered(207, line,
            ble_sound_status.response_received ? UI_GOOD : UI_MUTED, 1),
            TAG, "selected sound response");
        snprintf(line, sizeof(line), "DULT COMPLETION REPORT: %s",
            ble_sound_status.completion_reported ? "RECEIVED" : "NONE");
        ESP_RETURN_ON_ERROR(draw_centered(232, line, UI_MUTED, 1),
            TAG, "selected sound completion");
    }
    if (ble_sound_error != ESP_OK ||
        ble_sound_status.state == CERBERUS_S3_BLE_SOUND_ERROR) {
        esp_err_t error = ble_sound_error != ESP_OK ? ble_sound_error :
            (esp_err_t)ble_sound_status.error_code;
        snprintf(line, sizeof(line), "ERROR %s / STAGE %s / RC %ld",
            esp_err_to_name(error),
            ble_sound_stage_name(ble_sound_status.failed_stage),
            (long)ble_sound_status.last_host_rc);
        ESP_RETURN_ON_ERROR(draw_centered(282, line, UI_BAD, 1),
            TAG, "sound failure detail");
        if (ble_sound_error == ESP_ERR_INVALID_STATE &&
            cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER)
            ESP_RETURN_ON_ERROR(draw_centered(297,
                "STOP S3 WI-FI RECEIVER, THEN RETRY", UI_BAD, 1),
                TAG, "sound Wi-Fi owner guidance");
    } else if (ble_sound_attempt_owned) {
        ESP_RETURN_ON_ERROR(draw_centered(282,
            ble_sound_profile_name(ble_sound_status.profile), UI_MUTED, 1),
            TAG, "sound active profile");
    }
    ESP_RETURN_ON_ERROR(draw_centered(314,
        "COMMAND ACK IS NOT PROOF OF AUDIBLE SOUND", UI_MUTED, 1),
        TAG, "sound proof caveat");
    ESP_RETURN_ON_ERROR(draw_centered(342,
        "S3 BLE SCAN + GATT / C5 WI-FI INDEPENDENT", UI_MUTED, 1),
        TAG, "sound radio roles");
    return draw_button(40, 380, 240, 32,
        ble_sound_stop_pending ? "STOPPING" :
        (ble_sound_running || ble_sound_scan_owned || ble_sound_attempt_owned) ? "STOP" :
        ble_sound_all ? "START ENDLESS" :
        ble_sound_selected_started ? "BEEP AGAIN" : "BEEP SELECTED");
}

static esp_err_t draw_wardrive(void)
{
    cerberus_wardrive_status_t status;
    cerberus_wardrive_get_status(&status);
    char line[64];
    bool active = status.state == CERBERUS_WARDRIVE_STARTING ||
        status.state == CERBERUS_WARDRIVE_RUNNING ||
        status.state == CERBERUS_WARDRIVE_STOPPING;
    uint8_t expansion_count = 0U;
    for (uint8_t mask = status.expansion_present_mask; mask != 0U; mask >>= 1U)
        expansion_count += mask & 1U;

    ESP_RETURN_ON_ERROR(draw_centered(60, "CONTINUOUS SCAN / STOP TO SAVE", UI_MUTED, 1), TAG, "wardrive mode");
    bool gps_selected = active ? status.gps_requested : wardrive_gps_enabled;
    const char *gps_line = gps_selected ? "GPS ON / ABSENT / UNLOC CSV" :
        "GPS OFF / UNLOC CSV";
    uint16_t gps_color = UI_MUTED;
    if (gps_selected && status.gps_error != ESP_OK) {
        gps_line = "GPS UART ERROR / UNLOC CSV";
        gps_color = UI_BAD;
    } else if (gps_selected && status.gps.state == CERBERUS_WARDRIVE_GPS_NO_FIX) {
        gps_line = "GPS SIGNAL / NO FIX";
    } else if (gps_selected && status.gps.state == CERBERUS_WARDRIVE_GPS_STALE) {
        gps_line = "GPS FIX STALE / UNLOC CSV";
    } else if (gps_selected && status.gps.state == CERBERUS_WARDRIVE_GPS_FRESH) {
        gps_line = status.gps.utc_valid ?
            "GPS FIX + UTC / WIGLE CSV" : "GPS FIX / UTC UNVERIFIED";
        gps_color = status.gps.utc_valid ? UI_GOOD : UI_MUTED;
    }
    ESP_RETURN_ON_ERROR(draw_centered(80, gps_line, gps_color, 1), TAG, "wardrive GPS");
    snprintf(line, sizeof(line), "STATE: %s", wardrive_start_pending ?
        "WAITING FOR RADIOS" : wardrive_state_label(status.state));
    ESP_RETURN_ON_ERROR(draw_centered(105, line,
        status.state == CERBERUS_WARDRIVE_ERROR ? UI_BAD :
        active ? UI_GOOD : UI_TEXT, 1), TAG, "wardrive state");
    snprintf(line, sizeof(line), "C5 WI-FI: %lu SWEEPS / %lu AP",
        (unsigned long)status.c5_wifi_sweeps,
        (unsigned long)status.c5_wifi_observations);
    ESP_RETURN_ON_ERROR(draw_centered(132, line, UI_TEXT, 1), TAG, "wardrive Wi-Fi");
    snprintf(line, sizeof(line), "S3 BLE: %lu SWEEPS / %lu OBS",
        (unsigned long)status.s3_ble_sweeps,
        (unsigned long)status.s3_ble_observations);
    ESP_RETURN_ON_ERROR(draw_centered(156, line, UI_TEXT, 1), TAG, "wardrive BLE");
    if (status.c5_probe_state == CERBERUS_WARDRIVE_PROBE_AVAILABLE)
        snprintf(line, sizeof(line), "HINT OBS: PROBE %lu / BLE %lu",
            (unsigned long)status.c5_probe_observations,
            (unsigned long)status.s3_ble_flock_observations);
    else
        snprintf(line, sizeof(line), "PROBE %s / BLE HINTS %lu",
            status.c5_probe_state == CERBERUS_WARDRIVE_PROBE_WAITING ? "WAIT" :
            status.c5_probe_state == CERBERUS_WARDRIVE_PROBE_UNSUPPORTED ? "UNSUPPORTED" : "UNAVAILABLE",
            (unsigned long)status.s3_ble_flock_observations);
    ESP_RETURN_ON_ERROR(draw_centered(178, line, UI_MUTED, 1), TAG, "wardrive probe/BLE hints");
    snprintf(line, sizeof(line), "LOGGED: AP %lu / BLE %lu / R1-R6 %lu",
        (unsigned long)status.c5_wifi_logged,
        (unsigned long)status.s3_ble_logged,
        (unsigned long)status.expansion_logged);
    ESP_RETURN_ON_ERROR(draw_centered(200, line, UI_GOOD, 1), TAG, "wardrive logged");
    snprintf(line, sizeof(line), "DROPPED: AP %lu / BLE %lu / R1-R6 %lu",
        (unsigned long)status.c5_wifi_scan_dropped,
        (unsigned long)status.s3_ble_scan_dropped,
        (unsigned long)status.expansion_radio_dropped);
    ESP_RETURN_ON_ERROR(draw_centered(220, line, UI_MUTED, 1), TAG, "wardrive drops");
    snprintf(line, sizeof(line), "EXPANSION R1-R6: %u ONLINE", expansion_count);
    ESP_RETURN_ON_ERROR(draw_centered(240, line, UI_TEXT, 1), TAG, "wardrive expansions");
    snprintf(line, sizeof(line), "SD: %lu ROWS / %lu FILES / %llu KiB",
        (unsigned long)status.sd_rows, (unsigned long)status.files_saved,
        (unsigned long long)(status.sd_bytes / 1024U));
    ESP_RETURN_ON_ERROR(draw_centered(260, line, UI_TEXT, 1), TAG, "wardrive storage");
    snprintf(line, sizeof(line), "LAST FILE: %s", status.last_file_name[0] ?
        status.last_file_name : "NONE YET");
    ESP_RETURN_ON_ERROR(draw_centered(280, line, UI_MUTED, 1), TAG, "wardrive file");
    snprintf(line, sizeof(line), "GEO %lu / UNLOC %lu ROWS",
        (unsigned long)status.geotagged_rows, (unsigned long)status.unlocated_rows);
    ESP_RETURN_ON_ERROR(draw_centered(300, line, UI_MUTED, 1), TAG, "wardrive format");
    esp_err_t error = wardrive_start_error != ESP_OK ? wardrive_start_error : status.error_code;
    if (error != ESP_OK) {
        if (wardrive_blocked_reason[0])
            snprintf(line, sizeof(line), "%s", wardrive_blocked_reason);
        else if (status.c5_error != ESP_OK)
            snprintf(line, sizeof(line), "C5 WI-FI: %s", esp_err_to_name(status.c5_error));
        else if (status.s3_ble_error != ESP_OK)
            snprintf(line, sizeof(line), "S3 BLE: %s", esp_err_to_name(status.s3_ble_error));
        else if (status.expansion_error != ESP_OK)
            snprintf(line, sizeof(line), "R1-R6: %s", esp_err_to_name(status.expansion_error));
        else if (status.sd_error != ESP_OK)
            snprintf(line, sizeof(line), "SD: %s", esp_err_to_name(status.sd_error));
        else
            snprintf(line, sizeof(line), "%s", esp_err_to_name(error));
        ESP_RETURN_ON_ERROR(draw_centered(324, line, UI_BAD, 1), TAG, "wardrive error");
    } else if (wardrive_return_pending) {
        ESP_RETURN_ON_ERROR(draw_centered(324, "CLOSING SD FILE BEFORE EXIT", UI_GOOD, 1), TAG, "wardrive exit");
    }
    ESP_RETURN_ON_ERROR(draw_small_button(24, 346, 132, 28,
        wardrive_include_expansions ? "R1-R6: ON" : "R1-R6: OFF"),
        TAG, "wardrive expansion control");
    ESP_RETURN_ON_ERROR(draw_small_button(164, 346, 132, 28,
        wardrive_gps_enabled ? "GPS: ON" : "GPS: OFF"),
        TAG, "wardrive GPS control");
    return draw_button(40, 380, 240, 32,
        active || wardrive_start_pending ? "STOP" : "START");
}

static esp_err_t draw_detail_page(void)
{
    if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI) return csi_draw(true);
    if (current_page == UI_PAGE_REMOTE_ID) return remote_id_draw(true);
    if (current_page == UI_PAGE_MAC_MONITOR) return mac_monitor_draw(true);
    if (current_page == UI_PAGE_WATCH)
        return watch_draw((uint32_t)(esp_timer_get_time() / 1000), true);
    const ui_menu_item_t *item = find_item(current_page);
    if (item == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    ESP_RETURN_ON_ERROR(
        cerberus_display_fill_rect(0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, UI_BACKGROUND),
        TAG,
        "detail clear failed");
    ESP_RETURN_ON_ERROR(draw_header(item->label), TAG, "detail header failed");

    if (ui_capability_blocked) {
        ESP_RETURN_ON_ERROR(draw_centered(132, "CAPABILITY UNAVAILABLE", UI_BAD, 2), TAG, "capability blocked title failed");
        ESP_RETURN_ON_ERROR(
            draw_centered(
                184,
                ui_capability_blocked_reason != NULL
                    ? ui_capability_blocked_reason
                    : "REQUIRED HARDWARE IS UNAVAILABLE",
                UI_TEXT,
                1),
            TAG,
            "capability blocked reason failed");
        ESP_RETURN_ON_ERROR(draw_centered(236, "DETECTED FROM LIVE HARDWARE STATUS", UI_MUTED, 1), TAG, "capability discovery note failed");
        ESP_RETURN_ON_ERROR(draw_centered(270, "C5 STATUS RETRIES AUTOMATICALLY", UI_GOOD, 1), TAG, "capability retry note failed");
    } else if (current_page == UI_PAGE_RADIO_LAB) {
        ESP_RETURN_ON_ERROR(lab_draw(), TAG, "radio lab");
    } else if (current_page == UI_PAGE_DASHBOARD) {
        ESP_RETURN_ON_ERROR(draw_dashboard(), TAG, "dashboard failed");
    } else if (current_page == UI_PAGE_WIFI) {
        ESP_RETURN_ON_ERROR(draw_wifi(), TAG, "Wi-Fi failed");
    } else if (current_page == UI_PAGE_BLE) {
        ESP_RETURN_ON_ERROR(draw_ble(), TAG, "BLE failed");
    } else if (current_page == UI_PAGE_BLE_ATTACK) {
        ESP_RETURN_ON_ERROR(draw_ble_attack(), TAG, "BLE attack failed");
    } else if (current_page == UI_PAGE_BLE_SOUND) {
        ESP_RETURN_ON_ERROR(draw_ble_sound(), TAG, "BLE sound failed");
    } else if (current_page == UI_PAGE_WARDRIVE) {
        ESP_RETURN_ON_ERROR(draw_wardrive(), TAG, "wardriving failed");
    } else if (current_page == UI_PAGE_IEEE802154) {
        ESP_RETURN_ON_ERROR(draw_ieee802154(), TAG, "802.15.4 failed");
    } else if (current_page == UI_PAGE_FILES) {
        if (file_mode == UI_FILE_MODE_IMAGE) {
            ESP_RETURN_ON_ERROR(draw_image_viewer(), TAG, "image viewer failed");
        } else if (file_mode == UI_FILE_MODE_INFO) {
            ESP_RETURN_ON_ERROR(draw_file_info(), TAG, "file info failed");
        } else if (file_mode == UI_FILE_MODE_TEXT) {
            ESP_RETURN_ON_ERROR(draw_text_viewer(), TAG, "text viewer failed");
        } else if (file_mode == UI_FILE_MODE_TOOLS) {
            ESP_RETURN_ON_ERROR(draw_file_tools(), TAG, "file tools failed");
        } else if (file_mode == UI_FILE_MODE_ACTIONS) {
            ESP_RETURN_ON_ERROR(draw_file_actions(), TAG, "file actions failed");
        } else if (file_mode == UI_FILE_MODE_NAME_INPUT) {
            ESP_RETURN_ON_ERROR(cerberus_keyboard_draw(&file_keyboard,
                file_operation == UI_FILE_OPERATION_RENAME ? "RENAME" :
                (file_operation == UI_FILE_OPERATION_CREATE_DIRECTORY ? "FOLDER NAME" : "FILE NAME")), TAG, "file keyboard failed");
        } else if (file_mode == UI_FILE_MODE_DELETE_CONFIRM) {
            ESP_RETURN_ON_ERROR(draw_file_delete_confirm(), TAG, "file delete confirmation failed");
        } else if (file_mode == UI_FILE_MODE_PACKAGE_INFO) {
            ESP_RETURN_ON_ERROR(draw_file_package_info(), TAG, "package info failed");
        } else {
            ESP_RETURN_ON_ERROR(draw_files(), TAG, "files failed");
        }
    } else if (current_page == UI_PAGE_QR) {
        if (qr_mode == UI_QR_MODE_MENU) {
            ESP_RETURN_ON_ERROR(draw_qr_menu(), TAG, "QR menu failed");
        } else if (qr_mode == UI_QR_MODE_WIFI_SECURITY) {
            ESP_RETURN_ON_ERROR(draw_qr_wifi_security(), TAG, "Wi-Fi QR security failed");
        } else if (qr_mode == UI_QR_MODE_INPUT || qr_mode == UI_QR_MODE_WIFI_SSID_INPUT ||
            qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT) {
            ESP_RETURN_ON_ERROR(draw_qr_input(), TAG, "QR input failed");
        } else {
            ESP_RETURN_ON_ERROR(draw_qr_code(), TAG, "QR code failed");
        }
    } else if (current_page == UI_PAGE_WEB) {
        ESP_RETURN_ON_ERROR(draw_web_ui(), TAG, "Web UI failed");
    } else if (current_page == UI_PAGE_USB) {
        ESP_RETURN_ON_ERROR(draw_usb(), TAG, "USB console failed");
    } else if (current_page == UI_PAGE_SCRIPTS) {
        ESP_RETURN_ON_ERROR(draw_scripts(), TAG, "scripts failed");
    } else if (current_page == UI_PAGE_LOGS) {
        ESP_RETURN_ON_ERROR(draw_logs(), TAG, "logs failed");
    } else if (current_page == UI_PAGE_SELF_TEST) {
        ESP_RETURN_ON_ERROR(draw_self_test(), TAG, "self-test failed");
    } else if (current_page == UI_PAGE_SETTINGS) {
        ESP_RETURN_ON_ERROR(draw_settings(), TAG, "settings failed");
    } else {
        return ESP_ERR_NOT_FOUND;
    }

    const char *back_label = live_is_view() ? (tool_parent_page == UI_PAGE_MAIN ? "HOME" : "BACK") : "BACK";
    if (current_page == UI_PAGE_DASHBOARD &&
        dashboard_mode == UI_DASHBOARD_MODE_CRASH_RECORD) {
        back_label = "SYSTEM HEALTH";
    } else if (current_page == UI_PAGE_DASHBOARD &&
        dashboard_mode == UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_TEXT) {
        back_label = "FILES";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_INFO) {
        back_label = "TEXT";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_IMAGE) {
        back_label = "FILES";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_NAME_INPUT) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_DELETE_CONFIRM) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_PACKAGE_INFO) {
        back_label = "ACTIONS";
    } else if (current_page == UI_PAGE_FILES &&
        (file_mode == UI_FILE_MODE_TOOLS || file_mode == UI_FILE_MODE_SELECT || file_mode == UI_FILE_MODE_ACTIONS)) {
        back_label = "FILES";
    } else if (current_page == UI_PAGE_QR && qr_mode != UI_QR_MODE_MENU) {
        back_label = qr_mode == UI_QR_MODE_INPUT || qr_mode == UI_QR_MODE_WIFI_SECURITY ||
                qr_mode == UI_QR_MODE_WIFI_SSID_INPUT || qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT
            ? "CANCEL"
            : "QR MENU";
    } else if (current_page == UI_PAGE_USB && usb_mode == UI_USB_MODE_RECOVERY_CONFIRM) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_TEXT) {
        back_label = "LOGS";
    } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_INFO) {
        back_label = "REPORT";
    } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_QR) {
        back_label = "REPORT";
    } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_DELETE_CONFIRM) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_SETTINGS &&
        (settings_reset_confirmation || settings_restore_confirmation)) {
        back_label = "CANCEL";
    } else if (current_page == UI_PAGE_SETTINGS &&
        settings_mode == UI_SETTINGS_MODE_TOUCH_CALIBRATION) {
        back_label = touch_calibration_step < 4U ? "CANCEL" : "ACCESSIBILITY";
    } else if (current_page == UI_PAGE_SETTINGS &&
        settings_mode != UI_SETTINGS_MODE_DISPLAY) {
        back_label = "DISPLAY SETTINGS";
    } else if (current_page == UI_PAGE_SELF_TEST && self_test_mode == UI_SELF_TEST_MODE_TOUCH) {
        back_label = "DONE";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PASSWORD) {
        back_label = "NETWORK";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_AP_JOB) {
        back_label = "ATTACKS";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PORTAL) {
        back_label = "ATTACKS";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CAPTURE) {
        back_label = "TARGET";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MULTISSID) {
        back_label = multissid_ui_exit_pending || multissid_ui_list_pending ? "STOPPING" :
            multissid_ui_detail ? "RESULTS" : "WIFI MENU";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PINESCAN) {
        back_label = pinescan_ui_exit_pending || pinescan_ui_list_pending ? "STOPPING" :
            pinescan_ui_detail ? "RESULTS" : "WIFI MENU";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS) {
        back_label = wifi_relation_ui && wifi_relation_ui->detail ? "BACK TO CLIENTS" : "BACK TO AP";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_STATIONS && ap_station_selecting) {
        back_label = "ATTACK SETUP";
    } else if (current_page == UI_PAGE_WIFI && (wifi_mode == UI_WIFI_MODE_DETAIL || wifi_mode == UI_WIFI_MODE_STATIONS)) {
        back_label = "TARGET";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_TARGET) {
        back_label = "BACK TO SCAN";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CONNECTION && wifi_selection_valid) {
        back_label = "TARGET";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MDNS) {
        back_label = "NETWORK TOOLS";
    } else if (current_page == UI_PAGE_WIFI && wifi_mode != UI_WIFI_MODE_MENU) {
        back_label = "WIFI MENU";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_DETAIL) {
        back_label = ble_selected_from_monitor && ble_findmy_monitor ? "FIND MY MONITOR" : "BLE LIST";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_FOX_HUNT) {
        back_label = ble_fox_hunt_exit_pending ? "STOPPING" : "STOP TRACKING";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_ANALYZER) {
        back_label = ble_analyzer_exit_pending ? "STOPPING" : "STOP ANALYZER";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_FINDMY_MONITOR) {
        back_label = ble_findmy_stopping ? "RELEASING S3 BLE" : "BLE LIST";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_ENVIRONMENT) {
        back_label = "BLE MENU";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_SERVICES) {
        back_label = "DEVICE";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_CHARACTERISTICS) {
        back_label = "SERVICES";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_VALUE) {
        back_label = "CHARACTERISTICS";
    } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST) {
        back_label = "BLE MENU";
    } else if (current_page == UI_PAGE_BLE_SOUND) {
        back_label = ble_sound_stop_pending ? "RELEASING S3 BLE" :
            ble_sound_all ? "BLE ATTACKS" : "BLE DEVICE";
    } else if (current_page == UI_PAGE_SCRIPTS &&
        script_mode == UI_SCRIPT_MODE_DETAIL) {
        back_label = "SCRIPT LIST";
    } else if (current_page == UI_PAGE_SCRIPTS &&
        script_mode == UI_SCRIPT_MODE_RUNNING) {
        back_label = script_complete ? "SCRIPT LIST" : "CANCEL SCRIPT";
    } else if (current_page == UI_PAGE_IEEE802154 &&
        ieee802154_mode != UI_IEEE802154_MODE_MENU) {
        back_label = "RADIO MENU";
    }
    if (live_is_view()) back_label = tool_parent_page == UI_PAGE_TOOLS ? "DEVICE TOOLS" : "HOME";
    if (learning_tool_active) {
        return draw_button(12, UI_BACK_Y, 296, UI_BACK_HEIGHT, "RETURN TO LESSON");
    }
    if (!live_is_view() && live_page_tool() != SENTINEL_SCAN_NONE) {
        ESP_RETURN_ON_ERROR(draw_button(8, UI_BACK_Y, 168, UI_BACK_HEIGHT, "BACK"), TAG, "live back");
        return draw_button(184, UI_BACK_Y, 128, UI_BACK_HEIGHT, live_scan_action_label());
    }
    if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS) {
        return draw_button(8, UI_BACK_Y, 304, UI_BACK_HEIGHT, back_label);
    }
    if (strcmp(back_label, "BACK") == 0) {
        back_label = tool_parent_page == UI_PAGE_TOOLS ? tools_group_title() : "HOME";
    }
    return draw_button(64, UI_BACK_Y, 192, UI_BACK_HEIGHT, back_label);
}

static esp_err_t draw_current_page(void)
{
    if (current_page == UI_PAGE_MAIN) return draw_main_menu();
    if (current_page == UI_PAGE_FIELD_MENU) {
        wifi_target_sync_menu();
        field_menu.ble_advertiser_available = (ui_capability_map & UI_CAP_BLE) != 0U;
#ifdef CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
        field_menu.ble_connectable_available = field_menu.ble_advertiser_available;
#else
        field_menu.ble_connectable_available = false;
#endif
        field_menu.ble_capture_selected = ble_selection_valid;
        field_menu.ble_gatt_available = (ui_capability_map & UI_CAP_BLE_GATT) != 0U;
        field_menu.ble_capture_replayable = ble_attack_capture_valid;
        snprintf(field_menu.ble_target, sizeof(field_menu.ble_target), "%.32s",
            ble_selection_valid && ble_selected_result.name[0] ?
                ble_selected_result.name : "<UNNAMED>");
        ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(0, 0, CERBERUS_LCD_WIDTH,
            CERBERUS_LCD_HEIGHT, UI_BACKGROUND), TAG, "field menu clear");
        ESP_RETURN_ON_ERROR(draw_header("Field kit"), TAG, "field menu header");
        return cerberus_field_menu_draw(&field_menu, UI_WIDGET_THEME);
    }
    if (current_page == UI_PAGE_TOOLS) return draw_tools_menu();
    if (current_page == UI_PAGE_GUIDE) return sentinel_guide_draw();
    return draw_detail_page();
}

static void refresh_c5_status(void)
{
    bool was_online = c5_online;
    uint32_t previous_capability_map = ui_capability_map;
    cerberus_c5_status_t previous = c5_status;
    cerberus_c5_status_t candidate = c5_status;
    esp_err_t status_error = ui_config.c5_link_available
        ? c5_spi_link_get_status(&candidate)
        : ESP_ERR_INVALID_STATE;
    if (status_error == ESP_OK) {
        c5_status = candidate;
        c5_online = true;
        c5_status_failures = 0;
    } else {
        if (c5_status_failures < UINT8_MAX) {
            ++c5_status_failures;
        }
        c5_online = was_online && c5_status_failures < 3U;
        ESP_LOGW(
            TAG,
            "C5 status miss %u/3: %s",
            c5_status_failures,
            esp_err_to_name(status_error));
    }

    if (c5_online && (!was_online ||
        previous.firmware_version != c5_status.firmware_version ||
        previous.protocol_version != c5_status.protocol_version ||
        previous.capabilities != c5_status.capabilities)) {
        ESP_LOGI(
            TAG,
            "C5 online: protocol=%u firmware=%lu capabilities=0x%08lx",
            c5_status.protocol_version,
            (unsigned long)c5_status.firmware_version,
            (unsigned long)c5_status.capabilities);
        if (!was_online) {
            (void)cerberus_event_bus_publish(
                CERBERUS_EVENT_C5_ONLINE,
                (int32_t)c5_status.protocol_version);
        }
    } else if (!c5_online && was_online) {
        ESP_LOGW(TAG, "C5 link lost");
        (void)cerberus_event_bus_publish(
            CERBERUS_EVENT_C5_OFFLINE,
            (int32_t)c5_status_failures);
    }

    bool online_changed = was_online != c5_online;
    bool telemetry_changed = c5_online &&
        (previous.firmware_version != c5_status.firmware_version ||
         previous.uptime_ms / 5000U != c5_status.uptime_ms / 5000U ||
         previous.protocol_version != c5_status.protocol_version ||
         previous.capabilities != c5_status.capabilities);
    bool c5_restarted = c5_online && was_online && previous.uptime_ms > c5_status.uptime_ms;
    bool wifi_link_changed = online_changed || c5_restarted || (c5_online &&
        (previous.firmware_version != c5_status.firmware_version ||
         previous.protocol_version != c5_status.protocol_version ||
         previous.capabilities != c5_status.capabilities));
    if (c5_restarted) {
        live_reset_observations();
        clear_wifi_channel_history();
        wifi_observation_valid = false;
        wifi_scan_checked = false;
        ble_scan_checked = false;
        wifi_baseline.checked = false;
        esp_reset_reason_t restart_reason = (esp_reset_reason_t)c5_status.reset_reason;
        (void)cerberus_event_bus_publish(
            CERBERUS_EVENT_C5_RESTARTED,
            (int32_t)restart_reason);
    }
    /* Bounded scans may complete while a lesson or the home screen is open.
       Refresh their cached status without initiating another scan. */
    if (c5_online && (current_page == UI_PAGE_MAIN || current_page == UI_PAGE_TOOLS ||
        current_page == UI_PAGE_GUIDE)) {
        if (wifi_scan_checked && wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING) {
            (void)refresh_wifi_scan_status();
        }
        if (ble_scan_checked && ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING) {
            (void)refresh_ble_scan_status();
        }
    }
    if (current_page == UI_PAGE_WIFI && wifi_link_changed) {
        wifi_scan_checked = false;
        wifi_channel_generation = 0;
        wifi_channel_error = ESP_ERR_INVALID_STATE;
        wifi_analysis_generation = 0;
        wifi_analysis_error = ESP_ERR_INVALID_STATE;
        wifi_radio_stats_checked = false;
        wifi_radio_stats_error = ESP_ERR_INVALID_STATE;
    }
    if (current_page == UI_PAGE_BLE && wifi_link_changed) {
        ble_scan_checked = false;
        ble_connection_checked = false;
        ble_service_checked = false;
        ble_characteristic_checked = false;
        ble_read_checked = false;
    }
    if (current_page == UI_PAGE_IEEE802154 && wifi_link_changed) {
        ieee802154_scan_checked = false;
        ieee802154_scan_error = ESP_ERR_INVALID_STATE;
        ieee802154_beacon_checked = false;
        ieee802154_beacon_error = ESP_ERR_INVALID_STATE;
        ieee802154_beacon_result_count = 0U;
        ieee802154_beacon_page_offset = 0U;
        ieee802154_activity_checked = false;
        ieee802154_activity_error = ESP_ERR_INVALID_STATE;
        memset(ieee802154_activity_counts, 0, sizeof(ieee802154_activity_counts));
        ieee802154_export_message[0] = '\0';
    }
    refresh_capability_map();
    bool capability_changed = previous_capability_map != ui_capability_map;
    if (ui_capability_blocked) {
        const char *issue = page_capability_issue(current_page);
        if (issue == NULL) {
            ui_capability_blocked = false;
            ui_capability_blocked_reason = NULL;
        } else {
            ui_capability_blocked_reason = issue;
        }
    }
    bool redraw = (current_page == UI_PAGE_DASHBOARD && (online_changed || telemetry_changed)) ||
        (current_page == UI_PAGE_WIFI && wifi_link_changed) ||
        (current_page == UI_PAGE_BLE && wifi_link_changed) ||
        (current_page == UI_PAGE_IEEE802154 && wifi_link_changed) ||
        (current_page == UI_PAGE_MAIN && (online_changed || capability_changed)) ||
        (current_page == UI_PAGE_SELF_TEST && online_changed) ||
        (ui_capability_blocked && capability_changed);
    if (redraw) {
        esp_err_t err = draw_current_page();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "C5 status redraw failed: %s", esp_err_to_name(err));
        }
    }
}

static void open_main_menu(void)
{
    learning_tool_active = false;
    tool_parent_page = UI_PAGE_MAIN;
    ui_capability_blocked = false;
    ui_capability_blocked_reason = NULL;
    current_page = UI_PAGE_MAIN;
    ESP_ERROR_CHECK(draw_main_menu());
}

static void return_to_launcher(void)
{
    if (current_page == UI_PAGE_BLE_ATTACK) {
        /* A replay capture is valid for this review session only. Returning
         * to the catalog requires a fresh selected scan result next time. */
        ble_attack_capture_valid = false;
    }
    ui_capability_blocked = false;
    ui_capability_blocked_reason = NULL;
    learning_tool_active = false;
    current_page = tool_parent_page;
    ESP_ERROR_CHECK(draw_current_page());
}

static void open_learning_lesson(uint8_t lesson)
{
    learning_tool_active = false;
    ui_capability_blocked = false;
    ui_capability_blocked_reason = NULL;
    sentinel_guide_open_lesson(lesson);
    current_page = UI_PAGE_GUIDE;
    ESP_ERROR_CHECK(sentinel_guide_draw());
}

static void refresh_dashboard_crash_record(void)
{
    dashboard_crash_action[0] = '\0';
    (void)cerberus_crash_record_inspect(&dashboard_crash_record);
}

static void handle_dashboard_tap(uint16_t x, uint16_t y)
{
    if (dashboard_mode == UI_DASHBOARD_MODE_SUMMARY) {
        if (y < 220U || y >= 256U) {
            return;
        }
        refresh_dashboard_crash_record();
        dashboard_mode = UI_DASHBOARD_MODE_CRASH_RECORD;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (dashboard_mode == UI_DASHBOARD_MODE_CRASH_RECORD) {
        if (x < 40U || x >= 280U || y < 350U || y >= 408U) {
            return;
        }
        if (!dashboard_crash_record.present) {
            refresh_dashboard_crash_record();
        } else {
            dashboard_mode = UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (dashboard_mode != UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM ||
        y < 330U || y >= 388U) {
        return;
    }
    if (x >= 20U && x < 152U) {
        dashboard_mode = UI_DASHBOARD_MODE_CRASH_RECORD;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (x < 168U || x >= 300U) {
        return;
    }

    esp_err_t err = cerberus_crash_record_clear();
    if (err == ESP_OK) {
        (void)cerberus_storage_log_event(
            CERBERUS_LOG_INFO,
            "system",
            "crash_record_clear",
            "internal flash core dump erased");
        (void)cerberus_crash_record_inspect(&dashboard_crash_record);
        snprintf(dashboard_crash_action, sizeof(dashboard_crash_action), "CRASH RECORD CLEARED");
        dashboard_crash_action_color = UI_GOOD;
    } else {
        snprintf(
            dashboard_crash_action,
            sizeof(dashboard_crash_action),
            "CLEAR FAILED %s",
            esp_err_to_name(err));
        dashboard_crash_action_color = UI_BAD;
    }
    dashboard_mode = UI_DASHBOARD_MODE_CRASH_RECORD;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_settings_tap(uint16_t x, uint16_t y)
{
    if (settings_reset_confirmation) {
        if (y >= 330U && y < 388U && x >= 20U && x < 152U) {
            settings_reset_confirmation = false;
            settings_reset_error = ESP_OK;
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (y >= 330U && y < 388U && x >= 168U && x < 300U) {
            settings_reset_error = cerberus_settings_factory_reset();
            if (settings_reset_error == ESP_OK) {
                settings_reset_error = cerberus_web_ui_factory_reset();
            }
            if (settings_reset_error == ESP_OK) {
                settings_reset_error = sentinel_guide_reset_progress();
            }
            if (settings_reset_error == ESP_OK) {
                (void)cerberus_storage_log_event(
                    CERBERUS_LOG_WARNING,
                    "settings",
                    "factory_reset",
                    "Cerberus NVS settings cleared");
                ESP_ERROR_CHECK(cerberus_display_fill_rect(
                    0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, UI_BACKGROUND));
                ESP_ERROR_CHECK(draw_centered(180, "SETTINGS RESET", UI_GOOD, 2));
                ESP_ERROR_CHECK(draw_centered(230, "RESTARTING CERBERUS...", UI_TEXT, 1));
                present_ui_frame();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
            ESP_LOGE(TAG, "factory reset failed: %s", esp_err_to_name(settings_reset_error));
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (settings_mode == UI_SETTINGS_MODE_CLOCK_TIMER) {
        if (y >= 184U && y < 232U) {
            if (x >= 12U && x < 100U) {
                settings_timer_seconds = 60U;
            } else if (x >= 116U && x < 204U) {
                settings_timer_seconds = 300U;
            } else if (x >= 220U && x < 308U) {
                settings_timer_seconds = 600U;
            } else {
                return;
            }
        } else if (x >= 52U && x < 268U && y >= 276U && y < 330U) {
            int64_t now_ms = esp_timer_get_time() / 1000LL;
            if (cerberus_ui_timer_is_running()) {
                cerberus_ui_timer_cancel();
            } else {
                cerberus_ui_timer_start(settings_timer_seconds, now_ms);
            }
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (settings_mode == UI_SETTINGS_MODE_NOTIFICATIONS) {
        if (x >= 52U && x < 268U && y >= 350U && y < 404U) {
            cerberus_ui_notifications_clear();
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (settings_mode == UI_SETTINGS_MODE_ACCESSIBILITY) {
        if (x >= 52U && x < 268U && y >= 104U && y < 158U) {
            uint8_t next_size = ui_accessibility_size == 0U ? 1U : 0U;
            if (ui_config.settings_available) {
                esp_err_t err = cerberus_settings_set_accessibility_size(next_size);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "accessibility size save failed: %s", esp_err_to_name(err));
                    return;
                }
            }
            ui_accessibility_size = next_size;
        } else if (x >= 30U && x < 290U && y >= 292U && y < 344U) {
            touch_calibration_step = 0U;
            touch_calibration_error = ESP_OK;
            memset(touch_calibration_points, 0, sizeof(touch_calibration_points));
            settings_mode = UI_SETTINGS_MODE_TOUCH_CALIBRATION;
        } else if (x >= 72U && x < 248U && y >= 360U && y < 402U) {
            esp_err_t err = cerberus_settings_clear_touch_calibration();
            if (err == ESP_OK) {
                err = cerberus_touch_set_calibration(false, 0U, 0U, 0U, 0U);
            }
            touch_calibration_error = err;
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (settings_mode == UI_SETTINGS_MODE_DATA) {
        if (x >= 52U && x < 268U && y >= 140U && y < 184U) {
            if (!ui_config.settings_available) {
                return;
            }
            esp_err_t err = cerberus_settings_set_privacy_mask_addresses(
                !privacy_addresses_masked());
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "address privacy save failed: %s", esp_err_to_name(err));
                return;
            }
        } else if (x >= 28U && x < 152U && y >= 232U && y < 286U) {
            size_t backup_length = 0U;
            settings_backup_checked = true;
            settings_restore_checked = false;
            settings_backup_file_name[0] = '\0';
            settings_backup_error = cerberus_settings_export_backup(
                settings_backup_buffer, sizeof(settings_backup_buffer), &backup_length);
            if (settings_backup_error == ESP_OK) {
                settings_backup_error = cerberus_storage_write_config_backup(
                    settings_backup_buffer, backup_length, settings_backup_file_name,
                    sizeof(settings_backup_file_name));
            }
            memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
            storage_safely_removed = false;
            storage_checked = true;
            storage_last_error = settings_backup_error;
            if (settings_backup_error == ESP_OK) {
                (void)cerberus_storage_log_event(
                    CERBERUS_LOG_INFO, "settings", "backup", "configuration backup saved");
                storage_last_error = cerberus_storage_get_status(&storage_status);
                file_entry_count = 0U;
            }
        } else if (x >= 168U && x < 292U && y >= 232U && y < 286U) {
            settings_backup_checked = false;
            settings_restore_checked = true;
            settings_restore_confirmation = false;
            settings_restore_length = 0U;
            settings_restore_file_name[0] = '\0';
            memset(&settings_restore_info, 0, sizeof(settings_restore_info));
            memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
            settings_restore_error = cerberus_storage_read_latest_config_backup(
                settings_backup_buffer, sizeof(settings_backup_buffer),
                &settings_restore_length, settings_restore_file_name,
                sizeof(settings_restore_file_name));
            if (settings_restore_error == ESP_OK) {
                settings_restore_error = cerberus_settings_validate_backup(
                    settings_backup_buffer, settings_restore_length, &settings_restore_info);
            }
            storage_safely_removed = false;
            storage_checked = true;
            storage_last_error = settings_restore_error;
            if (settings_restore_error == ESP_OK) {
                storage_last_error = cerberus_storage_get_status(&storage_status);
                settings_restore_checked = false;
                settings_restore_confirmation = true;
            } else {
                settings_restore_length = 0U;
                memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
            }
        } else if (x >= 52U && x < 268U && y >= 344U && y < 402U) {
            settings_reset_confirmation = true;
            settings_reset_error = ESP_OK;
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (settings_restore_confirmation) {
        if (y >= 330U && y < 388U && x >= 20U && x < 152U) {
            settings_restore_confirmation = false;
            settings_restore_checked = false;
            settings_restore_error = ESP_ERR_INVALID_STATE;
            settings_restore_length = 0U;
            memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (y >= 330U && y < 388U && x >= 168U && x < 300U) {
            settings_restore_error = cerberus_settings_import_backup(
                settings_backup_buffer,
                settings_restore_length);
            if (settings_restore_error == ESP_OK) {
                memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
                (void)cerberus_storage_log_event(
                    CERBERUS_LOG_INFO,
                    "settings",
                    "restore",
                    "validated configuration applied");
                ESP_ERROR_CHECK(cerberus_display_fill_rect(
                    0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, UI_BACKGROUND));
                ESP_ERROR_CHECK(draw_centered(180, "CONFIGURATION RESTORED", UI_GOOD, 2));
                ESP_ERROR_CHECK(draw_centered(230, "RESTARTING CERBERUS...", UI_TEXT, 1));
                present_ui_frame();
                vTaskDelay(pdMS_TO_TICKS(500));
                esp_restart();
            }
            ESP_LOGE(TAG, "configuration restore failed: %s", esp_err_to_name(settings_restore_error));
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (y >= 86U && y < 130U) {
        uint8_t brightness = cerberus_display_get_brightness();
        if (x >= 28U && x < 140U) {
            brightness = brightness > 20U ? brightness - 10U : 10U;
        } else if (x >= 180U && x < 292U) {
            brightness = brightness < 90U ? brightness + 10U : 100U;
        } else {
            return;
        }

        ESP_ERROR_CHECK(cerberus_display_set_brightness(brightness));
        if (ui_config.settings_available) {
            esp_err_t err = cerberus_settings_set_brightness(brightness);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "brightness save failed: %s", esp_err_to_name(err));
                ui_config.settings_available = false;
            }
        }
    } else if (x >= 52U && x < 268U && y >= 190U && y < 242U) {
        switch (ui_screen_timeout_seconds) {
        case 0U:
            ui_screen_timeout_seconds = 30U;
            break;
        case 30U:
            ui_screen_timeout_seconds = 60U;
            break;
        case 60U:
            ui_screen_timeout_seconds = 120U;
            break;
        case 120U:
            ui_screen_timeout_seconds = 300U;
            break;
        default:
            ui_screen_timeout_seconds = 0U;
            break;
        }
        if (ui_config.settings_available) {
            esp_err_t err = cerberus_settings_set_screen_timeout(ui_screen_timeout_seconds);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "screen timeout save failed: %s", esp_err_to_name(err));
                ui_config.settings_available = false;
            }
        }
    } else if (y >= 350U && y < 404U) {
        if (x >= 4U && x < 78U) {
            settings_mode = UI_SETTINGS_MODE_CLOCK_TIMER;
        } else if (x >= 84U && x < 158U) {
            settings_mode = UI_SETTINGS_MODE_NOTIFICATIONS;
        } else if (x >= 164U && x < 238U) {
            settings_mode = UI_SETTINGS_MODE_ACCESSIBILITY;
        } else if (x >= 244U && x < 316U) {
            settings_mode = UI_SETTINGS_MODE_DATA;
        } else {
            return;
        }
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_ieee802154_tap(uint16_t x, uint16_t y)
{
    if (live_is_view()) { handle_live_scan_tap(x, y); return; }
    if (ieee802154_mode == UI_IEEE802154_MODE_MENU) {
        int item = sentinel_hub_hit_test(SENTINEL_HUB_RADIO, x, y);
        if (item == 6) { open_learning_lesson(4); return; }
        if (item == 0) {
            ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
            ieee802154_scan_checked = false;
        } else if (item == 1) {
            ieee802154_mode = UI_IEEE802154_MODE_BEACONS;
            ieee802154_beacon_checked = false;
        } else if (item == 2) {
            ieee802154_mode = UI_IEEE802154_MODE_ACTIVITY;
            ieee802154_activity_checked = false;
        } else if (item == 3) {
            ieee802154_mode = UI_IEEE802154_MODE_ASSESSMENT;
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (y < 364U || y >= 406U) {
        return;
    }

    if (ieee802154_mode == UI_IEEE802154_MODE_ENERGY) {
        if (ieee802154_scan_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_RUNNING) {
            return;
        }
        if (x >= 8U && x < 76U) {
            ieee802154_export_message[0] = '\0';
            ieee802154_scan_checked = true;
            ieee802154_scan_error =
                c5_spi_link_start_ieee802154_energy_scan(&ieee802154_scan_status);
        } else if (x >= 84U && x < 152U) {
            ieee802154_mode = UI_IEEE802154_MODE_BEACONS;
            ieee802154_beacon_checked = false;
        } else if (x >= 160U && x < 228U) {
            ieee802154_mode = UI_IEEE802154_MODE_ACTIVITY;
            ieee802154_activity_checked = false;
        } else if (x >= 236U && x < 312U) {
            ieee802154_mode = UI_IEEE802154_MODE_ASSESSMENT;
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ieee802154_mode == UI_IEEE802154_MODE_BEACONS) {
        if (ieee802154_beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_RUNNING) {
            return;
        }
        if (x >= 8U && x < 76U) {
            if (ieee802154_beacon_page_offset >= UI_IEEE802154_BEACON_VISIBLE_ROWS) {
                ieee802154_beacon_page_offset -= UI_IEEE802154_BEACON_VISIBLE_ROWS;
                ieee802154_beacon_checked = false;
            }
        } else if (x >= 84U && x < 152U) {
            if (ieee802154_beacon_page_offset + UI_IEEE802154_BEACON_VISIBLE_ROWS <
                ieee802154_beacon_status.stored_count) {
                ieee802154_beacon_page_offset += UI_IEEE802154_BEACON_VISIBLE_ROWS;
                ieee802154_beacon_checked = false;
            }
        } else if (x >= 160U && x < 228U) {
            ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
            ieee802154_scan_checked = false;
        } else if (x >= 236U && x < 312U) {
            ieee802154_beacon_page_offset = 0U;
            ieee802154_beacon_checked = true;
            ieee802154_beacon_error =
                c5_spi_link_start_ieee802154_beacon_scan(&ieee802154_beacon_status);
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ieee802154_mode == UI_IEEE802154_MODE_ACTIVITY) {
        if (ieee802154_activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING) {
            return;
        }
        if (x >= 8U && x < 76U) {
            ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
            ieee802154_scan_checked = false;
        } else if (x >= 84U && x < 152U) {
            ieee802154_mode = UI_IEEE802154_MODE_BEACONS;
            ieee802154_beacon_checked = false;
        } else if (x >= 160U && x < 228U) {
            ieee802154_mode = UI_IEEE802154_MODE_ASSESSMENT;
        } else if (x >= 236U && x < 312U) {
            ieee802154_export_message[0] = '\0';
            ieee802154_activity_checked = true;
            ieee802154_activity_error =
                c5_spi_link_start_ieee802154_activity_scan(&ieee802154_activity_status);
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (x >= 8U && x < 76U) {
        ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
        ieee802154_scan_checked = false;
    } else if (x >= 84U && x < 152U) {
        ieee802154_mode = UI_IEEE802154_MODE_ACTIVITY;
        ieee802154_activity_checked = false;
    } else if (x >= 160U && x < 228U) {
        ieee802154_mode = UI_IEEE802154_MODE_BEACONS;
        ieee802154_beacon_checked = false;
    } else if (x >= 236U && x < 312U) {
        (void)export_ieee802154_report();
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_wifi_tap(uint16_t x, uint16_t y)
{
    if (wifi_mode == UI_WIFI_MODE_TARGET) {
        wifi_target_handle_tap(x,y); ESP_ERROR_CHECK(draw_current_page()); return;
    }
    if (wifi_mode == UI_WIFI_MODE_NETWORK_SCAN) {
        net_tap(x, y); ESP_ERROR_CHECK(draw_detail_page()); return;
    }
    if (wifi_mode == UI_WIFI_MODE_STATIONS) {
        station_tap(x,y); ESP_ERROR_CHECK(draw_detail_page()); return;
    }
    if (wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS) {
        wifi_relation_tap(x,y); ESP_ERROR_CHECK(draw_detail_page()); return;
    }
    if (wifi_mode == UI_WIFI_MODE_AP_JOB) {ap_tap(x,y);ESP_ERROR_CHECK(draw_detail_page());return;}
    if (wifi_mode == UI_WIFI_MODE_PORTAL) {portal_tap(x,y);ESP_ERROR_CHECK(draw_detail_page());return;}
    if (wifi_mode == UI_WIFI_MODE_CAPTURE) {capture_tap(x,y);ESP_ERROR_CHECK(draw_detail_page());return;}
    if (wifi_mode == UI_WIFI_MODE_MULTISSID) {multissid_tap(x,y);ESP_ERROR_CHECK(draw_detail_page());return;}
    if (wifi_mode == UI_WIFI_MODE_PINESCAN) {pinescan_tap(x,y);ESP_ERROR_CHECK(draw_detail_page());return;}
    if (wifi_mode == UI_WIFI_MODE_FIELD_SURVEY) {
        field_survey_view_tap(x, y);
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (live_is_view()) { handle_live_scan_tap(x, y); return; }
    if (wifi_mode == UI_WIFI_MODE_MENU) {
        int item = sentinel_hub_hit_test(SENTINEL_HUB_WIFI, x, y);
        if (item == 6) { open_learning_lesson(0); return; }
        switch (item) {
        case 8: multissid_enter(); wifi_mode = UI_WIFI_MODE_MULTISSID; break;
        case 9: pinescan_enter(); wifi_mode = UI_WIFI_MODE_PINESCAN; break;
        case 7: wifi_mode = UI_WIFI_MODE_FIELD_SURVEY; break;
        case 0: wifi_mode = UI_WIFI_MODE_LIST; (void)refresh_wifi_scan_status(); break;
        case 1: wifi_mode = UI_WIFI_MODE_STATS; (void)refresh_wifi_scan_status(); break;
        case 2: wifi_mode = UI_WIFI_MODE_PACKET_MONITOR;
                packet_monitor_from_target = false;
                packet_monitor_channel = 1U;
                packet_monitor_attempted = false;
                packet_monitor_last_poll = 0U;
                packet_monitor_start_ui(); break;
        case 3: wifi_mode = UI_WIFI_MODE_BASELINE; wifi_baseline.checked = false;
                wifi_export_message[0] = '\0'; break;
        case 4: wifi_mode = UI_WIFI_MODE_DEFENSE; (void)refresh_wifi_defense_survey(); break;
        case 5:
            wifi_connection_error = c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
            if (wifi_selection_valid || wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING ||
                wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
                wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING) {
                wifi_mode = UI_WIFI_MODE_CONNECTION;
            } else { wifi_mode = UI_WIFI_MODE_LIST; (void)refresh_wifi_scan_status(); }
            break;
        default: return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (wifi_mode == UI_WIFI_MODE_BASELINE) {
        if (y < 364 || y >= 406) return;
        if (x >= 8 && x < 76) {
            wifi_mode = UI_WIFI_MODE_LIST;
            (void)refresh_wifi_scan_status();
        } else if (x >= 84 && x < 152) {
            wifi_baseline.checked = false;
            wifi_export_message[0] = '\0';
        } else if (x >= 160 && x < 228 && wifi_baseline.error == ESP_OK) {
            (void)export_wifi_baseline();
        } else if (x >= 236 && x < 312) {
            open_learning_lesson(5); return;
        } else { return; }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (wifi_mode == UI_WIFI_MODE_PASSWORD) {
        cerberus_keyboard_action_t action;
        esp_err_t err = cerberus_keyboard_handle_tap(&wifi_keyboard, x, y, &action);
        if (err == ESP_OK && action == CERBERUS_KEYBOARD_ACTION_SUBMIT) {
            wifi_connection_error = live_connect_selected_wifi(wifi_connection_password);
            memset(wifi_connection_password, 0, sizeof(wifi_connection_password));
            memset(&wifi_keyboard, 0, sizeof(wifi_keyboard));
            wifi_mode = UI_WIFI_MODE_CONNECTION;
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (err == ESP_OK && action == CERBERUS_KEYBOARD_ACTION_CHANGED) {
            ESP_ERROR_CHECK(cerberus_keyboard_draw(&wifi_keyboard, "NETWORK PASSWORD"));
        }
        return;
    }
    if (wifi_mode == UI_WIFI_MODE_MDNS) {
        if (y < 360U || y >= 408U) return;
        if (x >= 8U && x < 76U) {
            wifi_mode = UI_WIFI_MODE_DIAGNOSTICS;
        } else if (x >= 84U && x < 152U) {
            if (mdns_page_offset == 0U) return;
            mdns_page_offset = mdns_page_offset >= UI_WIFI_VISIBLE_ROWS
                ? mdns_page_offset - UI_WIFI_VISIBLE_ROWS : 0U;
            mdns_scan_error = load_mdns_results();
        } else if (x >= 160U && x < 228U) {
            if (mdns_page_offset + UI_WIFI_VISIBLE_ROWS >= mdns_scan_status.result_count) return;
            mdns_page_offset += UI_WIFI_VISIBLE_ROWS;
            mdns_scan_error = load_mdns_results();
        } else if (x >= 236U && x < 312U &&
            mdns_scan_status.state != CERBERUS_MDNS_SCAN_RUNNING) {
            mdns_page_offset = 0U;
            mdns_result_count = 0U;
            memset(&mdns_scan_status, 0, sizeof(mdns_scan_status));
            mdns_scan_error = c5_spi_link_start_mdns_scan(&mdns_scan_status);
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (wifi_mode == UI_WIFI_MODE_DIAGNOSTICS) {
        if (y >= 360U && y < 408U && x >= 8U && x < 152U) {
            wifi_mode = UI_WIFI_MODE_CONNECTION;
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (y >= 360U && y < 408U && x >= 168U && x < 312U &&
            network_diagnostics_status.state != CERBERUS_NETWORK_DIAGNOSTICS_RUNNING &&
            (c5_status.capabilities & CERBERUS_C5_CAP_MDNS_DISCOVERY) != 0U) {
            memset(&mdns_scan_status, 0, sizeof(mdns_scan_status));
            mdns_page_offset = 0U;
            mdns_result_count = 0U;
            mdns_scan_error = c5_spi_link_start_mdns_scan(&mdns_scan_status);
            wifi_mode = UI_WIFI_MODE_MDNS;
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (wifi_mode == UI_WIFI_MODE_CONNECTION) {
        if (x >= 8 && x < 312 && y >= 302 && y < 342 &&
            wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED &&
            (c5_status.capabilities & CERBERUS_C5_CAP_NETWORK_SCAN)) {
            net_enter(); wifi_mode = UI_WIFI_MODE_NETWORK_SCAN;
            ESP_ERROR_CHECK(draw_detail_page()); return;
        }
        if (x >= 8U && x < 152U && y >= 360U && y < 408U) {
            if (wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING ||
                wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
                wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING) {
                wifi_connection_error = c5_spi_link_disconnect_wifi(&wifi_connection_status);
                memset(&wifi_dns_status, 0, sizeof(wifi_dns_status));
                wifi_dns_error = ESP_ERR_INVALID_STATE;
            } else {
                wifi_mode = UI_WIFI_MODE_TARGET;
            }
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (x >= 168U && x < 312U && y >= 360U && y < 408U &&
            wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED &&
            (c5_status.capabilities & CERBERUS_C5_CAP_NETWORK_DIAGNOSTICS) != 0U) {
            memset(&network_diagnostics_status, 0, sizeof(network_diagnostics_status));
            network_diagnostics_error = c5_spi_link_start_network_diagnostics(
                &network_diagnostics_status);
            wifi_mode = UI_WIFI_MODE_DIAGNOSTICS;
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (wifi_mode == UI_WIFI_MODE_DETAIL) {
        if (x >= 8U && x < 152U && y >= 370U && y < 412U &&
            ui_config.settings_available && wifi_selected_result.ssid[0] != '\0') {
            bool trusted = false;
            wifi_trust_error = cerberus_settings_toggle_wifi_trusted(
                wifi_selected_result.ssid,
                wifi_selected_result.bssid,
                wifi_selected_result.security,
                &trusted);
            ESP_LOGI(TAG, "Wi-Fi detail trusted=%d error=%s", trusted, esp_err_to_name(wifi_trust_error));
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (x >= 168U && x < 312U && y >= 370U && y < 412U &&
            wifi_selected_result.ssid[0] != '\0') {
            wifi_mode = UI_WIFI_MODE_TARGET;
            wifi_target_connect();
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (wifi_mode == UI_WIFI_MODE_LIST && y >= UI_WIFI_LIST_Y && y < UI_WIFI_CONTROLS_Y &&
        wifi_scan_status.state == CERBERUS_WIFI_SCAN_COMPLETE) {
        size_t row = (y - UI_WIFI_LIST_Y) / UI_WIFI_ROW_HEIGHT;
        if (row < wifi_result_count) {
            wifi_selected_result = wifi_results[row];
            wifi_relation_bind_selection(false);
            wifi_selection_valid = true;
            wifi_selected_index = (uint8_t)(wifi_page_offset + row);
            wifi_analysis_generation = 0;
            wifi_analysis_error = analyze_selected_wifi_ssid();
            wifi_trust_error = ESP_OK;
            wifi_target_finish_selection();
            ESP_ERROR_CHECK(draw_current_page());
        }
        return;
    }

    if (y < UI_WIFI_CONTROLS_Y || y >= UI_WIFI_CONTROLS_Y + 42U) {
        return;
    }

    if (wifi_mode == UI_WIFI_MODE_STATS) {
        if (x >= 8U && x < 76U) {
            wifi_mode = UI_WIFI_MODE_DEFENSE;
            (void)refresh_wifi_defense_survey();
        } else if (x >= 84U && x < 152U) {
            wifi_mode = UI_WIFI_MODE_CHANNELS_24;
            wifi_channel_survey_start_ui();
        } else if (x >= 160U && x < 228U) {
            (void)export_wifi_radio_stats();
        } else if (x >= 236U && x < 312U) {
            wifi_mode = UI_WIFI_MODE_LIST;
            wifi_scan_error = load_wifi_results();
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (wifi_mode == UI_WIFI_MODE_DEFENSE) {
        if (x >= 8U && x < 76U) {
            if (wifi_defense_page_offset >= UI_WIFI_VISIBLE_ROWS) {
                wifi_defense_page_offset -= UI_WIFI_VISIBLE_ROWS;
            }
        } else if (x >= 84U && x < 152U) {
            if (wifi_defense_page_offset + UI_WIFI_VISIBLE_ROWS < wifi_defense_loaded_count) {
                wifi_defense_page_offset += UI_WIFI_VISIBLE_ROWS;
            }
        } else if (x >= 160U && x < 228U) {
            (void)export_wifi_defense_survey();
        } else if (x >= 236U && x < 312U) {
            live_toggle_pause();
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (wifi_mode == UI_WIFI_MODE_PACKET_MONITOR) {
        if (y < UI_WIFI_CONTROLS_Y || y >= UI_WIFI_CONTROLS_Y + 42U)
            return;
        if (x >= 8U && x < 76U) packet_monitor_step_channel(-1);
        else if (x >= 84U && x < 152U) packet_monitor_step_channel(1);
        else if (x >= 160U && x < 228U) {
            bool was_running = packet_monitor_status.state ==
                CERBERUS_WIFI_PACKET_MONITOR_RUNNING;
            if (!packet_monitor_stop_ui()) return;
            if (was_running) {
                wifi_mode = packet_monitor_channel <= 14U ?
                    UI_WIFI_MODE_CHANNELS_24 : UI_WIFI_MODE_CHANNELS_5;
                wifi_channel_survey_start_ui();
            } else {
                packet_monitor_last_poll = 0U;
                packet_monitor_start_ui();
            }
        } else if (x >= 236U && x < 312U) {
            if (!packet_monitor_stop_ui()) return;
            wifi_mode = packet_monitor_from_target ?
                UI_WIFI_MODE_TARGET : UI_WIFI_MODE_LIST;
            if (wifi_mode == UI_WIFI_MODE_LIST) wifi_scan_error = load_wifi_results();
        } else return;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (wifi_mode != UI_WIFI_MODE_LIST) {
        if (x >= 8U && x < 76U) {
            if (wifi_mode != UI_WIFI_MODE_CHANNELS_24) {
                if (!wifi_channel_survey_stop_ui()) return;
                wifi_mode = UI_WIFI_MODE_CHANNELS_24;
            } else if (wifi_channel_survey_status.state != CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING) {
                wifi_channel_survey_attempted = false;
            }
            wifi_channel_survey_start_ui();
        } else if (x >= 84U && x < 152U) {
            if (wifi_mode != UI_WIFI_MODE_CHANNELS_5) {
                if (!wifi_channel_survey_stop_ui()) return;
                wifi_mode = UI_WIFI_MODE_CHANNELS_5;
            } else if (wifi_channel_survey_status.state != CERBERUS_WIFI_CHANNEL_SURVEY_RUNNING) {
                wifi_channel_survey_attempted = false;
            }
            wifi_channel_survey_start_ui();
        } else if (x >= 160U && x < 228U) {
            if (c5_status.firmware_version >= CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE) {
                uint8_t pages = (uint8_t)((wifi_channel_survey_status.result_count +
                    UI_WIFI_VISIBLE_ROWS - 1U) / UI_WIFI_VISIBLE_ROWS);
                if (pages > 0U) wifi_channel_survey_page =
                    (uint8_t)((wifi_channel_survey_page + 1U) % pages);
                wifi_channel_survey_last_poll = 0U;
                wifi_channel_survey_refresh((uint32_t)(esp_timer_get_time() / 1000LL));
            } else reset_wifi_channel_history_to_current();
        } else if (x >= 236U && x < 312U) {
            if (!wifi_channel_survey_stop_ui()) return;
            wifi_mode = UI_WIFI_MODE_LIST;
            wifi_scan_error = load_wifi_results();
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (x < 76U) {
        if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE || wifi_page_offset == 0U) {
            return;
        }
        wifi_page_offset = wifi_page_offset >= UI_WIFI_VISIBLE_ROWS
            ? wifi_page_offset - UI_WIFI_VISIBLE_ROWS
            : 0U;
        wifi_scan_error = load_wifi_results();
    } else if (x >= 84U && x < 152U) {
        if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
            wifi_page_offset + UI_WIFI_VISIBLE_ROWS >= wifi_scan_status.stored_count) {
            return;
        }
        wifi_page_offset += UI_WIFI_VISIBLE_ROWS;
        wifi_scan_error = load_wifi_results();
    } else if (x >= 160U && x < 228U) {
        if (wifi_scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE) {
            return;
        }
        wifi_mode = UI_WIFI_MODE_STATS;
        if (wifi_channel_generation != wifi_scan_status.generation) {
            wifi_channel_error = rebuild_wifi_channel_summary();
        }
        (void)refresh_wifi_radio_stats();
    } else if (x >= 236U) {
        wifi_scan_checked = true;
        if (wifi_scan_status.state == CERBERUS_WIFI_SCAN_RUNNING) {
            wifi_scan_error = c5_spi_link_stop_wifi_scan(&wifi_scan_status);
        } else {
            wifi_mode = UI_WIFI_MODE_LIST;
            wifi_page_offset = 0;
            wifi_result_count = 0;
            wifi_channel_generation = 0;
            wifi_channel_error = ESP_ERR_INVALID_STATE;
            wifi_analysis_generation = 0;
            wifi_analysis_error = ESP_ERR_INVALID_STATE;
            wifi_selection_valid = false;
            wifi_radio_stats_checked = false;
            wifi_radio_stats_error = ESP_ERR_INVALID_STATE;
            memset(&wifi_radio_stats, 0, sizeof(wifi_radio_stats));
            wifi_defense_checked = false;
            wifi_defense_error = ESP_ERR_INVALID_STATE;
            wifi_defense_loaded_count = 0U;
            wifi_defense_page_offset = 0U;
            memset(&wifi_defense_status, 0, sizeof(wifi_defense_status));
            memset(wifi_defense_results, 0, sizeof(wifi_defense_results));
            wifi_export_message[0] = '\0';
            wifi_scan_error = c5_spi_link_start_wifi_scan(&wifi_scan_status);
        }
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static bool ble_attack_capture_selected_ad(uint8_t index)
{
    ble_attack_capture_valid = false;
    cerberus_c5_ble_advertisement_t advertisement = {0};
    esp_err_t error = ESP_OK;
    if (ble_selected_raw_frozen) advertisement = ble_findmy_selected.advertisement;
    else error = cerberus_s3_ble_scanner_get_advertisement(
        ble_scan_status.generation, index, &advertisement);
    if (error != ESP_OK ||
        memcmp(advertisement.address, ble_selected_result.address, 6U) != 0 ||
        advertisement.address_type != ble_selected_result.address_type) {
        snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
            "SCAN AGAIN / RESULT CHANGED");
        return false;
    }
    if ((advertisement.address_type != 1U && advertisement.address_type != 3U) ||
        (advertisement.address[5] & 0xC0U) != 0xC0U) {
        snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
            "STATIC RANDOM ADDRESS REQUIRED");
        return false;
    }
    if ((advertisement.flags & (CERBERUS_BLE_ADVERTISEMENT_TRUNCATED |
         CERBERUS_BLE_ADVERTISEMENT_MALFORMED)) != 0U ||
        !cerberus_ble_attack_ad_valid(advertisement.data, advertisement.length)) {
        snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
            "COMPLETE RAW AD REQUIRED");
        return false;
    }
    memset(&ble_attack_capture, 0, sizeof(ble_attack_capture));
    memcpy(ble_attack_capture.address, advertisement.address, 6U);
    ble_attack_capture.address_type = advertisement.address_type;
    ble_attack_capture.length = advertisement.length;
    memcpy(ble_attack_capture.data, advertisement.data, advertisement.length);
    ble_attack_capture_valid = true;
    ble_attack_capture_message[0] = '\0';
    return true;
}

static cerberus_findmy_profile_t selected_findmy_profile(uint8_t scan_index,
    const cerberus_c5_ble_result_t *peer, uint32_t *observed_ms)
{
    if (observed_ms) *observed_ms = 0U;
    if (!peer || (peer->flags & CERBERUS_BLE_RESULT_CONNECTABLE) == 0U)
        return CERBERUS_FINDMY_PROFILE_NONE;
    cerberus_c5_ble_advertisement_t advertisement = {0};
    uint32_t raw_observed_ms = 0U;
    if (ble_selected_raw_frozen) {
        advertisement = ble_findmy_selected.advertisement;
        raw_observed_ms = ble_findmy_selected.observed_ms;
        if (ble_selected_original_time64_valid) {
            uint64_t now_ms = (uint64_t)esp_timer_get_time() / 1000U;
            if (now_ms < ble_selected_original_ms64 ||
                now_ms - ble_selected_original_ms64 > CERBERUS_FINDMY_SELECTED_MAX_AGE_MS)
                return CERBERUS_FINDMY_PROFILE_NONE;
        }
    } else if (cerberus_s3_ble_scanner_get_advertisement(ble_scan_status.generation,
            scan_index, &advertisement) != ESP_OK ||
        cerberus_s3_ble_scanner_get_advertisement_time(
            ble_scan_status.generation, scan_index, &raw_observed_ms) != ESP_OK) {
        return CERBERUS_FINDMY_PROFILE_NONE;
    }
    if ((advertisement.flags & (CERBERUS_BLE_ADVERTISEMENT_TRUNCATED |
            CERBERUS_BLE_ADVERTISEMENT_MALFORMED)) != 0U)
        return CERBERUS_FINDMY_PROFILE_NONE;
    /* Raw capture is one original ADV. Scan-response name/UUID enrichment is
     * never promoted into a sound profile. */
    cerberus_findmy_profile_t profile = cerberus_findmy_ui_raw_profile(peer->address,
        peer->address_type, advertisement.address,
        advertisement.address_type, advertisement.event_type,
        advertisement.flags, advertisement.data, advertisement.length);
    if (profile != CERBERUS_FINDMY_PROFILE_NONE && observed_ms)
        *observed_ms = raw_observed_ms;
    return profile;
}

static void ble_sound_open(bool all)
{
    ble_sound_all = all;
    ble_sound_return_page = all ? UI_PAGE_FIELD_MENU : UI_PAGE_BLE;
    ble_sound_running = !all;
    ble_sound_scan_owned = false;
    ble_sound_attempt_owned = false;
    ble_sound_selected_started = false;
    ble_sound_stop_pending = false;
    ble_sound_exit_pending = false;
    ble_sound_scan_generation = 0U;
    ble_sound_last_scan_start_ms = 0U;
    ble_sound_busy_since_ms = 0U;
    ble_sound_stop_started_ms = 0U;
    ble_sound_sweeps = ble_sound_attempts = ble_sound_accepted = 0U;
    ble_sound_responses = ble_sound_reported = ble_sound_errors = 0U;
    ble_sound_error = ESP_OK;
    memset(&ble_sound_status, 0, sizeof(ble_sound_status));
    memset(&ble_sound_selection, 0, sizeof(ble_sound_selection));
    if (ble_sound_queue) {
        cerberus_findmy_queue_stop(ble_sound_queue);
        heap_caps_free(ble_sound_queue);
        ble_sound_queue = NULL;
    }
    if (all) {
        ble_sound_queue = heap_caps_calloc(1U, sizeof(*ble_sound_queue),
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (ble_sound_queue) {
            cerberus_findmy_queue_init(ble_sound_queue);
            cerberus_findmy_queue_start(ble_sound_queue);
        } else {
            ble_sound_error = ESP_ERR_NO_MEM;
        }
    } else {
        ble_sound_peer = ble_selected_result;
        ble_sound_profile = ble_selected_sound_profile;
        ble_sound_selected_scan_ms = ble_selected_scan_ms;
    }
    open_tool_page(UI_PAGE_BLE_SOUND);
}

#include "s3_findmy_monitor_ui.h"

static void handle_ble_tap(uint16_t x, uint16_t y)
{
    if (ble_mode == UI_BLE_MODE_FINDMY_MONITOR) {
        ble_findmy_monitor_tap(x, y);
        return;
    }
    if (live_is_view()) { handle_live_scan_tap(x, y); return; }
    if (ble_mode == UI_BLE_MODE_ANALYZER) {
        if (y >= 350U && y < 400U && x >= 40U && x < 280U &&
            !ble_analyzer_exit_pending) {
            if (ble_analyzer_error != ESP_OK) {
                ble_analyzer_error = ESP_OK;
                ble_analyzer_retry_requested = true;
                ble_analyzer_start_pending = true;
                ble_analyzer_start_requested_ms =
                    (uint32_t)(esp_timer_get_time() / 1000LL);
            } else {
                ble_analyzer_exit_pending = true;
                ble_analyzer_exit_started_ms =
                    (uint32_t)(esp_timer_get_time() / 1000LL);
            }
            ble_analyzer_last_tick_ms = 0U;
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (ble_mode == UI_BLE_MODE_FOX_HUNT) {
        if (y >= 350U && y < 400U && x >= 40U && x < 280U &&
            !ble_fox_hunt_exit_pending) {
            if (ble_fox_hunt_error != ESP_OK) {
                ble_fox_hunt_error = ESP_OK;
                ble_fox_hunt_retry_requested = true;
            }
            else {
                ble_fox_hunt_exit_pending = true;
                ble_fox_hunt_exit_started_ms =
                    (uint32_t)(esp_timer_get_time() / 1000LL);
            }
            ble_fox_hunt_last_tick_ms = 0U;
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (ble_mode == UI_BLE_MODE_MENU) {
        int item = sentinel_hub_hit_test(SENTINEL_HUB_BLE, x, y);
        if (item == 6) { open_learning_lesson(2); return; }
        if (item == 0) {
            ble_mode = UI_BLE_MODE_LIST;
            (void)refresh_ble_scan_status();
        } else if (item == 1) {
            ble_mode = ble_selection_valid ? UI_BLE_MODE_DETAIL : UI_BLE_MODE_LIST;
            if (!ble_selection_valid) (void)refresh_ble_scan_status();
        } else if (item == 2) {
            (void)refresh_ble_connection_status();
            if (!ble_selection_valid) {
                ble_mode = UI_BLE_MODE_LIST;
                (void)refresh_ble_scan_status();
            } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
                ble_service_checked && ble_service_error == ESP_OK &&
                ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
                ble_service_page_offset = 0U;
                ble_service_error = load_ble_services();
                ble_mode = ble_service_error == ESP_OK
                    ? UI_BLE_MODE_SERVICES
                    : UI_BLE_MODE_DETAIL;
            } else {
                ble_mode = UI_BLE_MODE_DETAIL;
            }
        } else if (item == 3) {
            if (!ble_selection_valid) {
                ble_mode = UI_BLE_MODE_LIST;
                (void)refresh_ble_scan_status();
            } else {
                (void)refresh_ble_connection_status();
                ble_mode = UI_BLE_MODE_ENVIRONMENT;
                if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
                    ble_connection_matches_selected()) {
                    begin_ble_environment_discovery();
                } else {
                    reset_ble_environment_viewer();
                }
            }
        } else if (item == 4) {
            ble_mode = UI_BLE_MODE_ANALYZER;
            ble_session_exit_page = UI_PAGE_BLE;
            ble_analyzer_start_pending = true;
            ble_analyzer_started = false;
            ble_analyzer_exit_pending = false;
            ble_analyzer_retry_requested = false;
            ble_analyzer_error = ESP_OK;
            ble_analyzer_last_tick_ms = 0U;
            ble_analyzer_last_draw_ms = 0U;
            ble_analyzer_start_requested_ms =
                (uint32_t)(esp_timer_get_time() / 1000LL);
        } else {
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ble_mode == UI_BLE_MODE_ENVIRONMENT) {
        if (x < 40U || x >= 280U || y < 330U || y >= 382U) {
            return;
        }
        bool connected = ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
            ble_connection_matches_selected();
        if (!connected) {
            ble_mode = UI_BLE_MODE_DETAIL;
        } else if (ble_environment_refresh_active) {
            return;
        } else if (ble_environment_characteristics_loaded &&
            ble_environment_reading_count > 0U) {
            begin_ble_environment_reads();
        } else {
            ble_service_checked = false;
            begin_ble_environment_discovery();
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ble_mode == UI_BLE_MODE_VALUE) {
        if (y >= 342U && y < 388U &&
            (ble_selected_characteristic.properties & 0x02U) != 0U &&
            ble_read_status.state != CERBERUS_BLE_GATT_READ_RUNNING) {
            begin_ble_characteristic_read();
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (ble_mode == UI_BLE_MODE_CHARACTERISTICS) {
        if (y >= UI_WIFI_LIST_Y && y < UI_WIFI_CONTROLS_Y &&
            ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
            size_t row = (y - UI_WIFI_LIST_Y) / UI_WIFI_ROW_HEIGHT;
            if (row < ble_characteristic_count) {
                ble_selected_characteristic = ble_characteristics[row];
                memset(&ble_read_status, 0, sizeof(ble_read_status));
                ble_read_value_length = 0;
                ble_read_checked = false;
                if ((ble_selected_characteristic.properties & 0x02U) != 0U) {
                    begin_ble_characteristic_read();
                } else {
                    ble_read_error = ESP_ERR_NOT_SUPPORTED;
                }
                ble_mode = UI_BLE_MODE_VALUE;
                ESP_ERROR_CHECK(draw_detail_page());
            }
            return;
        }
        if (y < UI_WIFI_CONTROLS_Y || y >= UI_WIFI_CONTROLS_Y + 42U ||
            ble_characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
            return;
        }
        if (x < 156U) {
            if (ble_characteristic_page_offset == 0U) {
                return;
            }
            ble_characteristic_page_offset =
                ble_characteristic_page_offset >= UI_BLE_CHARACTERISTIC_VISIBLE_ROWS
                ? ble_characteristic_page_offset - UI_BLE_CHARACTERISTIC_VISIBLE_ROWS
                : 0U;
        } else if (x >= 164U) {
            if (ble_characteristic_page_offset + UI_BLE_CHARACTERISTIC_VISIBLE_ROWS >=
                ble_characteristic_status.stored_count) {
                return;
            }
            ble_characteristic_page_offset += UI_BLE_CHARACTERISTIC_VISIBLE_ROWS;
        } else {
            return;
        }
        ble_characteristic_error = load_ble_characteristics();
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ble_mode == UI_BLE_MODE_SERVICES) {
        if (y >= UI_WIFI_LIST_Y && y < UI_WIFI_CONTROLS_Y) {
            size_t row = (y - UI_WIFI_LIST_Y) / UI_WIFI_ROW_HEIGHT;
            if (row < ble_service_count) {
                ble_selected_service = ble_services[row];
                ble_characteristic_page_offset = 0;
                ble_characteristic_count = 0;
                ble_characteristic_error = cerberus_s3_ble_central_start_characteristic_discovery(
                    &ble_selected_service,
                    &ble_characteristic_status);
                ble_characteristic_checked = true;
                ble_read_checked = false;
                if (ble_characteristic_error == ESP_OK &&
                    ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
                    ble_characteristic_error = load_ble_characteristics();
                } else if (ble_characteristic_error == ESP_OK &&
                    ble_characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING) {
                    ble_characteristic_error = ESP_ERR_INVALID_STATE;
                }
                ble_mode = UI_BLE_MODE_CHARACTERISTICS;
                ESP_ERROR_CHECK(draw_detail_page());
            }
            return;
        }
        if (y < UI_WIFI_CONTROLS_Y || y >= UI_WIFI_CONTROLS_Y + 42U) {
            return;
        }
        if (x < 156U) {
            if (ble_service_page_offset == 0U) {
                return;
            }
            ble_service_page_offset = ble_service_page_offset >= UI_BLE_SERVICE_VISIBLE_ROWS
                ? ble_service_page_offset - UI_BLE_SERVICE_VISIBLE_ROWS
                : 0U;
        } else if (x >= 164U) {
            if (ble_service_page_offset + UI_BLE_SERVICE_VISIBLE_ROWS >=
                ble_service_status.stored_count) {
                return;
            }
            ble_service_page_offset += UI_BLE_SERVICE_VISIBLE_ROWS;
        } else {
            return;
        }
        ble_service_error = load_ble_services();
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (ble_mode == UI_BLE_MODE_DETAIL) {
        if (x >= 24U && x < 296U && y >= 390U && y < 414U) {
            if ((ui_capability_map & UI_CAP_BLE_GATT) != 0U &&
                (ble_selected_result.flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U &&
                ble_selected_sound_profile != CERBERUS_FINDMY_PROFILE_NONE &&
                !ble_connection_is_active()) {
                if (!cerberus_findmy_ui_fresh(ble_selected_scan_ms,
                        (uint32_t)(esp_timer_get_time() / 1000LL))) {
                    set_ble_action_message("SCAN AGAIN / ADDRESS MAY ROTATE", UI_MUTED);
                    ESP_ERROR_CHECK(draw_detail_page());
                    return;
                }
                /* Revalidate this generation's original ADV before a write. */
                uint32_t observed_ms = 0U;
                cerberus_findmy_profile_t profile = selected_findmy_profile(
                    ble_selected_scan_index, &ble_selected_result, &observed_ms);
                if (profile == ble_selected_sound_profile &&
                    observed_ms == ble_selected_scan_ms) {
                    ble_sound_open(false);
                    return;
                }
                set_ble_action_message("SCAN AGAIN / SOUND HINT CHANGED", UI_MUTED);
                ESP_ERROR_CHECK(draw_detail_page());
            }
            return;
        }
        if (y >= 286U && y < 324U) {
            if (x < 154U) {
                if (ble_connection_is_active()) {
                    set_ble_action_message("DISCONNECT BEFORE TRACKING", UI_MUTED);
                } else {
                    esp_err_t track_error = cerberus_s3_ble_scanner_track_begin(
                        ble_selected_result.address,
                        ble_selected_result.address_type);
                    if (track_error == ESP_OK) {
                        ble_session_exit_page = UI_PAGE_BLE;
                        ble_fox_hunt_exit_pending = false;
                        ble_fox_hunt_exit_started_ms = 0U;
                        ble_fox_hunt_retry_requested = false;
                        ble_fox_hunt_error = ESP_OK;
                        ble_fox_hunt_last_tick_ms = 0U;
                        ble_fox_hunt_last_draw_ms = 0U;
                        ble_mode = UI_BLE_MODE_FOX_HUNT;
                    } else {
                        set_ble_action_message("S3 BLE SCANNER BUSY", UI_MUTED);
                    }
                }
                ESP_ERROR_CHECK(draw_detail_page());
                return;
            }
            bool selected_connected =
                ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
                ble_connection_matches_selected();
            bool current_results = ble_service_checked && ble_service_error == ESP_OK &&
                ble_service_status.connection_generation == ble_connection_status.generation;
            if (!selected_connected) {
                set_ble_action_message("CONNECT THIS DEVICE FIRST", UI_MUTED);
                ESP_ERROR_CHECK(draw_detail_page());
                return;
            }
            if (current_results &&
                ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
                ble_service_page_offset = 0;
                ble_service_error = load_ble_services();
                if (ble_service_error == ESP_OK) {
                    ble_mode = UI_BLE_MODE_SERVICES;
                }
            } else if (!current_results ||
                ble_service_status.state != CERBERUS_BLE_GATT_SERVICE_RUNNING) {
                ble_service_page_offset = 0;
                ble_service_count = 0;
                ble_service_error = cerberus_s3_ble_central_start_service_discovery(
                    &ble_service_status);
                ble_service_checked = true;
                ble_characteristic_checked = false;
                ble_read_checked = false;
                set_ble_action_message(
                    ble_service_error == ESP_OK
                        ? "SERVICE DISCOVERY REQUESTED"
                        : "SERVICE DISCOVERY COMMAND FAILED",
                    ble_service_error == ESP_OK ? UI_GOOD : UI_BAD);
            }
            ESP_ERROR_CHECK(draw_detail_page());
            return;
        }
        if (y < 330U || y >= 384U) {
            return;
        }
        if (x < 154U) {
            if (!ui_config.settings_available) {
                set_ble_action_message("FAVORITES STORAGE UNAVAILABLE", UI_BAD);
                ESP_ERROR_CHECK(draw_detail_page());
                return;
            }
            if (ble_connection_is_active() && ble_connection_matches_selected()) {
                set_ble_action_message("DISCONNECT BEFORE REMOVING FAVORITE", UI_MUTED);
                ESP_ERROR_CHECK(draw_detail_page());
                return;
            }
            bool favorite = false;
            esp_err_t err = cerberus_settings_toggle_ble_favorite(
                ble_selected_result.address,
                ble_selected_result.address_type,
                ble_selected_result.name,
                &favorite);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "BLE favorite update failed: %s", esp_err_to_name(err));
                set_ble_action_message("FAVORITE SAVE FAILED", UI_BAD);
            } else {
                ESP_LOGI(TAG, "BLE detail favorite=%d", favorite);
                if (!favorite) {
                    set_ble_action_message("REMOVED FROM FAVORITES", UI_MUTED);
                } else if ((ble_selected_result.flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U) {
                    set_ble_action_message("SAVED - TAP CONNECT", UI_GOOD);
                } else {
                    set_ble_action_message("SAVED - ADVERTISEMENT ONLY", UI_MUTED);
                }
            }
        } else if (x >= 166U) {
            ble_handle_connection_action();
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (y >= 48U && y < 80U && x >= 8U && x < 312U &&
        ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE) {
        ble_filter = (cerberus_s3_ble_filter_t)
            (((unsigned)ble_filter + 1U) % CERBERUS_S3_BLE_FILTER_COUNT);
        ble_page_offset = 0U;
        ble_scan_error = load_ble_results();
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (y >= UI_WIFI_LIST_Y && y < UI_WIFI_CONTROLS_Y &&
        ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE) {
        size_t row = (y - UI_WIFI_LIST_Y) / UI_WIFI_ROW_HEIGHT;
        if (row < ble_result_count) {
            cerberus_s3_ble_focus_t focus = {0};
            esp_err_t focus_error = cerberus_s3_ble_scanner_get_focus(
                ble_scan_status.generation, ble_result_indexes[row], &focus);
            if (focus_error != ESP_OK) {
                ble_scan_error = refresh_ble_scan_status();
                ESP_ERROR_CHECK(draw_detail_page());
                return;
            }
            ble_selected_from_monitor = false;
            ble_selected_raw_frozen = false;
            ble_selected_original_time64_valid = false;
            ble_selected_result = ble_results[row];
            ble_selected_focus = focus;
            ble_selected_scan_index = ble_result_indexes[row];
            ble_selected_sound_profile = selected_findmy_profile(
                ble_selected_scan_index, &ble_selected_result,
                &ble_selected_scan_ms);
            ble_selection_valid = true;
            ble_attack_capture_valid = false;
            if (ble_attack_selecting) {
                ble_attack_selecting = false;
                (void)ble_attack_capture_selected_ad(ble_result_indexes[row]);
                open_tool_page(UI_PAGE_BLE_ATTACK);
                return;
            }
            ble_mode = UI_BLE_MODE_DETAIL;
            ble_connection_checked = false;
            ble_service_checked = false;
            ble_characteristic_checked = false;
            ble_read_checked = false;
            set_ble_action_message("", UI_MUTED);
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (y < UI_WIFI_CONTROLS_Y || y >= UI_WIFI_CONTROLS_Y + 42U) {
        return;
    }
    if (ble_filter == CERBERUS_S3_BLE_FILTER_FIND_MY &&
        ble_scan_status.state == CERBERUS_BLE_SCAN_COMPLETE && !ble_attack_selecting) {
        if (x >= 160U && x < 236U) {
            if (!ble_connection_is_active()) ble_findmy_monitor_open();
        } else if (x < 76U) {
            ble_page_offset = ble_page_offset >= UI_WIFI_VISIBLE_ROWS ?
                ble_page_offset - UI_WIFI_VISIBLE_ROWS : 0U;
            ble_scan_error = load_ble_results();
        } else if (x >= 84U && x < 152U) {
            if (ble_page_offset + UI_WIFI_VISIBLE_ROWS < ble_filtered_count)
                ble_page_offset += UI_WIFI_VISIBLE_ROWS;
            ble_scan_error = load_ble_results();
        } else if (x >= 244U && !ble_connection_is_active()) {
            ble_selected_from_monitor = false;
            ble_selection_valid = false;
            ble_selected_sound_profile = CERBERUS_FINDMY_PROFILE_NONE;
            ble_page_offset = ble_result_count = 0U;
            ble_scan_error = cerberus_s3_ble_scanner_start_active(&ble_scan_status);
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (x < 92U) {
        if (ble_scan_status.state != CERBERUS_BLE_SCAN_COMPLETE || ble_page_offset == 0U) {
            return;
        }
        ble_page_offset = ble_page_offset >= UI_WIFI_VISIBLE_ROWS
            ? ble_page_offset - UI_WIFI_VISIBLE_ROWS
            : 0U;
        ble_scan_error = load_ble_results();
    } else if (x >= 98U && x < 182U) {
        if (ble_scan_status.state != CERBERUS_BLE_SCAN_COMPLETE ||
            ble_page_offset + UI_WIFI_VISIBLE_ROWS >= ble_filtered_count) {
            return;
        }
        ble_page_offset += UI_WIFI_VISIBLE_ROWS;
        ble_scan_error = load_ble_results();
    } else if (x >= 188U) {
        if (ble_connection_checked && ble_connection_is_active()) {
            return;
        }
        ble_scan_checked = true;
        if (ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING) {
            ble_scan_error = cerberus_s3_ble_scanner_stop(&ble_scan_status);
        } else {
            ble_filter = CERBERUS_S3_BLE_FILTER_ALL;
            ble_selected_from_monitor = false;
            ble_page_offset = 0;
            ble_result_count = 0;
            ble_selection_valid = false;
            ble_selected_sound_profile = CERBERUS_FINDMY_PROFILE_NONE;
            memset(&ble_selected_focus, 0, sizeof(ble_selected_focus));
            ble_attack_capture_valid = false;
            ble_scan_error = cerberus_s3_ble_scanner_start_active(&ble_scan_status);
        }
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_fox_hunt_finish_exit(bool asleep, bool host_held)
{
    cerberus_s3_ble_scanner_track_end();
    ble_fox_hunt_exit_pending = false;
    ble_fox_hunt_retry_requested = false;
    ble_fox_hunt_exit_started_ms = 0U;
    ble_fox_hunt_error = ESP_OK;
    ble_mode = UI_BLE_MODE_DETAIL;
    if (host_held)
        set_ble_action_message("BLE HOST HELD / RESET REQUIRED", UI_BAD);
    else
        set_ble_action_message("TRACKING STOPPED", UI_MUTED);
    ui_page_t exit_page = ble_session_exit_page;
    ble_session_exit_page = UI_PAGE_BLE;
    if (host_held) exit_page = UI_PAGE_BLE;
    if (exit_page == UI_PAGE_BLE_ATTACK) {
        ble_attack_selecting = false;
        open_tool_page(UI_PAGE_BLE_ATTACK);
        return;
    }
    if (exit_page != UI_PAGE_BLE) {
        return_to_launcher();
        return;
    }
    if (!asleep) ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_fox_hunt_tick(uint32_t now_ms, bool asleep)
{
    if (current_page != UI_PAGE_BLE || ble_mode != UI_BLE_MODE_FOX_HUNT) return;
    if (ble_fox_hunt_last_tick_ms != 0U &&
        (uint32_t)(now_ms - ble_fox_hunt_last_tick_ms) < 250U) return;
    ble_fox_hunt_last_tick_ms = now_ms;
    cerberus_ble_scan_status_payload_t status = {0};
    esp_err_t error = cerberus_s3_ble_scanner_get_status(&status);
    if (error != ESP_OK) {
        ble_fox_hunt_error = error;
        if (ble_fox_hunt_exit_pending &&
            cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_SCANNER)
            (void)cerberus_s3_ble_scanner_stop(&status);
    } else {
        ble_scan_status = status;
        ble_scan_checked = true;
        if (ble_fox_hunt_exit_pending) {
            if (status.state == CERBERUS_BLE_SCAN_RUNNING) {
                error = cerberus_s3_ble_scanner_stop(&status);
                if (error != ESP_OK && error != ESP_ERR_INVALID_STATE)
                    ble_fox_hunt_error = error;
            }
        } else if (ble_fox_hunt_error == ESP_OK) {
            if (status.state == CERBERUS_BLE_SCAN_ERROR &&
                !ble_fox_hunt_retry_requested) {
                ble_fox_hunt_error = status.error_code != 0 ?
                    (esp_err_t)status.error_code : ESP_FAIL;
            } else if ((status.state == CERBERUS_BLE_SCAN_COMPLETE ||
                status.state == CERBERUS_BLE_SCAN_IDLE ||
                (status.state == CERBERUS_BLE_SCAN_ERROR &&
                    ble_fox_hunt_retry_requested)) &&
                cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_NONE) {
                error = cerberus_s3_ble_scanner_start(&status);
                if (error == ESP_OK) {
                    ble_scan_status = status;
                    ble_fox_hunt_retry_requested = false;
                } else if (error != ESP_ERR_INVALID_STATE) {
                    ble_fox_hunt_error = error;
                    ble_fox_hunt_retry_requested = false;
                }
            }
        }
    }
    if (ble_fox_hunt_exit_pending) {
        bool host_held = cerberus_s3_ble_host_owner() ==
            CERBERUS_S3_BLE_OWNER_SCANNER;
        bool deadline_reached = (uint32_t)(now_ms -
            ble_fox_hunt_exit_started_ms) >= 12000U;
        if (!host_held || deadline_reached) {
            ble_fox_hunt_finish_exit(asleep, host_held);
            return;
        }
    }
    if (!asleep && (ble_fox_hunt_last_draw_ms == 0U ||
        (uint32_t)(now_ms - ble_fox_hunt_last_draw_ms) >= UI_BLE_REFRESH_MS)) {
        ble_fox_hunt_last_draw_ms = now_ms;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void ble_analyzer_finish_exit(bool asleep, bool host_held)
{
    cerberus_s3_ble_scanner_analyzer_end();
    ble_analyzer_start_pending = false;
    ble_analyzer_started = false;
    ble_analyzer_exit_pending = false;
    ble_analyzer_retry_requested = false;
    ble_analyzer_error = ESP_OK;
    ble_analyzer_exit_started_ms = 0U;
    ble_mode = UI_BLE_MODE_MENU;
    if (host_held)
        set_ble_action_message("BLE HOST HELD / RESET REQUIRED", UI_BAD);
    ui_page_t exit_page = ble_session_exit_page;
    ble_session_exit_page = UI_PAGE_BLE;
    if (host_held) exit_page = UI_PAGE_BLE;
    if (exit_page == UI_PAGE_BLE_ATTACK) {
        ble_attack_selecting = false;
        open_tool_page(UI_PAGE_BLE_ATTACK);
        return;
    }
    if (exit_page != UI_PAGE_BLE) {
        return_to_launcher();
        return;
    }
    if (!asleep) ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_analyzer_tick(uint32_t now_ms, bool asleep)
{
    if (current_page != UI_PAGE_BLE || ble_mode != UI_BLE_MODE_ANALYZER) return;
    if (ble_analyzer_last_tick_ms != 0U &&
        (uint32_t)(now_ms - ble_analyzer_last_tick_ms) < 250U) return;
    ble_analyzer_last_tick_ms = now_ms;
    if (ble_analyzer_start_pending &&
        (uint32_t)(now_ms - ble_analyzer_start_requested_ms) >= 12000U) {
        ble_analyzer_start_pending = false;
        ble_analyzer_error = ESP_ERR_TIMEOUT;
    }
    cerberus_ble_scan_status_payload_t status = {0};
    esp_err_t error = cerberus_s3_ble_scanner_get_status(&status);
    if (error != ESP_OK) ble_analyzer_error = error;
    else {
        ble_scan_status = status;
        ble_scan_checked = true;
    }
    cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
    if (ble_analyzer_exit_pending) {
        if (error == ESP_OK && status.state == CERBERUS_BLE_SCAN_RUNNING) {
            esp_err_t stop_error = cerberus_s3_ble_scanner_stop(&status);
            if (stop_error != ESP_OK && stop_error != ESP_ERR_INVALID_STATE)
                ble_analyzer_error = stop_error;
        }
        bool host_held = owner == CERBERUS_S3_BLE_OWNER_SCANNER;
        if (!host_held ||
            (uint32_t)(now_ms - ble_analyzer_exit_started_ms) >= 12000U) {
            ble_analyzer_finish_exit(asleep, host_held);
            return;
        }
    } else if (error == ESP_OK && ble_analyzer_start_pending) {
        if (status.state == CERBERUS_BLE_SCAN_RUNNING) {
            (void)cerberus_s3_ble_scanner_stop(&status);
        } else if (owner == CERBERUS_S3_BLE_OWNER_NONE) {
            if (ble_analyzer_retry_requested) {
                cerberus_s3_ble_scanner_analyzer_end();
                ble_analyzer_started = false;
                ble_analyzer_retry_requested = false;
            }
            if (!ble_analyzer_started) {
                error = cerberus_s3_ble_scanner_analyzer_begin();
                if (error == ESP_OK) ble_analyzer_started = true;
            }
            if (ble_analyzer_started && error == ESP_OK)
                error = cerberus_s3_ble_scanner_start(&status);
            if (error == ESP_OK) {
                ble_scan_status = status;
                ble_analyzer_start_pending = false;
            } else if (error != ESP_ERR_INVALID_STATE) {
                ble_analyzer_error = error;
                ble_analyzer_start_pending = false;
            }
        } else if (owner != CERBERUS_S3_BLE_OWNER_SCANNER) {
            ble_analyzer_error = ESP_ERR_INVALID_STATE;
            ble_analyzer_start_pending = false;
        }
    } else if (error == ESP_OK && ble_analyzer_started &&
        ble_analyzer_error == ESP_OK) {
        cerberus_ble_analyzer_view_t view = {0};
        if (cerberus_s3_ble_scanner_analyzer_status(&view) != ESP_OK) {
            ble_analyzer_error = ESP_ERR_INVALID_STATE;
        } else if (status.state == CERBERUS_BLE_SCAN_ERROR) {
            ble_analyzer_error = status.error_code != 0 ?
                (esp_err_t)status.error_code : ESP_FAIL;
        } else if ((status.state == CERBERUS_BLE_SCAN_COMPLETE ||
            status.state == CERBERUS_BLE_SCAN_IDLE) &&
            owner == CERBERUS_S3_BLE_OWNER_NONE) {
            error = cerberus_s3_ble_scanner_start(&status);
            if (error == ESP_OK) ble_scan_status = status;
            else if (error != ESP_ERR_INVALID_STATE)
                ble_analyzer_error = error;
        }
    }
    if (!asleep && (ble_analyzer_last_draw_ms == 0U ||
        (uint32_t)(now_ms - ble_analyzer_last_draw_ms) >= UI_BLE_REFRESH_MS)) {
        ble_analyzer_last_draw_ms = now_ms;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static bool ble_sound_terminal(cerberus_s3_ble_sound_state_t state)
{
    return state == CERBERUS_S3_BLE_SOUND_COMPLETE ||
        state == CERBERUS_S3_BLE_SOUND_STOPPED ||
        state == CERBERUS_S3_BLE_SOUND_ERROR;
}

static void ble_sound_release_queue(void)
{
    if (!ble_sound_queue) return;
    cerberus_findmy_queue_stop(ble_sound_queue);
    heap_caps_free(ble_sound_queue);
    ble_sound_queue = NULL;
}

static esp_err_t ble_sound_collect_sweep(
    const cerberus_ble_scan_status_payload_t *status, uint32_t now_ms)
{
    if (!status || !ble_sound_queue ||
        status->stored_count > CERBERUS_BLE_SCAN_MAX_RESULTS)
        return ESP_ERR_INVALID_SIZE;
    cerberus_findmy_queue_begin_sweep(ble_sound_queue, now_ms);
    for (uint8_t index = 0U; index < status->stored_count; ++index) {
        cerberus_c5_ble_result_t peer = {0};
        cerberus_c5_ble_advertisement_t advertisement = {0};
        esp_err_t err = cerberus_s3_ble_scanner_get_result(
            status->generation, index, &peer);
        if (err != ESP_OK) return err;
        err = cerberus_s3_ble_scanner_get_advertisement(
            status->generation, index, &advertisement);
        if (err != ESP_OK) return err;
        if ((peer.flags & CERBERUS_BLE_RESULT_CONNECTABLE) == 0U ||
            (advertisement.flags & (CERBERUS_BLE_ADVERTISEMENT_TRUNCATED |
                CERBERUS_BLE_ADVERTISEMENT_MALFORMED)) != 0U ||
            cerberus_findmy_ui_raw_profile(peer.address,
                peer.address_type, advertisement.address,
                advertisement.address_type, advertisement.event_type,
                advertisement.flags, advertisement.data,
                advertisement.length) == CERBERUS_FINDMY_PROFILE_NONE)
            continue;
        /* The raw slot is one original advertisement. Scanner-added scan
         * response fields are intentionally excluded from classification. */
        (void)cerberus_findmy_queue_observe(ble_sound_queue, peer.address,
            peer.address_type, peer.rssi, advertisement.data,
            advertisement.length, true, false, now_ms);
    }
    ++ble_sound_sweeps;
    return ESP_OK;
}

static void ble_sound_finish_exit(bool asleep)
{
    ble_sound_release_queue();
    ble_sound_running = false;
    ble_sound_scan_owned = false;
    ble_sound_attempt_owned = false;
    ble_sound_stop_pending = false;
    ble_sound_exit_pending = false;
    if (ble_sound_return_page == UI_PAGE_BLE) {
        current_page = UI_PAGE_BLE;
        ble_mode = UI_BLE_MODE_DETAIL;
        if (!asleep) ESP_ERROR_CHECK(draw_detail_page());
    } else {
        current_page = UI_PAGE_FIELD_MENU;
        if (!asleep) ESP_ERROR_CHECK(draw_current_page());
    }
}

static void ble_sound_tick(uint32_t now_ms, bool asleep)
{
    if (current_page != UI_PAGE_BLE_SOUND) return;
    if (ble_sound_last_tick_ms != 0U &&
        (uint32_t)(now_ms - ble_sound_last_tick_ms) < 250U) return;
    ble_sound_last_tick_ms = now_ms;
    cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
    cerberus_ble_scan_status_payload_t scan = {0};
    if (ble_sound_scan_owned) {
        esp_err_t error = cerberus_s3_ble_scanner_get_status(&scan);
        if (error != ESP_OK) {
            ble_sound_error = error;
            ble_sound_running = false;
        }
    }
    if (ble_sound_attempt_owned) {
        esp_err_t error = cerberus_s3_ble_central_sound_get_status(
            &ble_sound_status);
        if (error != ESP_OK) {
            ble_sound_error = error;
            ble_sound_running = false;
        } else if (ble_sound_status.generation != ble_sound_action_generation) {
            ble_sound_error = ESP_ERR_INVALID_STATE;
            ble_sound_running = false;
        }
    }
    if (ble_sound_stop_pending) {
        if (ble_sound_scan_owned && scan.state == CERBERUS_BLE_SCAN_RUNNING)
            (void)cerberus_s3_ble_scanner_stop(&scan);
        if (ble_sound_attempt_owned &&
            !ble_sound_terminal(ble_sound_status.state))
            (void)cerberus_s3_ble_central_sound_stop(&ble_sound_status);
        owner = cerberus_s3_ble_host_owner();
        if (owner == CERBERUS_S3_BLE_OWNER_NONE ||
            (!ble_sound_scan_owned && !ble_sound_attempt_owned)) {
            ble_sound_scan_owned = false;
            ble_sound_attempt_owned = false;
            ble_sound_release_queue();
            ble_sound_stop_pending = false;
            if (ble_sound_exit_pending) {
                ble_sound_finish_exit(asleep);
                return;
            }
        } else if ((uint32_t)(now_ms - ble_sound_stop_started_ms) >= 70000U) {
            ble_sound_error = ESP_ERR_TIMEOUT;
        }
    } else if (ble_sound_running && ble_sound_error == ESP_OK) {
        owner = cerberus_s3_ble_host_owner();
        if (ble_sound_attempt_owned) {
            if (ble_sound_terminal(ble_sound_status.state) &&
                owner == CERBERUS_S3_BLE_OWNER_NONE) {
                bool accepted = ble_sound_status.write_accepted;
                if (accepted) ++ble_sound_accepted;
                if (ble_sound_status.response_received) ++ble_sound_responses;
                if (ble_sound_status.completion_reported) ++ble_sound_reported;
                if (ble_sound_status.state == CERBERUS_S3_BLE_SOUND_ERROR) {
                    ++ble_sound_errors;
                    if (!ble_sound_all) ble_sound_error =
                        ble_sound_status.error_code != 0 ?
                        (esp_err_t)ble_sound_status.error_code : ESP_FAIL;
                }
                if (ble_sound_all && ble_sound_queue)
                    (void)cerberus_findmy_queue_finish_attempt(ble_sound_queue,
                        ble_sound_selection.ticket,
                        ble_sound_status.state == CERBERUS_S3_BLE_SOUND_COMPLETE &&
                            accepted, now_ms);
                ble_sound_attempt_owned = false;
                if (!ble_sound_all) ble_sound_running = false;
            }
        } else if (!ble_sound_all) {
            if (!ble_sound_selected_started &&
                owner == CERBERUS_S3_BLE_OWNER_NONE) {
                ++ble_sound_attempts;
                esp_err_t error = cerberus_s3_ble_central_sound_start(
                    &ble_sound_peer, ble_sound_profile, &ble_sound_status);
                if (error == ESP_OK) {
                    ble_sound_action_generation = ble_sound_status.generation;
                    ble_sound_attempt_owned = true;
                    ble_sound_selected_started = true;
                } else {
                    ble_sound_error = error;
                    ble_sound_running = false;
                    ++ble_sound_errors;
                }
            } else if (owner != CERBERUS_S3_BLE_OWNER_NONE) {
                ble_sound_error = ESP_ERR_INVALID_STATE;
                ble_sound_running = false;
            }
        } else if (ble_sound_queue && ble_sound_scan_owned) {
            if (scan.state == CERBERUS_BLE_SCAN_COMPLETE &&
                scan.generation == ble_sound_scan_generation &&
                owner == CERBERUS_S3_BLE_OWNER_NONE) {
                ble_sound_scan_owned = false;
                esp_err_t error = ble_sound_collect_sweep(&scan, now_ms);
                if (error != ESP_OK) {
                    ble_sound_error = error;
                    ble_sound_running = false;
                } else if (cerberus_findmy_queue_claim_next(ble_sound_queue,
                        now_ms, &ble_sound_selection)) {
                    memset(&ble_sound_peer, 0, sizeof(ble_sound_peer));
                    memcpy(ble_sound_peer.address, ble_sound_selection.address, 6U);
                    ble_sound_peer.address_type = ble_sound_selection.address_type;
                    ble_sound_peer.rssi = ble_sound_selection.rssi;
                    ble_sound_peer.flags = CERBERUS_BLE_RESULT_CONNECTABLE;
                    ble_sound_profile = ble_sound_selection.profile;
                    ++ble_sound_attempts;
                    error = cerberus_s3_ble_central_sound_start(
                        &ble_sound_peer, ble_sound_profile, &ble_sound_status);
                    if (error == ESP_OK) {
                        ble_sound_action_generation = ble_sound_status.generation;
                        ble_sound_attempt_owned = true;
                    } else {
                        (void)cerberus_findmy_queue_finish_attempt(ble_sound_queue,
                            ble_sound_selection.ticket, false, now_ms);
                        ble_sound_error = error;
                        ble_sound_running = false;
                        ++ble_sound_errors;
                    }
                }
            } else if (scan.state == CERBERUS_BLE_SCAN_ERROR ||
                (scan.state == CERBERUS_BLE_SCAN_IDLE &&
                    owner == CERBERUS_S3_BLE_OWNER_NONE)) {
                ble_sound_scan_owned = false;
                ble_sound_error = scan.error_code != 0 ?
                    (esp_err_t)scan.error_code : ESP_FAIL;
                ble_sound_running = false;
            }
        } else if (ble_sound_queue && owner == CERBERUS_S3_BLE_OWNER_NONE &&
            (ble_sound_last_scan_start_ms == 0U ||
             (uint32_t)(now_ms - ble_sound_last_scan_start_ms) >= 500U)) {
            ble_sound_last_scan_start_ms = now_ms;
            esp_err_t error = cerberus_s3_ble_scanner_start_active(&scan);
            if (error == ESP_OK) {
                ble_sound_scan_owned = true;
                ble_sound_scan_generation = scan.generation;
                ble_sound_busy_since_ms = 0U;
            } else if (error == ESP_ERR_INVALID_STATE) {
                if (ble_sound_busy_since_ms == 0U)
                    ble_sound_busy_since_ms = now_ms;
                if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER ||
                    (uint32_t)(now_ms - ble_sound_busy_since_ms) >= 12000U) {
                    ble_sound_error = error;
                    ble_sound_running = false;
                }
            } else {
                ble_sound_error = error;
                ble_sound_running = false;
            }
        } else if (owner != CERBERUS_S3_BLE_OWNER_NONE) {
            ble_sound_error = ESP_ERR_INVALID_STATE;
            ble_sound_running = false;
        }
    }
    if (ble_sound_error != ESP_OK && !ble_sound_stop_pending &&
        (ble_sound_scan_owned || ble_sound_attempt_owned)) {
        ble_sound_running = false;
        ble_sound_stop_pending = true;
        ble_sound_stop_started_ms = now_ms;
    }
    if (!asleep && (ble_sound_last_draw_ms == 0U ||
        (uint32_t)(now_ms - ble_sound_last_draw_ms) >= UI_BLE_REFRESH_MS)) {
        ble_sound_last_draw_ms = now_ms;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void navigate_file_directory(const char *name)
{
    char next_path[sizeof(file_path)];
    int length;
    if (strcmp(file_path, "/") == 0) {
        length = snprintf(next_path, sizeof(next_path), "/%s", name);
    } else {
        length = snprintf(next_path, sizeof(next_path), "%s/%s", file_path, name);
    }

    if (length < 0 || (size_t)length >= sizeof(next_path)) {
        ESP_LOGW(TAG, "directory path is too long");
        return;
    }

    snprintf(file_path, sizeof(file_path), "%s", next_path);
    file_page_offset = 0;
    storage_last_error = cerberus_storage_list_directory(
        file_path,
        file_entries,
        UI_FILE_ENTRY_CAPACITY,
        &file_entry_count);
    ESP_ERROR_CHECK(draw_detail_page());
}

static void navigate_file_parent(void)
{
    if (strcmp(file_path, "/") == 0) {
        return;
    }

    char *separator = strrchr(file_path, '/');
    if (separator == file_path) {
        file_path[1] = '\0';
    } else if (separator != NULL) {
        *separator = '\0';
    }

    file_page_offset = 0;
    storage_last_error = cerberus_storage_list_directory(
        file_path,
        file_entries,
        UI_FILE_ENTRY_CAPACITY,
        &file_entry_count);
}

static void open_text_file(const char *name)
{
    size_t name_length = strnlen(name, sizeof(viewed_file_name) - 1U);
    memcpy(viewed_file_name, name, name_length);
    viewed_file_name[name_length] = '\0';
    viewed_file_offset = 0;
    viewed_file_history_count = 0;
    viewed_file_hash_state = UI_FILE_HASH_NOT_RUN;
    viewed_file_hash_error = ESP_ERR_INVALID_STATE;
    memset(viewed_file_sha256, 0, sizeof(viewed_file_sha256));
    file_mode = UI_FILE_MODE_TEXT;
    (void)refresh_text_viewer();
    ESP_ERROR_CHECK(draw_detail_page());
}

static void open_image_file(const char *name)
{
    size_t name_length = strnlen(name, sizeof(viewed_file_name) - 1U);
    memcpy(viewed_file_name, name, name_length);
    viewed_file_name[name_length] = '\0';
    file_mode = UI_FILE_MODE_IMAGE;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_text_viewer_tap(uint16_t x, uint16_t y)
{
    if (y < UI_FILE_CONTROLS_Y || y >= UI_FILE_CONTROLS_Y + 42U || viewed_file_error != ESP_OK) {
        return;
    }

    if (x < 78) {
        if (viewed_file_history_count == 0) {
            return;
        }
        viewed_file_offset = viewed_file_history[--viewed_file_history_count];
    } else if (x >= 84 && x < 154) {
        if (viewed_file_advance == 0 || viewed_file_offset + viewed_file_advance >= viewed_file_size) {
            return;
        }
        if (viewed_file_history_count == UI_TEXT_VIEWER_HISTORY) {
            memmove(
                &viewed_file_history[0],
                &viewed_file_history[1],
                (UI_TEXT_VIEWER_HISTORY - 1U) * sizeof(viewed_file_history[0]));
            --viewed_file_history_count;
        }
        viewed_file_history[viewed_file_history_count++] = viewed_file_offset;
        viewed_file_offset += viewed_file_advance;
    } else if (x >= 160 && x < 230) {
        viewed_file_offset = 0;
        viewed_file_history_count = 0;
    } else if (x >= 236) {
        file_mode = UI_FILE_MODE_INFO;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    } else {
        return;
    }

    (void)refresh_text_viewer();
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_file_info_tap(uint16_t x, uint16_t y)
{
    if (x < 64 || x >= 256 || y < UI_FILE_INFO_HASH_Y || y >= UI_FILE_INFO_HASH_Y + UI_FILE_INFO_HASH_HEIGHT ||
        viewed_file_hash_state == UI_FILE_HASH_RUNNING) {
        return;
    }

    viewed_file_hash_state = UI_FILE_HASH_RUNNING;
    ESP_ERROR_CHECK(draw_detail_page());
    present_ui_frame();
    viewed_file_hash_error = cerberus_storage_sha256_file(
        viewed_file_relative_path(),
        viewed_file_name,
        viewed_file_sha256,
        &viewed_file_size);
    viewed_file_hash_state = viewed_file_hash_error == ESP_OK ? UI_FILE_HASH_READY : UI_FILE_HASH_ERROR;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void file_set_action_status(const char *message, bool success)
{
    snprintf(file_action_message, sizeof(file_action_message), "%s", message != NULL ? message : "");
    file_action_color = success ? UI_GOOD : UI_BAD;
}

static void begin_file_name_input(ui_file_operation_t operation, const char *initial_name)
{
    file_operation = operation;
    esp_err_t err = cerberus_keyboard_begin(
        &file_keyboard, file_name_input, sizeof(file_name_input), initial_name != NULL ? initial_name : "");
    if (err != ESP_OK) {
        file_set_action_status(esp_err_to_name(err), false);
        return;
    }
    file_mode = UI_FILE_MODE_NAME_INPUT;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void finish_file_name_input(void)
{
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (file_operation == UI_FILE_OPERATION_CREATE_DIRECTORY) {
        err = cerberus_storage_create_directory(file_path, file_name_input);
    } else if (file_operation == UI_FILE_OPERATION_CREATE_FILE) {
        err = cerberus_storage_create_empty_file(file_path, file_name_input);
    } else if (file_operation == UI_FILE_OPERATION_RENAME) {
        err = cerberus_storage_rename_entry(file_selected_path, file_selected_entry.name, file_name_input);
    }
    if (err == ESP_OK) {
        file_set_action_status(file_operation == UI_FILE_OPERATION_RENAME ? "RENAMED" : "CREATED", true);
        file_mode = UI_FILE_MODE_LIST;
        file_page_offset = 0U;
        (void)refresh_file_listing(false);
    } else {
        char message[64];
        snprintf(message, sizeof(message), "OPERATION FAILED: %s", esp_err_to_name(err));
        file_set_action_status(message, false);
        file_mode = file_operation == UI_FILE_OPERATION_RENAME ? UI_FILE_MODE_ACTIONS : UI_FILE_MODE_TOOLS;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void stage_file_clipboard(bool move)
{
    file_clipboard_active = true;
    file_clipboard_move = move;
    file_clipboard_is_directory = file_selected_entry.is_directory;
    snprintf(file_clipboard_path, sizeof(file_clipboard_path), "%s", file_selected_path);
    snprintf(file_clipboard_name, sizeof(file_clipboard_name), "%s", file_selected_entry.name);
    file_set_action_status(move ? "MOVE READY - CHOOSE DESTINATION" : "COPY READY - CHOOSE DESTINATION", true);
    file_mode = UI_FILE_MODE_LIST;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void paste_file_clipboard(void)
{
    esp_err_t err;
    if (file_clipboard_move) {
        err = cerberus_storage_move_entry(
            file_clipboard_path, file_clipboard_name, file_path, file_clipboard_name);
    } else if (file_clipboard_is_directory) {
        err = ESP_ERR_NOT_SUPPORTED;
    } else {
        err = cerberus_storage_copy_file(
            file_clipboard_path, file_clipboard_name, file_path, file_clipboard_name);
    }
    if (err == ESP_OK) {
        file_clipboard_active = false;
        file_set_action_status(file_clipboard_move ? "MOVE COMPLETE" : "COPY COMPLETE", true);
        file_page_offset = 0U;
        (void)refresh_file_listing(false);
    } else {
        char message[64];
        snprintf(message, sizeof(message), "PASTE FAILED: %s", esp_err_to_name(err));
        file_set_action_status(message, false);
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_file_tools_tap(uint16_t x, uint16_t y)
{
    if (x < 24U || x >= 296U) {
        return;
    }
    if (y >= 112U && y < 162U) {
        begin_file_name_input(UI_FILE_OPERATION_CREATE_DIRECTORY, "");
    } else if (y >= 178U && y < 228U) {
        begin_file_name_input(UI_FILE_OPERATION_CREATE_FILE, "");
    } else if (y >= 244U && y < 294U) {
        file_mode = UI_FILE_MODE_SELECT;
        ESP_ERROR_CHECK(draw_detail_page());
    } else if (y >= 310U && y < 360U) {
        cerberus_wardrive_request_stop();
        storage_last_error = cerberus_wardrive_wait_stopped(15000U);
        if (storage_last_error == ESP_OK)
            storage_last_error = cerberus_usb_mass_storage_start();
        if (storage_last_error == ESP_OK) {
            storage_safely_removed = true;
            storage_checked = true;
            file_entry_count = 0U;
            file_mode = UI_FILE_MODE_LIST;
        }
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void handle_file_actions_tap(uint16_t x, uint16_t y)
{
    if (y >= 116U && y < 168U) {
        if (x >= 16U && x < 152U) {
            begin_file_name_input(UI_FILE_OPERATION_RENAME, file_selected_entry.name);
        } else if (x >= 168U && x < 304U) {
            stage_file_clipboard(file_selected_entry.is_directory);
        }
        return;
    }
    if (y >= 184U && y < 236U) {
        if (x >= 16U && x < 152U && !file_selected_entry.is_directory) {
            stage_file_clipboard(true);
        } else if (x >= 168U && x < 304U) {
            file_mode = UI_FILE_MODE_DELETE_CONFIRM;
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (y >= 258U && y < 310U && !file_selected_entry.is_directory &&
        strcmp(file_selected_path, CERBERUS_STORAGE_FIRMWARE_PATH) == 0) {
        file_package_error = cerberus_storage_inspect_firmware_package(
            file_selected_entry.name, &file_package_info);
        file_mode = UI_FILE_MODE_PACKAGE_INFO;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void handle_file_delete_confirm_tap(uint16_t x, uint16_t y)
{
    if (x < 48U || x >= 272U || y < 258U || y >= 320U) {
        return;
    }
    esp_err_t err = cerberus_storage_delete_entry(file_selected_path, file_selected_entry.name);
    if (err == ESP_OK) {
        file_set_action_status("DELETED", true);
        file_mode = UI_FILE_MODE_LIST;
        file_page_offset = 0U;
        (void)refresh_file_listing(false);
    } else {
        char message[64];
        snprintf(message, sizeof(message), "DELETE FAILED: %s", esp_err_to_name(err));
        file_set_action_status(message, false);
        file_mode = UI_FILE_MODE_ACTIONS;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_files_tap(uint16_t x, uint16_t y)
{
    if (file_mode == UI_FILE_MODE_IMAGE) {
        return;
    }
    if (file_mode == UI_FILE_MODE_INFO) {
        handle_file_info_tap(x, y);
        return;
    }
    if (file_mode == UI_FILE_MODE_TEXT) {
        handle_text_viewer_tap(x, y);
        return;
    }
    if (file_mode == UI_FILE_MODE_TOOLS) {
        handle_file_tools_tap(x, y);
        return;
    }
    if (file_mode == UI_FILE_MODE_ACTIONS) {
        handle_file_actions_tap(x, y);
        return;
    }
    if (file_mode == UI_FILE_MODE_DELETE_CONFIRM) {
        handle_file_delete_confirm_tap(x, y);
        return;
    }
    if (file_mode == UI_FILE_MODE_PACKAGE_INFO) {
        return;
    }
    if (file_mode == UI_FILE_MODE_NAME_INPUT) {
        cerberus_keyboard_action_t action;
        esp_err_t err = cerberus_keyboard_handle_tap(&file_keyboard, x, y, &action);
        if (err == ESP_OK && action == CERBERUS_KEYBOARD_ACTION_SUBMIT) {
            finish_file_name_input();
        } else if (err == ESP_OK && action == CERBERUS_KEYBOARD_ACTION_CHANGED) {
            ESP_ERROR_CHECK(cerberus_keyboard_draw(&file_keyboard,
                file_operation == UI_FILE_OPERATION_RENAME ? "RENAME" :
                (file_operation == UI_FILE_OPERATION_CREATE_DIRECTORY ? "FOLDER NAME" : "FILE NAME")));
        }
        return;
    }

    if (storage_safely_removed) {
        if (x >= 40U && x < 280U && y >= UI_FILE_CONTROLS_Y && y < UI_FILE_CONTROLS_Y + 42U) {
            cerberus_usb_mass_storage_status_t msc;
            cerberus_usb_mass_storage_get_status(&msc);
            if (msc.active) {
                storage_last_error = cerberus_usb_mass_storage_stop();
                if (storage_last_error != ESP_OK) {
                    file_set_action_status(
                        storage_last_error == ESP_ERR_INVALID_STATE
                            ? "EJECT USB DRIVE FIRST"
                            : "USB DRIVE RELEASE FAILED",
                        false);
                    ESP_ERROR_CHECK(draw_detail_page());
                    return;
                }
            } else if (cerberus_storage_get_owner() == CERBERUS_STORAGE_OWNER_USB) {
                (void)cerberus_storage_release_from_usb();
            }
            (void)refresh_file_listing(false);
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }

    if (y >= UI_FILE_LIST_Y && y < UI_FILE_LIST_Y + UI_FILE_VISIBLE_ROWS * UI_FILE_ROW_HEIGHT && storage_last_error == ESP_OK) {
        size_t row = (y - UI_FILE_LIST_Y) / UI_FILE_ROW_HEIGHT;
        size_t index = file_page_offset + row;
        if (index < file_entry_count && file_mode == UI_FILE_MODE_SELECT) {
            file_selected_entry = file_entries[index];
            snprintf(file_selected_path, sizeof(file_selected_path), "%s", file_path);
            file_action_message[0] = '\0';
            file_mode = UI_FILE_MODE_ACTIONS;
            ESP_ERROR_CHECK(draw_detail_page());
        } else if (index < file_entry_count && file_entries[index].is_directory) {
            navigate_file_directory(file_entries[index].name);
        } else if (index < file_entry_count) {
            if (cerberus_bmp_is_file_name(file_entries[index].name)) {
                open_image_file(file_entries[index].name);
            } else {
                open_text_file(file_entries[index].name);
            }
        }
        return;
    }

    if (y < UI_FILE_CONTROLS_Y || y >= UI_FILE_CONTROLS_Y + 42U) {
        return;
    }

    if (file_clipboard_active) {
        if (x < 58U) {
            if (strcmp(file_path, "/") != 0) {
                navigate_file_parent();
            }
        } else if (x >= 62U && x < 114U) {
            file_page_offset = file_page_offset >= UI_FILE_VISIBLE_ROWS ? file_page_offset - UI_FILE_VISIBLE_ROWS : 0U;
        } else if (x >= 118U && x < 170U) {
            if (file_page_offset + UI_FILE_VISIBLE_ROWS < file_entry_count) {
                file_page_offset += UI_FILE_VISIBLE_ROWS;
            }
        } else if (x >= 174U && x < 226U) {
            file_clipboard_active = false;
            file_set_action_status("TRANSFER CANCELLED", true);
        } else if (x >= 230U) {
            paste_file_clipboard();
            return;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (x < 72) {
        if (file_mode == UI_FILE_MODE_SELECT) {
            file_mode = UI_FILE_MODE_LIST;
        } else if (strcmp(file_path, "/") == 0) {
            (void)cerberus_storage_log_event(
                CERBERUS_LOG_INFO,
                "storage",
                "safe_eject",
                "user requested SD unmount");
            storage_last_error = cerberus_storage_unmount();
            if (storage_last_error == ESP_OK) {
                storage_safely_removed = true;
                storage_checked = true;
                file_entry_count = 0U;
                file_page_offset = 0U;
                log_checked = false;
            }
        } else {
            navigate_file_parent();
        }
    } else if (x >= 78 && x < 148) {
        file_page_offset = file_page_offset >= UI_FILE_VISIBLE_ROWS ? file_page_offset - UI_FILE_VISIBLE_ROWS : 0;
    } else if (x >= 154 && x < 224) {
        if (file_page_offset + UI_FILE_VISIBLE_ROWS < file_entry_count) {
            file_page_offset += UI_FILE_VISIBLE_ROWS;
        }
    } else if (x >= 230) {
        if (file_clipboard_active) {
            paste_file_clipboard();
            return;
        }
        if (file_mode == UI_FILE_MODE_LIST) {
            file_mode = UI_FILE_MODE_TOOLS;
        }
    } else {
        return;
    }

    ESP_ERROR_CHECK(draw_detail_page());
}

static void open_log_report(const char *name)
{
    size_t name_length = strnlen(name, sizeof(viewed_file_name) - 1U);
    memcpy(viewed_file_name, name, name_length);
    viewed_file_name[name_length] = '\0';
    viewed_file_offset = 0U;
    viewed_file_history_count = 0U;
    viewed_file_hash_state = UI_FILE_HASH_NOT_RUN;
    viewed_file_hash_error = ESP_ERR_INVALID_STATE;
    memset(viewed_file_sha256, 0, sizeof(viewed_file_sha256));
    log_mode = UI_LOG_MODE_TEXT;
    (void)refresh_text_viewer();
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_log_text_tap(uint16_t x, uint16_t y)
{
    if (y < UI_FILE_CONTROLS_Y || y >= UI_FILE_CONTROLS_Y + 42U ||
        viewed_file_error != ESP_OK) {
        return;
    }
    if (x < 78U) {
        if (viewed_file_history_count == 0U) {
            return;
        }
        viewed_file_offset = viewed_file_history[--viewed_file_history_count];
    } else if (x >= 84U && x < 154U) {
        if (viewed_file_advance == 0U ||
            viewed_file_offset + viewed_file_advance >= viewed_file_size) {
            return;
        }
        if (viewed_file_history_count == UI_TEXT_VIEWER_HISTORY) {
            memmove(
                &viewed_file_history[0],
                &viewed_file_history[1],
                (UI_TEXT_VIEWER_HISTORY - 1U) * sizeof(viewed_file_history[0]));
            --viewed_file_history_count;
        }
        viewed_file_history[viewed_file_history_count++] = viewed_file_offset;
        viewed_file_offset += viewed_file_advance;
    } else if (x >= 160U && x < 230U) {
        viewed_file_offset = 0U;
        viewed_file_history_count = 0U;
    } else if (x >= 236U) {
        log_mode = UI_LOG_MODE_INFO;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    } else {
        return;
    }
    (void)refresh_text_viewer();
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_log_info_tap(uint16_t x, uint16_t y)
{
    if (y < UI_FILE_INFO_HASH_Y || y >= UI_FILE_INFO_HASH_Y + UI_FILE_INFO_HASH_HEIGHT ||
        viewed_file_hash_state == UI_FILE_HASH_RUNNING) {
        return;
    }
    if (x >= 8U && x < 104U) {
        viewed_file_hash_state = UI_FILE_HASH_RUNNING;
        ESP_ERROR_CHECK(draw_detail_page());
        present_ui_frame();
        viewed_file_hash_error = cerberus_storage_sha256_file(
            UI_LOG_PATH,
            viewed_file_name,
            viewed_file_sha256,
            &viewed_file_size);
        viewed_file_hash_state = viewed_file_hash_error == ESP_OK
            ? UI_FILE_HASH_READY
            : UI_FILE_HASH_ERROR;
        ESP_ERROR_CHECK(draw_detail_page());
    } else if (x >= 112U && x < 208U) {
        viewed_file_hash_state = UI_FILE_HASH_RUNNING;
        ESP_ERROR_CHECK(draw_detail_page());
        present_ui_frame();
        viewed_file_hash_error = cerberus_storage_sha256_file(
            UI_LOG_PATH,
            viewed_file_name,
            viewed_file_sha256,
            &viewed_file_size);
        viewed_file_hash_state = viewed_file_hash_error == ESP_OK
            ? UI_FILE_HASH_READY
            : UI_FILE_HASH_ERROR;
        if (viewed_file_hash_error != ESP_OK) {
            ESP_ERROR_CHECK(draw_detail_page());
            return;
        }

        static const char hex[] = "0123456789abcdef";
        char digest[65];
        for (uint8_t index = 0U; index < 32U; ++index) {
            digest[index * 2U] = hex[viewed_file_sha256[index] >> 4U];
            digest[index * 2U + 1U] = hex[viewed_file_sha256[index] & 0x0FU];
        }
        digest[64] = '\0';
        int payload_length = snprintf(
            log_qr_payload,
            sizeof(log_qr_payload),
            "CERBERUS-REPORT-V1|FILE=%.12s|BYTES=%llu|SHA256=%s",
            viewed_file_name,
            (unsigned long long)viewed_file_size,
            digest);
        if (payload_length < 0 || (size_t)payload_length >= sizeof(log_qr_payload)) {
            viewed_file_hash_state = UI_FILE_HASH_ERROR;
            viewed_file_hash_error = ESP_ERR_INVALID_SIZE;
            ESP_ERROR_CHECK(draw_detail_page());
            return;
        }
        log_mode = UI_LOG_MODE_QR;
        ESP_ERROR_CHECK(draw_detail_page());
    } else if (x >= 216U && x < 312U) {
        if (log_file_name_is_system(viewed_file_name)) {
            return;
        }
        log_mode = UI_LOG_MODE_DELETE_CONFIRM;
        ESP_ERROR_CHECK(draw_detail_page());
    }
}

static void handle_log_delete_tap(uint16_t x, uint16_t y)
{
    if (log_file_name_is_system(viewed_file_name)) {
        log_mode = UI_LOG_MODE_INFO;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (y < 330U || y >= 388U) {
        return;
    }
    if (x >= 20U && x < 152U) {
        log_mode = UI_LOG_MODE_INFO;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (x < 168U || x >= 300U) {
        return;
    }

    char deleted_name[13];
    snprintf(deleted_name, sizeof(deleted_name), "%.12s", viewed_file_name);
    esp_err_t err = cerberus_storage_delete_diagnostic_report(viewed_file_name);
    if (err == ESP_OK) {
        snprintf(log_action_message, sizeof(log_action_message),
            "DELETED %s - NOT RECOVERABLE", deleted_name);
        log_action_color = UI_GOOD;
    } else {
        snprintf(log_action_message, sizeof(log_action_message),
            "DELETE FAILED %s", esp_err_to_name(err));
        log_action_color = UI_BAD;
    }
    log_mode = UI_LOG_MODE_LIST;
    log_checked = false;
    (void)refresh_log_listing(false);
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_logs_tap(uint16_t x, uint16_t y)
{
    if (log_mode == UI_LOG_MODE_TEXT) {
        handle_log_text_tap(x, y);
        return;
    }
    if (log_mode == UI_LOG_MODE_INFO) {
        handle_log_info_tap(x, y);
        return;
    }
    if (log_mode == UI_LOG_MODE_QR) {
        return;
    }
    if (log_mode == UI_LOG_MODE_DELETE_CONFIRM) {
        handle_log_delete_tap(x, y);
        return;
    }

    if (y >= UI_FILE_LIST_Y &&
        y < UI_FILE_LIST_Y + UI_FILE_VISIBLE_ROWS * UI_FILE_ROW_HEIGHT &&
        log_error == ESP_OK) {
        size_t row = (y - UI_FILE_LIST_Y) / UI_FILE_ROW_HEIGHT;
        size_t index = log_page_offset + row;
        if (index < log_entry_count) {
            log_action_message[0] = '\0';
            open_log_report(log_entries[index].name);
        }
        return;
    }
    if (y < UI_FILE_CONTROLS_Y || y >= UI_FILE_CONTROLS_Y + 42U) {
        return;
    }
    if (x >= 28U && x < 104U) {
        log_page_offset = log_page_offset >= UI_FILE_VISIBLE_ROWS
            ? log_page_offset - UI_FILE_VISIBLE_ROWS
            : 0U;
    } else if (x >= 122U && x < 198U) {
        if (log_page_offset + UI_FILE_VISIBLE_ROWS < log_entry_count) {
            log_page_offset += UI_FILE_VISIBLE_ROWS;
        }
    } else if (x >= 216U && x < 292U) {
        log_page_offset = 0U;
        log_action_message[0] = '\0';
        (void)refresh_log_listing(true);
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void begin_custom_qr_input(bool is_url)
{
    qr_custom_is_url = is_url;
    reset_qr_save_status();
    esp_err_t err = cerberus_keyboard_begin(
        &qr_keyboard,
        qr_payload,
        UI_QR_CUSTOM_CAPACITY,
        is_url ? "https://" : "");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot start QR keyboard: %s", esp_err_to_name(err));
        return;
    }
    qr_mode = UI_QR_MODE_INPUT;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void begin_wifi_qr_ssid_input(void)
{
    esp_err_t err = cerberus_keyboard_begin(
        &qr_keyboard,
        qr_wifi_ssid,
        sizeof(qr_wifi_ssid),
        "");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot start Wi-Fi SSID keyboard: %s", esp_err_to_name(err));
        return;
    }
    qr_mode = UI_QR_MODE_WIFI_SSID_INPUT;
    ESP_ERROR_CHECK(draw_detail_page());
}



static void begin_wifi_qr_password_input(void)
{
    esp_err_t err = cerberus_keyboard_begin(
        &qr_keyboard,
        qr_wifi_password,
        sizeof(qr_wifi_password),
        "");
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot start Wi-Fi password keyboard: %s", esp_err_to_name(err));
        return;
    }
    cerberus_keyboard_set_masked(&qr_keyboard, true);
    qr_mode = UI_QR_MODE_WIFI_PASSWORD_INPUT;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void finish_wifi_qr_input(void)
{
    esp_err_t err = build_wifi_qr_payload();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "cannot build Wi-Fi QR payload: %s", esp_err_to_name(err));
        return;
    }
    reset_qr_save_status();
    qr_mode = UI_QR_MODE_WIFI_CODE;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_qr_tap(uint16_t x, uint16_t y)
{
    if (qr_mode == UI_QR_MODE_INPUT || qr_mode == UI_QR_MODE_WIFI_SSID_INPUT ||
        qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT) {
        cerberus_keyboard_action_t action;
        esp_err_t err = cerberus_keyboard_handle_tap(&qr_keyboard, x, y, &action);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "QR keyboard tap failed: %s", esp_err_to_name(err));
            return;
        }
        if (action == CERBERUS_KEYBOARD_ACTION_SUBMIT) {
            if (qr_mode == UI_QR_MODE_WIFI_SSID_INPUT) {
                if (qr_wifi_security == UI_QR_WIFI_SECURITY_OPEN) {
                    finish_wifi_qr_input();
                } else {
                    begin_wifi_qr_password_input();
                }
            } else if (qr_mode == UI_QR_MODE_WIFI_PASSWORD_INPUT) {
                finish_wifi_qr_input();
            } else {
                reset_qr_save_status();
                qr_mode = UI_QR_MODE_CUSTOM;
                ESP_ERROR_CHECK(draw_detail_page());
            }
        } else if (action == CERBERUS_KEYBOARD_ACTION_CHANGED) {
            ESP_ERROR_CHECK(draw_qr_input());
        }
        return;
    }
    if (qr_mode == UI_QR_MODE_WIFI_SECURITY) {
        if (x < 24U || x >= 296U) {
            return;
        }
        if (y >= 134U && y < 188U) {
            qr_wifi_security = UI_QR_WIFI_SECURITY_WPA;
        } else if (y >= 206U && y < 260U) {
            qr_wifi_security = UI_QR_WIFI_SECURITY_WEP;
        } else if (y >= 278U && y < 332U) {
            qr_wifi_security = UI_QR_WIFI_SECURITY_OPEN;
        } else {
            return;
        }
        begin_wifi_qr_ssid_input();
        return;
    }
    if (qr_mode_has_code(qr_mode)) {
        if (x < 64U || x >= 256U || y < 374U || y >= 410U) {
            return;
        }

        qr_save_checked = true;
        qr_save_file_name[0] = '\0';
        qr_save_error = prepare_qr_payload(qr_mode);
        uint16_t module_count = 0U;
        if (qr_save_error == ESP_OK) {
            qr_save_error = cerberus_qr_encode_text(
                qr_payload,
                qr_save_modules,
                sizeof(qr_save_modules),
                &module_count);
        }
        if (qr_save_error == ESP_OK) {
            qr_save_error = cerberus_storage_write_qr_bmp(
                qr_save_modules,
                module_count,
                qr_save_file_name,
                sizeof(qr_save_file_name));
        }
        secure_clear_buffer((char *)qr_save_modules, sizeof(qr_save_modules));
        storage_safely_removed = false;
        storage_checked = true;
        storage_last_error = qr_save_error;
        if (qr_save_error == ESP_OK) {
            (void)cerberus_storage_log_event(
                CERBERUS_LOG_INFO,
                "qr",
                "export",
                "QR bitmap saved");
            storage_last_error = cerberus_storage_get_status(&storage_status);
            log_checked = false;
            file_entry_count = 0U;
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (qr_mode != UI_QR_MODE_MENU) {
        return;
    }
    if (x < 24U || x >= 296U) {
        return;
    }

    if (y >= 94U && y < 148U) {
        clear_qr_wifi_credentials();
        reset_qr_save_status();
        qr_wifi_security = UI_QR_WIFI_SECURITY_WPA;
        qr_mode = UI_QR_MODE_WIFI_SECURITY;
    } else if (y >= 166U && y < 220U) {
        begin_custom_qr_input(false);
        return;
    } else if (y >= 238U && y < 292U) {
        begin_custom_qr_input(true);
        return;
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_usb_tap(uint16_t x, uint16_t y)
{
    if (x < 38U || x >= 282U || y < 350U || y >= 408U) {
        return;
    }

    if (usb_mode == UI_USB_MODE_SUMMARY) {
        usb_mode = UI_USB_MODE_RECOVERY_CONFIRM;
        usb_recovery_error = ESP_OK;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (!cerberus_recovery_download_mode_available()) {
        usb_recovery_error = ESP_ERR_NOT_SUPPORTED;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    (void)cerberus_event_bus_publish(CERBERUS_EVENT_RECOVERY_REQUESTED, 0);

    ESP_ERROR_CHECK(cerberus_display_fill_rect(
        0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, UI_BACKGROUND));
    ESP_ERROR_CHECK(draw_centered(176, "PREPARING RECOVERY MODE", UI_TEXT, 2));
    ESP_ERROR_CHECK(draw_centered(224, "SAFELY RELEASING MICROSD", UI_MUTED, 1));
    present_ui_frame();

    if (cerberus_storage_is_mounted()) {
        usb_recovery_error = cerberus_storage_unmount();
        if (usb_recovery_error != ESP_OK) {
            usb_mode = UI_USB_MODE_RECOVERY_CONFIRM;
            ESP_ERROR_CHECK(draw_detail_page());
            return;
        }
    }

    ESP_ERROR_CHECK(draw_centered(264, "RESTARTING INTO ROM LOADER", UI_GOOD, 1));
    present_ui_frame();
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    vTaskDelay(pdMS_TO_TICKS(250));
    usb_recovery_error = cerberus_recovery_enter_download_mode();
    ESP_ERROR_CHECK(draw_detail_page());
}

static uint16_t calibration_extrapolate_min(
    uint16_t raw_low,
    uint16_t raw_high,
    uint16_t logical_low,
    uint16_t logical_high)
{
    uint32_t span = raw_high > raw_low ? raw_high - raw_low : 0U;
    uint32_t logical_span = logical_high - logical_low;
    uint32_t margin = logical_span > 0U ? span * logical_low / logical_span : 0U;
    return margin < raw_low ? (uint16_t)(raw_low - margin) : 0U;
}

static uint16_t calibration_extrapolate_max(
    uint16_t raw_low,
    uint16_t raw_high,
    uint16_t logical_low,
    uint16_t logical_high,
    uint16_t logical_max)
{
    uint32_t span = raw_high > raw_low ? raw_high - raw_low : 0U;
    uint32_t logical_span = logical_high - logical_low;
    uint32_t margin = logical_span > 0U
        ? span * (logical_max - logical_high) / logical_span
        : 0U;
    uint32_t result = raw_high + margin;
    return result <= UINT16_MAX ? (uint16_t)result : UINT16_MAX;
}

static void handle_touch_calibration_sample(void)
{
    if (touch_calibration_step >= 4U) {
        return;
    }
    cerberus_touch_point_t raw;
    if (!cerberus_touch_get_last_raw(&raw)) {
        touch_calibration_error = ESP_ERR_INVALID_STATE;
        touch_calibration_step = 4U;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    touch_calibration_points[touch_calibration_step++] = raw;
    if (touch_calibration_step < 4U) {
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    uint16_t raw_left = (uint16_t)(((uint32_t)touch_calibration_points[0].x +
        touch_calibration_points[2].x) / 2U);
    uint16_t raw_right = (uint16_t)(((uint32_t)touch_calibration_points[1].x +
        touch_calibration_points[3].x) / 2U);
    uint16_t raw_top = (uint16_t)(((uint32_t)touch_calibration_points[0].y +
        touch_calibration_points[1].y) / 2U);
    uint16_t raw_bottom = (uint16_t)(((uint32_t)touch_calibration_points[2].y +
        touch_calibration_points[3].y) / 2U);
    cerberus_touch_calibration_t calibration = {
        .valid = true,
        .x_min = calibration_extrapolate_min(raw_left, raw_right, 12U, 308U),
        .x_max = calibration_extrapolate_max(raw_left, raw_right, 12U, 308U, 319U),
        .y_min = calibration_extrapolate_min(raw_top, raw_bottom, 62U, 408U),
        .y_max = calibration_extrapolate_max(raw_top, raw_bottom, 62U, 408U, 479U),
    };
    touch_calibration_error = cerberus_settings_set_touch_calibration(&calibration);
    if (touch_calibration_error == ESP_OK) {
        touch_calibration_error = cerberus_touch_set_calibration(
            true,
            calibration.x_min,
            calibration.x_max,
            calibration.y_min,
            calibration.y_max);
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void handle_scripts_tap(uint16_t x, uint16_t y)
{
    if (script_mode == UI_SCRIPT_MODE_RUNNING) {
        return;
    }
    if (script_mode == UI_SCRIPT_MODE_DETAIL) {
        if (script_load_error == ESP_OK && x >= 40U && x < 280U &&
            y >= 330U && y < 382U) {
            start_script_run();
            ESP_ERROR_CHECK(draw_detail_page());
        }
        return;
    }
    if (y >= UI_SCRIPT_LIST_Y &&
        y < UI_SCRIPT_LIST_Y + UI_SCRIPT_VISIBLE_ROWS * UI_SCRIPT_ROW_HEIGHT) {
        size_t row = (y - UI_SCRIPT_LIST_Y) / UI_SCRIPT_ROW_HEIGHT;
        size_t index = script_page_offset + row;
        if (index >= script_entry_count) {
            return;
        }
        script_load_error = cerberus_script_load(
            script_entries[index].name,
            &loaded_script,
            &script_parse_error);
        if (loaded_script.name[0] == '\0') {
            snprintf(loaded_script.name, sizeof(loaded_script.name), "%.63s",
                script_entries[index].name);
        }
        script_mode = UI_SCRIPT_MODE_DETAIL;
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }
    if (y < 360U || y >= 402U) {
        return;
    }
    if (x < 96U) {
        if (script_page_offset == 0U) {
            return;
        }
        script_page_offset = script_page_offset >= UI_SCRIPT_VISIBLE_ROWS
            ? script_page_offset - UI_SCRIPT_VISIBLE_ROWS
            : 0U;
    } else if (x >= 116U && x < 204U) {
        script_listing_checked = false;
        script_page_offset = 0U;
        (void)refresh_script_listing();
    } else if (x >= 224U) {
        if (script_page_offset + UI_SCRIPT_VISIBLE_ROWS >= script_entry_count) {
            return;
        }
        script_page_offset += UI_SCRIPT_VISIBLE_ROWS;
    } else {
        return;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

#include "sentinel_live_ui.h"
#include "sentinel_watch_ui.h"
#include "remote_id_live_ui.h"
#include "mac_monitor_live_ui.h"

static esp_err_t draw_battery_indicator(void)
{
    cerberus_battery_status_t battery;
    cerberus_battery_get_status(&battery);
    uint16_t background = current_page == UI_PAGE_GUIDE ? 0x0863U : UI_BACKGROUND;
    if (current_page == UI_PAGE_WATCH && (watch_page == WATCH_LIVE || watch_page == WATCH_DEMO))
        background = 0x0042U;
    if (current_page == UI_PAGE_REMOTE_ID) background = 0x0042U;
    if (current_page == UI_PAGE_MAC_MONITOR) background = 0x0042U;
    return sentinel_battery_view_update(battery.valid, battery.percent, background);
}

static void open_tool_page(ui_page_t page)
{
    current_page = page;
    refresh_capability_map();
    ui_capability_blocked_reason = page_capability_issue(current_page);
    ui_capability_blocked = ui_capability_blocked_reason != NULL;
    if (current_page == UI_PAGE_DASHBOARD) {
        dashboard_mode = UI_DASHBOARD_MODE_SUMMARY;
        dashboard_crash_action[0] = '\0';
    }
    if (current_page == UI_PAGE_WIFI) {
        wifi_attack_selecting = false;
        wifi_mode = UI_WIFI_MODE_LIST;
    }
    if (current_page == UI_PAGE_BLE) {
        ble_mode = UI_BLE_MODE_LIST;
    }
    if (current_page == UI_PAGE_BLE_ATTACK) {
        ble_attack_start_pending = false;
        ble_attack_return_pending = false;
        ble_attack_start_error = ESP_OK;
        ble_attack_job_id = 0U;
        ble_attack_last_draw_ms = 0U;
    }
    if (current_page == UI_PAGE_BLE_SOUND) {
        ble_sound_last_tick_ms = 0U;
        ble_sound_last_draw_ms = 0U;
    }
    if (current_page == UI_PAGE_WARDRIVE) {
        wardrive_gps_enabled = false;
        wardrive_start_pending = false;
        wardrive_return_pending = false;
        wardrive_start_error = ESP_OK;
        wardrive_blocked_reason[0] = '\0';
        wardrive_last_draw_ms = 0U;
    }
    if (current_page == UI_PAGE_IEEE802154) {
        ieee802154_mode = UI_IEEE802154_MODE_ENERGY;
    }
    if (current_page == UI_PAGE_FILES) {
        file_listing_checked = false;
    }
    if (current_page == UI_PAGE_SCRIPTS) {
        script_mode = UI_SCRIPT_MODE_LIST;
        script_listing_checked = false;
        script_page_offset = 0U;
        script_running = false;
        script_complete = false;
    }
    if (current_page == UI_PAGE_LOGS) {
        log_mode = UI_LOG_MODE_LIST;
        log_checked = false;
        log_action_message[0] = '\0';
    }
    if (current_page == UI_PAGE_SETTINGS) {
        settings_mode = UI_SETTINGS_MODE_DISPLAY;
        settings_reset_confirmation = false;
        settings_reset_error = ESP_OK;
        settings_backup_checked = false;
        settings_backup_error = ESP_ERR_INVALID_STATE;
        settings_backup_file_name[0] = '\0';
        settings_restore_confirmation = false;
        settings_restore_checked = false;
        settings_restore_error = ESP_ERR_INVALID_STATE;
        settings_restore_length = 0U;
        settings_restore_file_name[0] = '\0';
        memset(&settings_restore_info, 0, sizeof(settings_restore_info));
        memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
    }
    if (current_page == UI_PAGE_QR) {
        clear_qr_wifi_credentials();
        reset_qr_save_status();
        qr_mode = UI_QR_MODE_MENU;
    }
    if (current_page == UI_PAGE_USB) {
        usb_mode = UI_USB_MODE_SUMMARY;
        usb_recovery_error = ESP_OK;
    }
    if (current_page == UI_PAGE_SELF_TEST) {
        self_test_mode = UI_SELF_TEST_MODE_SUMMARY;
    }
    ESP_ERROR_CHECK(draw_detail_page());
}

static void open_tools_menu(void)
{
    tools_group = SENTINEL_TOOLS_MAIN;
    learning_tool_active = false;
    current_page = UI_PAGE_TOOLS;
    ESP_ERROR_CHECK(draw_tools_menu());
}

static void handle_tools_tap(uint16_t x, uint16_t y)
{
    sentinel_tools_action_t action = sentinel_tools_hit_test(tools_group, x, y);
    if (action == SENTINEL_TOOLS_BACK) {
        if (tools_group == SENTINEL_TOOLS_MAIN) {
            open_main_menu();
        } else {
            tools_group = SENTINEL_TOOLS_MAIN;
            ESP_ERROR_CHECK(draw_tools_menu());
        }
        return;
    }
    if (action == SENTINEL_TOOLS_OPEN_CARE || action == SENTINEL_TOOLS_OPEN_EXTRAS) {
        tools_group = action == SENTINEL_TOOLS_OPEN_CARE ? SENTINEL_TOOLS_CARE : SENTINEL_TOOLS_EXTRAS;
        ESP_ERROR_CHECK(draw_tools_menu());
        return;
    }

    ui_page_t page;
    switch (action) {
    case SENTINEL_TOOLS_SETTINGS: page = UI_PAGE_SETTINGS; break;
    case SENTINEL_TOOLS_FILES: page = UI_PAGE_FILES; break;
    case SENTINEL_TOOLS_LOGS: page = UI_PAGE_LOGS; break;
    case SENTINEL_TOOLS_WEB: page = UI_PAGE_WEB; break;
    case SENTINEL_TOOLS_STATUS: page = UI_PAGE_DASHBOARD; break;
    case SENTINEL_TOOLS_CHECKS: page = UI_PAGE_SELF_TEST; break;
    case SENTINEL_TOOLS_USB: page = UI_PAGE_USB; break;
    case SENTINEL_TOOLS_QR: page = UI_PAGE_QR; break;
    case SENTINEL_TOOLS_SCRIPTS: page = UI_PAGE_SCRIPTS; break;
    default: return;
    }
    tool_parent_page = UI_PAGE_TOOLS;
    learning_tool_active = false;
    open_tool_page(page);
}

static bool recon_sessions_idle_for_takeover(void)
{
    return live_session.desired == SENTINEL_SCAN_NONE &&
        live_session.active == SENTINEL_SCAN_NONE &&
        live_session.pending.kind == SENTINEL_SCAN_COMMAND_NONE &&
        live_ble_session.desired == SENTINEL_SCAN_NONE &&
        live_ble_session.active == SENTINEL_SCAN_NONE &&
        live_ble_session.pending.kind == SENTINEL_SCAN_COMMAND_NONE &&
        (!ble_discovery_live || cerberus_ble_discovery_session_released(&ble_discovery_live->session));
}

static void ble_sound_handle_tap(uint16_t x, uint16_t y)
{
    if (x < 40U || x >= 280U || y < 380U || y >= 412U ||
        ble_sound_stop_pending) return;
    if (ble_sound_running || ble_sound_scan_owned || ble_sound_attempt_owned) {
        ble_sound_running = false;
        ble_sound_stop_pending = true;
        ble_sound_stop_started_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    } else {
        if ((ui_capability_map & UI_CAP_BLE_GATT) == 0U) {
            ble_sound_error = ESP_ERR_NOT_SUPPORTED;
        } else if (cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_NONE) {
            ble_sound_error = ESP_ERR_INVALID_STATE;
        } else if (ble_sound_all) {
            if (!ble_sound_queue) {
                ble_sound_queue = heap_caps_calloc(1U, sizeof(*ble_sound_queue),
                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                if (ble_sound_queue) {
                    cerberus_findmy_queue_init(ble_sound_queue);
                    cerberus_findmy_queue_start(ble_sound_queue);
                }
            }
            if (!ble_sound_queue) ble_sound_error = ESP_ERR_NO_MEM;
            else {
                cerberus_findmy_queue_start(ble_sound_queue);
                ble_sound_sweeps = ble_sound_attempts = ble_sound_accepted = 0U;
                ble_sound_responses = ble_sound_reported = ble_sound_errors = 0U;
                ble_sound_last_scan_start_ms = 0U;
                ble_sound_busy_since_ms = 0U;
                ble_sound_error = ESP_OK;
                ble_sound_running = true;
            }
        } else if (ble_sound_profile != CERBERUS_FINDMY_PROFILE_NONE &&
            cerberus_findmy_ui_fresh(ble_sound_selected_scan_ms,
                (uint32_t)(esp_timer_get_time() / 1000LL)) &&
            (ble_sound_peer.flags & CERBERUS_BLE_RESULT_CONNECTABLE) != 0U) {
            ble_sound_error = ESP_OK;
            ble_sound_selected_started = false;
            memset(&ble_sound_status, 0, sizeof(ble_sound_status));
            ble_sound_running = true;
        } else if (!ble_sound_all) {
            ble_sound_error = ESP_ERR_INVALID_STATE;
        }
    }
    ble_sound_last_tick_ms = 0U;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_attack_handle_tap(uint16_t x, uint16_t y)
{
    if (x < 24U || x >= 296U) return;
    cerberus_s3_ble_attack_status_t status;
    cerberus_s3_ble_attack_get_status(&status);
    bool active = status.job_id == ble_attack_job_id && ble_attack_job_id != 0U &&
        (status.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
         status.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
         status.state == CERBERUS_S3_BLE_ATTACK_STOPPING);
    if (y >= 346U && y < 374U &&
        ble_attack_mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG &&
        !active && !ble_attack_start_pending) {
        ble_attack_selecting = true;
        ble_attack_capture_valid = false;
        ble_attack_capture_message[0] = '\0';
        open_tool_page(UI_PAGE_BLE);
        return;
    }
    if (y < 380U || y >= 412U) return;
    if (active) {
        (void)cerberus_s3_ble_attack_stop(ble_attack_job_id);
    } else if (ble_attack_start_pending) {
        ble_attack_start_pending = false;
    } else if (ble_attack_mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG &&
        !ble_attack_capture_valid) {
        snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
            "SELECT A VALID CAPTURE FIRST");
    } else if (cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_ADVERTISER) {
        ble_attack_start_error = ESP_ERR_INVALID_STATE;
    } else {
        ble_attack_start_error = ESP_OK;
        ble_attack_capture_message[0] = '\0';
        ble_attack_next_job_id++;
        if (ble_attack_next_job_id == 0U) ble_attack_next_job_id++;
        ble_attack_job_id = ble_attack_next_job_id;
        ble_attack_start_requested_ms = (uint32_t)(esp_timer_get_time() / 1000);
        ble_attack_last_start_attempt_ms = ble_attack_start_requested_ms - 250U;
        ble_attack_start_pending = true;
    }
    ble_attack_last_draw_ms = 0U;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void ble_attack_tick(uint32_t now_ms, bool asleep)
{
    if (current_page != UI_PAGE_BLE_ATTACK) return;
    cerberus_s3_ble_attack_status_t status;
    cerberus_s3_ble_attack_get_status(&status);
    bool own_job = ble_attack_job_id != 0U && status.job_id == ble_attack_job_id;
    bool active = own_job && (status.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
        status.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
        status.state == CERBERUS_S3_BLE_ATTACK_STOPPING);
    if (active && !ble_attack_return_pending &&
        (uint32_t)(now_ms - ble_attack_last_touch_ms) >= 1000U) {
        (void)cerberus_s3_ble_attack_touch(ble_attack_job_id);
        ble_attack_last_touch_ms = now_ms;
    }
    if (ble_attack_return_pending) {
        if (!active && cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_ADVERTISER) {
            ble_attack_return_pending = false;
            return_to_launcher();
            return;
        }
    } else if (ble_attack_start_pending) {
        if ((uint32_t)(now_ms - ble_attack_start_requested_ms) >= 15000U) {
            ble_attack_start_pending = false;
            ble_attack_start_error = ESP_ERR_TIMEOUT;
            snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
                "S3 RADIO DID NOT BECOME AVAILABLE");
        } else if (recon_sessions_idle_for_takeover() &&
                   cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_CENTRAL) {
            ble_attack_start_pending = false;
            ble_attack_start_error = ESP_ERR_INVALID_STATE;
            snprintf(ble_attack_capture_message, sizeof(ble_attack_capture_message),
                "DISCONNECT BLE DEVICE FIRST");
        } else if (recon_sessions_idle_for_takeover() &&
                   (uint32_t)(now_ms - ble_attack_last_start_attempt_ms) >= 250U) {
            ble_attack_last_start_attempt_ms = now_ms;
            cerberus_s3_ble_attack_request_t request = {
                .job_id = ble_attack_job_id,
                .mode = ble_attack_mode,
                .seed = esp_random(),
                .has_capture = ble_attack_mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG &&
                    ble_attack_capture_valid,
            };
            if (request.has_capture) request.capture = ble_attack_capture;
            esp_err_t error = cerberus_s3_ble_attack_start(&request, &status);
            if (error == ESP_OK) {
                ble_attack_start_pending = false;
                ble_attack_last_touch_ms = now_ms;
            } else if (error != ESP_ERR_INVALID_STATE) {
                ble_attack_start_pending = false;
                ble_attack_start_error = error;
            }
        }
    }
    if (!asleep && (ble_attack_last_draw_ms == 0U ||
        (uint32_t)(now_ms - ble_attack_last_draw_ms) >= 1000U)) {
        ESP_ERROR_CHECK(draw_detail_page());
        ble_attack_last_draw_ms = now_ms;
    }
}

static void wardrive_handle_tap(uint16_t x, uint16_t y)
{
    if (x < 24U || x >= 296U) return;
    cerberus_wardrive_status_t status;
    cerberus_wardrive_get_status(&status);
    bool active = status.state == CERBERUS_WARDRIVE_STARTING ||
        status.state == CERBERUS_WARDRIVE_RUNNING ||
        status.state == CERBERUS_WARDRIVE_STOPPING;
    if (y >= 346U && y < 374U && !active && !wardrive_start_pending) {
        if (x < 160U) wardrive_include_expansions = !wardrive_include_expansions;
        else wardrive_gps_enabled = !wardrive_gps_enabled;
    } else if (y >= 380U && y < 412U) {
        if (active) {
            cerberus_wardrive_request_stop();
        } else if (wardrive_start_pending) {
            wardrive_start_pending = false;
        } else {
            wardrive_start_error = ESP_OK;
            wardrive_blocked_reason[0] = '\0';
            wardrive_start_requested_ms = (uint32_t)(esp_timer_get_time() / 1000);
            wardrive_last_start_attempt_ms = wardrive_start_requested_ms - 250U;
            wardrive_start_pending = true;
        }
    } else {
        return;
    }
    wardrive_last_draw_ms = 0U;
    ESP_ERROR_CHECK(draw_detail_page());
}

static void wardrive_tick(uint32_t now_ms, bool asleep)
{
    if (current_page != UI_PAGE_WARDRIVE) return;
    cerberus_wardrive_status_t status;
    cerberus_wardrive_get_status(&status);
    if (wardrive_return_pending) {
        if (status.state != CERBERUS_WARDRIVE_STARTING &&
            status.state != CERBERUS_WARDRIVE_RUNNING &&
            status.state != CERBERUS_WARDRIVE_STOPPING) {
            wardrive_return_pending = false;
            return_to_launcher();
            return;
        }
    } else if (wardrive_start_pending) {
        if ((uint32_t)(now_ms - wardrive_start_requested_ms) >= 15000U) {
            wardrive_start_pending = false;
            wardrive_start_error = ESP_ERR_TIMEOUT;
            snprintf(wardrive_blocked_reason, sizeof(wardrive_blocked_reason),
                "RADIO DID NOT BECOME AVAILABLE");
        } else if (cerberus_storage_get_owner() == CERBERUS_STORAGE_OWNER_USB) {
            wardrive_start_pending = false;
            wardrive_start_error = ESP_ERR_INVALID_STATE;
            snprintf(wardrive_blocked_reason, sizeof(wardrive_blocked_reason),
                "RETURN SD FROM USB DRIVE MODE");
        } else if (recon_sessions_idle_for_takeover() &&
                   cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_CENTRAL) {
            wardrive_start_pending = false;
            wardrive_start_error = ESP_ERR_INVALID_STATE;
            snprintf(wardrive_blocked_reason, sizeof(wardrive_blocked_reason),
                "DISCONNECT BLE DEVICE FIRST");
        } else if (recon_sessions_idle_for_takeover() &&
                   (uint32_t)(now_ms - wardrive_last_start_attempt_ms) >= 250U) {
            wardrive_last_start_attempt_ms = now_ms;
            esp_err_t error = cerberus_wardrive_start(wardrive_include_expansions,
                wardrive_gps_enabled);
            if (error == ESP_OK) wardrive_start_pending = false;
            else if (error != ESP_ERR_INVALID_STATE) {
                wardrive_start_pending = false;
                wardrive_start_error = error;
            }
        }
    }
    if (!asleep && (wardrive_last_draw_ms == 0U ||
        (uint32_t)(now_ms - wardrive_last_draw_ms) >= 1000U)) {
        ESP_ERROR_CHECK(draw_detail_page());
        wardrive_last_draw_ms = now_ms;
    }
}

static void handle_tap(uint16_t x, uint16_t y)
{
    if (x >= CERBERUS_LCD_WIDTH || y >= CERBERUS_LCD_HEIGHT) {
        return;
    }

    if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI) {
        csi_handle_tap(x, y);
        return;
    }

    if (current_page == UI_PAGE_MAIN) {
        sentinel_dashboard_action_t action = sentinel_dashboard_hit_test(x, y);
        tool_parent_page = UI_PAGE_MAIN;
        learning_tool_active = false;
        if (action == SENTINEL_DASHBOARD_SCAN || action == SENTINEL_DASHBOARD_ATTACK) {
            field_menu.attack = action == SENTINEL_DASHBOARD_ATTACK;
            field_menu.bluetooth = false;
            field_menu.page = 0;
            current_page = UI_PAGE_FIELD_MENU;
            ESP_ERROR_CHECK(draw_current_page());
        }
        else if (action == SENTINEL_DASHBOARD_WIFI) open_tool_page(UI_PAGE_WIFI);
        else if (action == SENTINEL_DASHBOARD_WATCH) watch_open((uint32_t)(esp_timer_get_time() / 1000));
        else if (action == SENTINEL_DASHBOARD_BLE) open_tool_page(UI_PAGE_BLE);
        else if (action == SENTINEL_DASHBOARD_RADIO) open_tool_page(UI_PAGE_IEEE802154);
        else if (action == SENTINEL_DASHBOARD_TOOLS) {
            open_tools_menu();
        } else if (action == SENTINEL_DASHBOARD_GUIDE) {
            sentinel_guide_open();
            current_page = UI_PAGE_GUIDE;
            ESP_ERROR_CHECK(sentinel_guide_draw());
        }
        return;
    }

    if (current_page == UI_PAGE_REMOTE_ID) {
        remote_id_handle_tap(x, y);
        return;
    }
    if (current_page == UI_PAGE_MAC_MONITOR) {
        mac_monitor_handle_tap(x, y);
        return;
    }
    if (current_page == UI_PAGE_FIELD_MENU) {
        cerberus_field_action_t action = cerberus_field_menu_tap(&field_menu, x, y);
        if (action == CERBERUS_FIELD_HOME) { open_main_menu(); return; }
        if (action == CERBERUS_FIELD_REDRAW) { ESP_ERROR_CHECK(draw_current_page()); return; }
        if (action == CERBERUS_FIELD_NONE) return;
        tool_parent_page = UI_PAGE_FIELD_MENU;
        learning_tool_active = false;
        if (action == CERBERUS_FIELD_RECON) { watch_open((uint32_t)(esp_timer_get_time() / 1000)); return; }
        if (action == CERBERUS_FIELD_REMOTE_ID) { remote_id_open(); return; }
        if (action == CERBERUS_FIELD_MAC_MONITOR) { mac_monitor_open(); return; }
        if (action == CERBERUS_FIELD_GUIDE) {
            sentinel_guide_open(); current_page = UI_PAGE_GUIDE;
            ESP_ERROR_CHECK(sentinel_guide_draw()); return;
        }
        if (action == CERBERUS_FIELD_RADIO_LAB) { open_tool_page(UI_PAGE_RADIO_LAB); return; }
        if (action == CERBERUS_FIELD_BLE_FIND_MY_SOUND) {
            ble_sound_open(true);
            return;
        }
        if (action >= CERBERUS_FIELD_BLE_SOUR_APPLE &&
            action <= CERBERUS_FIELD_BLE_FIND_MY_SOUND) {
            switch (action) {
            case CERBERUS_FIELD_BLE_SOUR_APPLE: ble_attack_mode = CERBERUS_BLE_ATTACK_SOUR_APPLE; break;
            case CERBERUS_FIELD_BLE_APPLE_JUICE: ble_attack_mode = CERBERUS_BLE_ATTACK_APPLE_JUICE; break;
            case CERBERUS_FIELD_BLE_SWIFT_PAIR: ble_attack_mode = CERBERUS_BLE_ATTACK_SWIFT_PAIR; break;
            case CERBERUS_FIELD_BLE_SAMSUNG: ble_attack_mode = CERBERUS_BLE_ATTACK_SAMSUNG; break;
            case CERBERUS_FIELD_BLE_GOOGLE: ble_attack_mode = CERBERUS_BLE_ATTACK_GOOGLE; break;
            case CERBERUS_FIELD_BLE_FLIPPER: ble_attack_mode = CERBERUS_BLE_ATTACK_FLIPPER; break;
            case CERBERUS_FIELD_BLE_ALL: ble_attack_mode = CERBERUS_BLE_ATTACK_ALL; break;
            case CERBERUS_FIELD_BLE_SPOOF_AIRTAG: ble_attack_mode = CERBERUS_BLE_ATTACK_SPOOF_AIRTAG; break;
            default: return; /* The field catalog keeps unbacked routes inert. */
            }
            ble_attack_capture_message[0] = '\0';
            open_tool_page(UI_PAGE_BLE_ATTACK);
            return;
        }
        if (action == CERBERUS_FIELD_BLE) open_tool_page(UI_PAGE_BLE);
        else if (action == CERBERUS_FIELD_WARDRIVE) open_tool_page(UI_PAGE_WARDRIVE);
        else if (action == CERBERUS_FIELD_RADIO) open_tool_page(UI_PAGE_IEEE802154);
        else {
            open_tool_page(UI_PAGE_WIFI);
            if (!ui_capability_blocked) {
                if (action == CERBERUS_FIELD_SURVEY) wifi_mode = UI_WIFI_MODE_FIELD_SURVEY;
                else if (action == CERBERUS_FIELD_WIFI_PORTAL && wifi_selection_valid) {
                    portal_enter(); wifi_mode = UI_WIFI_MODE_PORTAL;
                }
                else if (action>=CERBERUS_FIELD_WIFI_DEAUTH&&action<=CERBERUS_FIELD_WIFI_SAE&&wifi_selection_valid) {
                    static const uint8_t modes[]={CERBERUS_AP_DEAUTH,CERBERUS_AP_PROBE_REQUEST,CERBERUS_AP_BAD_MESSAGE,
                        CERBERUS_AP_ASSOC_SLEEP,CERBERUS_AP_CHANNEL_SWITCH,CERBERUS_AP_QUIET,CERBERUS_AP_CLONE_SPAM,
                        CERBERUS_AP_SAE_COMMIT};
                    ap_station_selecting=false;ap_choose(modes[action-CERBERUS_FIELD_WIFI_DEAUTH]);
                    ap_enter();wifi_mode=UI_WIFI_MODE_AP_JOB;
                }
                else if (action == CERBERUS_FIELD_WIFI_SELECT) wifi_attack_selecting = true;
                else if (action == CERBERUS_FIELD_WIFI_TARGET && wifi_selection_valid) {
                    wifi_mode = UI_WIFI_MODE_TARGET; wifi_target_message[0] = 0;
                    wifi_connection_error = c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
                }
                else if (action == CERBERUS_FIELD_NETWORK) {
                    wifi_connection_error = c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
                    if (wifi_connection_error == ESP_OK && wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED) {
                        if (c5_status.capabilities & CERBERUS_C5_CAP_NETWORK_SCAN) {
                            net_enter(); wifi_mode = UI_WIFI_MODE_NETWORK_SCAN;
                        } else wifi_mode = UI_WIFI_MODE_CONNECTION;
                    }
                }
            }
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (current_page == UI_PAGE_WATCH) {
        watch_handle_tap(x, y);
        return;
    }

    if (current_page == UI_PAGE_GUIDE) {
        sentinel_guide_action_t action = sentinel_guide_handle_touch(x, y);
        if (action == SENTINEL_GUIDE_HOME) { open_main_menu(); return; }
        if (action == SENTINEL_GUIDE_REDRAW) { ESP_ERROR_CHECK(sentinel_guide_draw()); return; }
        if (action == SENTINEL_GUIDE_NONE) return;
        tool_parent_page = UI_PAGE_GUIDE;
        learning_tool_active = true;
        if (action == SENTINEL_GUIDE_OPEN_WIFI || action == SENTINEL_GUIDE_OPEN_WIFI_CHANNELS ||
            action == SENTINEL_GUIDE_OPEN_WIFI_BASELINE) {
            open_tool_page(UI_PAGE_WIFI);
            if (!ui_capability_blocked) {
                wifi_mode = action == SENTINEL_GUIDE_OPEN_WIFI_CHANNELS ? UI_WIFI_MODE_CHANNELS_24 :
                    (action == SENTINEL_GUIDE_OPEN_WIFI_BASELINE ? UI_WIFI_MODE_BASELINE : UI_WIFI_MODE_LIST);
                wifi_baseline.checked = false;
                if (wifi_mode == UI_WIFI_MODE_CHANNELS_24) wifi_channel_survey_start_ui();
                else (void)refresh_wifi_scan_status();
            }
        } else if (action == SENTINEL_GUIDE_OPEN_BLE || action == SENTINEL_GUIDE_OPEN_BLE_SIGNAL) {
            open_tool_page(UI_PAGE_BLE);
            if (!ui_capability_blocked) {
                ble_mode = action == SENTINEL_GUIDE_OPEN_BLE_SIGNAL && ble_selection_valid ?
                    UI_BLE_MODE_DETAIL : UI_BLE_MODE_LIST;
                (void)refresh_ble_scan_status();
            }
        } else {
            open_tool_page(UI_PAGE_IEEE802154);
            if (!ui_capability_blocked) {
                ieee802154_mode = action == SENTINEL_GUIDE_OPEN_RADIO_ACTIVITY ?
                    UI_IEEE802154_MODE_ACTIVITY : UI_IEEE802154_MODE_ENERGY;
                ieee802154_scan_checked = false;
                ieee802154_activity_checked = false;
            }
        }
        ESP_ERROR_CHECK(draw_detail_page());
        return;
    }

    if (learning_tool_active &&
        current_page != UI_PAGE_BLE_SOUND &&
        !(current_page == UI_PAGE_BLE &&
          (ble_mode == UI_BLE_MODE_FOX_HUNT ||
           ble_mode == UI_BLE_MODE_ANALYZER)) &&
        x >= 12 && x < 308 &&
        y >= UI_BACK_Y && y < UI_BACK_Y + UI_BACK_HEIGHT) {
        /* A partially entered network password must not outlive its screen. */
        secure_clear_buffer(wifi_connection_password, sizeof(wifi_connection_password));
        secure_clear_buffer((char *)&wifi_keyboard, sizeof(wifi_keyboard));
        return_to_launcher();
        return;
    }
    if (current_page == UI_PAGE_TOOLS) {
        handle_tools_tap(x, y);
        return;
    }

    if (current_page == UI_PAGE_SELF_TEST && self_test_mode == UI_SELF_TEST_MODE_TOUCH) {
        handle_self_test_tap(x, y);
        return;
    }

    if (current_page == UI_PAGE_SETTINGS &&
        settings_mode == UI_SETTINGS_MODE_TOUCH_CALIBRATION &&
        touch_calibration_step < 4U && y < UI_BACK_Y) {
        handle_touch_calibration_sample();
        return;
    }

    if (current_page != UI_PAGE_TOOLS) {
        if (y >= UI_BACK_Y) {
            if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_FINDMY_MONITOR) {
                ble_findmy_request_stop(true, false);
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_FOX_HUNT) {
                ble_session_exit_page = ble_attack_selecting ? UI_PAGE_BLE_ATTACK :
                    (learning_tool_active ? tool_parent_page : UI_PAGE_BLE);
                if (!ble_fox_hunt_exit_pending) {
                    ble_fox_hunt_exit_pending = true;
                    ble_fox_hunt_exit_started_ms =
                        (uint32_t)(esp_timer_get_time() / 1000LL);
                }
                ble_fox_hunt_last_tick_ms = 0U;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_ANALYZER) {
                ble_session_exit_page = ble_attack_selecting ? UI_PAGE_BLE_ATTACK :
                    (learning_tool_active ? tool_parent_page : UI_PAGE_BLE);
                if (!ble_analyzer_exit_pending) {
                    ble_analyzer_exit_pending = true;
                    ble_analyzer_exit_started_ms =
                        (uint32_t)(esp_timer_get_time() / 1000LL);
                }
                ble_analyzer_last_tick_ms = 0U;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_attack_selecting) {
                ble_attack_selecting = false;
                open_tool_page(UI_PAGE_BLE_ATTACK);
            } else if (!learning_tool_active && !live_is_view() && live_page_tool() != SENTINEL_SCAN_NONE && x >= 184U) {
                live_toggle_pause();
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (live_is_view()) {
                return_to_launcher();
            } else if (ui_capability_blocked) {
                return_to_launcher();
            } else if (current_page == UI_PAGE_BLE_ATTACK) {
                ble_attack_start_pending = false;
                cerberus_s3_ble_attack_status_t status;
                cerberus_s3_ble_attack_get_status(&status);
                if (status.job_id == ble_attack_job_id && ble_attack_job_id != 0U &&
                    (status.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
                     status.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
                     status.state == CERBERUS_S3_BLE_ATTACK_STOPPING)) {
                    ble_attack_return_pending = true;
                    (void)cerberus_s3_ble_attack_stop(ble_attack_job_id);
                    ble_attack_last_draw_ms = 0U;
                    ESP_ERROR_CHECK(draw_detail_page());
                } else if (cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_ADVERTISER) {
                    ble_attack_start_error = ESP_ERR_INVALID_STATE;
                    ESP_ERROR_CHECK(draw_detail_page());
                } else {
                    return_to_launcher();
                }
            } else if (current_page == UI_PAGE_BLE_SOUND) {
                ble_sound_running = false;
                ble_sound_exit_pending = true;
                if (!ble_sound_stop_pending) {
                    ble_sound_stop_pending = true;
                    ble_sound_stop_started_ms =
                        (uint32_t)(esp_timer_get_time() / 1000LL);
                }
                ble_sound_last_tick_ms = 0U;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WARDRIVE) {
                wardrive_start_pending = false;
                cerberus_wardrive_status_t status;
                cerberus_wardrive_get_status(&status);
                if (status.state == CERBERUS_WARDRIVE_STARTING ||
                    status.state == CERBERUS_WARDRIVE_RUNNING ||
                    status.state == CERBERUS_WARDRIVE_STOPPING) {
                    wardrive_return_pending = true;
                    cerberus_wardrive_request_stop();
                    wardrive_last_draw_ms = 0U;
                    ESP_ERROR_CHECK(draw_detail_page());
                } else {
                    return_to_launcher();
                }
            } else if (current_page == UI_PAGE_DASHBOARD &&
                dashboard_mode == UI_DASHBOARD_MODE_CRASH_CLEAR_CONFIRM) {
                dashboard_mode = UI_DASHBOARD_MODE_CRASH_RECORD;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_DASHBOARD &&
                dashboard_mode == UI_DASHBOARD_MODE_CRASH_RECORD) {
                dashboard_mode = UI_DASHBOARD_MODE_SUMMARY;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_IMAGE) {
                file_mode = UI_FILE_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_INFO) {
                file_mode = UI_FILE_MODE_TEXT;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_TEXT) {
                file_mode = UI_FILE_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_PACKAGE_INFO) {
                file_mode = UI_FILE_MODE_ACTIONS;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_DELETE_CONFIRM) {
                file_mode = UI_FILE_MODE_ACTIONS;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES && file_mode == UI_FILE_MODE_NAME_INPUT) {
                file_mode = file_operation == UI_FILE_OPERATION_RENAME ? UI_FILE_MODE_ACTIONS : UI_FILE_MODE_TOOLS;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_FILES &&
                (file_mode == UI_FILE_MODE_TOOLS || file_mode == UI_FILE_MODE_SELECT || file_mode == UI_FILE_MODE_ACTIONS)) {
                file_mode = UI_FILE_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_LOGS &&
                log_mode == UI_LOG_MODE_DELETE_CONFIRM) {
                log_mode = UI_LOG_MODE_INFO;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_QR) {
                log_mode = UI_LOG_MODE_INFO;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_INFO) {
                log_mode = UI_LOG_MODE_TEXT;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_LOGS && log_mode == UI_LOG_MODE_TEXT) {
                log_mode = UI_LOG_MODE_LIST;
                log_checked = false;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_SETTINGS && settings_reset_confirmation) {
                settings_reset_confirmation = false;
                settings_reset_error = ESP_OK;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_SETTINGS && settings_restore_confirmation) {
                settings_restore_confirmation = false;
                settings_restore_checked = false;
                settings_restore_error = ESP_ERR_INVALID_STATE;
                settings_restore_length = 0U;
                memset(settings_backup_buffer, 0, sizeof(settings_backup_buffer));
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_SETTINGS &&
                settings_mode == UI_SETTINGS_MODE_TOUCH_CALIBRATION) {
                settings_mode = UI_SETTINGS_MODE_ACCESSIBILITY;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_SETTINGS &&
                settings_mode != UI_SETTINGS_MODE_DISPLAY) {
                settings_mode = UI_SETTINGS_MODE_DISPLAY;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_QR && qr_mode != UI_QR_MODE_MENU) {
                if (qr_mode_is_wifi(qr_mode)) {
                    clear_qr_wifi_credentials();
                }
                reset_qr_save_status();
                qr_mode = UI_QR_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_USB &&
                usb_mode == UI_USB_MODE_RECOVERY_CONFIRM) {
                usb_mode = UI_USB_MODE_SUMMARY;
                usb_recovery_error = ESP_OK;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_RADIO_LAB) {
                lab_stop(); return_to_launcher();
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PASSWORD) {
                memset(wifi_connection_password, 0, sizeof(wifi_connection_password));
                memset(&wifi_keyboard, 0, sizeof(wifi_keyboard));
                wifi_mode = UI_WIFI_MODE_TARGET;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS) {
                wifi_relation_back(); ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_STATIONS) {
                station_cancel();
                if(ap_station_selecting){ap_station_selecting=false;ap_enter();wifi_mode=UI_WIFI_MODE_AP_JOB;}
                else wifi_mode=UI_WIFI_MODE_TARGET;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_AP_JOB) {
                ap_stop();wifi_target_open_attacks();ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PORTAL) {
                if (portal_back()) wifi_target_open_attacks();
                ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CAPTURE) {
                if (capture_back()) wifi_mode = UI_WIFI_MODE_TARGET;
                ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MULTISSID) {
                if (multissid_back()) wifi_mode = UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PINESCAN) {
                if (pinescan_back()) wifi_mode = UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_NETWORK_SCAN) {
                if (net_editing) net_editing = 0;
                else { net_cancel(); wifi_mode = UI_WIFI_MODE_CONNECTION; }
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CONNECTION) {
                wifi_mode = wifi_selection_valid ? UI_WIFI_MODE_TARGET : UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_DIAGNOSTICS) {
                wifi_mode = UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MDNS) {
                wifi_mode = UI_WIFI_MODE_DIAGNOSTICS;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_DETAIL) {
                wifi_mode = UI_WIFI_MODE_TARGET;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_TARGET) {
                wifi_mode = UI_WIFI_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_LIST && wifi_attack_selecting) {
                wifi_attack_selecting = false;
                current_page = UI_PAGE_FIELD_MENU;
                ESP_ERROR_CHECK(draw_current_page());
            } else if (current_page == UI_PAGE_WIFI &&
                wifi_mode == UI_WIFI_MODE_PACKET_MONITOR) {
                if (packet_monitor_stop_ui()) wifi_mode =
                    packet_monitor_from_target ? UI_WIFI_MODE_TARGET : UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI &&
                (wifi_mode == UI_WIFI_MODE_CHANNELS_24 ||
                 wifi_mode == UI_WIFI_MODE_CHANNELS_5)) {
                if (wifi_channel_survey_stop_ui()) wifi_mode = UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_WIFI && wifi_mode != UI_WIFI_MODE_MENU) {
                wifi_mode = UI_WIFI_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_DETAIL) {
                ble_mode = ble_selected_from_monitor && ble_findmy_monitor ?
                    UI_BLE_MODE_FINDMY_MONITOR : UI_BLE_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE &&
                ble_mode == UI_BLE_MODE_ENVIRONMENT) {
                ble_mode = UI_BLE_MODE_MENU;
                reset_ble_environment_viewer();
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_SERVICES) {
                ble_mode = UI_BLE_MODE_DETAIL;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE &&
                ble_mode == UI_BLE_MODE_CHARACTERISTICS) {
                ble_mode = UI_BLE_MODE_SERVICES;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_VALUE) {
                ble_mode = UI_BLE_MODE_CHARACTERISTICS;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST) {
                if (ble_discovery_page()) ble_discovery_leave();
                ble_mode = UI_BLE_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_IEEE802154 &&
                ieee802154_mode != UI_IEEE802154_MODE_MENU) {
                ieee802154_mode = UI_IEEE802154_MODE_MENU;
                ESP_ERROR_CHECK(draw_detail_page());
            } else if (current_page == UI_PAGE_SCRIPTS &&
                script_mode != UI_SCRIPT_MODE_LIST) {
                script_running = false;
                script_complete = false;
                script_mode = UI_SCRIPT_MODE_LIST;
                ESP_ERROR_CHECK(draw_detail_page());
            } else {
                return_to_launcher();
            }
        } else if (ui_capability_blocked) {
            return;
        } else if (current_page == UI_PAGE_RADIO_LAB) {
            lab_tap(x,y); ESP_ERROR_CHECK(draw_detail_page());
        } else if (current_page == UI_PAGE_DASHBOARD) {
            handle_dashboard_tap(x, y);
        } else if (current_page == UI_PAGE_SETTINGS) {
            handle_settings_tap(x, y);
        } else if (current_page == UI_PAGE_WIFI) {
            handle_wifi_tap(x, y);
        } else if (current_page == UI_PAGE_BLE) {
            handle_ble_tap(x, y);
        } else if (current_page == UI_PAGE_BLE_ATTACK) {
            ble_attack_handle_tap(x, y);
        } else if (current_page == UI_PAGE_BLE_SOUND) {
            ble_sound_handle_tap(x, y);
        } else if (current_page == UI_PAGE_WARDRIVE) {
            wardrive_handle_tap(x, y);
        } else if (current_page == UI_PAGE_IEEE802154) {
            handle_ieee802154_tap(x, y);
        } else if (current_page == UI_PAGE_SCRIPTS) {
            handle_scripts_tap(x, y);
        } else if (current_page == UI_PAGE_FILES) {
            handle_files_tap(x, y);
        } else if (current_page == UI_PAGE_QR) {
            handle_qr_tap(x, y);
        } else if (current_page == UI_PAGE_USB) {
            handle_usb_tap(x, y);
        } else if (current_page == UI_PAGE_WEB) {
            if (x >= 24 && x < 296 && y >= 360 && y < 408) {
                cerberus_web_ui_status_t status;
                cerberus_web_ui_get_status(&status);
                web_action_error = status.running || status.radio_active ? cerberus_web_ui_stop() : cerberus_web_ui_start();
                ESP_ERROR_CHECK(draw_detail_page());
            }
        } else if (current_page == UI_PAGE_LOGS) {
            handle_logs_tap(x, y);
        } else if (current_page == UI_PAGE_SELF_TEST) {
            handle_self_test_tap(x, y);
        }
        return;
    }


}

void cerberus_ui_run(const cerberus_ui_config_t *config)
{
    if (config == NULL) {
        ESP_LOGE(TAG, "UI configuration is required");
        return;
    }

    ui_config = *config;
    esp_err_t buffer_error = cerberus_display_enable_buffering();
    if (buffer_error != ESP_OK) {
        ESP_LOGW(TAG, "display buffering unavailable; direct drawing: %s", esp_err_to_name(buffer_error));
    }
    sentinel_scan_session_init(&live_session);
    esp_err_t guide_error = sentinel_guide_init();
    if (guide_error != ESP_OK) {
        ESP_LOGW(TAG, "Field Guide progress storage: %s", esp_err_to_name(guide_error));
    }
    ui_screen_timeout_seconds = cerberus_settings_get()->screen_timeout_seconds;
    ui_accessibility_size = cerberus_settings_get()->accessibility_size <= 1U
        ? cerberus_settings_get()->accessibility_size
        : 0U;
    storage_checked = true;
    storage_safely_removed = false;
    storage_last_error = ui_config.storage_available
        ? cerberus_storage_get_status(&storage_status)
        : ESP_ERR_NOT_FOUND;
    int64_t service_start_ms = esp_timer_get_time() / 1000LL;
    cerberus_battery_init();
    (void)cerberus_battery_poll(service_start_ms);
    sentinel_battery_view_invalidate();
    cerberus_ui_time_service_init(service_start_ms);
    refresh_c5_status();
    ESP_ERROR_CHECK(draw_main_menu());

    present_ui_frame();

    int64_t last_c5_refresh_ms = esp_timer_get_time() / 1000;
    int64_t last_wifi_refresh_ms = last_c5_refresh_ms;
    int64_t last_ble_list_refresh_ms = last_c5_refresh_ms;
    int64_t last_ble_connection_refresh_ms = last_c5_refresh_ms;
    int64_t last_ble_service_refresh_ms = last_c5_refresh_ms;
    int64_t last_ble_characteristic_refresh_ms = last_c5_refresh_ms;
    int64_t last_ble_read_refresh_ms = last_c5_refresh_ms;
    int64_t last_status_refresh_ms = last_c5_refresh_ms;
    int64_t last_health_refresh_ms = last_c5_refresh_ms;
    const int64_t touch_guard_until_ms = last_c5_refresh_ms + UI_TOUCH_STARTUP_GUARD_MS;
    int64_t last_touch_sample_ms = 0;
    int64_t last_user_activity_ms = last_c5_refresh_ms;
    bool touch_locked = false;
    bool screen_asleep = false;
    bool heap_warning_logged = false;
    bool stack_warning_logged = false;
    uint8_t awake_brightness = cerberus_display_get_brightness();

    while (true) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        bool lab_visible=current_page==UI_PAGE_RADIO_LAB&&!screen_asleep;
        if(lab_visible||lab_busy()){
            uint32_t previous=lab_last_poll;lab_tick((uint32_t)now_ms,lab_visible);
            if(lab_visible&&previous!=lab_last_poll)ESP_ERROR_CHECK(draw_detail_page());
        }
        (void)cerberus_battery_poll(now_ms);
        live_scan_tick((uint32_t)now_ms, screen_asleep);
        wifi_relation_tick((uint32_t)now_ms, !screen_asleep && !wifi_attack_selecting &&
            current_page == UI_PAGE_WIFI && (wifi_mode == UI_WIFI_MODE_LIST ||
            wifi_mode == UI_WIFI_MODE_TARGET || wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS),
            !screen_asleep && current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_OBSERVED_CLIENTS);
        remote_id_tick((uint32_t)now_ms, screen_asleep);
        mac_monitor_tick((uint32_t)now_ms, screen_asleep);
        ble_attack_tick((uint32_t)now_ms, screen_asleep);
        wardrive_tick((uint32_t)now_ms, screen_asleep);
        ble_fox_hunt_tick((uint32_t)now_ms, screen_asleep);
        ble_analyzer_tick((uint32_t)now_ms, screen_asleep);
        ble_sound_tick((uint32_t)now_ms, screen_asleep);
        ble_findmy_monitor_tick((uint32_t)now_ms, screen_asleep);
        bool wardrive_page_active = false;
        if (current_page == UI_PAGE_WARDRIVE) {
            cerberus_wardrive_status_t wardrive_runtime;
            cerberus_wardrive_get_status(&wardrive_runtime);
            wardrive_page_active = wardrive_runtime.state == CERBERUS_WARDRIVE_STARTING ||
                wardrive_runtime.state == CERBERUS_WARDRIVE_RUNNING ||
                wardrive_runtime.state == CERBERUS_WARDRIVE_STOPPING;
        }
        bool ble_attack_page_active = false;
        if (current_page == UI_PAGE_BLE_ATTACK) {
            cerberus_s3_ble_attack_status_t attack_runtime;
            cerberus_s3_ble_attack_get_status(&attack_runtime);
            ble_attack_page_active = attack_runtime.job_id == ble_attack_job_id &&
                ble_attack_job_id != 0U &&
                (attack_runtime.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
                 attack_runtime.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
                 attack_runtime.state == CERBERUS_S3_BLE_ATTACK_STOPPING);
        }
        bool network_visible = current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_NETWORK_SCAN && !screen_asleep;
        bool station_visible=current_page==UI_PAGE_WIFI&&wifi_mode==UI_WIFI_MODE_STATIONS&&!screen_asleep;
        bool ap_visible=current_page==UI_PAGE_WIFI&&wifi_mode==UI_WIFI_MODE_AP_JOB&&!screen_asleep;
        if(ap_visible||ap_busy()){
            uint32_t previous=ap_last_poll;ap_poll((uint32_t)now_ms,ap_visible);
            if(ap_visible&&previous!=ap_last_poll)ESP_ERROR_CHECK(draw_detail_page());
        }
        bool portal_visible = current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_PORTAL && !screen_asleep;
        if (portal_visible) {
            uint32_t previous = portal_last_poll;
            portal_tick((uint32_t)now_ms);
            if (portal_last_poll != previous) ESP_ERROR_CHECK(draw_detail_page());
        }
        bool capture_visible = current_page == UI_PAGE_WIFI &&
            wifi_mode == UI_WIFI_MODE_CAPTURE && !screen_asleep;
        if (capture_visible && capture_tick((uint32_t)now_ms))
            ESP_ERROR_CHECK(draw_detail_page());
        bool pinescan_visible = current_page == UI_PAGE_WIFI &&
            wifi_mode == UI_WIFI_MODE_PINESCAN && !screen_asleep;
        if (pinescan_tick((uint32_t)now_ms, pinescan_visible) && pinescan_visible)
            ESP_ERROR_CHECK(draw_detail_page());
        bool multissid_visible = current_page == UI_PAGE_WIFI &&
            wifi_mode == UI_WIFI_MODE_MULTISSID && !screen_asleep;
        if (multissid_tick((uint32_t)now_ms, multissid_visible) && multissid_visible)
            ESP_ERROR_CHECK(draw_detail_page());
        csi_live_tick(current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_CSI && !screen_asleep);
        bool packet_monitor_visible = current_page == UI_PAGE_WIFI &&
            wifi_mode == UI_WIFI_MODE_PACKET_MONITOR && !screen_asleep &&
            c5_status.firmware_version >= CERBERUS_WIFI_PACKET_MONITOR_MIN_FIRMWARE;
        if (packet_monitor_visible) {
            uint32_t previous_poll = packet_monitor_last_poll;
            packet_monitor_refresh((uint32_t)now_ms);
            if (previous_poll != packet_monitor_last_poll)
                ESP_ERROR_CHECK(draw_detail_page());
        } else if (packet_monitor_attempted &&
            (current_page != UI_PAGE_WIFI ||
             wifi_mode != UI_WIFI_MODE_PACKET_MONITOR || screen_asleep)) {
            (void)packet_monitor_stop_ui();
        }
        bool channels_visible = current_page == UI_PAGE_WIFI &&
            (wifi_mode == UI_WIFI_MODE_CHANNELS_24 ||
             wifi_mode == UI_WIFI_MODE_CHANNELS_5) && !screen_asleep &&
            c5_status.firmware_version >= CERBERUS_WIFI_CHANNEL_SURVEY_MIN_FIRMWARE;
        if (channels_visible) {
            uint32_t previous_poll = wifi_channel_survey_last_poll;
            wifi_channel_survey_refresh((uint32_t)now_ms);
            if (previous_poll != wifi_channel_survey_last_poll)
                ESP_ERROR_CHECK(draw_detail_page());
        } else if (wifi_channel_survey_attempted) {
            (void)wifi_channel_survey_stop_ui();
        }
        if(station_visible||station_busy()){
            uint32_t previous=station_last_poll;station_poll((uint32_t)now_ms,station_visible);
            if(station_visible&&previous!=station_last_poll)ESP_ERROR_CHECK(draw_detail_page());
        }
        if (network_visible || net_busy()) {
            uint32_t previous_poll = net_last_poll;
            net_poll((uint32_t)now_ms, network_visible);
            if (network_visible && net_last_poll != previous_poll) ESP_ERROR_CHECK(draw_detail_page());
        }
        if ((current_page != UI_PAGE_WIFI || wifi_mode != UI_WIFI_MODE_FIELD_SURVEY || screen_asleep) &&
            !(wardrive_page_active && wardrive_include_expansions))
            (void)field_survey_set_running(false);
        if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_FIELD_SURVEY && !screen_asleep &&
            now_ms - last_status_refresh_ms >= 1000) {
            ESP_ERROR_CHECK(draw_detail_page());
            last_status_refresh_ms = now_ms;
        }
        if (!screen_asleep) watch_ui_tick((uint32_t)now_ms);
        bool notification_added = cerberus_ui_time_service_tick(now_ms);
        if (notification_added && !screen_asleep && current_page == UI_PAGE_SETTINGS &&
            (settings_mode == UI_SETTINGS_MODE_CLOCK_TIMER ||
                settings_mode == UI_SETTINGS_MODE_NOTIFICATIONS)) {
            ESP_ERROR_CHECK(draw_detail_page());
        }
        if (current_page == UI_PAGE_SCRIPTS &&
            script_mode == UI_SCRIPT_MODE_RUNNING && script_running &&
            advance_script(now_ms) && !screen_asleep) {
            ESP_ERROR_CHECK(draw_detail_page());
        }
        if (ui_config.touch_available && !screen_asleep && !ap_busy() && !capture_busy() && !multissid_busy() && !pinescan_busy() &&
            !(current_page == UI_PAGE_BLE_ATTACK &&
                (ble_attack_start_pending || ble_attack_return_pending || ble_attack_page_active)) &&
            !(current_page == UI_PAGE_BLE_SOUND &&
                (ble_sound_running || ble_sound_stop_pending)) &&
            !(current_page == UI_PAGE_WARDRIVE &&
                (wardrive_start_pending || wardrive_return_pending || wardrive_page_active)) &&
            !(current_page == UI_PAGE_BLE &&
                (ble_mode == UI_BLE_MODE_FOX_HUNT ||
                 ble_mode == UI_BLE_MODE_ANALYZER)) &&
            ui_screen_timeout_seconds > 0U &&
            !watch_keeps_awake() &&
            !remote_id_keeps_awake() &&
            !mac_monitor_keeps_awake() &&
            !ble_discovery_keeps_awake() &&
            !(current_page == UI_PAGE_SCRIPTS && script_running) &&
            now_ms - last_user_activity_ms >= (int64_t)ui_screen_timeout_seconds * 1000LL) {
            awake_brightness = cerberus_display_get_brightness();
            esp_err_t sleep_err = cerberus_display_set_brightness(0U);
            if (sleep_err == ESP_OK) {
                screen_asleep = true;
                ESP_LOGI(TAG, "screen asleep after %us", (unsigned)ui_screen_timeout_seconds);
            } else {
                ESP_LOGW(TAG, "screen sleep failed: %s", esp_err_to_name(sleep_err));
                last_user_activity_ms = now_ms;
            }
        }
        if (now_ms - last_c5_refresh_ms >= UI_C5_REFRESH_MS &&
            !ble_discovery_defers_c5_telemetry() && !(wifi_relation_ui && wifi_relation_ui->reading)) {
            refresh_c5_status();
            last_c5_refresh_ms = now_ms;
        }
        if (current_page == UI_PAGE_BLE && ble_mode == UI_BLE_MODE_LIST &&
            !screen_asleep && ble_scan_checked &&
            ble_scan_status.state == CERBERUS_BLE_SCAN_RUNNING &&
            now_ms - last_ble_list_refresh_ms >= UI_BLE_REFRESH_MS) {
            uint8_t previous_state = ble_scan_status.state;
            esp_err_t previous_error = ble_scan_error;
            (void)refresh_ble_scan_status();
            if (previous_state != ble_scan_status.state ||
                previous_error != ble_scan_error) ESP_ERROR_CHECK(draw_detail_page());
            last_ble_list_refresh_ms = now_ms;
        }
        if (current_page == UI_PAGE_MAIN && !screen_asleep &&
            now_ms - last_status_refresh_ms >= UI_STATUS_REFRESH_MS) {
            sentinel_dashboard_status_t status = sentinel_status();
            ESP_ERROR_CHECK(sentinel_dashboard_animate(&status, ++sentinel_frame));
            last_status_refresh_ms = now_ms;
        } else if (current_page == UI_PAGE_GUIDE && !screen_asleep &&
            sentinel_guide_tick((uint32_t)now_ms)) {
            ESP_ERROR_CHECK(sentinel_guide_draw());
        } else if (current_page == UI_PAGE_SETTINGS && !screen_asleep &&
            settings_mode == UI_SETTINGS_MODE_CLOCK_TIMER &&
            now_ms - last_status_refresh_ms >= UI_STATUS_REFRESH_MS) {
            ESP_ERROR_CHECK(draw_detail_page());
            last_status_refresh_ms = now_ms;
        }
        if (now_ms - last_health_refresh_ms >= UI_HEALTH_REFRESH_MS) {
            cerberus_system_health_t health;
            cerberus_system_health_snapshot(&health);
            if (!heap_warning_logged &&
                health.minimum_internal_bytes < CERBERUS_HEAP_WARNING_BYTES) {
                (void)cerberus_event_bus_publish(
                    CERBERUS_EVENT_HEAP_PRESSURE,
                    (int32_t)health.minimum_internal_bytes);
                heap_warning_logged = true;
            }
            if (!stack_warning_logged &&
                health.main_task_minimum_stack_bytes < CERBERUS_MAIN_STACK_WARNING_BYTES) {
                (void)cerberus_event_bus_publish(
                    CERBERUS_EVENT_STACK_PRESSURE,
                    (int32_t)health.main_task_minimum_stack_bytes);
                stack_warning_logged = true;
            }
            if (current_page == UI_PAGE_DASHBOARD &&
                dashboard_mode == UI_DASHBOARD_MODE_SUMMARY && !screen_asleep) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_health_refresh_ms = now_ms;
        }

        if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_MDNS &&
            now_ms - last_wifi_refresh_ms >= UI_WIFI_REFRESH_MS) {
            cerberus_mdns_scan_status_payload_t previous = mdns_scan_status;
            esp_err_t previous_error = mdns_scan_error;
            mdns_scan_error = c5_spi_link_get_mdns_scan_status(&mdns_scan_status);
            bool changed = memcmp(&previous, &mdns_scan_status, sizeof(previous)) != 0 ||
                previous_error != mdns_scan_error;
            if (mdns_scan_error == ESP_OK &&
                (changed || mdns_result_count == 0U) && mdns_scan_status.result_count > 0U) {
                esp_err_t load_error = load_mdns_results();
                if (load_error != ESP_OK) mdns_scan_error = load_error;
            }
            if (changed || previous_error != mdns_scan_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_wifi_refresh_ms = now_ms;
        } else if (current_page == UI_PAGE_WIFI && wifi_mode == UI_WIFI_MODE_DIAGNOSTICS &&
            now_ms - last_wifi_refresh_ms >= UI_WIFI_REFRESH_MS) {
            cerberus_network_diagnostics_status_payload_t previous = network_diagnostics_status;
            esp_err_t previous_error = network_diagnostics_error;
            network_diagnostics_error = c5_spi_link_get_network_diagnostics_status(
                &network_diagnostics_status);
            if (memcmp(&previous, &network_diagnostics_status, sizeof(previous)) != 0 ||
                previous_error != network_diagnostics_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_wifi_refresh_ms = now_ms;
        } else if (current_page == UI_PAGE_WIFI &&
            (wifi_mode == UI_WIFI_MODE_CONNECTION || wifi_mode == UI_WIFI_MODE_TARGET) &&
            now_ms - last_wifi_refresh_ms >= UI_WIFI_REFRESH_MS) {
            cerberus_wifi_connection_status_payload_t previous = wifi_connection_status;
            cerberus_wifi_dns_status_payload_t previous_dns = wifi_dns_status;
            esp_err_t previous_error = wifi_connection_error;
            esp_err_t previous_dns_error = wifi_dns_error;
            wifi_connection_error = c5_spi_link_get_wifi_connection_status(&wifi_connection_status);
            if (wifi_connection_error == ESP_OK &&
                wifi_connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED &&
                (c5_status.capabilities & CERBERUS_C5_CAP_WIFI_DNS_STATUS) != 0U) {
                wifi_dns_error = c5_spi_link_get_wifi_dns_status(&wifi_dns_status);
            } else {
                memset(&wifi_dns_status, 0, sizeof(wifi_dns_status));
                wifi_dns_error = ESP_ERR_INVALID_STATE;
            }
            if (memcmp(&previous, &wifi_connection_status, sizeof(previous)) != 0 ||
                memcmp(&previous_dns, &wifi_dns_status, sizeof(previous_dns)) != 0 ||
                previous_error != wifi_connection_error || previous_dns_error != wifi_dns_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_wifi_refresh_ms = now_ms;
        }

        if (current_page == UI_PAGE_BLE && ble_connection_checked &&
            (ble_mode == UI_BLE_MODE_DETAIL || ble_connection_is_active()) &&
            now_ms - last_ble_connection_refresh_ms >= UI_BLE_REFRESH_MS) {
            uint8_t previous_state = ble_connection_status.state;
            uint8_t previous_generation = ble_connection_status.generation;
            int32_t previous_c5_error = ble_connection_status.error_code;
            esp_err_t previous_link_error = ble_connection_error;
            (void)refresh_ble_connection_status();
            if (ble_connection_status.state != previous_state ||
                ble_connection_status.generation != previous_generation) {
                ble_service_checked = false;
                ble_characteristic_checked = false;
                ble_read_checked = false;
                if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED) {
                    set_ble_action_message("CONNECTED - DISCOVER SERVICES", UI_GOOD);
                } else if (ble_connection_status.state == CERBERUS_BLE_CONNECTION_ERROR) {
                    set_ble_action_message("CONNECTION FAILED - TAP TO RETRY", UI_BAD);
                } else if (previous_state == CERBERUS_BLE_CONNECTION_DISCONNECTING &&
                    ble_connection_status.state == CERBERUS_BLE_CONNECTION_IDLE) {
                    set_ble_action_message("DISCONNECTED", UI_MUTED);
                }
                if ((ble_mode == UI_BLE_MODE_SERVICES ||
                    ble_mode == UI_BLE_MODE_CHARACTERISTICS ||
                    ble_mode == UI_BLE_MODE_VALUE) &&
                    ble_connection_status.state != CERBERUS_BLE_CONNECTION_CONNECTED) {
                    ble_mode = UI_BLE_MODE_DETAIL;
                }
                if (ble_mode == UI_BLE_MODE_ENVIRONMENT &&
                    ble_connection_status.state != CERBERUS_BLE_CONNECTION_CONNECTED) {
                    reset_ble_environment_viewer();
                }
            }
            if (ble_connection_status.state != previous_state ||
                ble_connection_status.generation != previous_generation ||
                ble_connection_status.error_code != previous_c5_error ||
                ble_connection_error != previous_link_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_ble_connection_refresh_ms = now_ms;
        }

        if (current_page == UI_PAGE_BLE &&
            (ble_mode == UI_BLE_MODE_DETAIL || ble_mode == UI_BLE_MODE_ENVIRONMENT) &&
            ble_service_checked &&
            ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING &&
            now_ms - last_ble_service_refresh_ms >= UI_BLE_REFRESH_MS) {
            uint8_t previous_state = ble_service_status.state;
            uint8_t previous_generation = ble_service_status.generation;
            int32_t previous_c5_error = ble_service_status.error_code;
            esp_err_t previous_link_error = ble_service_error;
            (void)refresh_ble_service_status();
            if (ble_mode == UI_BLE_MODE_ENVIRONMENT &&
                ble_service_error == ESP_OK &&
                ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
                ble_environment_error = find_ble_environment_service();
            }
            if (ble_service_status.state != previous_state) {
                if (ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
                    set_ble_action_message("DISCOVERY COMPLETE - TAP VIEW", UI_GOOD);
                } else if (ble_service_status.state == CERBERUS_BLE_GATT_SERVICE_ERROR) {
                    set_ble_action_message("SERVICE DISCOVERY FAILED - RETRY", UI_BAD);
                }
            }
            if (ble_service_status.state != previous_state ||
                ble_service_status.generation != previous_generation ||
                ble_service_status.error_code != previous_c5_error ||
                ble_service_error != previous_link_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_ble_service_refresh_ms = now_ms;
        }

        if (current_page == UI_PAGE_BLE &&
            (ble_mode == UI_BLE_MODE_CHARACTERISTICS ||
                ble_mode == UI_BLE_MODE_ENVIRONMENT) &&
            ble_characteristic_checked &&
            ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
            now_ms - last_ble_characteristic_refresh_ms >= UI_BLE_REFRESH_MS) {
            uint8_t previous_state = ble_characteristic_status.state;
            uint8_t previous_generation = ble_characteristic_status.generation;
            int32_t previous_c5_error = ble_characteristic_status.error_code;
            esp_err_t previous_link_error = ble_characteristic_error;
            (void)refresh_ble_characteristic_status();
            if (ble_mode == UI_BLE_MODE_ENVIRONMENT &&
                ble_characteristic_error == ESP_OK &&
                ble_characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
                ble_environment_error = load_ble_environment_characteristics();
            }
            if (ble_characteristic_status.state != previous_state ||
                ble_characteristic_status.generation != previous_generation ||
                ble_characteristic_status.error_code != previous_c5_error ||
                ble_characteristic_error != previous_link_error) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_ble_characteristic_refresh_ms = now_ms;
        }

        if (current_page == UI_PAGE_BLE &&
            (ble_mode == UI_BLE_MODE_VALUE || ble_mode == UI_BLE_MODE_ENVIRONMENT) &&
            ble_read_checked && ble_read_status.state == CERBERUS_BLE_GATT_READ_RUNNING &&
            now_ms - last_ble_read_refresh_ms >= UI_BLE_REFRESH_MS) {
            uint8_t previous_state = ble_read_status.state;
            uint8_t previous_generation = ble_read_status.generation;
            int32_t previous_c5_error = ble_read_status.error_code;
            esp_err_t previous_link_error = ble_read_error;
            (void)refresh_ble_read_status();
            if (ble_mode == UI_BLE_MODE_ENVIRONMENT) {
                continue_ble_environment_reads();
            }
            if (ble_read_status.state != previous_state ||
                ble_read_status.generation != previous_generation ||
                ble_read_status.error_code != previous_c5_error ||
                ble_read_error != previous_link_error ||
                ble_mode == UI_BLE_MODE_ENVIRONMENT) {
                ESP_ERROR_CHECK(draw_detail_page());
            }
            last_ble_read_refresh_ms = now_ms;
        }

        if (ui_config.touch_available) {
            cerberus_touch_point_t point;
            bool touched = false;
            esp_err_t err = cerberus_touch_read(&point, &touched);
            if (err == ESP_OK && touched) {
                last_touch_sample_ms = now_ms;
                if (!touch_locked && now_ms >= touch_guard_until_ms) {
                    touch_locked = true;
                    if (screen_asleep) {
                        present_ui_frame();
                        esp_err_t wake_err = cerberus_display_set_brightness(awake_brightness);
                        if (wake_err == ESP_OK) {
                            screen_asleep = false;
                            last_user_activity_ms = now_ms;
                            ESP_LOGI(TAG, "screen awake");
                        } else {
                            ESP_LOGW(TAG, "screen wake failed: %s", esp_err_to_name(wake_err));
                        }
                    } else {
                        last_user_activity_ms = now_ms;
                        ESP_LOGI(TAG, "touch x=%u y=%u page=%u", point.x, point.y, current_page);
                        handle_tap(point.x, point.y);
                    }
                }
            } else if (err != ESP_OK) {
                ESP_LOGW(TAG, "touch read failed: %s", esp_err_to_name(err));
            }

            if (touch_locked && now_ms - last_touch_sample_ms >= UI_TOUCH_REARM_MS) {
                touch_locked = false;
            }
        }

        if (!screen_asleep) present_ui_frame();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
