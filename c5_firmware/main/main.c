#include "cerberus_pins.h"
#include "cerberus_protocol.h"
#include "ble_scanner.h"
#include "event_queue.h"
#include "fragment_transfer.h"
#include "ieee802154_scanner.h"
#include "mdns_scanner.h"
#include "network_diagnostics.h"
#include "wifi_scanner.h"

#include "driver/gpio.h"
#include "driver/spi_slave.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "cerberus_c5";
static WORD_ALIGNED_ATTR uint8_t rx_buffer[CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE];
static WORD_ALIGNED_ATTR uint8_t tx_buffer[CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE];
static WORD_ALIGNED_ATTR cerberus_c5_event_batch_frame_t event_batch_cache;
static uint8_t event_batch_cache_sequence;
static uint8_t event_batch_cache_max_events;
static bool event_batch_cache_valid;

static uint16_t heap_kib(uint32_t bytes)
{
    uint32_t kib = bytes / 1024U;
    return kib > UINT16_MAX ? UINT16_MAX : (uint16_t)kib;
}

static uint8_t current_radio_state(void)
{
    cerberus_wifi_scan_status_payload_t wifi_status;
    cerberus_wifi_connection_status_payload_t wifi_connection;
    cerberus_wifi_defense_status_payload_t wifi_defense;
    cerberus_ble_scan_status_payload_t ble_status;
    cerberus_ble_connection_status_payload_t connection_status;
    cerberus_ieee802154_energy_scan_status_payload_t energy_status;
    cerberus_ieee802154_beacon_scan_status_payload_t beacon_status;
    cerberus_ieee802154_activity_scan_status_payload_t activity_status;
    cerberus_wifi_scanner_get_status(&wifi_status);
    cerberus_wifi_connection_get_status(&wifi_connection);
    cerberus_wifi_defense_survey_get_status(&wifi_defense);
    cerberus_ble_scanner_get_status(&ble_status);
    cerberus_ble_connection_get_status(&connection_status);
    cerberus_ieee802154_scanner_get_status(&energy_status);
    cerberus_ieee802154_beacon_scanner_get_status(&beacon_status);
    cerberus_ieee802154_activity_scanner_get_status(&activity_status);

    uint8_t state = 0U;
    if (wifi_status.state == CERBERUS_WIFI_SCAN_RUNNING) {
        state |= CERBERUS_C5_RADIO_WIFI_SCAN;
    }
    if (wifi_connection.state == CERBERUS_WIFI_CONNECTION_CONNECTED) {
        state |= CERBERUS_C5_RADIO_WIFI_CONNECTED;
    }
    if (wifi_defense.state == CERBERUS_WIFI_DEFENSE_RUNNING) {
        state |= CERBERUS_C5_RADIO_WIFI_DEFENSE_SURVEY;
    }
    if (ble_status.state == CERBERUS_BLE_SCAN_RUNNING) {
        state |= CERBERUS_C5_RADIO_BLE_SCAN;
    }
    if (connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        connection_status.state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        connection_status.state == CERBERUS_BLE_CONNECTION_DISCONNECTING) {
        state |= CERBERUS_C5_RADIO_BLE_CONNECTION;
    }
    if (energy_status.state == CERBERUS_IEEE802154_ENERGY_SCAN_RUNNING ||
        beacon_status.state == CERBERUS_IEEE802154_BEACON_SCAN_RUNNING ||
        activity_status.state == CERBERUS_IEEE802154_ACTIVITY_SCAN_RUNNING) {
        state |= CERBERUS_C5_RADIO_IEEE802154_SCAN;
    }
    return state;
}

