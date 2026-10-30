#include "wardrive_gps_uart.h"

#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <limits.h>
#include <string.h>

#define GPS_UART UART_NUM_2
#define GPS_RX_GPIO GPIO_NUM_1
#define GPS_RX_BUFFER_BYTES CERBERUS_WARDRIVE_GPS_RX_BUFFER_BYTES
#define GPS_TASK_STACK_BYTES CERBERUS_WARDRIVE_GPS_TASK_STACK_BYTES
#define GPS_READ_CHUNK_BYTES 256U
#define GPS_READ_WAIT_MS 100U
#define GPS_STOP_GRACE_MS 1000U

static SemaphoreHandle_t gps_lock;
static cerberus_wardrive_gps_core_t gps_core;
static bool uart_active, task_running, stop_requested;
static esp_err_t last_uart_error;

static uint64_t monotonic_ms(void)
{
    return (uint64_t)(esp_timer_get_time() / 1000LL);
}

static void gps_reader(void *unused)
{
    (void)unused;
    uint8_t bytes[GPS_READ_CHUNK_BYTES];
    for (;;) {
        xSemaphoreTake(gps_lock, portMAX_DELAY);
        bool stopping = stop_requested;
        xSemaphoreGive(gps_lock);
        if (stopping) break;
        int count = uart_read_bytes(GPS_UART, bytes, sizeof(bytes),
            pdMS_TO_TICKS(GPS_READ_WAIT_MS));
        if (count < 0) {
            xSemaphoreTake(gps_lock, portMAX_DELAY);
            last_uart_error = ESP_FAIL;
            xSemaphoreGive(gps_lock);
            break;
        }
        if (count > 0) {
            uint64_t received_ms = monotonic_ms();
            xSemaphoreTake(gps_lock, portMAX_DELAY);
            cerberus_wardrive_gps_core_feed(&gps_core, bytes,
                (size_t)count, received_ms);
            xSemaphoreGive(gps_lock);
        }
    }
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    task_running = false;
    xSemaphoreGive(gps_lock);
    vTaskDelete(NULL);
}

esp_err_t cerberus_wardrive_gps_uart_start(void)
{
    if (gps_lock == NULL) {
        gps_lock = xSemaphoreCreateMutex();
        if (gps_lock == NULL) return ESP_ERR_NO_MEM;
    }
    /* A timed-out previous Stop may have left the driver installed while its
     * reader was exiting. Reap it before attempting a new session. */
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    bool cleanup_needed = uart_active && stop_requested;
    xSemaphoreGive(gps_lock);
    if (cleanup_needed) {
        esp_err_t cleanup = cerberus_wardrive_gps_uart_stop();
        if (cleanup != ESP_OK) return cleanup;
    }
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    bool busy = uart_active || task_running;
    if (!busy) {
        cerberus_wardrive_gps_core_reset(&gps_core);
        stop_requested = false;
        last_uart_error = ESP_OK;
    }
    xSemaphoreGive(gps_lock);
    if (busy) return ESP_ERR_INVALID_STATE;

    esp_err_t error = uart_driver_install(GPS_UART, GPS_RX_BUFFER_BYTES,
        0, 0, NULL, 0);
    if (error != ESP_OK) return error;
    const uart_config_t config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    error = uart_param_config(GPS_UART, &config);
    if (error == ESP_OK)
        error = uart_set_pin(GPS_UART, UART_PIN_NO_CHANGE, GPS_RX_GPIO,
            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (error == ESP_OK) error = uart_flush_input(GPS_UART);
    if (error != ESP_OK) {
        (void)uart_driver_delete(GPS_UART);
        return error;
    }
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    uart_active = true;
    task_running = true;
    xSemaphoreGive(gps_lock);
    if (xTaskCreate(gps_reader, "wardrive_gps", GPS_TASK_STACK_BYTES,
            NULL, 3, NULL) != pdPASS) {
        xSemaphoreTake(gps_lock, portMAX_DELAY);
        task_running = false;
        uart_active = false;
        xSemaphoreGive(gps_lock);
        (void)uart_driver_delete(GPS_UART);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t cerberus_wardrive_gps_uart_stop(void)
{
    if (gps_lock == NULL) return ESP_OK;
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    bool active = uart_active;
    stop_requested = true;
    xSemaphoreGive(gps_lock);
    if (!active) return ESP_OK;
    uint64_t deadline = monotonic_ms() + GPS_STOP_GRACE_MS;
    for (;;) {
        xSemaphoreTake(gps_lock, portMAX_DELAY);
        bool running = task_running;
        xSemaphoreGive(gps_lock);
        if (!running) break;
        if (monotonic_ms() >= deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    esp_err_t error = uart_driver_delete(GPS_UART);
    if (error == ESP_OK) {
        xSemaphoreTake(gps_lock, portMAX_DELAY);
        uart_active = false;
        xSemaphoreGive(gps_lock);
    }
    return error;
}

void cerberus_wardrive_gps_uart_snapshot(
    cerberus_wardrive_gps_snapshot_t *out, esp_err_t *uart_error)
{
    if (out == NULL) return;
    if (gps_lock == NULL) {
        memset(out, 0, sizeof(*out));
        out->sentence_age_ms = UINT32_MAX;
        out->fix_age_ms = UINT32_MAX;
        if (uart_error != NULL) *uart_error = ESP_OK;
        return;
    }
    xSemaphoreTake(gps_lock, portMAX_DELAY);
    cerberus_wardrive_gps_core_snapshot(&gps_core, monotonic_ms(), out);
    if (stop_requested) {
        /* The receiver is no longer being serviced. Do not present the last
         * fix as current while Stop is draining the other radios. */
        out->state = CERBERUS_WARDRIVE_GPS_ABSENT;
        out->coordinates_valid = false;
        out->latitude = 0.0;
        out->longitude = 0.0;
        out->altitude_valid = false;
        out->altitude_m = 0.0f;
        out->utc_valid = false;
        out->utc_from_rmc = false;
        out->utc[0] = '\0';
    }
    if (uart_error != NULL) *uart_error = last_uart_error;
    xSemaphoreGive(gps_lock);
}
