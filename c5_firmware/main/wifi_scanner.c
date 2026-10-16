#include "wifi_scanner.h"

#include "ble_scanner.h"
#include "ieee802154_scanner.h"
#include "cerberus_watch_capture.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define WIFI_SCAN_TASK_STACK 4096U
#define WIFI_SCAN_TASK_PRIORITY 4U
#define WIFI_PASSIVE_DWELL_MS 120U
#define WIFI_DEFENSE_TASK_STACK 4096U
#define WIFI_DEFENSE_TASK_PRIORITY 4U
#define WIFI_DEFENSE_HIGH_BEACON_RATE 50U
#define WIFI_DEFENSE_HIGH_FRAME_RATE 1000U
#define WIFI_DEFENSE_ELEVATED_NOISE_DBM (-75)

static const char *TAG = "wifi_scanner";
static SemaphoreHandle_t scanner_mutex;
static TaskHandle_t scanner_task_handle;
static wifi_ap_record_t scan_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static cerberus_wifi_scan_status_payload_t scan_status;
static cerberus_wifi_radio_stats_payload_t radio_stats;
static int64_t scan_started_us;
static bool scanner_ready;
static bool scan_cancelled;
static esp_netif_t *station_netif;
static cerberus_wifi_connection_status_payload_t connection_status;
static uint8_t staged_password[CERBERUS_WIFI_PASSWORD_MAX_LENGTH + 1U];
static bool staged_password_received[CERBERUS_WIFI_PASSWORD_MAX_LENGTH];
static uint8_t staged_password_length;
static TaskHandle_t defense_task_handle;
static cerberus_wifi_defense_status_payload_t defense_status;
static cerberus_wifi_defense_result_payload_t
    defense_results[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static uint8_t defense_channels[CERBERUS_WIFI_SCAN_MAX_RESULTS];
static volatile bool defense_cancelled;
static portMUX_TYPE defense_capture_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool defense_capture_active;
static volatile uint32_t defense_management_frames;
static volatile uint32_t defense_control_frames;
static volatile uint32_t defense_data_frames;
static volatile uint32_t defense_beacon_frames;
static volatile uint32_t defense_deauth_frames;
static volatile uint32_t defense_disassoc_frames;
static volatile int64_t defense_noise_sum;
static volatile uint32_t defense_noise_samples;
static volatile int8_t defense_strongest_rssi;
static cerberus_watch_capture_t watch_capture;
static bool watch_capture_active;

static uint16_t saturate_u16(uint32_t value)
{
    return value > UINT16_MAX ? UINT16_MAX : (uint16_t)value;
}

static void wifi_promiscuous_callback(void *buffer, wifi_promiscuous_pkt_type_t type)
{
    if (buffer == NULL || type == WIFI_PKT_MISC) return;
    const wifi_promiscuous_pkt_t *packet = buffer;
    const wifi_pkt_rx_ctrl_t *rx = &packet->rx_ctrl;
    if (type == WIFI_PKT_MGMT && rx->rx_state == 0 && rx->sig_len >= 28U) {
        portENTER_CRITICAL(&defense_capture_mux);
        bool capture = watch_capture_active;
        uint8_t generation = watch_capture.status.generation;
        portEXIT_CRITICAL(&defense_capture_mux);
        if (capture) {
            sentinel_watch_protocol_record_t protocol;
            /* ESP-IDF sig_len includes the trailing four-byte FCS. Parsing is
             * outside the critical section; publication rechecks generation. */
            bool matched = sentinel_watch_protocol_decode_wifi(packet->payload, rx->sig_len - 4U, &protocol);
            const uint8_t *identity = packet->payload +
                (matched && protocol.kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI ? 16U : 10U);
            portENTER_CRITICAL(&defense_capture_mux);
            if (watch_capture_active && watch_capture.status.generation == generation)
                cerberus_watch_capture_observe(&watch_capture, matched ? &protocol : NULL,
                    identity, 0U, rx->rssi, rx->channel);
            portEXIT_CRITICAL(&defense_capture_mux);
        }
    }
    portENTER_CRITICAL(&defense_capture_mux);
    if (!defense_capture_active) {
        portEXIT_CRITICAL(&defense_capture_mux);
        return;
    }

    if (type == WIFI_PKT_MGMT) {
        ++defense_management_frames;
        if (rx->sig_len >= 2U) {
            uint8_t subtype = (uint8_t)((packet->payload[0] >> 4U) & 0x0FU);
            if (subtype == 8U) ++defense_beacon_frames;
            else if (subtype == 10U) ++defense_disassoc_frames;
            else if (subtype == 12U) ++defense_deauth_frames;
        }
    } else if (type == WIFI_PKT_CTRL) {
        ++defense_control_frames;
    } else if (type == WIFI_PKT_DATA) {
        ++defense_data_frames;
    }
    if (rx->noise_floor < 0) {
        defense_noise_sum += rx->noise_floor;
        ++defense_noise_samples;
    }
    if (rx->rssi > defense_strongest_rssi) defense_strongest_rssi = rx->rssi;
    portEXIT_CRITICAL(&defense_capture_mux);
}

static void reset_defense_capture(void)
{
    portENTER_CRITICAL(&defense_capture_mux);
    defense_capture_active = false;
    defense_management_frames = 0U;
    defense_control_frames = 0U;
    defense_data_frames = 0U;
    defense_beacon_frames = 0U;
    defense_deauth_frames = 0U;
    defense_disassoc_frames = 0U;
    defense_noise_sum = 0;
    defense_noise_samples = 0U;
    defense_strongest_rssi = -127;
    portEXIT_CRITICAL(&defense_capture_mux);
}

static void finish_defense_result(uint8_t generation, uint8_t index, uint8_t channel)
{
    uint32_t management;
    uint32_t control;
    uint32_t data;
    uint32_t beacon;
    uint32_t deauth;
    uint32_t disassoc;
    int64_t noise_sum;
    uint32_t noise_samples;
    int8_t strongest;
    portENTER_CRITICAL(&defense_capture_mux);
    defense_capture_active = false;
    management = defense_management_frames;
    control = defense_control_frames;
    data = defense_data_frames;
    beacon = defense_beacon_frames;
    deauth = defense_deauth_frames;
    disassoc = defense_disassoc_frames;
    noise_sum = defense_noise_sum;
    noise_samples = defense_noise_samples;
    strongest = defense_strongest_rssi;
    portEXIT_CRITICAL(&defense_capture_mux);

    cerberus_wifi_defense_result_payload_t result = {
        .generation = generation,
        .index = index,
        .channel = channel,
        .management_frames = saturate_u16(management),
        .control_frames = saturate_u16(control),
        .data_frames = saturate_u16(data),
        .beacon_frames = saturate_u16(beacon),
        .deauth_frames = saturate_u16(deauth),
        .disassoc_frames = saturate_u16(disassoc),
        .average_noise_floor = noise_samples > 0U
            ? (int8_t)(noise_sum / (int64_t)noise_samples)
            : -127,
        .strongest_rssi = strongest,
        .dwell_ms = CERBERUS_WIFI_DEFENSE_DWELL_MS,
        .beacon_rate_per_sec = saturate_u16(
            (beacon * 1000U) / CERBERUS_WIFI_DEFENSE_DWELL_MS),
    };
    uint32_t total = management + control + data;
    if (deauth > 0U || disassoc > 0U) {
        result.assessment_flags |= CERBERUS_WIFI_DEFENSE_FLAG_DEAUTH_ACTIVITY;
    }
    if (result.beacon_rate_per_sec >= WIFI_DEFENSE_HIGH_BEACON_RATE) {
        result.assessment_flags |= CERBERUS_WIFI_DEFENSE_FLAG_HIGH_BEACON_RATE;
    }
    if (result.average_noise_floor != -127 &&
        result.average_noise_floor >= WIFI_DEFENSE_ELEVATED_NOISE_DBM) {
        result.assessment_flags |= CERBERUS_WIFI_DEFENSE_FLAG_ELEVATED_NOISE;
    }
    if ((total * 1000U) / CERBERUS_WIFI_DEFENSE_DWELL_MS >= WIFI_DEFENSE_HIGH_FRAME_RATE) {
        result.assessment_flags |= CERBERUS_WIFI_DEFENSE_FLAG_HIGH_ACTIVITY;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    defense_results[index] = result;
    defense_status.completed_count = (uint8_t)(index + 1U);
    if (result.assessment_flags != 0U) ++defense_status.flagged_count;
    xSemaphoreGive(scanner_mutex);
}

static void wifi_defense_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        uint8_t generation = defense_status.generation;
        uint8_t channel_count = defense_status.result_count;
        xSemaphoreGive(scanner_mutex);
        int64_t started_us = esp_timer_get_time();
        esp_err_t error = ESP_OK;

        wifi_promiscuous_filter_t filter = {
            .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT |
                WIFI_PROMIS_FILTER_MASK_CTRL | WIFI_PROMIS_FILTER_MASK_DATA,
        };
        error = esp_wifi_set_promiscuous_filter(&filter);
        if (error == ESP_OK) error = esp_wifi_set_promiscuous(true);

        for (uint8_t index = 0U; error == ESP_OK && index < channel_count; ++index) {
            if (defense_cancelled) break;
            uint8_t channel = defense_channels[index];
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            defense_status.current_index = index;
            defense_status.current_channel = channel;
            defense_status.elapsed_ms = (uint32_t)((esp_timer_get_time() - started_us) / 1000LL);
            xSemaphoreGive(scanner_mutex);

            reset_defense_capture();
            error = esp_wifi_set_band(channel <= 14U ? WIFI_BAND_2G : WIFI_BAND_5G);
            if (error == ESP_OK) error = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
            if (error != ESP_OK) break;
            portENTER_CRITICAL(&defense_capture_mux);
            defense_capture_active = true;
            portEXIT_CRITICAL(&defense_capture_mux);
            for (uint16_t elapsed = 0U;
                 elapsed < CERBERUS_WIFI_DEFENSE_DWELL_MS && !defense_cancelled;
                 elapsed += 50U) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            finish_defense_result(generation, index, channel);
        }

        portENTER_CRITICAL(&defense_capture_mux);
        defense_capture_active = false;
        portEXIT_CRITICAL(&defense_capture_mux);
        (void)esp_wifi_set_promiscuous(false);
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        defense_status.current_channel = 0U;
        defense_status.current_index = defense_status.completed_count;
        defense_status.elapsed_ms = (uint32_t)((esp_timer_get_time() - started_us) / 1000LL);
        defense_status.error_code = error;
        defense_status.state = error == ESP_OK
            ? CERBERUS_WIFI_DEFENSE_COMPLETE
            : CERBERUS_WIFI_DEFENSE_ERROR;
        defense_cancelled = false;
        xSemaphoreGive(scanner_mutex);
        ESP_LOGI(TAG, "defensive survey complete: channels=%u error=%s",
            defense_status.completed_count, esp_err_to_name(error));
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (scanner_mutex == NULL) return;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event = data;
        connection_status.state = CERBERUS_WIFI_CONNECTION_CONNECTED;
        connection_status.error_code = ESP_OK;
        memcpy(connection_status.ipv4, &event->ip_info.ip.addr, sizeof(connection_status.ipv4));
        memcpy(connection_status.gateway, &event->ip_info.gw.addr, sizeof(connection_status.gateway));
        memcpy(connection_status.netmask, &event->ip_info.netmask.addr, sizeof(connection_status.netmask));
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *event = data;
        bool requested = connection_status.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING ||
            connection_status.state == CERBERUS_WIFI_CONNECTION_IDLE;
        connection_status.state = requested ? CERBERUS_WIFI_CONNECTION_IDLE : CERBERUS_WIFI_CONNECTION_ERROR;
        connection_status.error_code = requested ? ESP_OK : (int32_t)event->reason;
        memset(connection_status.ipv4, 0, sizeof(connection_status.ipv4));
        memset(connection_status.gateway, 0, sizeof(connection_status.gateway));
        memset(connection_status.netmask, 0, sizeof(connection_status.netmask));
    }
    xSemaphoreGive(scanner_mutex);
}