static void build_diagnostic_snapshot(cerberus_c5_diagnostic_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    cerberus_c5_event_queue_status_payload_t event_status;
    cerberus_wifi_scan_status_payload_t wifi_status;
    cerberus_ble_scan_status_payload_t ble_status;
    cerberus_ble_connection_status_payload_t connection_status;
    cerberus_ieee802154_energy_scan_status_payload_t energy_status;
    cerberus_ieee802154_beacon_scan_status_payload_t beacon_status;
    cerberus_ieee802154_activity_scan_status_payload_t activity_status;
    cerberus_c5_event_queue_get_status(&event_status);
    cerberus_wifi_scanner_get_status(&wifi_status);
    cerberus_ble_scanner_get_status(&ble_status);
    cerberus_ble_connection_get_status(&connection_status);
    cerberus_ieee802154_scanner_get_status(&energy_status);
    cerberus_ieee802154_beacon_scanner_get_status(&beacon_status);
    cerberus_ieee802154_activity_scanner_get_status(&activity_status);

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->firmware_version = CERBERUS_C5_FIRMWARE_VERSION;
    snapshot->uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    snapshot->capabilities = CERBERUS_C5_CAPABILITIES;
    snapshot->free_heap_bytes = esp_get_free_heap_size();
    snapshot->minimum_free_heap_bytes = esp_get_minimum_free_heap_size();
    snapshot->event_produced = event_status.produced;
    snapshot->event_consumed = event_status.consumed;
    snapshot->event_dropped = event_status.dropped;
    snapshot->event_deferred = event_status.deferred;
    snapshot->event_pending = event_status.pending;
    snapshot->event_capacity = event_status.capacity;
    snapshot->event_flow_flags = event_status.flow_flags;
    snapshot->radio_state = current_radio_state();
    snapshot->reset_reason = (uint8_t)esp_reset_reason();
    snapshot->boot_count = cerberus_ble_scanner_get_boot_count();
    snapshot->ble_phase = cerberus_ble_scanner_get_phase();
    snapshot->wifi_state = wifi_status.state;
    snapshot->wifi_generation = wifi_status.generation;
    snapshot->ble_scan_state = ble_status.state;
    snapshot->ble_scan_generation = ble_status.generation;
    snapshot->ble_connection_state = connection_status.state;
    snapshot->ble_connection_generation = connection_status.generation;
    snapshot->ieee802154_energy_state = energy_status.state;
    snapshot->ieee802154_energy_generation = energy_status.generation;
    snapshot->ieee802154_beacon_state = beacon_status.state;
    snapshot->ieee802154_beacon_generation = beacon_status.generation;
    snapshot->ieee802154_activity_state = activity_status.state;
    snapshot->ieee802154_activity_generation = activity_status.generation;
}

static void prepare_frame_response(
    cerberus_spi_command_t command,
    uint8_t sequence,
    const void *payload,
    size_t payload_length)
{
    (void)cerberus_protocol_prepare_frame(
        (cerberus_spi_frame_t *)tx_buffer,
        command,
        sequence,
        payload,
        payload_length);
}

static void prepare_v1_response(const cerberus_spi_v1_request_t *request)
{
    if (request == NULL ||
        request->magic != CERBERUS_SPI_MAGIC ||
        request->protocol_version != CERBERUS_PROTOCOL_VERSION_V1 ||
        request->command != CERBERUS_SPI_COMMAND_STATUS) {
        return;
    }

    cerberus_spi_v1_response_t *response = (cerberus_spi_v1_response_t *)tx_buffer;
    response->magic = CERBERUS_SPI_MAGIC;
    response->protocol_version = CERBERUS_PROTOCOL_VERSION_V1;
    response->command = request->command;
    response->sequence = request->sequence;
    response->ready = 1;
    response->firmware_version = CERBERUS_C5_FIRMWARE_VERSION;
    response->uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
}

