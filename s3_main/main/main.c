#include "boot_logo.h"
#include "c5_control.h"
#include "c5_event_pump.h"
#include "c5_flash_bridge.h"
#include "c5_spi_link.h"
#include "cerberus_pins.h"
#include "display.h"
#include "event_bus.h"
#include "settings.h"
#include "sentinel_dashboard.h"
#include "storage.h"
#include "system_health.h"
#include "touch.h"
#include "ui.h"
#include "usb_console.h"
#include "field_survey.h"
#include "recon_store.h"
#include "wifi_multissid_session.h"
#include "web_ui.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>

static const char *TAG = "cerberus";

static void persist_application_event(const cerberus_event_t *event, void *context)
{
    (void)context;
    cerberus_log_level_t level = CERBERUS_LOG_INFO;
    const char *component = "system";
    const char *action = "event";
    char detail[72];
    detail[0] = '\0';

    switch (event->type) {
    case CERBERUS_EVENT_SYSTEM_BOOT: {
        esp_reset_reason_t reason = (esp_reset_reason_t)event->value;
        level = cerberus_reset_reason_is_fault(reason)
            ? CERBERUS_LOG_WARNING
            : CERBERUS_LOG_INFO;
        action = "boot";
        snprintf(detail, sizeof(detail), "startup after %s reset", cerberus_reset_reason_label(reason));
        break;
    }
    case CERBERUS_EVENT_STORAGE_MOUNTED:
        component = "storage";
        action = "mounted";
        snprintf(detail, sizeof(detail), "microSD filesystem available");
        break;
    case CERBERUS_EVENT_STORAGE_UNAVAILABLE:
        level = CERBERUS_LOG_WARNING;
        component = "storage";
        action = "unavailable";
        snprintf(detail, sizeof(detail), "mount failed: %s", esp_err_to_name((esp_err_t)event->value));
        break;
    case CERBERUS_EVENT_STORAGE_UNMOUNTED:
        component = "storage";
        action = "unmounted";
        snprintf(detail, sizeof(detail), "firmware released microSD ownership");
        break;
    case CERBERUS_EVENT_USB_CONSOLE_READY:
        component = "usb";
        action = "console_start";
        snprintf(detail, sizeof(detail), "read-only diagnostic console ready");
        break;
    case CERBERUS_EVENT_C5_ONLINE:
        component = "c5";
        action = "online";
        snprintf(detail, sizeof(detail), "protocol %ld link established", (long)event->value);
        break;
    case CERBERUS_EVENT_C5_OFFLINE:
        level = CERBERUS_LOG_WARNING;
        component = "c5";
        action = "offline";
        snprintf(detail, sizeof(detail), "%ld consecutive status misses", (long)event->value);
        break;
    case CERBERUS_EVENT_C5_RESTARTED: {
        esp_reset_reason_t reason = (esp_reset_reason_t)event->value;
        level = cerberus_reset_reason_is_fault(reason)
            ? CERBERUS_LOG_WARNING
            : CERBERUS_LOG_INFO;
        component = "c5";
        action = "restart";
        snprintf(detail, sizeof(detail), "firmware restarted after %s reset", cerberus_reset_reason_label(reason));
        break;
    }
    case CERBERUS_EVENT_HEAP_PRESSURE:
        level = CERBERUS_LOG_WARNING;
        action = "heap_pressure";
        snprintf(detail, sizeof(detail), "internal heap low-water %ld bytes", (long)event->value);
        break;
    case CERBERUS_EVENT_STACK_PRESSURE:
        level = CERBERUS_LOG_WARNING;
        action = "stack_pressure";
        snprintf(detail, sizeof(detail), "main UI stack low-water %ld bytes", (long)event->value);
        break;
    case CERBERUS_EVENT_RECOVERY_REQUESTED:
        level = CERBERUS_LOG_WARNING;
        component = "usb";
        action = "recovery_mode";
        snprintf(detail, sizeof(detail), "physical confirmation accepted");
        break;
    case CERBERUS_EVENT_C5_WIFI_STATE:
    case CERBERUS_EVENT_C5_BLE_STATE:
    case CERBERUS_EVENT_C5_IEEE802154_STATE: {
        uint32_t packed = (uint32_t)event->value;
        uint8_t source = (uint8_t)(packed >> 24U);
        uint8_t state = (uint8_t)(packed >> 16U);
        uint8_t generation = (uint8_t)(packed >> 8U);
        bool has_error = (packed & 1U) != 0U;
        level = has_error ? CERBERUS_LOG_WARNING : CERBERUS_LOG_INFO;
        component = event->type == CERBERUS_EVENT_C5_WIFI_STATE
            ? "c5_wifi"
            : (event->type == CERBERUS_EVENT_C5_BLE_STATE ? "c5_ble" : "c5_802154");
        action = "state";
        snprintf(
            detail,
            sizeof(detail),
            "source %u state %u generation %u%s",
            source,
            state,
            generation,
            has_error ? " error" : "");
        break;
    }
    case CERBERUS_EVENT_C5_EVENT_DROPPED:
        level = CERBERUS_LOG_WARNING;
        component = "c5";
        action = "event_drop";
        snprintf(detail, sizeof(detail), "remote event queue dropped %ld total", (long)event->value);
        break;
    case CERBERUS_EVENT_C5_BACKPRESSURE: {
        uint32_t packed = (uint32_t)event->value;
        bool active = (packed & 0x80000000UL) != 0U;
        uint8_t pending = (uint8_t)((packed >> 16U) & 0x7FU);
        uint16_t deferred = (uint16_t)packed;
        level = active ? CERBERUS_LOG_WARNING : CERBERUS_LOG_INFO;
        component = "c5";
        action = "event_backpressure";
        snprintf(
            detail,
            sizeof(detail),
            "%s pending %u deferred %u",
            active ? "active" : "clear",
            pending,
            deferred);
        break;
    }
    default:
        return;
    }

    (void)cerberus_storage_log_event(level, component, action, detail);
}

