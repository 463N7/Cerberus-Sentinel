#include "usb_console.h"
#include "battery.h"

#include "c5_spi_link.h"
#include "c5_event_pump.h"
#include "cerberus_protocol.h"
#include "display.h"
#include "event_bus.h"
#include "recovery.h"
#include "storage.h"
#include "system_health.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define USB_CONSOLE_LINE_CAPACITY 64U
#define USB_CONSOLE_OUTPUT_CAPACITY 768U
#define USB_CONSOLE_TASK_STACK_BYTES 4096U
#define USB_CONSOLE_TASK_PRIORITY 4U

static const char *TAG = "usb_console";
static TaskHandle_t console_task_handle;

static void console_write(const char *text)
{
    if (text == NULL) {
        return;
    }

    size_t remaining = strlen(text);
    const char *cursor = text;
    while (remaining > 0U && usb_serial_jtag_is_connected()) {
        int written = usb_serial_jtag_write_bytes(
            cursor,
            remaining,
            pdMS_TO_TICKS(100));
        if (written <= 0) {
            break;
        }
        cursor += written;
        remaining -= (size_t)written;
    }
}

static void console_printf(const char *format, ...)
{
    char output[USB_CONSOLE_OUTPUT_CAPACITY];
    va_list arguments;
    va_start(arguments, format);
    int length = vsnprintf(output, sizeof(output), format, arguments);
    va_end(arguments);
    if (length <= 0) {
        return;
    }
    output[sizeof(output) - 1U] = '\0';
    console_write(output);
}

static void print_banner(void)
{
    console_write(
        "\r\nCERBERUS READ-ONLY DIAGNOSTIC CONSOLE\r\n"
        "No shell, radio control, HID actions, or firmware writes.\r\n"
        "Type 'help' for commands.\r\n");
}

static void print_help(void)
{
    console_write(
        "help     Show this command list\r\n"
        "info     Show firmware and protocol information\r\n"
        "health   Show reset, memory, stack, and crash status\r\n"
        "c5       Show onboard C5 link health and capabilities\r\n"
        "storage  Show microSD health and capacity\r\n"
        "battery  Show cached onboard battery gauge status\r\n"
        "All commands are read-only and accept no arguments.\r\n");
}