static uint8_t map_security(wifi_auth_mode_t authmode)
{
    switch (authmode) {
    case WIFI_AUTH_OPEN:
        return CERBERUS_WIFI_SECURITY_OPEN;
    case WIFI_AUTH_WEP:
        return CERBERUS_WIFI_SECURITY_WEP;
    case WIFI_AUTH_WPA_PSK:
        return CERBERUS_WIFI_SECURITY_WPA;
    case WIFI_AUTH_WPA2_PSK:
        return CERBERUS_WIFI_SECURITY_WPA2;
    case WIFI_AUTH_WPA_WPA2_PSK:
        return CERBERUS_WIFI_SECURITY_WPA_WPA2;
    case WIFI_AUTH_WPA3_PSK:
        return CERBERUS_WIFI_SECURITY_WPA3;
    case WIFI_AUTH_WPA2_WPA3_PSK:
        return CERBERUS_WIFI_SECURITY_WPA2_WPA3;
    case WIFI_AUTH_OWE:
        return CERBERUS_WIFI_SECURITY_OWE;
    case WIFI_AUTH_ENTERPRISE:
    case WIFI_AUTH_WPA3_ENT_192:
    case WIFI_AUTH_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA2_WPA3_ENTERPRISE:
    case WIFI_AUTH_WPA_ENTERPRISE:
        return CERBERUS_WIFI_SECURITY_ENTERPRISE;
    default:
        return CERBERUS_WIFI_SECURITY_OTHER;
    }
}

