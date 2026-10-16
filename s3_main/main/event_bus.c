#include "event_bus.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <stdbool.h>
#include <string.h>

#define CERBERUS_EVENT_TASK_STACK_BYTES 4096U
#define CERBERUS_EVENT_TASK_PRIORITY 3U

typedef struct {
    cerberus_event_handler_t handler;
    void *context;
} cerberus_event_subscriber_t;

static QueueHandle_t event_queue;
static SemaphoreHandle_t subscriber_mutex;
static TaskHandle_t event_task_handle;
static cerberus_event_subscriber_t subscribers[CERBERUS_EVENT_SUBSCRIBER_LIMIT];
static uint8_t subscriber_count;
static uint32_t publish_attempts;
static uint32_t dispatched_events;
static uint32_t dropped_events;
static portMUX_TYPE status_lock = portMUX_INITIALIZER_UNLOCKED;

static bool event_type_is_valid(cerberus_event_type_t type)
{
    return type >= CERBERUS_EVENT_SYSTEM_BOOT &&
        type <= CERBERUS_EVENT_C5_BACKPRESSURE;
}

static void event_dispatch_task(void *argument)
{
    (void)argument;
    while (true) {
        cerberus_event_t event;
        if (xQueueReceive(event_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        cerberus_event_subscriber_t active[CERBERUS_EVENT_SUBSCRIBER_LIMIT];
        uint8_t active_count = 0U;
        if (xSemaphoreTake(subscriber_mutex, portMAX_DELAY) == pdTRUE) {
            active_count = subscriber_count;
            memcpy(active, subscribers, active_count * sizeof(active[0]));
            xSemaphoreGive(subscriber_mutex);
        }

        for (uint8_t index = 0U; index < active_count; ++index) {
            active[index].handler(&event, active[index].context);
        }

        portENTER_CRITICAL(&status_lock);
        ++dispatched_events;
        portEXIT_CRITICAL(&status_lock);
    }
}

esp_err_t cerberus_event_bus_init(void)
{
    if (event_queue != NULL && subscriber_mutex != NULL && event_task_handle != NULL) {
        return ESP_OK;
    }

    subscriber_mutex = xSemaphoreCreateMutex();
    if (subscriber_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }
    event_queue = xQueueCreate(CERBERUS_EVENT_QUEUE_DEPTH, sizeof(cerberus_event_t));
    if (event_queue == NULL) {
        vSemaphoreDelete(subscriber_mutex);
        subscriber_mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    BaseType_t created = xTaskCreate(
        event_dispatch_task,
        "event_bus",
        CERBERUS_EVENT_TASK_STACK_BYTES,
        NULL,
        CERBERUS_EVENT_TASK_PRIORITY,
        &event_task_handle);
    if (created != pdPASS) {
        vQueueDelete(event_queue);
        vSemaphoreDelete(subscriber_mutex);
        event_queue = NULL;
        subscriber_mutex = NULL;
        event_task_handle = NULL;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cerberus_event_bus_subscribe(cerberus_event_handler_t handler, void *context)
{
    if (handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (subscriber_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(subscriber_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    for (uint8_t index = 0U; index < subscriber_count; ++index) {
        if (subscribers[index].handler == handler && subscribers[index].context == context) {
            xSemaphoreGive(subscriber_mutex);
            return ESP_OK;
        }
    }
    if (subscriber_count >= CERBERUS_EVENT_SUBSCRIBER_LIMIT) {
        xSemaphoreGive(subscriber_mutex);
        return ESP_ERR_NO_MEM;
    }

    subscribers[subscriber_count].handler = handler;
    subscribers[subscriber_count].context = context;
    ++subscriber_count;
    xSemaphoreGive(subscriber_mutex);
    return ESP_OK;
}

esp_err_t cerberus_event_bus_publish(cerberus_event_type_t type, int32_t value)
{
    if (!event_type_is_valid(type)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_queue == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cerberus_event_t event = {
        .type = type,
        .uptime_ms = (uint64_t)(esp_timer_get_time() / 1000),
        .value = value,
    };

    portENTER_CRITICAL(&status_lock);
    ++publish_attempts;
    portEXIT_CRITICAL(&status_lock);

    if (xQueueSend(event_queue, &event, 0U) != pdTRUE) {
        portENTER_CRITICAL(&status_lock);
        ++dropped_events;
        portEXIT_CRITICAL(&status_lock);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cerberus_event_bus_get_status(cerberus_event_bus_status_t *status)
{
    if (status == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (event_queue == NULL || subscriber_mutex == NULL) {
        memset(status, 0, sizeof(*status));
        return ESP_ERR_INVALID_STATE;
    }

    memset(status, 0, sizeof(*status));
    portENTER_CRITICAL(&status_lock);
    status->publish_attempts = publish_attempts;
    status->dispatched_events = dispatched_events;
    status->dropped_events = dropped_events;
    portEXIT_CRITICAL(&status_lock);
    status->pending_events = (uint32_t)uxQueueMessagesWaiting(event_queue);

    if (xSemaphoreTake(subscriber_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    status->subscriber_count = subscriber_count;
    xSemaphoreGive(subscriber_mutex);
    return ESP_OK;
}
