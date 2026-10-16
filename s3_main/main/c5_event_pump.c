#include "c5_event_pump.h"

#include "c5_spi_link.h"
#include "event_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#define C5_EVENT_PUMP_INTERVAL_MS 500U
#define C5_EVENT_PUMP_FAST_INTERVAL_MS 50U
#define C5_EVENT_PUMP_RETRY_MS 1500U
#define C5_EVENT_PUMP_MAX_DRAIN 8U
#define C5_EVENT_PUMP_STACK_BYTES 4096U
#define C5_EVENT_PUMP_PRIORITY 2U

static TaskHandle_t pump_task_handle;
static cerberus_c5_event_pump_status_t pump_status;
static portMUX_TYPE pump_status_lock = portMUX_INITIALIZER_UNLOCKED;

static cerberus_event_type_t local_event_type(uint8_t source)
{
    switch ((cerberus_c5_event_source_t)source) {
    case CERBERUS_C5_EVENT_SOURCE_WIFI_SCAN:
        return CERBERUS_EVENT_C5_WIFI_STATE;
    case CERBERUS_C5_EVENT_SOURCE_BLE_SCAN:
    case CERBERUS_C5_EVENT_SOURCE_BLE_CONNECTION:
    case CERBERUS_C5_EVENT_SOURCE_BLE_GATT_SERVICE:
    case CERBERUS_C5_EVENT_SOURCE_BLE_GATT_CHARACTERISTIC:
    case CERBERUS_C5_EVENT_SOURCE_BLE_GATT_READ:
        return CERBERUS_EVENT_C5_BLE_STATE;
    case CERBERUS_C5_EVENT_SOURCE_IEEE802154_ENERGY:
    case CERBERUS_C5_EVENT_SOURCE_IEEE802154_BEACON:
    case CERBERUS_C5_EVENT_SOURCE_IEEE802154_ACTIVITY:
        return CERBERUS_EVENT_C5_IEEE802154_STATE;
    default:
        return 0;
    }
}

static int32_t pack_event_value(const cerberus_c5_event_payload_t *event)
{
    uint32_t value = ((uint32_t)event->source << 24U) |
        ((uint32_t)event->state << 16U) |
        ((uint32_t)event->generation << 8U) |
        (event->error_code != 0 ? 1U : 0U);
    return (int32_t)value;
}

static int32_t pack_backpressure_value(
    const cerberus_c5_event_queue_status_payload_t *remote)
{
    uint32_t value = ((remote->flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ACTIVE) != 0U
            ? 0x80000000UL
            : 0U) |
        ((uint32_t)(remote->pending & 0x7FU) << 16U) |
        remote->deferred;
    return (int32_t)value;
}

static void set_link_state(
    bool online,
    bool supported,
    bool flow_supported,
    bool flow_configured,
    bool batch_supported)
{
    portENTER_CRITICAL(&pump_status_lock);
    pump_status.c5_online = online;
    pump_status.supported = supported;
    pump_status.flow_supported = flow_supported;
    pump_status.flow_configured = flow_configured;
    pump_status.batch_supported = batch_supported;
    portEXIT_CRITICAL(&pump_status_lock);
}

static void process_event(const cerberus_c5_event_payload_t *event)
{
    if (event == NULL || event->source == CERBERUS_C5_EVENT_SOURCE_NONE) {
        return;
    }

    portENTER_CRITICAL(&pump_status_lock);
    ++pump_status.received_events;
    pump_status.last_event = *event;
    portEXIT_CRITICAL(&pump_status_lock);

    cerberus_event_type_t type = local_event_type(event->source);
    if (type != 0 &&
        cerberus_event_bus_publish(type, pack_event_value(event)) != ESP_OK) {
        portENTER_CRITICAL(&pump_status_lock);
        ++pump_status.local_publish_failures;
        portEXIT_CRITICAL(&pump_status_lock);
    }
}

