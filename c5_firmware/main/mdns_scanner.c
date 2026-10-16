#include "mdns_scanner.h"

#include "wifi_scanner.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mdns.h"

#include <string.h>

#define MDNS_QUERY_TIMEOUT_MS 800U
#define MDNS_QUERY_MAX_RESULTS 4U
#define MDNS_TASK_STACK 6144U
#define MDNS_TASK_PRIORITY 3U

typedef struct {
    const char *service;
    const char *proto;
    uint8_t type;
    uint8_t protocol;
} mdns_query_definition_t;

static const mdns_query_definition_t query_definitions[] = {
    { "_http", "_tcp", CERBERUS_MDNS_SERVICE_HTTP, 0U },
    { "_https", "_tcp", CERBERUS_MDNS_SERVICE_HTTPS, 0U },
    { "_ssh", "_tcp", CERBERUS_MDNS_SERVICE_SSH, 0U },
    { "_workstation", "_tcp", CERBERUS_MDNS_SERVICE_WORKSTATION, 0U },
    { "_printer", "_tcp", CERBERUS_MDNS_SERVICE_PRINTER, 0U },
    { "_ipp", "_tcp", CERBERUS_MDNS_SERVICE_IPP, 0U },
    { "_airplay", "_tcp", CERBERUS_MDNS_SERVICE_AIRPLAY, 0U },
};

static SemaphoreHandle_t scanner_mutex;
static TaskHandle_t scanner_task_handle;
static cerberus_mdns_scan_status_payload_t scanner_status;
static cerberus_mdns_result_payload_t scanner_results[CERBERUS_MDNS_MAX_RESULTS];

static bool same_result(
    const cerberus_mdns_result_payload_t *left,
    const cerberus_mdns_result_payload_t *right)
{
    return left->service_type == right->service_type && left->port == right->port &&
        memcmp(left->ipv4, right->ipv4, sizeof(left->ipv4)) == 0 &&
        strncmp(left->hostname, right->hostname, CERBERUS_MDNS_HOSTNAME_LENGTH) == 0;
}

static void store_result(uint8_t type, uint8_t protocol, const mdns_result_t *source)
{
    if (source == NULL) return;
    cerberus_mdns_result_payload_t candidate = {
        .service_type = type,
        .protocol = protocol,
        .port = source->port,
    };
    const char *name = source->hostname != NULL ? source->hostname : source->instance_name;
    if (name != NULL) {
        strncpy(candidate.hostname, name, CERBERUS_MDNS_HOSTNAME_LENGTH - 1U);
    }
    for (const mdns_ip_addr_t *address = source->addr; address != NULL; address = address->next) {
        if (address->addr.type == ESP_IPADDR_TYPE_V4) {
            memcpy(candidate.ipv4, &address->addr.u_addr.ip4.addr, sizeof(candidate.ipv4));
            break;
        }
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    candidate.generation = scanner_status.generation;
    for (uint8_t index = 0; index < scanner_status.result_count; ++index) {
        if (same_result(&scanner_results[index], &candidate)) {
            xSemaphoreGive(scanner_mutex);
            return;
        }
    }
    if (scanner_status.result_count < CERBERUS_MDNS_MAX_RESULTS) {
        candidate.index = scanner_status.result_count;
        scanner_results[scanner_status.result_count++] = candidate;
    }
    xSemaphoreGive(scanner_mutex);
}

static void scanner_task(void *argument)
{
    (void)argument;
    for (size_t index = 0; index < sizeof(query_definitions) / sizeof(query_definitions[0]); ++index) {
        cerberus_wifi_connection_status_payload_t connection;
        cerberus_wifi_connection_get_status(&connection);
        if (connection.state != CERBERUS_WIFI_CONNECTION_CONNECTED) {
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            scanner_status.error_code = ESP_ERR_INVALID_STATE;
            scanner_status.state = CERBERUS_MDNS_SCAN_ERROR;
            xSemaphoreGive(scanner_mutex);
            goto complete;
        }

        mdns_result_t *results = NULL;
        esp_err_t err = mdns_query_ptr(query_definitions[index].service,
            query_definitions[index].proto, MDNS_QUERY_TIMEOUT_MS,
            MDNS_QUERY_MAX_RESULTS, &results);
        if (err == ESP_OK) {
            for (mdns_result_t *result = results; result != NULL; result = result->next) {
                store_result(query_definitions[index].type,
                    query_definitions[index].protocol, result);
            }
        } else if (err != ESP_ERR_NOT_FOUND) {
            xSemaphoreTake(scanner_mutex, portMAX_DELAY);
            if (scanner_status.error_code == ESP_OK) scanner_status.error_code = err;
            xSemaphoreGive(scanner_mutex);
        }
        if (results != NULL) mdns_query_results_free(results);
        xSemaphoreTake(scanner_mutex, portMAX_DELAY);
        scanner_status.queried_type_count++;
        xSemaphoreGive(scanner_mutex);
    }

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    scanner_status.state = CERBERUS_MDNS_SCAN_COMPLETE;
    xSemaphoreGive(scanner_mutex);

complete:
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    scanner_task_handle = NULL;
    xSemaphoreGive(scanner_mutex);
    vTaskDelete(NULL);
}

esp_err_t cerberus_mdns_scanner_init(void)
{
    scanner_mutex = xSemaphoreCreateMutex();
    if (scanner_mutex == NULL) return ESP_ERR_NO_MEM;
    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        vSemaphoreDelete(scanner_mutex);
        scanner_mutex = NULL;
    }
    return err;
}

esp_err_t cerberus_mdns_scanner_start(void)
{
    if (scanner_mutex == NULL) return ESP_ERR_INVALID_STATE;
    cerberus_wifi_connection_status_payload_t connection;
    cerberus_wifi_connection_get_status(&connection);
    if (connection.state != CERBERUS_WIFI_CONNECTION_CONNECTED) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (scanner_task_handle != NULL) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t generation = (uint8_t)(scanner_status.generation + 1U);
    memset(&scanner_status, 0, sizeof(scanner_status));
    memset(scanner_results, 0, sizeof(scanner_results));
    scanner_status.state = CERBERUS_MDNS_SCAN_RUNNING;
    scanner_status.generation = generation;
    BaseType_t created = xTaskCreate(scanner_task, "mdns_scan", MDNS_TASK_STACK,
        NULL, MDNS_TASK_PRIORITY, &scanner_task_handle);
    if (created != pdPASS) {
        scanner_task_handle = NULL;
        scanner_status.state = CERBERUS_MDNS_SCAN_ERROR;
        scanner_status.error_code = ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(scanner_mutex);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

void cerberus_mdns_scanner_get_status(cerberus_mdns_scan_status_payload_t *status)
{
    if (status == NULL) return;
    memset(status, 0, sizeof(*status));
    if (scanner_mutex == NULL) {
        status->state = CERBERUS_MDNS_SCAN_ERROR;
        status->error_code = ESP_ERR_INVALID_STATE;
        return;
    }
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    *status = scanner_status;
    xSemaphoreGive(scanner_mutex);
}

esp_err_t cerberus_mdns_scanner_get_result(
    uint8_t generation,
    uint8_t index,
    cerberus_mdns_result_payload_t *result)
{
    if (result == NULL || scanner_mutex == NULL) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scanner_mutex, portMAX_DELAY);
    if (generation != scanner_status.generation || index >= scanner_status.result_count) {
        xSemaphoreGive(scanner_mutex);
        return ESP_ERR_NOT_FOUND;
    }
    *result = scanner_results[index];
    xSemaphoreGive(scanner_mutex);
    return ESP_OK;
}