static uint16_t saturating_increment_u16(uint16_t value)
{
    return value < UINT16_MAX ? (uint16_t)(value + 1U) : UINT16_MAX;
}

static void summarize_scan_locked(uint16_t total_count, uint16_t stored_count)
{
    uint16_t successful_scans = saturating_increment_u16(radio_stats.successful_scans);
    uint16_t failed_scans = radio_stats.failed_scans;
    memset(&radio_stats, 0, sizeof(radio_stats));
    radio_stats.generation = scan_status.generation;
    radio_stats.total_count = total_count;
    radio_stats.analyzed_count = stored_count > UINT8_MAX ? UINT8_MAX : (uint8_t)stored_count;
    radio_stats.truncated = total_count > stored_count ? 1U : 0U;
    radio_stats.strongest_rssi = -127;
    radio_stats.successful_scans = successful_scans;
    radio_stats.failed_scans = failed_scans;
    if (scan_started_us > 0) {
        int64_t elapsed_us = esp_timer_get_time() - scan_started_us;
        radio_stats.scan_duration_ms = elapsed_us > 0
            ? (uint32_t)(elapsed_us / 1000LL)
            : 0U;
    }

    bool active_channels[256] = {0};
    for (uint16_t index = 0; index < stored_count; ++index) {
        const wifi_ap_record_t *record = &scan_results[index];
        uint8_t channel = record->primary;
        if (channel <= 14U) {
            if (radio_stats.count_24_ghz < UINT8_MAX) ++radio_stats.count_24_ghz;
        } else {
            if (radio_stats.count_5_ghz < UINT8_MAX) ++radio_stats.count_5_ghz;
        }
        if (record->authmode == WIFI_AUTH_OPEN) {
            if (radio_stats.open_count < UINT8_MAX) ++radio_stats.open_count;
        } else if (radio_stats.secured_count < UINT8_MAX) {
            ++radio_stats.secured_count;
        }
        if (record->ssid[0] == '\0' && radio_stats.hidden_count < UINT8_MAX) {
            ++radio_stats.hidden_count;
        }
        if (channel > 0U) active_channels[channel] = true;
        if (radio_stats.strongest_channel == 0U || record->rssi > radio_stats.strongest_rssi) {
            radio_stats.strongest_rssi = record->rssi;
            radio_stats.strongest_channel = channel;
        }
    }
    for (uint16_t channel = 1U; channel < 256U; ++channel) {
        if (!active_channels[channel]) continue;
        if (channel <= 14U) {
            if (radio_stats.active_channels_24 < UINT8_MAX) ++radio_stats.active_channels_24;
        } else if (radio_stats.active_channels_5 < UINT8_MAX) {
            ++radio_stats.active_channels_5;
        }
    }
}