static void event_pump_task(void *argument)
{
    (void)argument;
    uint16_t reported_remote_drops = 0U;
    uint16_t reported_deferred = 0U;
    bool reported_backpressure = false;
    bool backpressure_report_valid = false;
    bool flow_configured = false;
    uint32_t last_c5_uptime_ms = 0U;

    while (true) {
        cerberus_c5_status_t c5_status;
        esp_err_t err = c5_spi_link_get_status(&c5_status);
        if (err != ESP_OK) {
            flow_configured = false;
            last_c5_uptime_ms = 0U;
            backpressure_report_valid = false;
            set_link_state(false, false, false, false, false);
            vTaskDelay(pdMS_TO_TICKS(C5_EVENT_PUMP_RETRY_MS));
            continue;
        }

        bool supported = (c5_status.capabilities & CERBERUS_C5_CAP_EVENT_QUEUE) != 0U;
        bool flow_supported =
            (c5_status.capabilities & CERBERUS_C5_CAP_EVENT_FLOW_CONTROL) != 0U;
        bool batch_supported =
            (c5_status.capabilities & CERBERUS_C5_CAP_EVENT_BATCH) != 0U;
        if (last_c5_uptime_ms != 0U && c5_status.uptime_ms < last_c5_uptime_ms) {
            flow_configured = false;
            reported_remote_drops = 0U;
            reported_deferred = 0U;
            backpressure_report_valid = false;
        }
        last_c5_uptime_ms = c5_status.uptime_ms;

        if (!flow_supported) {
            flow_configured = false;
        } else if (!flow_configured) {
            cerberus_c5_event_queue_status_payload_t configured_status;
            err = c5_spi_link_configure_event_flow_control(
                CERBERUS_C5_EVENT_FLOW_DEFAULT_LOW_WATERMARK,
                CERBERUS_C5_EVENT_FLOW_DEFAULT_HIGH_WATERMARK,
                true,
                &configured_status);
            flow_configured = err == ESP_OK &&
                configured_status.low_watermark ==
                    CERBERUS_C5_EVENT_FLOW_DEFAULT_LOW_WATERMARK &&
                configured_status.high_watermark ==
                    CERBERUS_C5_EVENT_FLOW_DEFAULT_HIGH_WATERMARK &&
                (configured_status.flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ENABLED) != 0U;
        }

        set_link_state(true, supported, flow_supported, flow_configured, batch_supported);
        if (!supported) {
            vTaskDelay(pdMS_TO_TICKS(C5_EVENT_PUMP_RETRY_MS));
            continue;
        }

        cerberus_c5_event_queue_status_payload_t remote;
        err = c5_spi_link_get_event_queue_status(&remote);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(C5_EVENT_PUMP_INTERVAL_MS));
            continue;
        }

        portENTER_CRITICAL(&pump_status_lock);
        pump_status.remote = remote;
        portEXIT_CRITICAL(&pump_status_lock);

        if (remote.dropped != reported_remote_drops) {
            if (remote.dropped > reported_remote_drops) {
                (void)cerberus_event_bus_publish(
                    CERBERUS_EVENT_C5_EVENT_DROPPED,
                    (int32_t)remote.dropped);
            }
            reported_remote_drops = remote.dropped;
        }

        bool backpressure_active =
            (remote.flow_flags & CERBERUS_C5_EVENT_FLOW_FLAG_ACTIVE) != 0U;
        if (!backpressure_report_valid) {
            reported_backpressure = backpressure_active;
            reported_deferred = remote.deferred;
            backpressure_report_valid = true;
        } else if (backpressure_active != reported_backpressure ||
            remote.deferred != reported_deferred) {
            (void)cerberus_event_bus_publish(
                CERBERUS_EVENT_C5_BACKPRESSURE,
                pack_backpressure_value(&remote));
            reported_backpressure = backpressure_active;
            reported_deferred = remote.deferred;
        }

        uint8_t drain_limit = C5_EVENT_PUMP_MAX_DRAIN;
        if (backpressure_active || remote.pending >= remote.high_watermark) {
            drain_limit = remote.capacity;
        }
        uint8_t drain_count = remote.pending < drain_limit ? remote.pending : drain_limit;
        if (batch_supported) {
            uint8_t remaining = drain_count;
            while (remaining > 0U) {
                uint8_t requested = remaining < CERBERUS_C5_EVENT_BATCH_MAX_EVENTS
                    ? remaining
                    : CERBERUS_C5_EVENT_BATCH_MAX_EVENTS;
                cerberus_c5_event_batch_frame_t batch;
                err = c5_spi_link_pop_event_batch(requested, &batch);
                if (err != ESP_OK) {
                    portENTER_CRITICAL(&pump_status_lock);
                    ++pump_status.batch_failures;
                    portEXIT_CRITICAL(&pump_status_lock);
                    break;
                }

                portENTER_CRITICAL(&pump_status_lock);
                ++pump_status.batch_transfers;
                portEXIT_CRITICAL(&pump_status_lock);
                if (batch.event_count == 0U) {
                    break;
                }

                for (uint8_t index = 0U; index < batch.event_count; ++index) {
                    cerberus_c5_event_payload_t event;
                    memcpy(&event, &batch.events[index], sizeof(event));
                    process_event(&event);
                }
                remote.pending = batch.pending_after;
                remote.dropped = batch.dropped;
                remote.deferred = batch.deferred;
                remote.flow_flags = batch.flow_flags;
                remote.control_generation = batch.control_generation;
                remaining = batch.event_count < remaining
                    ? (uint8_t)(remaining - batch.event_count)
                    : 0U;
                if (batch.event_count < requested) {
                    break;
                }
            }
        } else {
            for (uint8_t index = 0U; index < drain_count; ++index) {
                cerberus_c5_event_payload_t event;
                err = c5_spi_link_pop_event(&event);
                if (err != ESP_OK || event.source == CERBERUS_C5_EVENT_SOURCE_NONE) {
                    break;
                }
                portENTER_CRITICAL(&pump_status_lock);
                ++pump_status.single_event_transfers;
                portEXIT_CRITICAL(&pump_status_lock);
                process_event(&event);
            }
        }

        portENTER_CRITICAL(&pump_status_lock);
        pump_status.remote = remote;
        portEXIT_CRITICAL(&pump_status_lock);

        uint32_t delay_ms = backpressure_active || remote.pending >= remote.high_watermark
            ? C5_EVENT_PUMP_FAST_INTERVAL_MS
            : C5_EVENT_PUMP_INTERVAL_MS;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
    }
}

esp_err_t cerberus_c5_event_pump_start(void)
{
    if (pump_task_handle != NULL) {
        return ESP_OK;
    }

    memset(&pump_status, 0, sizeof(pump_status));
    BaseType_t created = xTaskCreate(
        event_pump_task,
        "c5_event_pump",
        C5_EVENT_PUMP_STACK_BYTES,
        NULL,
        C5_EVENT_PUMP_PRIORITY,
        &pump_task_handle);
    if (created != pdPASS) {
        pump_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    portENTER_CRITICAL(&pump_status_lock);
    pump_status.started = true;
    portEXIT_CRITICAL(&pump_status_lock);
    return ESP_OK;
}

esp_err_t cerberus_c5_event_pump_get_status(cerberus_c5_event_pump_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&pump_status_lock);
    *status = pump_status;
    portEXIT_CRITICAL(&pump_status_lock);
    return status->started ? ESP_OK : ESP_ERR_INVALID_STATE;
}
