#include "network_diagnostics.h"

#include "wifi_scanner.h"

#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_netif_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"

#include <string.h>
#include <time.h>

#define DIAGNOSTICS_TASK_STACK 9216U
#define DIAGNOSTICS_TASK_PRIORITY 3U

static SemaphoreHandle_t diagnostics_mutex;
static cerberus_network_diagnostics_status_payload_t diagnostics_status;
static TaskHandle_t diagnostics_task_handle;

typedef struct {
    SemaphoreHandle_t done;
    bool success;
    uint32_t elapsed_ms;
} ping_context_t;

static void remember_error(esp_err_t error)
{
    if (error == ESP_OK) return;
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    if (diagnostics_status.error_code == ESP_OK) diagnostics_status.error_code = error;
    xSemaphoreGive(diagnostics_mutex);
}

static void set_active(uint8_t test)
{
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    diagnostics_status.active_test = test;
    xSemaphoreGive(diagnostics_mutex);
}

static void finish_test(uint8_t test, bool passed)
{
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    diagnostics_status.completed_mask |= test;
    if (passed) diagnostics_status.passed_mask |= test;
    xSemaphoreGive(diagnostics_mutex);
}

static void ping_success(esp_ping_handle_t handle, void *argument)
{
    ping_context_t *context = argument;
    uint32_t elapsed = 0;
    if (context != NULL && esp_ping_get_profile(
            handle, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed)) == ESP_OK) {
        context->success = true;
        context->elapsed_ms = elapsed;
    }
}

static void ping_end(esp_ping_handle_t handle, void *argument)
{
    (void)handle;
    ping_context_t *context = argument;
    if (context != NULL && context->done != NULL) xSemaphoreGive(context->done);
}

static esp_err_t ping_gateway(const uint8_t gateway[4], uint16_t *elapsed_ms)
{
    if (gateway == NULL || elapsed_ms == NULL) return ESP_ERR_INVALID_ARG;
    ping_context_t context = { .done = xSemaphoreCreateBinary() };
    if (context.done == NULL) return ESP_ERR_NO_MEM;
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    config.count = 2;
    config.interval_ms = 150;
    config.timeout_ms = 1200;
    IP_ADDR4(&config.target_addr, gateway[0], gateway[1], gateway[2], gateway[3]);
    esp_ping_callbacks_t callbacks = {
        .cb_args = &context,
        .on_ping_success = ping_success,
        .on_ping_timeout = NULL,
        .on_ping_end = ping_end,
    };
    esp_ping_handle_t handle = NULL;
    esp_err_t err = esp_ping_new_session(&config, &callbacks, &handle);
    if (err == ESP_OK) err = esp_ping_start(handle);
    if (err == ESP_OK && xSemaphoreTake(context.done, pdMS_TO_TICKS(3500)) != pdTRUE) {
        err = ESP_ERR_TIMEOUT;
        (void)esp_ping_stop(handle);
    }
    if (handle != NULL) (void)esp_ping_delete_session(handle);
    vSemaphoreDelete(context.done);
    if (err == ESP_OK && !context.success) err = ESP_ERR_TIMEOUT;
    if (err == ESP_OK) *elapsed_ms = context.elapsed_ms > UINT16_MAX
        ? UINT16_MAX : (uint16_t)context.elapsed_ms;
    return err;
}

static esp_err_t resolve_test_host(uint8_t address[4])
{
    struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_STREAM };
    struct addrinfo *result = NULL;
    int rc = getaddrinfo("example.com", NULL, &hints, &result);
    if (rc != 0 || result == NULL) return ESP_ERR_NOT_FOUND;
    const struct sockaddr_in *ipv4 = (const struct sockaddr_in *)result->ai_addr;
    memcpy(address, &ipv4->sin_addr.s_addr, 4U);
    freeaddrinfo(result);
    return ESP_OK;
}

static esp_err_t run_ntp(uint32_t *epoch_utc)
{
    if (epoch_utc == NULL) return ESP_ERR_INVALID_ARG;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_err_t err = esp_netif_sntp_init(&config);
    bool initialized = err == ESP_OK;
    if (initialized) err = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(8000));
    time_t now = 0;
    time(&now);
    if (initialized) esp_netif_sntp_deinit();
    if (err == ESP_OK && now < 1700000000) err = ESP_ERR_INVALID_RESPONSE;
    if (err == ESP_OK) *epoch_utc = now > UINT32_MAX ? UINT32_MAX : (uint32_t)now;
    return err;
}

