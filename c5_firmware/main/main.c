#include "cerberus_pins.h"
#include "cerberus_protocol.h"
#include "ble_scanner.h"
#include "event_queue.h"
#include "fragment_transfer.h"
#include "ieee802154_scanner.h"
#include "mdns_scanner.h"
#include "network_diagnostics.h"
#include "network_scanner.h"
#include "radio_test_service.h"
#include "wifi_scanner.h"
#include "wifi_capture_service.h"

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
    if (cerberus_c5_test_running()) state |= CERBERUS_C5_RADIO_TEST;
    if (cerberus_wifi_station_running()) state |= CERBERUS_C5_RADIO_WIFI_STATIONS;
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
    case CERBERUS_SPI_COMMAND_WIFI_RELATION_STATUS: {
        if (request->payload_length != sizeof(cerberus_wifi_relation_status_query_t)) return;
        cerberus_wifi_relation_status_query_t query;
        cerberus_wifi_relation_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_relation_get_status(query.generation, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_RELATION_STATUS, request->sequence,
                &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_RELATION_CHUNK: {
        if (request->payload_length != sizeof(cerberus_wifi_relation_query_t)) return;
        cerberus_wifi_relation_query_t query;
        cerberus_wifi_relation_chunk_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_relation_get_result_chunk(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_RELATION_CHUNK, request->sequence,
                &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_RELATION_CLOCK: {
        if (request->payload_length != sizeof(cerberus_wifi_relation_clock_query_t)) return;
        cerberus_wifi_relation_clock_query_t query;
        cerberus_wifi_relation_clock_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_relation_get_source_clock(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_RELATION_CLOCK, request->sequence,
                &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS: {
        if (request->payload_length != sizeof(cerberus_wifi_probe_status_query_t)) return;
        cerberus_wifi_probe_status_query_t query;
        cerberus_wifi_probe_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_probe_get_status(query.generation, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_PROBE_STATUS, request->sequence,
                &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK: {
        if (request->payload_length != sizeof(cerberus_wifi_probe_query_t)) return;
        cerberus_wifi_probe_query_t query;
        cerberus_wifi_probe_chunk_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_probe_get_result_chunk(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_PROBE_CHUNK, request->sequence,
                &payload, sizeof(payload));
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
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START:
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STATUS:
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP: {
        esp_err_t error=ESP_OK;
        if(request->command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_START){
            if(request->payload_length!=sizeof(cerberus_wifi_capture_start_request_t))return;
            cerberus_wifi_capture_start_request_t query;
            memcpy(&query,request->payload,sizeof(query));
            error=cerberus_wifi_capture_start(&query);
        }else if(request->command==CERBERUS_SPI_COMMAND_WIFI_CAPTURE_STOP){
            if(request->payload_length!=sizeof(cerberus_wifi_capture_stop_request_t))return;
            cerberus_wifi_capture_stop_request_t query;
            memcpy(&query,request->payload,sizeof(query));
            error=cerberus_wifi_capture_stop(query.generation);
        }else if(request->payload_length!=0U)return;
        cerberus_wifi_capture_status_payload_t payload;
        cerberus_wifi_capture_status(&payload);
        if(error!=ESP_OK){payload.state=CERBERUS_WIFI_CAPTURE_ERROR;payload.error_code=error;}
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence,&payload,sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK: {
        if(request->payload_length!=sizeof(cerberus_wifi_capture_stop_request_t))return;
        cerberus_wifi_capture_stop_request_t query;
        cerberus_wifi_capture_peek_payload_t payload;
        memcpy(&query,request->payload,sizeof(query));
        if(cerberus_wifi_capture_service_peek(query.generation,&payload)==ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_PEEK,
                request->sequence,&payload,sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ: {
        if(request->payload_length!=sizeof(cerberus_wifi_capture_read_request_t))return;
        cerberus_wifi_capture_read_request_t query;
        cerberus_wifi_capture_read_payload_t payload;
        memcpy(&query,request->payload,sizeof(query));
        if(cerberus_wifi_capture_service_read(&query,&payload)==ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_READ,
                request->sequence,&payload,sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CAPTURE_ACK: {
        if(request->payload_length!=sizeof(cerberus_wifi_capture_ack_request_t))return;
        cerberus_wifi_capture_ack_request_t query;
        memcpy(&query,request->payload,sizeof(query));
        int32_t error=cerberus_wifi_capture_service_ack(&query);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CAPTURE_ACK,
            request->sequence,&error,sizeof(error));
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
    case CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_START:
    case CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STOP:
    case CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STATUS: {
        esp_err_t action_error = ESP_OK;
        if (request->command == CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_START) {
            if (request->payload_length != sizeof(cerberus_wifi_channel_survey_start_request_t)) return;
            cerberus_wifi_channel_survey_start_request_t query;
            memcpy(&query, request->payload, sizeof(query));
            action_error = cerberus_wifi_channel_survey_start(query.band);
        } else if (request->command == CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_STOP) {
            if (request->payload_length != 0U) return;
            action_error = cerberus_wifi_channel_survey_preempt();
        } else if (request->payload_length != 0U) return;
        cerberus_wifi_channel_survey_status_payload_t payload;
        cerberus_wifi_channel_survey_get_status(&payload);
        if (action_error != ESP_OK) {
            payload.state = CERBERUS_WIFI_CHANNEL_SURVEY_ERROR;
            payload.error_code = action_error;
        }
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_RESULT: {
        if (request->payload_length != sizeof(cerberus_wifi_channel_survey_result_request_t)) return;
        cerberus_wifi_channel_survey_result_request_t query;
        cerberus_wifi_channel_survey_result_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_channel_survey_get_result(query.generation,
            query.index, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_CHANNEL_SURVEY_RESULT,
                request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_START:
    case CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STOP:
    case CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STATUS: {
        esp_err_t action_error = ESP_OK;
        if (request->command == CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_START) {
            if (request->payload_length !=
                sizeof(cerberus_wifi_packet_monitor_start_request_t)) return;
            cerberus_wifi_packet_monitor_start_request_t query;
            memcpy(&query, request->payload, sizeof(query));
            action_error = cerberus_wifi_packet_monitor_start(query.channel);
        } else if (request->command == CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_STOP) {
            if (request->payload_length !=
                sizeof(cerberus_wifi_packet_monitor_stop_request_t)) return;
            cerberus_wifi_packet_monitor_stop_request_t query;
            memcpy(&query, request->payload, sizeof(query));
            action_error = cerberus_wifi_packet_monitor_stop(query.generation);
        } else if (request->payload_length != 0U) return;
        cerberus_wifi_packet_monitor_status_payload_t payload;
        cerberus_wifi_packet_monitor_get_status(&payload);
        if (action_error != ESP_OK) {
            payload.state = CERBERUS_WIFI_PACKET_MONITOR_ERROR;
            payload.error_code = action_error;
        }
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_SAMPLE: {
        if (request->payload_length !=
            sizeof(cerberus_wifi_packet_monitor_sample_request_t)) return;
        cerberus_wifi_packet_monitor_sample_request_t query;
        cerberus_wifi_packet_monitor_sample_payload_t sample;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_packet_monitor_get_sample(query.generation,
                query.sequence, &sample) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_PACKET_MONITOR_SAMPLE,
                request->sequence, &sample, sizeof(sample));
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
    case CERBERUS_SPI_COMMAND_WIFI_AP_VALIDATE: {
        if(request->payload_length!=sizeof(cerberus_wifi_ap_job_t))return;
        cerberus_wifi_ap_job_t query;memcpy(&query,request->payload,sizeof(query));
        int32_t error=current_radio_state()!=0?ESP_ERR_INVALID_STATE:cerberus_wifi_ap_target_valid(&query);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_AP_VALIDATE,request->sequence,&error,sizeof(error));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_AP_STATION_BEGIN: {
        if(request->payload_length!=sizeof(cerberus_wifi_ap_station_begin_t))return;
        cerberus_wifi_ap_station_begin_t query;memcpy(&query,request->payload,sizeof(query));
        int32_t error=cerberus_c5_ap_stage_begin(&query,current_radio_state()!=0);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_AP_STATION_BEGIN,request->sequence,&error,sizeof(error));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_AP_STATION_CHUNK: {
        if(request->payload_length!=sizeof(cerberus_wifi_ap_station_chunk_t))return;
        cerberus_wifi_ap_station_chunk_t query;memcpy(&query,request->payload,sizeof(query));
        int32_t error=cerberus_c5_ap_stage_chunk(&query);
        prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_AP_STATION_CHUNK,request->sequence,&error,sizeof(error));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_AP_START:
    case CERBERUS_SPI_COMMAND_RADIO_TEST_BEGIN:
    case CERBERUS_SPI_COMMAND_RADIO_TEST_CHUNK:
    case CERBERUS_SPI_COMMAND_RADIO_TEST_START:
    case CERBERUS_SPI_COMMAND_RADIO_TEST_STATUS:
    case CERBERUS_SPI_COMMAND_RADIO_TEST_STOP: {
        esp_err_t err=ESP_OK;
        uint16_t id=0;
        if(request->command==CERBERUS_SPI_COMMAND_WIFI_AP_START){
            if(request->payload_length!=sizeof(cerberus_wifi_ap_job_t))return;
            cerberus_wifi_ap_job_t query;memcpy(&query,request->payload,sizeof(query));id=query.job_id;
            err=cerberus_wifi_channel_survey_preempt();
            if(err==ESP_OK)
                err=cerberus_c5_ap_start(&query,(current_radio_state()&~CERBERUS_C5_RADIO_TEST)!=0);
        } else if(request->command==CERBERUS_SPI_COMMAND_RADIO_TEST_BEGIN){
            if(request->payload_length!=sizeof(cerberus_radio_test_begin_t))return;
            cerberus_radio_test_begin_t query;memcpy(&query,request->payload,sizeof(query));
            id=query.job_id;err=cerberus_c5_test_begin(&query);
        } else if(request->command==CERBERUS_SPI_COMMAND_RADIO_TEST_CHUNK){
            if(request->payload_length!=sizeof(cerberus_radio_test_chunk_t))return;
            cerberus_radio_test_chunk_t query;memcpy(&query,request->payload,sizeof(query));
            id=query.job_id;err=cerberus_c5_test_chunk(&query);
        } else {
            if(request->payload_length!=sizeof(id))return;
            memcpy(&id,request->payload,sizeof(id));
            if(request->command==CERBERUS_SPI_COMMAND_RADIO_TEST_START){
                err=cerberus_wifi_channel_survey_preempt();
                cerberus_wifi_connection_status_payload_t link;
                cerberus_wifi_connection_get_status(&link);
                bool busy=(current_radio_state() & ~CERBERUS_C5_RADIO_TEST)!=0 ||
                    (link.state!=CERBERUS_WIFI_CONNECTION_IDLE && link.state!=CERBERUS_WIFI_CONNECTION_ERROR);
                if(err==ESP_OK)err=cerberus_c5_test_start(id,busy);
            } else if(request->command==CERBERUS_SPI_COMMAND_RADIO_TEST_STOP)err=cerberus_c5_test_stop(id);
        }
        cerberus_radio_test_status_t payload;
        cerberus_c5_test_status(request->command==CERBERUS_SPI_COMMAND_RADIO_TEST_STATUS?id:0,&payload);
        if(err!=ESP_OK){
            if(request->command==CERBERUS_SPI_COMMAND_WIFI_AP_START)
                payload=(cerberus_radio_test_status_t){.job_id=id,.state=CERBERUS_TEST_ERROR,.mode=CERBERUS_TEST_WIFI_AP};
            payload.error_code=err;
        }
        prepare_frame_response((cerberus_spi_command_t)request->command,request->sequence,&payload,sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_STATION_START:
    case CERBERUS_SPI_COMMAND_WIFI_STATION_STATUS:
    case CERBERUS_SPI_COMMAND_WIFI_STATION_STOP: {
        esp_err_t error=ESP_OK;uint16_t id=0;
        if(request->command==CERBERUS_SPI_COMMAND_WIFI_STATION_START){
            if(request->payload_length!=sizeof(cerberus_wifi_station_request_t))return;
            cerberus_wifi_station_request_t r;memcpy(&r,request->payload,sizeof(r));id=r.job_id;
            error=cerberus_wifi_channel_survey_preempt();
            if(error==ESP_OK)error=cerberus_wifi_station_start(&r);
        }else{
            if(request->payload_length!=sizeof(id))return;
            memcpy(&id,request->payload,sizeof(id));
            if(request->command==CERBERUS_SPI_COMMAND_WIFI_STATION_STOP)error=cerberus_wifi_station_stop(id);
        }
        cerberus_wifi_station_status_t payload;
        cerberus_wifi_station_status(request->command==CERBERUS_SPI_COMMAND_WIFI_STATION_STATUS?id:0,&payload);
        if(error!=ESP_OK)payload=(cerberus_wifi_station_status_t){.job_id=id,.state=CERBERUS_STATION_ERROR,.error_code=error};
        prepare_frame_response((cerberus_spi_command_t)request->command,request->sequence,&payload,sizeof(payload));break;
    }
    case CERBERUS_SPI_COMMAND_WIFI_STATION_RESULT: {
        if(request->payload_length!=sizeof(cerberus_wifi_station_query_t))return;
        cerberus_wifi_station_query_t query;cerberus_wifi_station_result_t result;
        memcpy(&query,request->payload,sizeof(query));
        if(cerberus_wifi_station_result(&query,&result)==ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_WIFI_STATION_RESULT,request->sequence,&result,sizeof(result));
        break;
    }
    case CERBERUS_SPI_COMMAND_NET_SCAN_START:
    case CERBERUS_SPI_COMMAND_NET_SCAN_STATUS:
    case CERBERUS_SPI_COMMAND_NET_SCAN_STOP: {
        esp_err_t err = ESP_OK;
        if (request->command == CERBERUS_SPI_COMMAND_NET_SCAN_START) {
            if (request->payload_length != sizeof(cerberus_net_scan_request_t)) return;
            cerberus_net_scan_request_t query;
            memcpy(&query, request->payload, sizeof(query));
            err = cerberus_network_scanner_start(&query);
        } else if (request->command == CERBERUS_SPI_COMMAND_NET_SCAN_STOP) {
            if (request->payload_length != sizeof(uint16_t)) return;
            uint16_t job_id;
            memcpy(&job_id, request->payload, sizeof(job_id));
            err = cerberus_network_scanner_stop(job_id);
        } else if (request->payload_length != 0) return;
        cerberus_net_scan_status_t payload;
        cerberus_network_scanner_status(&payload);
        if (err != ESP_OK) payload.error_code = err;
        prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_NET_SCAN_RESULT: {
        if (request->payload_length != sizeof(cerberus_net_scan_query_t)) return;
        cerberus_net_scan_query_t query;
        cerberus_net_scan_result_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_network_scanner_result(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_NET_SCAN_RESULT,
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
    case CERBERUS_SPI_COMMAND_WATCH_SCAN_TIMING: {
        if (request->payload_length != sizeof(cerberus_watch_scan_timing_request_t)) return;
        cerberus_watch_scan_timing_request_t query;
        cerberus_watch_scan_timing_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        esp_err_t err = query.radio == CERBERUS_WATCH_RADIO_BLE ?
            cerberus_ble_watch_get_timing(&query, &payload) : query.radio == CERBERUS_WATCH_RADIO_WIFI ?
            cerberus_wifi_watch_get_timing(&query, &payload) : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) prepare_frame_response(CERBERUS_SPI_COMMAND_WATCH_SCAN_TIMING,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION:
    case CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING: {
        if (request->payload_length != sizeof(cerberus_watch_scan_data_request_t)) return;
        cerberus_watch_scan_data_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        if (query.chunk) return;
        bool timing = request->command == CERBERUS_SPI_COMMAND_WATCH_SCAN_MOTION_TIMING;
        cerberus_watch_scan_motion_payload_t motion_payload;
        cerberus_watch_scan_timing_payload_t timing_payload;
        esp_err_t err = query.radio == CERBERUS_WATCH_RADIO_BLE ?
            (timing ? cerberus_ble_watch_get_motion_timing(&query, &timing_payload) :
                cerberus_ble_watch_get_motion(&query, &motion_payload)) :
            query.radio == CERBERUS_WATCH_RADIO_WIFI ?
            (timing ? cerberus_wifi_watch_get_motion_timing(&query, &timing_payload) :
                cerberus_wifi_watch_get_motion(&query, &motion_payload)) : ESP_ERR_INVALID_ARG;
        if (err == ESP_OK) prepare_frame_response((cerberus_spi_command_t)request->command,
            request->sequence, timing ? (const void *)&timing_payload : (const void *)&motion_payload,
            CERBERUS_SPI_FRAME_PAYLOAD_SIZE);
        break;
    }
    case CERBERUS_SPI_COMMAND_REMOTE_ID_START: {
        if (request->payload_length != sizeof(cerberus_remote_id_start_request_t)) return;
        cerberus_remote_id_start_request_t query;
        cerberus_remote_id_status_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        cerberus_wifi_remote_id_start(&query, &payload);
        prepare_frame_response(CERBERUS_SPI_COMMAND_REMOTE_ID_START,
            request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS:
    case CERBERUS_SPI_COMMAND_REMOTE_ID_STOP:
    case CERBERUS_SPI_COMMAND_REMOTE_ID_RENEW: {
        if (request->payload_length != sizeof(cerberus_remote_id_job_request_t)) return;
        cerberus_remote_id_job_request_t query;
        cerberus_remote_id_status_payload_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (request->command == CERBERUS_SPI_COMMAND_REMOTE_ID_STATUS)
            cerberus_wifi_remote_id_status(query.job_id, &payload);
        else if (request->command == CERBERUS_SPI_COMMAND_REMOTE_ID_STOP)
            cerberus_wifi_remote_id_stop(query.job_id, &payload);
        else cerberus_wifi_remote_id_renew(query.job_id, &payload);
        prepare_frame_response((cerberus_spi_command_t)request->command,
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
    case CERBERUS_SPI_COMMAND_MAC_START: {
        if (request->payload_length != sizeof(cerberus_mac_start_request_t)) return;
        cerberus_mac_start_request_t query;
        cerberus_mac_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_mac_start(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_MAC_START, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MAC_STATUS: {
        if (request->payload_length != sizeof(cerberus_mac_status_request_t)) return;
        cerberus_mac_status_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        union { cerberus_mac_status_t status; cerberus_mac_info_t info;
            cerberus_mac_counters_t counters; cerberus_mac_profile_t profile; } payload;
        esp_err_t error = ESP_ERR_INVALID_ARG;
        if (query.page == CERBERUS_MAC_PAGE_CORE) error = cerberus_wifi_mac_get_status(&query.binding, &payload.status);
        else if (query.page == CERBERUS_MAC_PAGE_COUNTERS) error = cerberus_wifi_mac_get_counters(&query.binding, &payload.counters);
        else if (query.page == CERBERUS_MAC_PAGE_PROFILE) error = cerberus_wifi_mac_get_profile(&query.binding, &payload.profile);
        else if (query.page == CERBERUS_MAC_PAGE_INFO && !query.binding.boot_id && !query.binding.job_id && !query.binding.epoch)
            error = cerberus_wifi_mac_get_info(&payload.info);
        if (error == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_MAC_STATUS, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MAC_STOP:
    case CERBERUS_SPI_COMMAND_MAC_PEEK: {
        if (request->payload_length != sizeof(cerberus_mac_binding_t)) return;
        cerberus_mac_binding_t query;
        memcpy(&query, request->payload, sizeof(query));
        union { cerberus_mac_status_t status; cerberus_mac_peek_t peek; } payload;
        esp_err_t error = request->command == CERBERUS_SPI_COMMAND_MAC_STOP ?
            cerberus_wifi_mac_stop(&query, &payload.status) : cerberus_wifi_mac_peek(&query, &payload.peek);
        if (error == ESP_OK)
            prepare_frame_response((cerberus_spi_command_t)request->command, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MAC_RENEW: {
        if (request->payload_length != sizeof(cerberus_mac_renew_request_t)) return;
        cerberus_mac_renew_request_t query;
        cerberus_mac_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_mac_renew(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_MAC_RENEW, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MAC_READ: {
        if (request->payload_length != sizeof(cerberus_mac_read_request_t)) return;
        cerberus_mac_read_request_t query;
        cerberus_mac_chunk_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_mac_read(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_MAC_READ, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_MAC_ACK: {
        if (request->payload_length != sizeof(cerberus_mac_ack_request_t)) return;
        cerberus_mac_ack_request_t query;
        cerberus_mac_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_mac_ack(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_MAC_ACK, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_START: {
        if (request->payload_length != sizeof(cerberus_csi_start_request_t)) return;
        cerberus_csi_start_request_t query;
        cerberus_csi_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_csi_start(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_CSI_START, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_STATUS: {
        if (request->payload_length != sizeof(cerberus_csi_status_request_t)) return;
        cerberus_csi_status_request_t query;
        memcpy(&query, request->payload, sizeof(query));
        cerberus_csi_binding_t binding = {.boot_id=query.boot_id, .job_id=query.job_id, .epoch=query.epoch};
        union {
            cerberus_csi_status_t status;
            cerberus_csi_clock_t clock;
            cerberus_csi_counters_t counters;
            cerberus_csi_info_t info;
            cerberus_csi_target_payload_t target;
        } payload;
        esp_err_t error = ESP_ERR_INVALID_ARG;
        if (query.page == CERBERUS_CSI_PAGE_STATUS) error = cerberus_wifi_csi_get_status(&binding, &payload.status);
        else if (query.page == CERBERUS_CSI_PAGE_CLOCK) error = cerberus_wifi_csi_get_clock(&binding, &payload.clock);
        else if (query.page == CERBERUS_CSI_PAGE_COUNTERS) error = cerberus_wifi_csi_get_counters(&binding, &payload.counters);
        else if (query.page == CERBERUS_CSI_PAGE_TARGET) error = cerberus_wifi_csi_get_target(&binding, &payload.target);
        else if (query.page == CERBERUS_CSI_PAGE_INFO && !query.boot_id && !query.job_id && !query.epoch)
            error = cerberus_wifi_csi_get_info(&payload.info);
        if (error == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_CSI_STATUS, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_STOP:
    case CERBERUS_SPI_COMMAND_CSI_PEEK: {
        if (request->payload_length != sizeof(cerberus_csi_binding_t)) return;
        cerberus_csi_binding_t query;
        memcpy(&query, request->payload, sizeof(query));
        union { cerberus_csi_status_t status; cerberus_csi_peek_t peek; } payload;
        esp_err_t error = request->command == CERBERUS_SPI_COMMAND_CSI_STOP ?
            cerberus_wifi_csi_stop(&query, &payload.status) : cerberus_wifi_csi_peek(&query, &payload.peek);
        if (error == ESP_OK)
            prepare_frame_response((cerberus_spi_command_t)request->command, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_RENEW: {
        if (request->payload_length != sizeof(cerberus_csi_renew_request_t)) return;
        cerberus_csi_renew_request_t query;
        cerberus_csi_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_csi_renew(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_CSI_RENEW, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_READ: {
        if (request->payload_length != sizeof(cerberus_csi_read_request_t)) return;
        cerberus_csi_read_request_t query;
        cerberus_csi_chunk_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_csi_read(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_CSI_READ, request->sequence, &payload, sizeof(payload));
        break;
    }
    case CERBERUS_SPI_COMMAND_CSI_ACK: {
        if (request->payload_length != sizeof(cerberus_csi_ack_request_t)) return;
        cerberus_csi_ack_request_t query;
        cerberus_csi_status_t payload;
        memcpy(&query, request->payload, sizeof(query));
        if (cerberus_wifi_csi_ack(&query, &payload) == ESP_OK)
            prepare_frame_response(CERBERUS_SPI_COMMAND_CSI_ACK, request->sequence, &payload, sizeof(payload));
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
    esp_err_t radio_test_err=cerberus_c5_test_init();
    if(radio_test_err!=ESP_OK)ESP_LOGW(TAG,"radio test service unavailable: %s",esp_err_to_name(radio_test_err));
    esp_err_t wifi_err = cerberus_wifi_scanner_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi scanner unavailable: %s", esp_err_to_name(wifi_err));
    }
    esp_err_t network_diagnostics_err = cerberus_network_diagnostics_init();
    if (network_diagnostics_err != ESP_OK) {
        ESP_LOGW(TAG, "network diagnostics unavailable: %s",
            esp_err_to_name(network_diagnostics_err));
    }
    esp_err_t network_scanner_err = cerberus_network_scanner_init();
    if (network_scanner_err != ESP_OK)
        ESP_LOGW(TAG, "network scanner unavailable: %s", esp_err_to_name(network_scanner_err));
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