#define SCREEN_BACKGROUND 0x0842
#define BOOT_SCREEN_MIN_DURATION_MS 3000
#define BOOT_LOGO_X ((CERBERUS_LCD_WIDTH - CERBERUS_BOOT_LOGO_WIDTH) / 2)
#define BOOT_LOGO_Y 139
#define BOOT_TITLE_X 64
#define BOOT_TITLE_Y 285
#define BOOT_STATUS_X 118
#define BOOT_STATUS_Y 325

static esp_err_t draw_boot_screen(void)
{
    ESP_RETURN_ON_ERROR(cerberus_display_fill_rect(
        0, 0, CERBERUS_LCD_WIDTH, CERBERUS_LCD_HEIGHT, SCREEN_BACKGROUND),
        TAG, "boot background failed");
    ESP_RETURN_ON_ERROR(sentinel_guardian_draw(0, false), TAG, "guardian boot failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(
        64, 244, "CERBERUS", 0xEF9F, SCREEN_BACKGROUND, 4),
        TAG, "boot title failed");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(
        112, 287, "SENTINEL", 0x2EFD, SCREEN_BACKGROUND, 2), TAG, "boot subtitle");
    ESP_RETURN_ON_ERROR(cerberus_display_draw_text(
        82, 340, "WATCH / SCOUT / SENSE", 0x9D16, SCREEN_BACKGROUND, 1), TAG, "boot purpose");
    return cerberus_display_draw_text(109, 373, "STARTING SYSTEMS", 0x9D16, SCREEN_BACKGROUND, 1);
}

