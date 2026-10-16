#include "event_queue.h"

#include "ble_scanner.h"
#include "ieee802154_scanner.h"
#include "wifi_scanner.h"

#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

#define C5_EVENT_MONITOR_INTERVAL_MS 100U
#define C5_EVENT_MONITOR_STACK_BYTES 4096U
#define C5_EVENT_MONITOR_PRIORITY 3U

typedef struct {
    bool valid;
    uint8_t state;
    uint8_t generation;
    int32_t error_code;
} observed_state_t;

static QueueHandle_t event_queue;
static TaskHandle_t monitor_task_handle;
static uint16_t next_event_sequence;
static uint16_t dropped_events;
static uint16_t deferred_events;
static uint32_t produced_events;
static uint32_t consumed_events;
static uint8_t low_watermark = CERBERUS_C5_EVENT_FLOW_DEFAULT_LOW_WATERMARK;
static uint8_t high_watermark = CERBERUS_C5_EVENT_FLOW_DEFAULT_HIGH_WATERMARK;
static uint8_t flow_flags = CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED;
static uint8_t control_generation;
static bool backpressure_active;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

static bool refresh_backpressure(uint8_t pending, bool count_deferral)
{
    bool active;
    portENTER_CRITICAL(&status_lock);
    bool enabled = (flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED) != 0U;
    if (!enabled) {
        backpressure_active = false;
    } else if (backpressure_active) {
        if (pending <= low_watermark) {
            backpressure_active = false;
        }
    } else if (pending >= high_watermark) {
        backpressure_active = true;
    }
    active = backpressure_active;
    if (active && count_deferral && deferred_events != UINT16_MAX) {
        ++deferred_events;
    }
    portEXIT_CRITICAL(&status_lock);
    return active;
}

static bool publish_event(
    cerberus_c5_event_source_t source,
    uint8_t state,
    uint8_t generation,
    int32_t error_code,
    uint32_t value)
{
    if (event_queue == NULL || source == CERBERUS_C5_EVENT_SOURCE_NONE) {
        return false;
    }

    uint8_t pending = (uint8_t)uxQueueMessagesWaiting(event_queue);
    if (refresh_backpressure(pending, true)) {
        return false;
    }

    cerberus_c5_event_payload_t event = {
        .source = (uint8_t)source,
        .state = state,
        .generation = generation,
        .uptime_ms = (uint32_t)(esp_timer_get_time() / 1000),
        .error_code = error_code,
        .value = value,
    };
    portENTER_CRITICAL(&status_lock);
    event.event_sequence = ++next_event_sequence;
    portEXIT_CRITICAL(&status_lock);

    if (xQueueSend(event_queue, &event, 0U) != pdTRUE) {
        portENTER_CRITICAL(&status_lock);
        if (dropped_events != UINT16_MAX) {
            ++dropped_events;
        }
        portEXIT_CRITICAL(&status_lock);
        return false;
    }

    portENTER_CRITICAL(&status_lock);
    ++produced_events;
    portEXIT_CRITICAL(&status_lock);
    (void)refresh_backpressure((uint8_t)uxQueueMessagesWaiting(event_queue), false);
    return true;
}

static void observe_state(
    observed_state_t *observed,
    cerberus_c5_event_source_t source,
    uint8_t state,
    uint8_t generation,
    int32_t error_code,
    uint32_t value)
{
    if (observed == NULL) {
        return;
    }
    if (!observed->valid) {
        observed->valid = true;
        observed->state = state;
        observed->generation = generation;
        observed->error_code = error_code;
        return;
    }
    if (observed->state == state && observed->generation == generation &&
        observed->error_code == error_code) {
        return;
    }

    if (publish_event(source, state, generation, error_code, value)) {
        observed->state = state;
        observed->generation = generation;
        observed->error_code = error_code;
    }
}

static void event_monitor_task(void *argument)
{
    (void)argument;
    observed_state_t wifi = {0};
    observed_state_t ble = {0};
    observed_state_t connection = {0};
    observed_state_t service = {0};
    observed_state_t characteristic = {0};
    observed_state_t read = {0};
    observed_state_t energy = {0};
    observed_state_t beacon = {0};
    observed_state_t activity = {0};

    while (true) {
        cerberus_wifi_scan_status_payload_t wifi_status;
        cerberus_ble_scan_status_payload_t ble_status;
        cerberus_ble_connection_status_payload_t connection_status;
        cerberus_ble_gatt_service_status_payload_t service_status;
        cerberus_ble_gatt_characteristic_status_payload_t characteristic_status;
        cerberus_ble_gatt_read_status_payload_t read_status;
        cerberus_ieee802154_energy_scan_status_payload_t energy_status;
        cerberus_ieee802154_beacon_scan_status_payload_t beacon_status;
        cerberus_ieee802154_activity_scan_status_payload_t activity_status;

        cerberus_wifi_scanner_get_status(&wifi_status);
        cerberus_ble_scanner_get_status(&ble_status);
        cerberus_ble_connection_get_status(&connection_status);
        cerberus_ble_gatt_service_get_status(&service_status);
        cerberus_ble_gatt_characteristic_get_status(&characteristic_status);
        cerberus_ble_gatt_read_get_status(&read_status);
        cerberus_ieee802154_scanner_get_status(&energy_status);
        cerberus_ieee802154_beacon_scanner_get_status(&beacon_status);
        cerberus_ieee802154_activity_scanner_get_status(&activity_status);

        observe_state(&wifi, CERBERUS_C5_EVENT_SOURCE_WIFI_SCAN,
            wifi_status.state, wifi_status.generation, wifi_status.error_code,
            wifi_status.total_count);
        observe_state(&ble, CERBERUS_C5_EVENT_SOURCE_BLE_SCAN,
            ble_status.state, ble_status.generation, ble_status.error_code,
            ble_status.total_count);
        observe_state(&connection, CERBERUS_C5_EVENT_SOURCE_BLE_CONNECTION,
            connection_status.state, connection_status.generation,
            connection_status.error_code, connection_status.connection_handle);
        observe_state(&service, CERBERUS_C5_EVENT_SOURCE_BLE_GATT_SERVICE,
            service_status.state, service_status.generation, service_status.error_code,
            service_status.total_count);
        observe_state(&characteristic, CERBERUS_C5_EVENT_SOURCE_BLE_GATT_CHARACTERISTIC,
            characteristic_status.state, characteristic_status.generation,
            characteristic_status.error_code, characteristic_status.total_count);
        observe_state(&read, CERBERUS_C5_EVENT_SOURCE_BLE_GATT_READ,
            read_status.state, read_status.generation, read_status.error_code,
            read_status.total_length);
        observe_state(&energy, CERBERUS_C5_EVENT_SOURCE_IEEE802154_ENERGY,
            energy_status.state, energy_status.generation, energy_status.error_code,
            energy_status.completed_count);
        observe_state(&beacon, CERBERUS_C5_EVENT_SOURCE_IEEE802154_BEACON,
            beacon_status.state, beacon_status.generation, beacon_status.error_code,
            beacon_status.beacon_count);
        observe_state(&activity, CERBERUS_C5_EVENT_SOURCE_IEEE802154_ACTIVITY,
            activity_status.state, activity_status.generation, activity_status.error_code,
            activity_status.total_frames);

        vTaskDelay(pdMS_TO_TICKS(C5_EVENT_MONITOR_INTERVAL_MS));
    }
}