static esp_err_t run_https(uint16_t *status_code)
{
    if (status_code == NULL) return ESP_ERR_INVALID_ARG;
    esp_http_client_config_t config = {
        .url = "https://example.com/",
        .method = HTTP_METHOD_HEAD,
        .timeout_ms = 7000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .user_agent = "Cerberus-Base-Firmware/1",
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = esp_http_client_perform(client);
    int code = err == ESP_OK ? esp_http_client_get_status_code(client) : 0;
    esp_http_client_cleanup(client);
    if (code >= 100 && code <= UINT16_MAX) *status_code = (uint16_t)code;
    if (err == ESP_OK && (code < 200 || code >= 400)) err = ESP_ERR_INVALID_RESPONSE;
    return err;
}

static void diagnostics_task(void *argument)
{
    (void)argument;
    cerberus_wifi_connection_status_payload_t connection;
    cerberus_wifi_connection_get_status(&connection);
    if (connection.state != CERBERUS_WIFI_CONNECTION_CONNECTED) {
        remember_error(ESP_ERR_INVALID_STATE);
        goto complete;
    }

    set_active(CERBERUS_NETWORK_TEST_DNS);
    uint8_t resolved[4] = {0};
    esp_err_t err = resolve_test_host(resolved);
    if (err == ESP_OK) {
        xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
        memcpy(diagnostics_status.resolved_ipv4, resolved, sizeof(resolved));
        xSemaphoreGive(diagnostics_mutex);
    } else remember_error(err);
    finish_test(CERBERUS_NETWORK_TEST_DNS, err == ESP_OK);

    set_active(CERBERUS_NETWORK_TEST_ROUTE);
    bool route_valid = connection.gateway[0] != 0U || connection.gateway[1] != 0U ||
        connection.gateway[2] != 0U || connection.gateway[3] != 0U;
    if (!route_valid) remember_error(ESP_ERR_NOT_FOUND);
    finish_test(CERBERUS_NETWORK_TEST_ROUTE, route_valid);

    set_active(CERBERUS_NETWORK_TEST_GATEWAY_PING);
    uint16_t ping_ms = 0;
    err = route_valid ? ping_gateway(connection.gateway, &ping_ms) : ESP_ERR_INVALID_STATE;
    if (err == ESP_OK) {
        xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
        diagnostics_status.gateway_ping_ms = ping_ms;
        xSemaphoreGive(diagnostics_mutex);
    } else remember_error(err);
    finish_test(CERBERUS_NETWORK_TEST_GATEWAY_PING, err == ESP_OK);

    set_active(CERBERUS_NETWORK_TEST_NTP);
    uint32_t epoch = 0;
    err = run_ntp(&epoch);
    if (err == ESP_OK) {
        xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
        diagnostics_status.epoch_utc = epoch;
        xSemaphoreGive(diagnostics_mutex);
    } else remember_error(err);
    finish_test(CERBERUS_NETWORK_TEST_NTP, err == ESP_OK);

    set_active(CERBERUS_NETWORK_TEST_HTTPS);
    uint16_t http_status = 0;
    err = run_https(&http_status);
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    diagnostics_status.https_status = http_status;
    xSemaphoreGive(diagnostics_mutex);
    if (err != ESP_OK) remember_error(err);
    finish_test(CERBERUS_NETWORK_TEST_HTTPS, err == ESP_OK);

complete:
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    diagnostics_status.active_test = 0;
    diagnostics_status.state = diagnostics_status.completed_mask == CERBERUS_NETWORK_TEST_ALL
        ? CERBERUS_NETWORK_DIAGNOSTICS_COMPLETE : CERBERUS_NETWORK_DIAGNOSTICS_ERROR;
    diagnostics_task_handle = NULL;
    xSemaphoreGive(diagnostics_mutex);
    vTaskDelete(NULL);
}

esp_err_t cerberus_network_diagnostics_init(void)
{
    diagnostics_mutex = xSemaphoreCreateMutex();
    return diagnostics_mutex == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

esp_err_t cerberus_network_diagnostics_start(void)
{
    if (diagnostics_mutex == NULL) return ESP_ERR_INVALID_STATE;
    cerberus_wifi_connection_status_payload_t connection;
    cerberus_wifi_connection_get_status(&connection);
    if (connection.state != CERBERUS_WIFI_CONNECTION_CONNECTED) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    if (diagnostics_task_handle != NULL) {
        xSemaphoreGive(diagnostics_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    memset(&diagnostics_status, 0, sizeof(diagnostics_status));
    diagnostics_status.state = CERBERUS_NETWORK_DIAGNOSTICS_RUNNING;
    BaseType_t created = xTaskCreate(diagnostics_task, "network_diag", DIAGNOSTICS_TASK_STACK,
        NULL, DIAGNOSTICS_TASK_PRIORITY, &diagnostics_task_handle);
    if (created != pdPASS) {
        diagnostics_task_handle = NULL;
        diagnostics_status.state = CERBERUS_NETWORK_DIAGNOSTICS_ERROR;
        diagnostics_status.error_code = ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(diagnostics_mutex);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void cerberus_network_diagnostics_get_status(
    cerberus_network_diagnostics_status_payload_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (diagnostics_mutex == NULL) {
        status->state = CERBERUS_NETWORK_DIAGNOSTICS_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(diagnostics_mutex, portMAX_DELAY);
    *status = diagnostics_status;
    xSemaphoreGive(diagnostics_mutex);
}