#ifdef CERBERUS_C5_BLE_DIAGNOSTIC
static void run_c5_ble_diagnostic(void)
{
    ESP_LOGI(
        TAG,
        "BLE-DIAG begin ready_gpio=%d level=%d",
        CERBERUS_ONBOARD_C5_READY_GPIO,
        gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO));
    cerberus_c5_status_t c5_status = {0};
    esp_err_t err = ESP_ERR_TIMEOUT;
    for (uint8_t attempt = 0; attempt < 50U && err != ESP_OK; ++attempt) {
        err = c5_spi_link_get_status(&c5_status);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE-DIAG C5 status failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(
        TAG,
        "BLE-DIAG C5 protocol=%u firmware=%lu uptime=%lu boot=%u reset=%u phase=%u capabilities=0x%08lx",
        c5_status.protocol_version,
        (unsigned long)c5_status.firmware_version,
        (unsigned long)c5_status.uptime_ms,
        c5_status.boot_count,
        c5_status.reset_reason,
        c5_status.ble_phase,
        (unsigned long)c5_status.capabilities);
    ESP_LOGI(TAG, "BLE-DIAG ready after status=%d", gpio_get_level(CERBERUS_ONBOARD_C5_READY_GPIO));

    cerberus_ble_scan_status_payload_t scan_status = {0};
    err = c5_spi_link_get_ble_scan_status(&scan_status);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE-DIAG service status failed: %s", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "BLE-DIAG initial state=%u generation=%u", scan_status.state, scan_status.generation);

    err = c5_spi_link_start_ble_scan(&scan_status);
    ESP_LOGI(
        TAG,
        "BLE-DIAG start err=%s state=%u generation=%u",
        esp_err_to_name(err),
        scan_status.state,
        scan_status.generation);
    if (err != ESP_OK) {
        vTaskDelay(pdMS_TO_TICKS(500));
        cerberus_c5_status_t after_start_status = {0};
        esp_err_t c5_err = c5_spi_link_get_status(&after_start_status);
        ESP_LOGI(
            TAG,
            "BLE-DIAG after-start C5 err=%s protocol=%u firmware=%lu uptime=%lu",
            esp_err_to_name(c5_err),
            c5_err == ESP_OK ? after_start_status.protocol_version : 0,
            c5_err == ESP_OK ? (unsigned long)after_start_status.firmware_version : 0UL,
            c5_err == ESP_OK ? (unsigned long)after_start_status.uptime_ms : 0UL);
        esp_err_t scan_err = c5_spi_link_get_ble_scan_status(&scan_status);
        ESP_LOGI(
            TAG,
            "BLE-DIAG after-start scan err=%s state=%u generation=%u c5_error=%ld",
            esp_err_to_name(scan_err),
            scan_status.state,
            scan_status.generation,
            (long)scan_status.error_code);
        return;
    }

    for (uint8_t poll = 0; poll < 160U && scan_status.state == CERBERUS_BLE_SCAN_RUNNING; ++poll) {
        vTaskDelay(pdMS_TO_TICKS(250));
        err = c5_spi_link_get_ble_scan_status(&scan_status);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "BLE-DIAG poll failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(1000));
            cerberus_c5_status_t after_poll_status = {0};
            esp_err_t c5_err = c5_spi_link_get_status(&after_poll_status);
            ESP_LOGI(
                TAG,
                "BLE-DIAG after-poll C5 err=%s firmware=%lu uptime=%lu boot=%u reset=%u phase=%u",
                esp_err_to_name(c5_err),
                c5_err == ESP_OK ? (unsigned long)after_poll_status.firmware_version : 0UL,
                c5_err == ESP_OK ? (unsigned long)after_poll_status.uptime_ms : 0UL,
                c5_err == ESP_OK ? after_poll_status.boot_count : 0,
                c5_err == ESP_OK ? after_poll_status.reset_reason : 0,
                c5_err == ESP_OK ? after_poll_status.ble_phase : 0);
            return;
        }
    }
    ESP_LOGI(
        TAG,
        "BLE-DIAG final state=%u total=%u stored=%u unnamed=%u c5_error=%ld",
        scan_status.state,
        (unsigned)scan_status.total_count,
        (unsigned)scan_status.stored_count,
        (unsigned)scan_status.unnamed_count,
        (long)scan_status.error_code);

    if (scan_status.state == CERBERUS_BLE_SCAN_COMPLETE && scan_status.stored_count > 0U) {
        cerberus_c5_ble_result_t result;
        err = c5_spi_link_get_ble_scan_result(scan_status.generation, 0, &result);
        ESP_LOGI(
            TAG,
            "BLE-DIAG result err=%s name=%.32s rssi=%d uuid_len=%u",
            esp_err_to_name(err),
            err == ESP_OK ? result.name : "",
            err == ESP_OK ? result.rssi : 0,
            err == ESP_OK ? result.uuid_length : 0);
    }
}
#endif