esp_err_t cerberus_c5_event_queue_init(void)
{
    if (event_queue != NULL && monitor_task_handle != NULL) {
        return ESP_OK;
    }

    event_queue = xQueueCreate(CERBERUS_C5_EVENT_QUEUE_DEPTH, sizeof(cerberus_c5_event_payload_t));
    if (event_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    BaseType_t created = xTaskCreate(
        event_monitor_task,
        "c5_event_monitor",
        C5_EVENT_MONITOR_STACK_BYTES,
        NULL,
        C5_EVENT_MONITOR_PRIORITY,
        &monitor_task_handle);
    if (created != pdPASS) {
        vQueueDelete(event_queue);
        event_queue = NULL;
        monitor_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }

    (void)publish_event(
        CERBERUS_C5_EVENT_SOURCE_SYSTEM,
        1U,
        0U,
        0,
        CERBERUS_C5_CAPABILITIES);
    return ESP_OK;
}

void cerberus_c5_event_queue_get_status(cerberus_c5_event_queue_status_payload_t *status)
{
    if (status == NULL) {
        return;
    }
    memset(status, 0, sizeof(*status));
    status->capacity = CERBERUS_C5_EVENT_QUEUE_DEPTH;
    if (event_queue == NULL) {
        return;
    }

    status->pending = (uint8_t)uxQueueMessagesWaiting(event_queue);
    (void)refresh_backpressure(status->pending, false);
    portENTER_CRITICAL(&status_lock);
    status->dropped = dropped_events;
    status->produced = produced_events;
    status->consumed = consumed_events;
    status->deferred = deferred_events;
    status->low_watermark = low_watermark;
    status->high_watermark = high_watermark;
    status->flow_flags = flow_flags |
        (backpressure_active ? CERBERUS_C5_EVENT_FLOW_FLAG_ACTIVE : 0U);
    status->control_generation = control_generation;
    portEXIT_CRITICAL(&status_lock);
}

bool cerberus_c5_event_queue_pop(cerberus_c5_event_payload_t *event)
{
    return cerberus_c5_event_queue_pop_batch(event, 1U) == 1U;
}

uint8_t cerberus_c5_event_queue_pop_batch(
    cerberus_c5_event_payload_t *events,
    uint8_t max_events)
{
    if (events == NULL || event_queue == NULL || max_events == 0U ||
        max_events > CERBERUS_C5_EVENT_BATCH_MAX_EVENTS) {
        return 0U;
    }

    memset(events, 0, max_events * sizeof(events[0]));
    uint8_t received = 0U;
    while (received < max_events &&
        xQueueReceive(event_queue, &events[received], 0U) == pdTRUE) {
        ++received;
    }
    if (received == 0U) {
        return 0U;
    }
    portENTER_CRITICAL(&status_lock);
    consumed_events += received;
    portEXIT_CRITICAL(&status_lock);
    (void)refresh_backpressure((uint8_t)uxQueueMessagesWaiting(event_queue), false);
    return received;
}

esp_err_t cerberus_c5_event_queue_configure_flow_control(
    const cerberus_c5_event_flow_control_request_t *request,
    cerberus_c5_event_queue_status_payload_t *status)
{
    if (request == NULL || status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (request->reserved != 0U ||
        (request->flow_flags & ~CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED) != 0U ||
        request->low_watermark == 0U ||
        request->low_watermark >= request->high_watermark ||
        request->high_watermark > CERBERUS_C5_EVENT_QUEUE_DEPTH) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t pending = (uint8_t)uxQueueMessagesWaiting(event_queue);
    portENTER_CRITICAL(&status_lock);
    low_watermark = request->low_watermark;
    high_watermark = request->high_watermark;
    flow_flags = request->flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED;
    backpressure_active = (flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED) != 0U &&
        pending >= high_watermark;
    ++control_generation;
    portEXIT_CRITICAL(&status_lock);

    cerberus_c5_event_queue_get_status(status);
    return ESP_OK;
}