static void print_info(void)
{
    const esp_app_desc_t *description = esp_app_get_description();
    uint8_t mac[6];
    char mac_text[18] = "unavailable";
    if (esp_efuse_mac_get_default(mac) == ESP_OK) {
        (void)snprintf(mac_text, sizeof(mac_text), "%02x:%02x:%02x:%02x:%02x:%02x",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    console_printf(
        "product=Cerberus Sentinel\r\n"
        "s3_mac=%s\r\n"
        "project=%s\r\n"
        "version=%s\r\n"
        "idf=%s\r\n"
        "build=%s %s\r\n"
        "s3_c5_protocol=%u\r\n"
        "s3_rom_download=%s\r\n"
        "screen_buffered=%s\r\n"
        "interface=native USB Serial/JTAG\r\n"
        "access=read-only diagnostics\r\n",
        mac_text,
        description->project_name,
        description->version,
        description->idf_ver,
        description->date,
        description->time,
        CERBERUS_PROTOCOL_VERSION,
        cerberus_recovery_download_mode_available() ? "available" : "blocked-by-efuse",
        cerberus_display_buffering_enabled() ? "yes" : "no");
}

static void print_health(void)
{
    cerberus_system_health_t health;
    cerberus_system_health_snapshot(&health);
    console_printf(
        "reset=%s\r\n"
        "previous_reset_fault=%s\r\n"
        "uptime_ms=%" PRIu64 "\r\n"
        "heap_free_bytes=%u\r\n"
        "heap_low_water_bytes=%u\r\n"
        "main_stack_low_water_bytes=%u\r\n",
        cerberus_reset_reason_label(health.reset_reason),
        health.previous_reset_fault ? "yes" : "no",
        health.uptime_ms,
        (unsigned)health.free_internal_bytes,
        (unsigned)health.minimum_internal_bytes,
        (unsigned)health.main_task_minimum_stack_bytes);

    cerberus_crash_record_t crash_record;
    esp_err_t err = cerberus_crash_record_inspect(&crash_record);
    if (err == ESP_ERR_NOT_FOUND || !crash_record.present) {
        console_write("crash_record=none\r\n");
    } else if (err == ESP_OK && crash_record.valid) {
        console_printf(
            "crash_record=valid\r\n"
            "crash_size_bytes=%u\r\n"
            "crash_task=%s\r\n",
            (unsigned)crash_record.size_bytes,
            crash_record.task_name);
    } else {
        console_printf(
            "crash_record=invalid\r\n"
            "crash_status=%s\r\n"
            "crash_size_bytes=%u\r\n",
            esp_err_to_name(err),
            (unsigned)crash_record.size_bytes);
    }

    cerberus_event_bus_status_t event_status;
    err = cerberus_event_bus_get_status(&event_status);
    if (err == ESP_OK) {
        console_printf(
            "event_attempts=%" PRIu32 "\r\n"
            "event_dispatched=%" PRIu32 "\r\n"
            "event_pending=%" PRIu32 "\r\n"
            "event_dropped=%" PRIu32 "\r\n"
            "event_subscribers=%u\r\n",
            event_status.publish_attempts,
            event_status.dispatched_events,
            event_status.pending_events,
            event_status.dropped_events,
            event_status.subscriber_count);
    } else {
        console_printf("event_bus=unavailable\r\nevent_error=%s\r\n", esp_err_to_name(err));
    }
}

static void print_c5(void)
{
    cerberus_c5_status_t status;
    esp_err_t err = c5_spi_link_get_status(&status);
    if (err != ESP_OK) {
        console_printf("status=unavailable\r\nerror=%s\r\n", esp_err_to_name(err));
        return;
    }

    console_printf(
        "status=online\r\n"
        "spi_clock_hz=%" PRIu32 "\r\n"
        "protocol=%u\r\n"
        "firmware=%" PRIu32 "\r\n"
        "uptime_ms=%" PRIu32 "\r\n"
        "reset=%s\r\n"
        "boot_count=%u\r\n"
        "heap_free_kib=%u\r\n"
        "heap_low_water_kib=%u\r\n"
        "radio_state=0x%02x\r\n"
        "capabilities=0x%08" PRIx32 "\r\n",
        c5_spi_link_clock_hz(),
        status.protocol_version,
        status.firmware_version,
        status.uptime_ms,
        cerberus_reset_reason_label((esp_reset_reason_t)status.reset_reason),
        status.boot_count,
        status.free_heap_kib,
        status.minimum_free_heap_kib,
        status.radio_state,
        status.capabilities);

    if ((status.capabilities & CERBERUS_C5_CAP_WIFI_RADIO_STATS) != 0U) {
        cerberus_wifi_radio_stats_payload_t stats;
        err = c5_spi_link_get_wifi_radio_stats(&stats);
        if (err == ESP_OK) {
            console_printf(
                "wifi_stats_generation=%u\r\n"
                "wifi_stats_total=%u\r\n"
                "wifi_stats_analyzed=%u\r\n"
                "wifi_stats_24_ghz=%u\r\n"
                "wifi_stats_5_ghz=%u\r\n"
                "wifi_stats_open=%u\r\n"
                "wifi_stats_secured=%u\r\n"
                "wifi_stats_hidden=%u\r\n"
                "wifi_stats_duration_ms=%" PRIu32 "\r\n"
                "wifi_stats_successful_scans=%u\r\n"
                "wifi_stats_failed_scans=%u\r\n",
                stats.generation,
                stats.total_count,
                stats.analyzed_count,
                stats.count_24_ghz,
                stats.count_5_ghz,
                stats.open_count,
                stats.secured_count,
                stats.hidden_count,
                stats.scan_duration_ms,
                stats.successful_scans,
                stats.failed_scans);
        } else {
            console_printf("wifi_stats=unavailable\r\nwifi_stats_error=%s\r\n", esp_err_to_name(err));
        }
    }

    if ((status.capabilities & CERBERUS_C5_CAP_WIFI_DEFENSE_SURVEY) != 0U) {
        cerberus_wifi_defense_status_payload_t defense;
        err = c5_spi_link_get_wifi_defense_status(&defense);
        if (err == ESP_OK) {
            console_printf(
                "wifi_defense_state=%u\r\n"
                "wifi_defense_generation=%u\r\n"
                "wifi_defense_completed=%u\r\n"
                "wifi_defense_channels=%u\r\n"
                "wifi_defense_flagged=%u\r\n"
                "wifi_defense_current_channel=%u\r\n"
                "wifi_defense_elapsed_ms=%" PRIu32 "\r\n"
                "wifi_defense_scope=aggregate_metadata_only\r\n",
                defense.state, defense.generation, defense.completed_count,
                defense.result_count, defense.flagged_count, defense.current_channel,
                defense.elapsed_ms);
            for (uint8_t index = 0U; index < defense.completed_count; ++index) {
                cerberus_wifi_defense_result_payload_t item;
                if (c5_spi_link_get_wifi_defense_result(
                        defense.generation, index, &item) == ESP_OK) {
                    console_printf(
                        "wifi_defense_channel=%u flags=0x%02x mgmt=%u ctrl=%u data=%u "
                        "beacon_per_sec=%u deauth=%u disassoc=%u noise=%d peak=%d\r\n",
                        item.channel, item.assessment_flags, item.management_frames,
                        item.control_frames, item.data_frames, item.beacon_rate_per_sec,
                        item.deauth_frames, item.disassoc_frames,
                        item.average_noise_floor, item.strongest_rssi);
                }
            }
        } else {
            console_printf("wifi_defense=unavailable\r\nwifi_defense_error=%s\r\n",
                esp_err_to_name(err));
        }
    }

    cerberus_c5_event_pump_status_t event_status;
    err = cerberus_c5_event_pump_get_status(&event_status);
    if (err == ESP_OK && event_status.supported) {
        console_printf(
            "event_queue=supported\r\n"
            "event_pending=%u\r\n"
            "event_capacity=%u\r\n"
            "event_produced=%" PRIu32 "\r\n"
            "event_consumed=%" PRIu32 "\r\n"
            "event_remote_dropped=%u\r\n"
            "event_flow_control=%s\r\n"
            "event_flow_configured=%s\r\n"
            "event_flow_enabled=%s\r\n"
            "event_backpressure=%s\r\n"
            "event_low_watermark=%u\r\n"
            "event_high_watermark=%u\r\n"
            "event_deferred=%u\r\n"
            "event_control_generation=%u\r\n"
            "event_batch=%s\r\n"
            "event_batch_transfers=%" PRIu32 "\r\n"
            "event_batch_failures=%" PRIu32 "\r\n"
            "event_single_transfers=%" PRIu32 "\r\n"
            "event_received=%" PRIu32 "\r\n"
            "event_local_publish_failures=%" PRIu32 "\r\n"
            "event_last_sequence=%u\r\n"
            "event_last_source=%u\r\n"
            "event_last_state=%u\r\n",
            event_status.remote.pending,
            event_status.remote.capacity,
            event_status.remote.produced,
            event_status.remote.consumed,
            event_status.remote.dropped,
            event_status.flow_supported ? "supported" : "unsupported",
            event_status.flow_configured ? "yes" : "no",
            (event_status.remote.flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED) != 0U
                ? "yes"
                : "no",
            (event_status.remote.flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ACTIVE) != 0U
                ? "active"
                : "clear",
            event_status.remote.low_watermark,
            event_status.remote.high_watermark,
            event_status.remote.deferred,
            event_status.remote.control_generation,
            event_status.batch_supported ? "supported" : "unsupported",
            event_status.batch_transfers,
            event_status.batch_failures,
            event_status.single_event_transfers,
            event_status.received_events,
            event_status.local_publish_failures,
            event_status.last_event.event_sequence,
            event_status.last_event.source,
            event_status.last_event.state);
    } else if ((status.capabilities & CERBERUS_C5_CAP_EVENT_QUEUE) != 0U) {
        console_write("event_queue=starting\r\n");
    } else {
        console_write("event_queue=unsupported\r\n");
    }

    if ((status.capabilities & CERBERUS_C5_CAP_FRAGMENTED_OBJECTS) != 0U) {
        cerberus_c5_diagnostic_snapshot_t snapshot;
        uint16_t snapshot_length = 0U;
        uint32_t snapshot_crc32 = 0U;
        err = c5_spi_link_read_fragmented_object(
            CERBERUS_FRAGMENT_OBJECT_C5_DIAGNOSTIC_SNAPSHOT,
            &snapshot,
            sizeof(snapshot),
            &snapshot_length,
            &snapshot_crc32);
        if (err == ESP_OK && snapshot_length == sizeof(snapshot) &&
            snapshot.firmware_version == status.firmware_version) {
            console_printf(
                "fragment_transfer=supported\r\n"
                "fragment_object=c5_diagnostic_snapshot\r\n"
                "fragment_bytes=%u\r\n"
                "fragment_chunks=%u\r\n"
                "fragment_crc32=%08" PRIx32 "\r\n"
                "snapshot_uptime_ms=%" PRIu32 "\r\n"
                "snapshot_heap_free_bytes=%" PRIu32 "\r\n"
                "snapshot_event_produced=%" PRIu32 "\r\n"
                "snapshot_event_consumed=%" PRIu32 "\r\n",
                snapshot_length,
                (unsigned)((snapshot_length + CERBERUS_FRAGMENT_DATA_SIZE - 1U) /
                    CERBERUS_FRAGMENT_DATA_SIZE),
                snapshot_crc32,
                snapshot.uptime_ms,
                snapshot.free_heap_bytes,
                snapshot.event_produced,
                snapshot.event_consumed);
        } else {
            console_printf(
                "fragment_transfer=unavailable\r\nfragment_error=%s\r\n",
                esp_err_to_name(err != ESP_OK ? err : ESP_ERR_INVALID_RESPONSE));
        }
    } else {
        console_write("fragment_transfer=unsupported\r\n");
    }
}

static void print_storage(void)
{
    cerberus_storage_status_t status;
    esp_err_t err = cerberus_storage_get_status(&status);
    if (err != ESP_OK || !status.mounted) {
        console_printf("status=unavailable\r\nerror=%s\r\n", esp_err_to_name(err));
        return;
    }

    console_printf(
        "status=mounted\r\n"
        "card=%s\r\n"
        "capacity_bytes=%" PRIu64 "\r\n"
        "free_bytes=%" PRIu64 "\r\n"
        "sector_bytes=%" PRIu32 "\r\n",
        status.card_name,
        status.total_bytes,
        status.free_bytes,
        status.sector_size);
}

static void print_battery(void)
{
    cerberus_battery_status_t status;
    cerberus_battery_get_status(&status);
    if (!status.valid) {
        console_printf("status=unavailable\r\nerror=%s\r\nlast_attempt_ms=%" PRId64 "\r\n",
            esp_err_to_name(status.last_error), status.last_attempt_ms);
        return;
    }
    console_printf("status=available\r\ngauge=MAX17048\r\npercent=%u\r\nvoltage_mv=%u\r\n"
        "gauge_version=0x%04x\r\nlast_read_ms=%" PRId64 "\r\ncharging=unknown\r\n",
        (unsigned)status.percent, (unsigned)status.voltage_mv,
        (unsigned)status.version, status.last_read_ms);
}

static void execute_command(char *line)
{
    char *start = line;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        ++start;
    }

    char *end = start + strlen(start);
    while (end > start && isspace((unsigned char)end[-1])) {
        --end;
    }
    *end = '\0';
    for (char *cursor = start; *cursor != '\0'; ++cursor) {
        *cursor = (char)tolower((unsigned char)*cursor);
    }

    if (*start == '\0') {
        return;
    }
    if (strcmp(start, "help") == 0) {
        print_help();
    } else if (strcmp(start, "info") == 0) {
        print_info();
    } else if (strcmp(start, "health") == 0) {
        print_health();
    } else if (strcmp(start, "c5") == 0) {
        print_c5();
    } else if (strcmp(start, "storage") == 0) {
        print_storage();
    } else if (strcmp(start, "battery") == 0) {
        print_battery();
    } else {
        console_write("ERR unknown command; type 'help'\r\n");
    }
}

static void usb_console_task(void *argument)
{
    (void)argument;
    char line[USB_CONSOLE_LINE_CAPACITY];
    size_t line_length = 0U;
    bool discarding_line = false;
    bool session_announced = false;
    bool previous_was_cr = false;

    while (true) {
        if (!usb_serial_jtag_is_connected()) {
            session_announced = false;
            line_length = 0U;
            discarding_line = false;
            previous_was_cr = false;
        }

        uint8_t byte = 0U;
        int received = usb_serial_jtag_read_bytes(&byte, 1U, pdMS_TO_TICKS(50));
        if (received <= 0) {
            continue;
        }

        if (!session_announced) {
            print_banner();
            session_announced = true;
        }

        if (byte == '\r' || byte == '\n') {
            if (byte == '\n' && previous_was_cr) {
                previous_was_cr = false;
                continue;
            }
            previous_was_cr = byte == '\r';
            console_write("\r\n");
            if (discarding_line) {
                console_write("ERR line too long\r\n");
            } else {
                line[line_length] = '\0';
                execute_command(line);
            }
            line_length = 0U;
            discarding_line = false;
            console_write("cerberus> ");
            continue;
        }

        previous_was_cr = false;
        if (byte == '\b' || byte == 0x7fU) {
            if (!discarding_line && line_length > 0U) {
                --line_length;
                console_write("\b \b");
            }
            continue;
        }
        if (byte < 0x20U || byte > 0x7eU) {
            continue;
        }
        if (discarding_line) {
            continue;
        }
        if (line_length + 1U >= sizeof(line)) {
            discarding_line = true;
            continue;
        }
        line[line_length++] = (char)byte;
        char echo[2] = {(char)byte, '\0'};
        console_write(echo);
    }
}

esp_err_t cerberus_usb_console_start(void)
{
    if (console_task_handle != NULL) {
        return ESP_OK;
    }

    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t driver_config = {
            .tx_buffer_size = 1024U,
            .rx_buffer_size = 256U,
        };
        esp_err_t err = usb_serial_jtag_driver_install(&driver_config);
        if (err != ESP_OK) {
            return err;
        }
    }
    usb_serial_jtag_vfs_use_driver();

    BaseType_t created = xTaskCreate(
        usb_console_task,
        "usb_console",
        USB_CONSOLE_TASK_STACK_BYTES,
        NULL,
        USB_CONSOLE_TASK_PRIORITY,
        &console_task_handle);
    if (created != pdPASS) {
        console_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "read-only native USB diagnostic console ready");
    return ESP_OK;
}

esp_err_t cerberus_usb_console_stop(void)
{
    if (console_task_handle != NULL) {
        TaskHandle_t task = console_task_handle;
        console_task_handle = NULL;
        vTaskDelete(task);
    }
    if (!usb_serial_jtag_is_driver_installed()) {
        return ESP_OK;
    }
    (void)usb_serial_jtag_wait_tx_done(pdMS_TO_TICKS(100));
    esp_err_t err = usb_serial_jtag_driver_uninstall();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "native USB diagnostic console stopped");
    }
    return err;
}