static void finish_scan(esp_err_t scan_error)
{
    uint16_t total_count = 0;
    uint16_t stored_count = CERBERUS_WIFI_SCAN_MAX_RESULTS;
    esp_err_t result_error = scan_error;

    if (result_error == ESP_OK) {
        result_error = esp_wifi_scan_get_ap_num(&total_count);
    }
    if (result_error == ESP_OK) {
        memset(scan_results, 0, sizeof(scan_results));
        result_error = esp_wifi_scan_get_ap_records(&stored_count, scan_results);
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (scan_cancelled) {
        scan_status.state = CERBERUS_WIFI_SCAN_IDLE;
        scan_status.error_code = ESP_OK;
        scan_status.total_count = 0;
        scan_status.stored_count = 0;
        scan_status.hidden_count = 0;
        scan_cancelled = false;
    } else if (result_error != ESP_OK) {
        radio_stats.failed_scans = saturating_increment_u16(radio_stats.failed_scans);
        radio_stats.scan_duration_ms = scan_started_us > 0
            ? (uint32_t)((esp_timer_get_time() - scan_started_us) / 1000LL)
            : 0U;
        scan_status.state = CERBERUS_WIFI_SCAN_ERROR;
        scan_status.error_code = result_error;
        scan_status.total_count = 0;
        scan_status.stored_count = 0;
        scan_status.hidden_count = 0;
        ESP_LOGW(TAG, "passive scan failed: %s", esp_err_to_name(result_error));
    } else {
        uint8_t hidden_count = 0;
        for (uint16_t index = 0; index < stored_count; ++index) {
            if (scan_results[index].ssid[0] == '\0') {
                ++hidden_count;
            }
        }
        scan_status.state = CERBERUS_WIFI_SCAN_COMPLETE;
        scan_status.error_code = ESP_OK;
        scan_status.total_count = total_count;
        scan_status.stored_count = (uint8_t)stored_count;
        scan_status.hidden_count = hidden_count;
        summarize_scan_locked(total_count, stored_count);
        ESP_LOGI(TAG, "passive scan complete: total=%u stored=%u hidden=%u",
            total_count, stored_count, hidden_count);
    }
    portENTER_CRITICAL(&defense_capture_mux);
    cerberus_watch_capture_finish(&watch_capture, scan_status.state,
        (uint32_t)(esp_timer_get_time() / 1000LL));
    portEXIT_CRITICAL(&defense_capture_mux);
    xSemaphoreGive(scanner_mutex);
}

static void wifi_scan_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        wifi_scan_config_t config = {
            .show_hidden = true,
            .scan_type = WIFI_SCAN_TYPE_PASSIVE,
            .scan_time.passive = WIFI_PASSIVE_DWELL_MS,
        };
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        uint8_t generation = scan_status.generation;
        bool cancelled = scan_cancelled;
        xSemaphoreGive(scanner_mutex);
        portENTER_CRITICAL(&defense_capture_mux);
        cerberus_watch_capture_begin(&watch_capture, CERBERUS_WATCH_RADIO_WIFI, generation,
            (uint32_t)(esp_timer_get_time() / 1000LL));
        watch_capture_active = false;
        portEXIT_CRITICAL(&defense_capture_mux);
        if (cancelled) { finish_scan(ESP_OK); continue; }
        wifi_promiscuous_filter_t filter = {.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT};
        esp_err_t capture_error = esp_wifi_set_promiscuous_filter(&filter);
        if (capture_error == ESP_OK) capture_error = esp_wifi_set_promiscuous(true);
        portENTER_CRITICAL(&defense_capture_mux);
        watch_capture_active = capture_error == ESP_OK;
        if (capture_error != ESP_OK) watch_capture.status.flags |= CERBERUS_WATCH_CAPTURE_UNAVAILABLE;
        portEXIT_CRITICAL(&defense_capture_mux);
        esp_err_t scan_error = esp_wifi_scan_start(&config, true);
        portENTER_CRITICAL(&defense_capture_mux);
        watch_capture_active = false;
        portEXIT_CRITICAL(&defense_capture_mux);
        esp_err_t stop_error = esp_wifi_set_promiscuous(false);
        finish_scan(scan_error == ESP_OK ? stop_error : scan_error);
    }
}