static void prepare_v2_response(const cerberus_spi_frame_t *request)
{
    if (!cerberus_protocol_frame_is_valid(request)) {
        return;
    }
    if (request->command != CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH) {
        event_batch_cache_valid = false;
    }

    switch ((cerberus_spi_command_t)request->command) {
    case CERBERUS_SPI_COMMAND_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        cerberus_status_payload_t payload = {
            .ready = 1,
            .reset_reason = (uint8_t)esp_reset_reason(),
            .boot_count = cerberus_ble_scanner_get_boot_count(),
            .ble_phase = cerberus_ble_scanner_get_phase(),
            .firmware_version = CERBERUS_C5_FIRMWARE_VERSION,
            .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000),
            .capabilities = CERBERUS_C5_CAPABILITIES,
            .free_heap_kib = heap_kib(esp_get_free_heap_size()),
            .minimum_free_heap_kib = heap_kib(esp_get_minimum_free_heap_size()),
            .radio_state = current_radio_state(),
        };
        prepare_frame_response(CERBERUS_SPI_COMMAND_STATUS, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_SCAN_START:
    case CERBERUS_SPI_COMMAND_WIFI_SCAN_STOP:
    case CERBERUS_SPI_COMMAND_WIFI_SCAN_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_WIFI_SCAN_START) {
            (void)cerberus_wifi_scanner_start();
        } else if (request->command == CERBERUS_SPI_COMMAND_WIFI_SCAN_STOP) {
            (void)cerberus_wifi_scanner_stop();
        }
        cerberus_wifi_scan_status_payload_t payload;
        cerberus_wifi_scanner_get_status(&payload);
        prepare_frame_response((cerberus_spi_command_t)request->command, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_wifi_scan_result_request_t)) {
            return;
        }
        cerberus_wifi_scan_result_request_t query;
        cerberus_wifi_scan_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_scanner_get_result(query.generation, query.index, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_SCAN_RESULT, request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_SCAN_SSID: {
        if (request->payload_length != sizeof(cerberus_wifi_scan_ssid_request_t)) {
            return;
        }
        cerberus_wifi_scan_ssid_request_t query;
        cerberus_wifi_scan_ssid_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_scanner_get_ssid_chunk(
                query.generation, query.index, query.chunk, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_SCAN_SSID, request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_RADIO_STATS: {
        if (request->payload_length != 0U) return;
        cerberus_wifi_radio_stats_payload_t payload;
        cerberus_wifi_scanner_get_radio_stats(&payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_RADIO_STATS,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_DEFENSE_START:
    case CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STOP:
    case CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STATUS: {
        if (request->payload_length != 0U) return;
        esp_err_t action_error = ESP_OK;
        if (request->command == CERBERUS_SPI_COMMAND_WIFI_DEFENSE_START) {
            action_error = cerberus_wifi_defense_survey_start();
        } else if (request->command == CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STOP) {
            action_error = cerberus_wifi_defense_survey_stop();
        }
        cerberus_wifi_defense_status_payload_t payload;
        cerberus_wifi_defense_survey_get_status(&payload);
        if (action_error != ESP_OK && request->command != CERBERUS_SPI_COMMAND_WIFI_DEFENSE_STATUS) {
            payload.state = CERBERUS_WIFI_DEFENSE_ERROR;
            payload.error_code = action_error;
        }
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_DEFENSE_RESULT: {
        if (request->payload_length != sizeof(cerberus_wifi_defense_result_request_t)) return;
        cerberus_wifi_defense_result_request_t query;
        cerberus_wifi_defense_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_defense_survey_get_result(
                query.generation, query.index, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_DEFENSE_RESULT,
                request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_BEGIN: {
        if (request->payload_length != sizeof(cerberus_wifi_connect_begin_request_t)) return;
        cerberus_wifi_connect_begin_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        (void)cerberus_wifi_connection_begin(&query);
        cerberus_wifi_connection_status_payload_t payload;
        cerberus_wifi_connection_get_status(&payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CONNECT_BEGIN, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_PASSWORD: {
        if (request->payload_length != sizeof(cerberus_wifi_connect_password_request_t)) return;
        cerberus_wifi_connect_password_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        (void)cerberus_wifi_connection_password(&query);
        cerberus_wifi_connection_status_payload_t payload;
        cerberus_wifi_connection_get_status(&payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CONNECT_PASSWORD, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CONNECT_COMMIT:
    case CERBERUS_SPI_COMMAND_WIFI_DISCONNECT:
    case CERBERUS_SPI_COMMAND_WIFI_CONNECTION_STATUS: {
        if (request->payload_length != 0U) return;
        if (request->command == CERBERUS_SPI_COMMAND_WIFI_CONNECT_COMMIT) {
            (void)cerberus_wifi_connection_commit();
        } else if (request->command == CERBERUS_SPI_COMMAND_WIFI_DISCONNECT) {
            (void)cerberus_wifi_connection_disconnect();
        }
        cerberus_wifi_connection_status_payload_t payload;
        cerberus_wifi_connection_get_status(&payload);
        prepare_frame_response((cerberus_spi_command_t)request->command, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_DNS_STATUS: {
        if (request->payload_length != 0U) return;
        cerberus_wifi_dns_status_payload_t payload;
        cerberus_wifi_connection_get_dns_status(&payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_DNS_STATUS,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_START:
    case CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_STATUS: {
        if (request->payload_length != 0U) return;
        if (request->command == CERBERUS_SPI_COMMAND_NETWORK_DIAGNOSTICS_START) {
            (void)cerberus_network_diagnostics_start();
        }
        cerberus_network_diagnostics_status_payload_t payload;
        cerberus_network_diagnostics_get_status(&payload);
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MDNS_SCAN_START:
    case CERBERUS_SPI_COMMAND_MDNS_SCAN_STATUS: {
        if (request->payload_length != 0U) return;
        if (request->command == CERBERUS_SPI_COMMAND_MDNS_SCAN_START) {
            (void)cerberus_mdns_scanner_start();
        }
        cerberus_mdns_scan_status_payload_t payload;
        cerberus_mdns_scanner_get_status(&payload);
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MDNS_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_mdns_result_request_t)) return;
        cerberus_mdns_result_request_t query;
        cerberus_mdns_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_mdns_scanner_get_result(
                query.generation, query.index, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_MDNS_SCAN_RESULT,
                request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_SCAN_START:
    case CERBERUS_SPI_COMMAND_BLE_SCAN_STOP:
    case CERBERUS_SPI_COMMAND_BLE_SCAN_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_BLE_SCAN_START) {
            (void)cerberus_ble_scanner_start();
        } else if (request->command == CERBERUS_SPI_COMMAND_BLE_SCAN_STOP) {
            (void)cerberus_ble_scanner_stop();
        }
        cerberus_ble_scan_status_payload_t payload;
        cerberus_ble_scanner_get_status(&payload);
        prepare_frame_response((cerberus_spi_command_t)request->command, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_ble_scan_result_request_t)) {
            return;
        }
        cerberus_ble_scan_result_request_t query;
        cerberus_ble_scan_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ble_scanner_get_result(query.generation, query.index, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_BLE_SCAN_RESULT, request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_SCAN_DATA: {
        if (request->payload_length != sizeof(cerberus_ble_scan_data_request_t)) {
            return;
        }
        cerberus_ble_scan_data_request_t query;
        cerberus_ble_scan_data_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ble_scanner_get_data_chunk(
                query.generation, query.index, query.kind, query.chunk, &payload) == ESP_OK) {
            prepare_frame_response(CERBERUS_SPI_COMMAND_BLE_SCAN_DATA, request->sequence, &payload, sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_CONNECT: {
        if (request->payload_length != sizeof(cerberus_ble_connect_request_t)) {
            return;
        }
        cerberus_ble_connect_request_t connection_request;
        cerberus_ble_connection_status_payload_t payload;
        memcpy(&connection_request, request->payload, sizeof(connection_request));
        (void)cerberus_ble_connection_start(&connection_request);
        cerberus_ble_connection_get_status(&payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_BLE_CONNECT, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_DISCONNECT:
    case CERBERUS_SPI_COMMAND_BLE_CONNECTION_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_BLE_DISCONNECT) {
            (void)cerberus_ble_connection_disconnect();
        }
        cerberus_ble_connection_status_payload_t payload;
        cerberus_ble_connection_get_status(&payload);
        prepare_frame_response(
            (cerberus_spi_command_t)request->command,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS: {
        if (request->payload_length != sizeof(cerberus_watch_scan_request_t)) return;
        cerberus_watch_scan_request_t query;
        cerberus_watch_scan_status_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        esp_err_t err = query.radio == CERBERUS_WATCH_RADIO_BLE ?
            cerberus_ble_watch_get_status(query.generation, &payload) :
            query.radio == CERBERUS_WATCH_RADIO_WIFI ?
            cerberus_wifi_watch_get_status(query.generation, &payload) : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) prepare_frame_response(CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA: {
        if (request->payload_length != sizeof(cerberus_watch_scan_data_request_t)) return;
        cerberus_watch_scan_data_request_t query;
        cerberus_watch_scan_data_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        esp_err_t err = query.radio == CERBERUS_WATCH_RADIO_BLE ?
            cerberus_ble_watch_get_data(&query, &payload) : query.radio == CERBERUS_WATCH_RADIO_WIFI ?
            cerberus_wifi_watch_get_data(&query, &payload) : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) prepare_frame_response(CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_ACTIVITY: {
        if (request->payload_length != sizeof(cerberus_watch_scan_request_t)) return;
        cerberus_watch_scan_request_t query;
        cerberus_ble_activity_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (query.radio == CERBERUS_WATCH_RADIO_BLE &&
            cerberus_ble_watch_get_activity(query.generation, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_BLE_ACTIVITY, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_DISCOVER:
    case CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_DISCOVER) {
            (void)cerberus_ble_gatt_service_discovery_start();
        }
        cerberus_ble_gatt_service_status_payload_t payload;
        cerberus_ble_gatt_service_get_status(&payload);
        prepare_frame_response(
            (cerberus_spi_command_t)request->command,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_RESULT: {
        if (request->payload_length != sizeof(cerberus_ble_gatt_service_result_request_t)) {
            return;
        }
        cerberus_ble_gatt_service_result_request_t query;
        cerberus_ble_gatt_service_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ble_gatt_service_get_result(
                query.generation,
                query.index,
                &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_BLE_GATT_SERVICE_RESULT,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_DISCOVER: {
        if (request->payload_length != sizeof(cerberus_ble_gatt_characteristic_discover_request_t)) {
            return;
        }
        cerberus_ble_gatt_characteristic_discover_request_t discovery_request;
        cerberus_ble_gatt_characteristic_status_payload_t payload;
        memcpy(&discovery_request, request->payload, sizeof(discovery_request));
        (void)cerberus_ble_gatt_characteristic_discovery_start(&discovery_request);
        cerberus_ble_gatt_characteristic_get_status(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_DISCOVER,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        cerberus_ble_gatt_characteristic_status_payload_t payload;
        cerberus_ble_gatt_characteristic_get_status(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_STATUS,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_RESULT: {
        if (request->payload_length != sizeof(cerberus_ble_gatt_characteristic_result_request_t)) {
            return;
        }
        cerberus_ble_gatt_characteristic_result_request_t query;
        cerberus_ble_gatt_characteristic_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ble_gatt_characteristic_get_result(
                query.generation,
                query.index,
                &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_BLE_GATT_CHARACTERISTIC_RESULT,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_READ_START: {
        if (request->payload_length != sizeof(cerberus_ble_gatt_read_request_t)) {
            return;
        }
        cerberus_ble_gatt_read_request_t read_request;
        cerberus_ble_gatt_read_status_payload_t payload;
        memcpy(&read_request, request->payload, sizeof(read_request));
        (void)cerberus_ble_gatt_read_start(&read_request);
        cerberus_ble_gatt_read_get_status(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_BLE_GATT_READ_START,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_READ_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        cerberus_ble_gatt_read_status_payload_t payload;
        cerberus_ble_gatt_read_get_status(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_BLE_GATT_READ_STATUS,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_BLE_GATT_READ_DATA: {
        if (request->payload_length != sizeof(cerberus_ble_gatt_read_data_request_t)) {
            return;
        }
        cerberus_ble_gatt_read_data_request_t query;
        cerberus_ble_gatt_read_data_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ble_gatt_read_get_data(
                query.generation,
                query.chunk,
                &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_BLE_GATT_READ_DATA,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_START:
    case CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_START) {
            (void)cerberus_ieee802154_scanner_start();
        }
        cerberus_ieee802154_energy_scan_status_payload_t payload;
        cerberus_ieee802154_scanner_get_status(&payload);
        prepare_frame_response(
            (cerberus_spi_command_t)request->command,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_ieee802154_energy_scan_result_request_t)) {
            return;
        }
        cerberus_ieee802154_energy_scan_result_request_t query;
        cerberus_ieee802154_energy_scan_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ieee802154_scanner_get_result(query.generation, &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_IEEE802154_ENERGY_SCAN_RESULT,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_START:
    case CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_START) {
            (void)cerberus_ieee802154_beacon_scanner_start();
        }
        cerberus_ieee802154_beacon_scan_status_payload_t payload;
        cerberus_ieee802154_beacon_scanner_get_status(&payload);
        prepare_frame_response(
            (cerberus_spi_command_t)request->command,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_ieee802154_beacon_scan_result_request_t)) {
            return;
        }
        cerberus_ieee802154_beacon_scan_result_request_t query;
        cerberus_ieee802154_beacon_scan_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ieee802154_beacon_scanner_get_result(
                query.generation, query.index, &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_IEEE802154_BEACON_SCAN_RESULT,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_START:
    case CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        if (request->command == CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_START) {
            (void)cerberus_ieee802154_activity_scanner_start();
        }
        cerberus_ieee802154_activity_scan_status_payload_t payload;
        cerberus_ieee802154_activity_scanner_get_status(&payload);
        prepare_frame_response(
            (cerberus_spi_command_t)request->command,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_ieee802154_activity_scan_result_request_t)) {
            return;
        }
        cerberus_ieee802154_activity_scan_result_request_t query;
        cerberus_ieee802154_activity_scan_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_ieee802154_activity_scanner_get_result(
                query.generation, query.result_index, &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_IEEE802154_ACTIVITY_SCAN_RESULT,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS: {
        if (request->payload_length != 0U) {
            return;
        }
        cerberus_c5_event_queue_status_payload_t payload;
        cerberus_c5_event_queue_get_status(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_EVENT_QUEUE_STATUS,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP: {
        if (request->payload_length != 0U) {
            return;
        }
        cerberus_c5_event_payload_t payload;
        (void)cerberus_c5_event_queue_pop(&payload);
        prepare_frame_response(
            CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP,
            request->sequence,
            &payload,
            sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_EVENT_QUEUE_FLOW_CONTROL: {
        if (request->payload_length != sizeof(cerberus_c5_event_flow_control_request_t)) {
            return;
        }
        cerberus_c5_event_flow_control_request_t query;
        cerberus_c5_event_queue_status_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_c5_event_queue_configure_flow_control(&query, &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_EVENT_QUEUE_FLOW_CONTROL,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_EVENT_QUEUE_POP_BATCH: {
        if (request->payload_length != sizeof(cerberus_c5_event_batch_request_t)) {
            return;
        }
        cerberus_c5_event_batch_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        if (query.max_events == 0U ||
            query.max_events > CERBERUS_C5_EVENT_BATCH_MAX_EVENTS) {
            return;
        }

        if (event_batch_cache_valid &&
            event_batch_cache_sequence == request->sequence &&
            event_batch_cache_max_events == query.max_events) {
            memcpy(tx_buffer, &event_batch_cache, sizeof(event_batch_cache));
            break;
        }

        cerberus_c5_event_payload_t events[CERBERUS_C5_EVENT_BATCH_MAX_EVENTS];
        uint8_t event_count = cerberus_c5_event_queue_pop_batch(events, query.max_events);
        cerberus_c5_event_queue_status_payload_t status;
        cerberus_c5_event_queue_get_status(&status);
        if (cerberus_protocol_prepare_event_batch(
                &event_batch_cache,
                request->sequence,
                events,
                event_count,
                status.pending,
                status.dropped,
                status.deferred,
                status.flow_flags,
                status.control_generation)) {
            event_batch_cache_sequence = request->sequence;
            event_batch_cache_max_events = query.max_events;
            event_batch_cache_valid = true;
            memcpy(tx_buffer, &event_batch_cache, sizeof(event_batch_cache));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_FRAGMENT_OPEN: {
        if (request->payload_length != sizeof(cerberus_fragment_open_request_t)) {
            return;
        }
        cerberus_fragment_open_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        if (query.object_type != CERBERUS_FRAGMENT_OBJECT_C5_DIAGNOSTIC_SNAPSHOT) {
            return;
        }

        cerberus_c5_diagnostic_snapshot_t snapshot;
        cerberus_fragment_info_payload_t payload;
        build_diagnostic_snapshot(&snapshot);
        if (cerberus_c5_fragment_open(
                query.object_type,
                &snapshot,
                sizeof(snapshot),
                &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_FRAGMENT_OPEN,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    case CERBERUS_SPI_COMMAND_FRAGMENT_READ: {
        if (request->payload_length != sizeof(cerberus_fragment_read_request_t)) {
            return;
        }
        cerberus_fragment_read_request_t query;
        cerberus_fragment_read_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_c5_fragment_read(&query, &payload) == ESP_OK) {
            prepare_frame_response(
                CERBERUS_SPI_COMMAND_FRAGMENT_READ,
                request->sequence,
                &payload,
                sizeof(payload));
        }
        break;
    }
    default:
        break;
    }
}

static void prepare_response(const uint8_t *request_bytes)
{
    memset(tx_buffer, 0, sizeof(tx_buffer));
    if (request_bytes == NULL) {
        return;
    }

    const cerberus_spi_v1_request_t *prefix = (const cerberus_spi_v1_request_t *)request_bytes;
    if (prefix->magic != CERBERUS_SPI_MAGIC) {
        return;
    }
    if (prefix->protocol_version == CERBERUS_PROTOCOL_VERSION) {
        prepare_v2_response((const cerberus_spi_frame_t *)request_bytes);
    } else if (prefix->protocol_version == CERBERUS_PROTOCOL_VERSION_V1) {
        prepare_v1_response(prefix);
    }
}

static void spi_slave_task(void *arg)
{
    (void)arg;

    gpio_config_t ready_gpio = {
        .pin_bit_mask = 1ULL << CERBERUS_C5_READY_GPIO,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&ready_gpio));
    gpio_set_level(CERBERUS_C5_READY_GPIO, 0);

    spi_bus_config_t bus = {
        .mosi_io_num = CERBERUS_C5_SLAVE_SPI_MOSI_GPIO,
        .miso_io_num = CERBERUS_C5_SLAVE_SPI_MISO_GPIO,
        .sclk_io_num = CERBERUS_C5_SLAVE_SPI_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE,
    };
    spi_slave_interface_config_t slave = {
        .mode = 0,
        .spics_io_num = CERBERUS_C5_SLAVE_SPI_CS_GPIO,
        .queue_size = 1,
    };

    ESP_ERROR_CHECK(spi_slave_initialize(SPI2_HOST, &bus, &slave, SPI_DMA_DISABLED));
    ESP_LOGI(TAG, "onboard SPI link ready");

    while (true) {
        memset(rx_buffer, 0, sizeof(rx_buffer));
        spi_slave_transaction_t transaction = {
            .length = CERBERUS_SPI_EVENT_BATCH_TRANSFER_SIZE * 8,
            .tx_buffer = tx_buffer,
            .rx_buffer = rx_buffer,
        };
        esp_err_t err = spi_slave_queue_trans(SPI2_HOST, &transaction, portMAX_DELAY);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "SPI queue failed: %s", esp_err_to_name(err));
            continue;
        }

        /* Assert only after the driver owns a queued transaction. */
        gpio_set_level(CERBERUS_C5_READY_GPIO, 1);
        spi_slave_transaction_t *completed = NULL;
        err = spi_slave_get_trans_result(SPI2_HOST, &completed, portMAX_DELAY);
        gpio_set_level(CERBERUS_C5_READY_GPIO, 0);
        if (err == ESP_OK) {
            prepare_response(rx_buffer);
        } else {
            ESP_LOGW(TAG, "SPI transaction failed: %s", esp_err_to_name(err));
        }
    }
}

void app_main(void)
{
#ifdef CERBERUS_C5_PANIC_DIAGNOSTIC
    esp_rom_printf("C5-DIAG app_main\r\n");
#endif
    ESP_LOGI(TAG, "Cerberus C5 base firmware starting");
    ESP_ERROR_CHECK(cerberus_protocol_self_test() ? ESP_OK : ESP_FAIL);
    esp_err_t wifi_err = cerberus_wifi_scanner_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scanner unavailable: %s", esp_err_to_name(wifi_err));
    }
    esp_err_t network_diagnostics_err = cerberus_network_diagnostics_init();
    if (network_diagnostics_err != ESP_OK) {
        ESP_LOGW(TAG, "network diagnostics unavailable: %s",
            esp_err_to_name(network_diagnostics_err));
    }
    esp_err_t mdns_err = cerberus_mdns_scanner_init();
    if (mdns_err != ESP_OK) {
        ESP_LOGW(TAG, "mDNS discovery unavailable: %s", esp_err_to_name(mdns_err));
    }
    esp_err_t ble_err = cerberus_ble_scanner_init();
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "BLE observer unavailable: %s", esp_err_to_name(ble_err));
    }
    esp_err_t ieee802154_err = cerberus_ieee802154_scanner_init();
    if (ieee802154_err != ESP_OK) {
        ESP_LOGW(TAG, "802.15.4 energy scanner unavailable: %s", esp_err_to_name(ieee802154_err));
    }
    esp_err_t event_err = cerberus_c5_event_queue_init();
    if (event_err != ESP_OK) {
        ESP_LOGW(TAG, "asynchronous event queue unavailable: %s", esp_err_to_name(event_err));
    }
#ifndef CERBERUS_C5_PANIC_DIAGNOSTIC
    xTaskCreate(spi_slave_task, "spi_slave", 4096, NULL, 5, NULL);
#endif
}