void app_main(void)
{
#ifdef CERBERUS_C5_FLASH_BRIDGE
    ESP_ERROR_CHECK(c5_flash_bridge_run());
    return;
#endif

    cerberus_system_health_bind_main_task();
    ESP_LOGI(TAG, "Cerberus base firmware starting");

    esp_err_t crash_record_err = cerberus_crash_record_prepare();
    if (crash_record_err != ESP_OK) {
        ESP_LOGW(TAG, "crash record storage unavailable: %s", esp_err_to_name(crash_record_err));
    }

    esp_err_t settings_err = cerberus_settings_init();
    if (settings_err != ESP_OK) {
        ESP_LOGW(TAG, "persistent settings unavailable: %s", esp_err_to_name(settings_err));
    } else {
        esp_err_t style_err = cerberus_settings_apply_sentinel_style();
        if (style_err != ESP_OK) ESP_LOGW(TAG, "Sentinel style persistence: %s", esp_err_to_name(style_err));
    }

    esp_err_t display_err = cerberus_display_init();
    int64_t boot_started_ms = esp_timer_get_time() / 1000;
    if (display_err == ESP_OK) {
        esp_err_t brightness_err = cerberus_display_set_brightness(
            cerberus_settings_get()->brightness_percent);
        if (brightness_err != ESP_OK) {
            ESP_LOGW(TAG, "saved brightness unavailable: %s", esp_err_to_name(brightness_err));
        }
        ESP_ERROR_CHECK(draw_boot_screen());
    } else {
        ESP_LOGE(TAG, "display unavailable: %s", esp_err_to_name(display_err));
    }

    ESP_ERROR_CHECK(c5_control_init());
    c5_control_reset_normal();

    esp_err_t c5_err = c5_spi_link_init();
    if (c5_err != ESP_OK) {
        ESP_LOGW(TAG, "onboard C5 link unavailable: %s", esp_err_to_name(c5_err));
    }
    esp_err_t recon_err = recon_store_init();
    if (recon_err != ESP_OK) ESP_LOGW(TAG, "SD vendor lookup unavailable: %s", esp_err_to_name(recon_err));
    esp_err_t multissid_err = cerberus_s3_multissid_init();
    if (multissid_err != ESP_OK) ESP_LOGW(TAG, "beacon analysis unavailable: %s", esp_err_to_name(multissid_err));
    esp_err_t survey_err = field_survey_init();
    if (survey_err != ESP_OK) ESP_LOGW(TAG, "field survey unavailable: %s", esp_err_to_name(survey_err));

    esp_err_t touch_err = cerberus_touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW(TAG, "touch unavailable: %s", esp_err_to_name(touch_err));
    } else {
        const cerberus_touch_calibration_t *calibration =
            cerberus_settings_get_touch_calibration();
        if (calibration->valid) {
            esp_err_t calibration_err = cerberus_touch_set_calibration(
                true,
                calibration->x_min,
                calibration->x_max,
                calibration->y_min,
                calibration->y_max);
            if (calibration_err != ESP_OK) {
                ESP_LOGW(TAG, "saved touch calibration unavailable: %s",
                    esp_err_to_name(calibration_err));
            }
        }
    }

    esp_err_t storage_err = cerberus_storage_mount();
    if (storage_err != ESP_OK) {
        ESP_LOGW(TAG, "microSD unavailable at boot: %s", esp_err_to_name(storage_err));
    }

    cerberus_system_health_t startup_health;
    cerberus_system_health_snapshot(&startup_health);
    esp_err_t event_bus_err = cerberus_event_bus_init();
    if (event_bus_err == ESP_OK) {
        event_bus_err = cerberus_event_bus_subscribe(persist_application_event, NULL);
    }
    if (event_bus_err != ESP_OK) {
        ESP_LOGW(TAG, "application event bus unavailable: %s", esp_err_to_name(event_bus_err));
        if (storage_err == ESP_OK) {
            char boot_detail[64];
            snprintf(
                boot_detail,
                sizeof(boot_detail),
                "startup after %s reset",
                cerberus_reset_reason_label(startup_health.reset_reason));
            (void)cerberus_storage_log_event(
                startup_health.previous_reset_fault ? CERBERUS_LOG_WARNING : CERBERUS_LOG_INFO,
                "system",
                "boot",
                boot_detail);
        }
    } else {
        (void)cerberus_event_bus_publish(
            CERBERUS_EVENT_SYSTEM_BOOT,
            (int32_t)startup_health.reset_reason);
        (void)cerberus_event_bus_publish(
            storage_err == ESP_OK
                ? CERBERUS_EVENT_STORAGE_MOUNTED
                : CERBERUS_EVENT_STORAGE_UNAVAILABLE,
            (int32_t)storage_err);
    }

    if (c5_err == ESP_OK && event_bus_err == ESP_OK) {
        esp_err_t c5_event_err = cerberus_c5_event_pump_start();
        if (c5_event_err != ESP_OK) {
            ESP_LOGW(TAG, "C5 asynchronous event pump unavailable: %s", esp_err_to_name(c5_event_err));
        }
    }

    esp_err_t usb_console_err = cerberus_usb_console_start();
    if (usb_console_err != ESP_OK) {
        ESP_LOGW(TAG, "native USB console unavailable: %s", esp_err_to_name(usb_console_err));
    } else {
        (void)cerberus_event_bus_publish(CERBERUS_EVENT_USB_CONSOLE_READY, 0);
    }

    /* The owner starts the local access point from Device Tools / Web UI. */