esp_err_t cerberus_wifi_watch_get_status(uint8_t generation, cerberus_watch_scan_status_payload_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    portENTER_CRITICAL(&defense_capture_mux);
    bool valid = watch_capture.status.generation == generation && watch_capture.status.radio == CERBERUS_WATCH_RADIO_WIFI;
    if (valid) *out = watch_capture.status;
    portEXIT_CRITICAL(&defense_capture_mux);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_wifi_watch_get_data(const cerberus_watch_scan_data_request_t *request,
    cerberus_watch_scan_data_payload_t *out)
{
    if (!request || !out) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&defense_capture_mux);
    bool valid = cerberus_watch_capture_get_chunk(&watch_capture, request->radio,
        request->generation, request->index, request->chunk, out);
    portEXIT_CRITICAL(&defense_capture_mux);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_wifi_scanner_init(void)
{
    scanner_mutex = xSemaphoreCreateMutex();
    if (scanner_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err == ESP_OK) {
            err = nvs_flash_init();
        }
    }
    if (err == ESP_OK) {
        err = esp_netif_init();
    }
    if (err == ESP_OK) {
        err = esp_event_loop_create_default();
    }
    if (err == ESP_OK) {
        station_netif = esp_netif_create_default_wifi_sta();
        if (station_netif == NULL) err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK) {
        wifi_init_config_t config = WIFI_INIT_CONFIG_DEFAULT();
        err = esp_wifi_init(&config);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_mode(WIFI_MODE_STA);
    }
    if (err == ESP_OK) {
        err = esp_wifi_start();
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO);
    }
    if (err == ESP_OK) {
        err = esp_wifi_set_promiscuous_rx_cb(wifi_promiscuous_callback);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
    }
    if (err == ESP_OK) {
        err = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
    }
    if (err == ESP_OK && xTaskCreate(
            wifi_scan_task,
            "wifi_scan",
            WIFI_SCAN_TASK_STACK,
            NULL,
            WIFI_SCAN_TASK_PRIORITY,
            &scanner_task_handle) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }
    if (err == ESP_OK && xTaskCreate(
            wifi_defense_task,
            "wifi_defense",
            WIFI_DEFENSE_TASK_STACK,
            NULL,
            WIFI_DEFENSE_TASK_PRIORITY,
            &defense_task_handle) != pdPASS) {
        err = ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    scanner_ready = err == ESP_OK;
    scan_status.state = scanner_ready ? CERBERUS_WIFI_SCAN_IDLE : CERBERUS_WIFI_SCAN_ERROR;
    scan_status.error_code = err;
    defense_status.state = scanner_ready ? CERBERUS_WIFI_DEFENSE_IDLE : CERBERUS_WIFI_DEFENSE_ERROR;
    defense_status.error_code = err;
    defense_status.dwell_ms = CERBERUS_WIFI_DEFENSE_DWELL_MS;
    xSemaphoreGive(scanner_mutex);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "dual-band passive scanner ready");
    } else {
        ESP_LOGE(TAG, "scanner initialization failed: %s", esp_err_to_name(err));
    }
    return err;
}

static bool another_radio_operation_is_active(void)
{
    cerberus_ble_scan_status_payload_t ble_status;
    cerberus_ble_connection_status_payload_t ble_connection;
    cerberus_ble_scanner_get_status(&ble_status);
    cerberus_ble_connection_get_status(&ble_connection);
    return cerberus_ieee802154_scanner_is_running() ||
        ble_status.state == CERBERUS_BLE_SCAN_RUNNING ||
        ble_connection.state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        ble_connection.state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        ble_connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
}

/* Called while scanner_mutex is held. STAGING reserves the selected snapshot. */
static bool wifi_connection_is_active_locked(void)
{
    return connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING;
}

esp_err_t cerberus_wifi_connection_begin(const cerberus_wifi_connect_begin_request_t *request)
{
    if (request == NULL || scanner_mutex == NULL ||
        request->password_length > CERBERUS_WIFI_PASSWORD_MAX_LENGTH) return ESP_ERR_INVALID_ARG;
    if (another_radio_operation_is_active()) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!scanner_ready || scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING || wifi_connection_is_active_locked() ||
        request->generation != scan_status.generation || request->index >= scan_status.stored_count ||
        scan_results[request->index].ssid[0] == '\0') {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&connection_status, 0, sizeof(connection_status));
    connection_status.state = CERBERUS_WIFI_CONNECTION_STAGING;
    connection_status.generation = request->generation;
    connection_status.index = request->index;
    connection_status.channel = scan_results[request->index].primary;
    connection_status.security = map_security(scan_results[request->index].authmode);
    connection_status.rssi = scan_results[request->index].rssi;
    staged_password_length = request->password_length;
    memset(staged_password, 0, sizeof(staged_password));
    memset(staged_password_received, 0, sizeof(staged_password_received));
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}

esp_err_t cerberus_wifi_connection_password(const cerberus_wifi_connect_password_request_t *request)
{
    if (request == NULL || scanner_mutex == NULL || request->byte_count > CERBERUS_WIFI_PASSWORD_CHUNK_SIZE ||
        (size_t)request->offset + request->byte_count > CERBERUS_WIFI_PASSWORD_MAX_LENGTH) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (connection_status.state != CERBERUS_WIFI_CONNECTION_STAGING ||
        (size_t)request->offset + request->byte_count > staged_password_length) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(staged_password + request->offset, request->bytes, request->byte_count);
    memset(staged_password_received + request->offset, 1, request->byte_count);
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}

esp_err_t cerberus_wifi_connection_commit(void)
{
    if (scanner_mutex == NULL) return ESP_ERR_INVALID_STATE;
    if (another_radio_operation_is_active()) return ESP_ERR_INVALID_STATE;
    wifi_config_t config = {0};
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!scanner_ready || connection_status.state != CERBERUS_WIFI_CONNECTION_STAGING ||
        scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING ||
        connection_status.generation != scan_status.generation ||
        connection_status.index >= scan_status.stored_count ||
        scan_results[connection_status.index].ssid[0] == '\0') {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    for (uint8_t i = 0; i < staged_password_length; ++i) {
        if (!staged_password_received[i]) {
            xSemaphoreGive(scanner_mutex);
            return ESP_ERR_INVALID_SIZE;
        }
    }
    const wifi_ap_record_t *record = &scan_results[connection_status.index];
    memcpy(config.sta.ssid, record->ssid, sizeof(config.sta.ssid));
    memcpy(config.sta.password, staged_password, staged_password_length);
    config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    config.sta.bssid_set = true;
    memcpy(config.sta.bssid, record->bssid, sizeof(config.sta.bssid));
    config.sta.channel = record->primary;
    config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    config.sta.threshold.rssi = -127;
    config.sta.threshold.authmode = record->authmode == WIFI_AUTH_OPEN
        ? WIFI_AUTH_OPEN
        : (record->authmode == WIFI_AUTH_WEP
            ? WIFI_AUTH_WEP
            : (record->authmode == WIFI_AUTH_WPA_PSK ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_WPA2_PSK));
    config.sta.pmf_cfg.capable = true;
    config.sta.pmf_cfg.required = false;
    config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    config.sta.failure_retry_cnt = 3;
    config.sta.owe_enabled = record->authmode == WIFI_AUTH_OWE;
    memset(staged_password, 0, sizeof(staged_password));
    memset(staged_password_received, 0, sizeof(staged_password_received));
    staged_password_length = 0;
    /* BEGIN excludes existing connections; keep the reservation through setup. */
    connection_status.state = CERBERUS_WIFI_CONNECTION_CONNECTING;
    connection_status.error_code = ESP_OK;
    xSemaphoreGive(scanner_mutex);
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &config);
    memset(&config, 0, sizeof(config));
    if (err == ESP_OK) err = esp_wifi_connect();
    if (err != ESP_OK) {
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        connection_status.state = CERBERUS_WIFI_CONNECTION_ERROR;
        connection_status.error_code = err;
        xSemaphoreGive(scanner_mutex);
    }
    return err;
}

esp_err_t cerberus_wifi_connection_disconnect(void)
{
    if (scanner_mutex == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool staged = connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING;
    memset(staged_password, 0, sizeof(staged_password));
    memset(staged_password_received, 0, sizeof(staged_password_received));
    staged_password_length = 0U;
    if (staged) {
        connection_status.state = CERBERUS_WIFI_CONNECTION_IDLE;
        connection_status.error_code = ESP_OK;
        xSemaphoreGive(scanner_mutex);
        return ESP_OK;
    }
    connection_status.state = CERBERUS_WIFI_CONNECTION_DISCONNECTING;
    xSemaphoreGive(scanner_mutex);
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK) {
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        connection_status.state = err == ESP_ERR_WIFI_NOT_CONNECT
            ? CERBERUS_WIFI_CONNECTION_IDLE : CERBERUS_WIFI_CONNECTION_ERROR;
        connection_status.error_code = err == ESP_ERR_WIFI_NOT_CONNECT ? ESP_OK : err;
        memset(connection_status.ipv4, 0, sizeof(connection_status.ipv4));
        memset(connection_status.gateway, 0, sizeof(connection_status.gateway));
        memset(connection_status.netmask, 0, sizeof(connection_status.netmask));
        xSemaphoreGive(scanner_mutex);
        if (err == ESP_ERR_WIFI_NOT_CONNECT) err = ESP_OK;
    }
    return err;
}