#ifdef CERBERUS_C5_BLE_DIAGNOSTIC
    if (c5_err == ESP_OK) {
        run_c5_ble_diagnostic();
    }
#endif

    if (display_err == ESP_OK) {
        int64_t boot_elapsed_ms = (esp_timer_get_time() / 1000) - boot_started_ms;
        if (boot_elapsed_ms < BOOT_SCREEN_MIN_DURATION_MS) {
            vTaskDelay(pdMS_TO_TICKS(BOOT_SCREEN_MIN_DURATION_MS - boot_elapsed_ms));
        }
        cerberus_ui_config_t ui_config = {
            .touch_available = touch_err == ESP_OK,
            .c5_link_available = c5_err == ESP_OK,
            .settings_available = settings_err == ESP_OK,
            .storage_available = storage_err == ESP_OK,
        };
        cerberus_ui_run(&ui_config);
    }

    /* Keep C5 heartbeat logging available even if the display cannot start. */
    while (true) {
        cerberus_c5_status_t status;
        if (c5_err == ESP_OK) {
            esp_err_t err = c5_spi_link_get_status(&status);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "C5 online, protocol=%u firmware=%lu uptime=%lu ms capabilities=0x%08lx",
                    status.protocol_version,
                    (unsigned long)status.firmware_version,
                    (unsigned long)status.uptime_ms,
                    (unsigned long)status.capabilities);
            } else {
                ESP_LOGD(TAG, "C5 status unavailable: %s", esp_err_to_name(err));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