void cerberus_wifi_connection_get_status(cerberus_wifi_connection_status_payload_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (scanner_mutex == NULL) {
        status->state = CERBERUS_WIFI_CONNECTION_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = connection_status;
    xSemaphoreGive(scanner_mutex);
    if (status->state == CERBERUS_WIFI_CONNECTION_CONNECTED) {
        wifi_ap_record_t ap;
        if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
            status->rssi = ap.rssi;
            status->channel = ap.primary;
        }
    }
}

static bool copy_ipv4_dns(uint8_t destination[4], const esp_netif_dns_info_t *dns)
{
    if (destination == NULL || dns == NULL || dns->ip.type != ESP_IPADDR_TYPE_V4 ||
        dns->ip.u_addr.ip4.addr == 0U) {
        return false;
    }
    memcpy(destination, &dns->ip.u_addr.ip4.addr, 4U);
    return true;
}

void cerberus_wifi_connection_get_dns_status(cerberus_wifi_dns_status_payload_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (scanner_mutex == NULL || station_netif == NULL) {
        status->state = CERBERUS_WIFI_CONNECTION_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    status->state = connection_status.state;
    xSemaphoreGive(scanner_mutex);
    if (status->state != CERBERUS_WIFI_CONNECTION_CONNECTED) return;

    const esp_netif_dns_type_t types[] = {
        ESP_NETIF_DNS_MAIN, ESP_NETIF_DNS_BACKUP, ESP_NETIF_DNS_FALLBACK,
    };
    uint8_t *destinations[] = { status->primary, status->backup, status->fallback };
    const uint8_t flags[] = {
        CERBERUS_WIFI_DNS_PRIMARY_VALID,
        CERBERUS_WIFI_DNS_BACKUP_VALID,
        CERBERUS_WIFI_DNS_FALLBACK_VALID,
    };
    for (size_t index = 0; index < 3U; ++index) {
        esp_netif_dns_info_t dns = {0};
        esp_err_t err = esp_netif_get_dns_info(station_netif, types[index], &dns);
        if (err == ESP_OK && copy_ipv4_dns(destinations[index], &dns)) {
            status->valid_flags |= flags[index];
        } else if (err != ESP_OK && status->error_code == ESP_OK) {
            status->error_code = err;
        }
    }
}

esp_err_t cerberus_wifi_scanner_start(void)
{
    if (scanner_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (another_radio_operation_is_active()) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (!scanner_ready || scanner_task_handle == NULL) {
        esp_err_t err = scan_status.error_code != ESP_OK ? scan_status.error_code : ESP_ERR_INVALID_STATE;
        xSemaphoreGive(scanner_mutex);
        return err;
    }
    if (scan_status.state == CERBERUS_WIFI_SCAN_RUNNING ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING || wifi_connection_is_active_locked()) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    ++scan_status.generation;
    scan_status.state = CERBERUS_WIFI_SCAN_RUNNING;
    scan_status.error_code = ESP_OK;
    scan_status.total_count = 0;
    scan_status.stored_count = 0;
    scan_status.hidden_count = 0;
    scan_cancelled = false;
    scan_started_us = esp_timer_get_time();
    uint16_t successful_scans = radio_stats.successful_scans;
    uint16_t failed_scans = radio_stats.failed_scans;
    memset(&radio_stats, 0, sizeof(radio_stats));
    radio_stats.generation = scan_status.generation;
    radio_stats.strongest_rssi = -127;
    radio_stats.successful_scans = successful_scans;
    radio_stats.failed_scans = failed_scans;
    xSemaphoreGive(scanner_mutex);

    xTaskNotifyGive(scanner_task_handle);
    return ESP_OK;
}

esp_err_t cerberus_wifi_scanner_stop(void)
{
    if (scanner_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (scan_status.state != CERBERUS_WIFI_SCAN_RUNNING) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    scan_cancelled = true;
    xSemaphoreGive(scanner_mutex);
    return esp_wifi_scan_stop();
}

void cerberus_wifi_scanner_get_status(cerberus_wifi_scan_status_payload_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    if (scanner_mutex == NULL) {
        status->state = CERBERUS_WIFI_SCAN_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = scan_status;
    xSemaphoreGive(scanner_mutex);
}

void cerberus_wifi_scanner_get_radio_stats(cerberus_wifi_radio_stats_payload_t *stats)
{
    if (stats == NULL) return;
    memset(stats, 0, sizeof(*stats));
    if (scanner_mutex == NULL) return;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *stats = radio_stats;
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_defense_survey_start(void)
{
    if (scanner_mutex == NULL || defense_task_handle == NULL) return ESP_ERR_INVALID_STATE;
    if (cerberus_ieee802154_scanner_is_running()) return ESP_ERR_INVALID_STATE;

    cerberus_ble_scan_status_payload_t ble_status;
    cerberus_ble_connection_status_payload_t ble_connection;
    cerberus_ble_scanner_get_status(&ble_status);
    cerberus_ble_connection_get_status(&ble_connection);
    bool ble_connection_active = ble_connection.state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        ble_connection.state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        ble_connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
    if (ble_status.state == CERBERUS_BLE_SCAN_RUNNING || ble_connection_active) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool wifi_connection_active = connection_status.state == CERBERUS_WIFI_CONNECTION_STAGING ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTING ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_CONNECTED ||
        connection_status.state == CERBERUS_WIFI_CONNECTION_DISCONNECTING;
    if (!scanner_ready || scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING || wifi_connection_active) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    bool seen[256] = {0};
    uint8_t channel_count = 0U;
    for (uint8_t index = 0U; index < scan_status.stored_count; ++index) {
        uint8_t channel = scan_results[index].primary;
        if (channel == 0U || seen[channel]) continue;
        seen[channel] = true;
        defense_channels[channel_count++] = channel;
    }
    if (channel_count == 0U) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    for (uint8_t left = 0U; left < channel_count; ++left) {
        for (uint8_t right = (uint8_t)(left + 1U); right < channel_count; ++right) {
            if (defense_channels[right] < defense_channels[left]) {
                uint8_t swap = defense_channels[left];
                defense_channels[left] = defense_channels[right];
                defense_channels[right] = swap;
            }
        }
    }

    ++defense_status.generation;
    defense_status.state = CERBERUS_WIFI_DEFENSE_RUNNING;
    defense_status.result_count = channel_count;
    defense_status.completed_count = 0U;
    defense_status.current_index = 0U;
    defense_status.current_channel = defense_channels[0];
    defense_status.flagged_count = 0U;
    defense_status.elapsed_ms = 0U;
    defense_status.error_code = ESP_OK;
    defense_status.dwell_ms = CERBERUS_WIFI_DEFENSE_DWELL_MS;
    defense_cancelled = false;
    memset(defense_results, 0, sizeof(defense_results));
    xSemaphoreGive(scanner_mutex);
    xTaskNotifyGive(defense_task_handle);
    return ESP_OK;
}

esp_err_t cerberus_wifi_defense_survey_stop(void)
{
    if (scanner_mutex == NULL) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (defense_status.state != CERBERUS_WIFI_DEFENSE_RUNNING) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    defense_cancelled = true;
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}

bool cerberus_wifi_defense_survey_is_running(void)
{
    if (scanner_mutex == NULL) return false;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    bool running = defense_status.state == CERBERUS_WIFI_DEFENSE_RUNNING;
    xSemaphoreGive(scanner_mutex);
    return running;
}

void cerberus_wifi_defense_survey_get_status(cerberus_wifi_defense_status_payload_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (scanner_mutex == NULL) {
        status->state = CERBERUS_WIFI_DEFENSE_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = defense_status;
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_wifi_defense_survey_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_defense_result_payload_t *result)
{
    if (result == NULL || scanner_mutex == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if ((defense_status.state != CERBERUS_WIFI_DEFENSE_COMPLETE &&
         defense_status.state != CERBERUS_WIFI_DEFENSE_RUNNING) ||
        generation != defense_status.generation || index >= defense_status.completed_count) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    *result = defense_results[index];
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}

esp_err_t cerberus_wifi_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_scan_result_payload_t *result)
{
    if (result == NULL || scanner_mutex == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        generation != scan_status.generation || index >= scan_status.stored_count) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    const wifi_ap_record_t *record = &scan_results[index];
    memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->index = index;
    result->rssi = record->rssi;
    result->channel = record->primary;
    result->security = map_security(record->authmode);
    result->ssid_length = (uint8_t)strnlen((const char *)record->ssid, CERBERUS_WIFI_SSID_MAX_LENGTH);
    memcpy(result->bssid, record->bssid, sizeof(result->bssid));
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}

esp_err_t cerberus_wifi_scanner_get_ssid_chunk(
    uint8_t generation,
    uint8_t index,
    uint8_t chunk,
    cerberus_wifi_scan_ssid_payload_t *result)
{
    if (result == NULL || scanner_mutex == NULL || chunk > 1U) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (scan_status.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        generation != scan_status.generation || index >= scan_status.stored_count) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    const wifi_ap_record_t *record = &scan_results[index];
    size_t ssid_length = strnlen((const char *)record->ssid, CERBERUS_WIFI_SSID_MAX_LENGTH);
    size_t offset = (size_t)chunk * CERBERUS_WIFI_SSID_CHUNK_SIZE;
    size_t byte_count = offset < ssid_length ? ssid_length - offset : 0U;
    if (byte_count > CERBERUS_WIFI_SSID_CHUNK_SIZE) {
        byte_count = CERBERUS_WIFI_SSID_CHUNK_SIZE;
    }

    memset(result, 0, sizeof(*result));
    result->generation = generation;
    result->index = index;
    result->chunk = chunk;
    result->byte_count = (uint8_t)byte_count;
    if (byte_count > 0U) {
        memcpy(result->bytes, record->ssid + offset, byte_count);
    }
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}
