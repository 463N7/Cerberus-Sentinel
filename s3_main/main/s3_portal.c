#include "s3_portal.h"

#include "s3_wifi_owner.h"
#include "storage.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

#include <stdarg.h>
#include <stdatomic.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PORTAL_IP "172.0.0.1"
#define PORTAL_TEMPLATE_MAX 2048U
#define PORTAL_REPORT_MAX 4096U
#define PORTAL_BODY_MAX 512U
#define PORTAL_DNS_PACKET_MAX 512U

typedef struct {
    uint8_t field_count;
    cerberus_portal_form_field_t fields[CERBERUS_PORTAL_FORM_FIELDS];
} portal_submission_t;

static const char *TAG = "s3_portal";
static const uint8_t portal_address[4] = {172U, 0U, 0U, 1U};
static portMUX_TYPE portal_lock = portMUX_INITIALIZER_UNLOCKED;
static atomic_bool stop_requested;
static cerberus_s3_portal_request_t active_request;
static cerberus_s3_portal_status_t portal_status;
static portal_submission_t submissions[CERBERUS_PORTAL_SUBMISSIONS];
static uint8_t submission_count;
static char html_template[PORTAL_TEMPLATE_MAX];
static size_t html_template_length;
static int dns_socket = -1;
static TaskHandle_t dns_task;
static SemaphoreHandle_t dns_done;
static httpd_handle_t portal_http;
static esp_netif_t *portal_netif;
static esp_event_handler_instance_t portal_wifi_handler;
static bool wifi_claimed, wifi_initialized, wifi_started, handler_registered;
static uint32_t started_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static uint32_t increment(uint32_t value) { return value == UINT32_MAX ? value : value + 1U; }

static bool valid_request(const cerberus_s3_portal_request_t *request)
{
    if (!request || !request->c5_preflight_passed || !request->c5_scan_generation ||
        request->source_ssid_length == 0U || request->source_ssid_length > 32U ||
        request->source_channel == 0U || request->source_channel > 196U ||
        request->local_channel == 0U || request->local_channel > 11U ||
        request->duration_seconds < 15U || request->duration_seconds > 600U ||
        (request->source_bssid[0] & 1U)) return false;
    uint8_t aggregate = 0U;
    for (size_t index = 0U; index < 6U; ++index) aggregate |= request->source_bssid[index];
    return aggregate != 0U;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t event, void *data)
{
    (void)arg; (void)base; (void)data;
    portENTER_CRITICAL(&portal_lock);
    if (event == WIFI_EVENT_AP_STACONNECTED) {
        portal_status.client_joins = increment(portal_status.client_joins);
        if (portal_status.connected_clients < 4U) ++portal_status.connected_clients;
    }
    else if (event == WIFI_EVENT_AP_STADISCONNECTED && portal_status.connected_clients)
        --portal_status.connected_clients;
    portEXIT_CRITICAL(&portal_lock);
}

static esp_err_t landing_get(httpd_req_t *request)
{
    portENTER_CRITICAL(&portal_lock);
    portal_status.http_requests = increment(portal_status.http_requests);
    portEXIT_CRITICAL(&portal_lock);
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (html_template_length)
        return httpd_resp_send(request, html_template, html_template_length);
    char name[193];
    if (!cerberus_portal_html_escape(active_request.source_ssid,
        active_request.source_ssid_length, name, sizeof(name)))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "invalid SSID");
    char page[768];
    int length = snprintf(page, sizeof(page),
        "<!doctype html><html><meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>%s</title><body><h1>%s</h1><p>Cerberus local portal test</p>"
        "<form action='/submit' method='post'><label>Test name <input name='name' maxlength='95'></label>"
        "<button type='submit'>Submit</button></form></body></html>", name, name);
    if (length < 0 || (size_t)length >= sizeof(page))
        return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "page too long");
    return httpd_resp_send(request, page, length);
}

static esp_err_t submit_post(httpd_req_t *request)
{
    portENTER_CRITICAL(&portal_lock);
    portal_status.http_requests = increment(portal_status.http_requests);
    portEXIT_CRITICAL(&portal_lock);
    if (request->content_len == 0U || request->content_len > PORTAL_BODY_MAX)
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "form size rejected");
    char body[PORTAL_BODY_MAX + 1U];
    size_t received = 0U;
    unsigned timeouts = 0U;
    while (received < request->content_len) {
        int count = httpd_req_recv(request, body + received, request->content_len - received);
        if (count == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 4U) continue;
        if (count <= 0) return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "form incomplete");
        received += (size_t)count;
    }
    body[received] = '\0';
    portal_submission_t parsed = {0};
    if (!cerberus_portal_form_decode(body, received, parsed.fields, &parsed.field_count)) {
        memset(body, 0, sizeof(body));
        memset(&parsed, 0, sizeof(parsed));
        return httpd_resp_send_err(request, HTTPD_400_BAD_REQUEST, "form rejected");
    }
    memset(body, 0, sizeof(body));
    bool accepted = false;
    portENTER_CRITICAL(&portal_lock);
    if (submission_count < CERBERUS_PORTAL_SUBMISSIONS) {
        submissions[submission_count++] = parsed;
        portal_status.form_submissions = increment(portal_status.form_submissions);
        accepted = true;
    } else portal_status.forms_dropped = increment(portal_status.forms_dropped);
    portEXIT_CRITICAL(&portal_lock);
    memset(&parsed, 0, sizeof(parsed));
    if (!accepted) return httpd_resp_send_err(request, HTTPD_500_INTERNAL_SERVER_ERROR, "session record full");
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    return httpd_resp_sendstr(request, "<!doctype html><html><body><p>Submitted to local portal.</p></body></html>");
}

static esp_err_t cleanup_resources(void)
{
    esp_err_t error;
    if (dns_socket >= 0) { close(dns_socket); dns_socket = -1; }
    if (portal_http) {
        error = httpd_stop(portal_http);
        if (error != ESP_OK) return error;
        portal_http = NULL;
    }
    if (wifi_started) {
        error = esp_wifi_stop();
        if (error != ESP_OK && error != ESP_ERR_WIFI_NOT_STARTED) return error;
        wifi_started = false;
    }
    if (handler_registered) {
        error = esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, portal_wifi_handler);
        if (error != ESP_OK) return error;
        handler_registered = false;
    }
    if (wifi_initialized) {
        error = esp_wifi_deinit();
        if (error != ESP_OK) return error;
        wifi_initialized = false;
    }
    if (portal_netif) {
        esp_netif_destroy_default_wifi(portal_netif);
        portal_netif = NULL;
    }
    if (wifi_claimed) {
        error = cerberus_s3_wifi_release(CERBERUS_S3_WIFI_PORTAL);
        if (error != ESP_OK) return error;
        wifi_claimed = false;
    }
    return ESP_OK;
}

static void dns_worker(void *unused)
{
    (void)unused;
    uint8_t query[PORTAL_DNS_PACKET_MAX], reply[PORTAL_DNS_PACKET_MAX];
    bool expired = false;
    esp_err_t runtime_error = ESP_OK;
    while (!atomic_load(&stop_requested)) {
        uint32_t elapsed = now_ms() - started_ms;
        if (elapsed >= (uint32_t)active_request.duration_seconds * 1000U) { expired = true; break; }
        struct sockaddr_in sender = {0};
        socklen_t sender_length = sizeof(sender);
        int count = recvfrom(dns_socket, query, sizeof(query), 0,
            (struct sockaddr *)&sender, &sender_length);
        if (count < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            runtime_error = ESP_FAIL;
            break;
        }
        if (count == 0) continue;
        portENTER_CRITICAL(&portal_lock);
        portal_status.dns_queries = increment(portal_status.dns_queries);
        portEXIT_CRITICAL(&portal_lock);
        size_t size = cerberus_portal_dns_reply(query, (size_t)count,
            portal_address, reply, sizeof(reply));
        if (size) (void)sendto(dns_socket, reply, size, 0,
            (struct sockaddr *)&sender, sender_length);
    }
    esp_err_t error = cleanup_resources();
    portENTER_CRITICAL(&portal_lock);
    portal_status.running = false;
    portal_status.resources_held = error != ESP_OK;
    portal_status.report_pending = error == ESP_OK;
    portal_status.lease_expired = expired;
    portal_status.error_code = error != ESP_OK ? error : runtime_error;
    portal_status.elapsed_ms = now_ms() - started_ms;
    dns_task = NULL;
    portEXIT_CRITICAL(&portal_lock);
    xSemaphoreGive(dns_done);
    vTaskDelete(NULL);
}

static esp_err_t configure_netif(void)
{
    esp_netif_dhcp_status_t dhcp_state;
    esp_err_t error = esp_netif_dhcps_get_status(portal_netif, &dhcp_state);
    if (error != ESP_OK) return error;
    /* set_ip_info requires STOPPED, including when the default AP netif is
     * still in INIT before esp_wifi_start(). */
    if (dhcp_state != ESP_NETIF_DHCP_STOPPED) {
        error = esp_netif_dhcps_stop(portal_netif);
        if (error != ESP_OK) return error;
    }
    esp_netif_ip_info_t ip = {0};
    IP4_ADDR(&ip.ip, 172, 0, 0, 1);
    IP4_ADDR(&ip.gw, 172, 0, 0, 1);
    IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
    error = esp_netif_set_ip_info(portal_netif, &ip);
    if (error != ESP_OK) return error;
    esp_netif_dns_info_t dns = {0};
    dns.ip.type = ESP_IPADDR_TYPE_V4;
    dns.ip.u_addr.ip4.addr = ip.ip.addr;
    error = esp_netif_set_dns_info(portal_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (error != ESP_OK) return error;
    uint8_t offer_dns = 1U;
    error = esp_netif_dhcps_option(portal_netif, ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER, &offer_dns, sizeof(offer_dns));
    if (error != ESP_OK) return error;
    return esp_netif_dhcps_start(portal_netif);
}

static void load_template(void)
{
    html_template_length = 0U;
    if (!cerberus_storage_is_mounted() ||
        cerberus_storage_get_owner() != CERBERUS_STORAGE_OWNER_FIRMWARE) return;
    size_t count = 0U;
    uint64_t size = 0U;
    esp_err_t error = cerberus_storage_read_file_chunk("/CERBERUS/PORTAL", "INDEX.HTM",
        0U, (uint8_t *)html_template, sizeof(html_template), &count, &size);
    if (error == ESP_OK && size > 0U && size <= sizeof(html_template) && count == size)
        html_template_length = count;
}

esp_err_t cerberus_s3_portal_start(const cerberus_s3_portal_request_t *request)
{
    if (!valid_request(request)) return ESP_ERR_INVALID_ARG;
    if (dns_done || dns_task || wifi_claimed || portal_status.report_pending) return ESP_ERR_INVALID_STATE;
    esp_err_t error = cerberus_s3_wifi_claim(CERBERUS_S3_WIFI_PORTAL);
    if (error != ESP_OK) return error;
    wifi_claimed = true;
    active_request = *request;
    memset(submissions, 0, sizeof(submissions));
    submission_count = 0U;
    portal_status = (cerberus_s3_portal_status_t){
        .resources_held = true,
        .c5_preflight_passed = true,
        .source_channel = request->source_channel,
        .local_channel = request->local_channel,
        .c5_scan_generation = request->c5_scan_generation,
    };
    memcpy(portal_status.source_bssid, request->source_bssid, 6U);
    atomic_store(&stop_requested, false);
    load_template();
#define PORTAL_TRY(call) do { error = (call); if (error != ESP_OK) goto failed; } while (0)
    PORTAL_TRY(cerberus_s3_wifi_prepare(CERBERUS_S3_WIFI_PORTAL));
    portal_netif = esp_netif_create_default_wifi_ap();
    if (!portal_netif) { error = ESP_ERR_NO_MEM; goto failed; }
    PORTAL_TRY(configure_netif());
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    PORTAL_TRY(esp_wifi_init(&init));
    wifi_initialized = true;
    PORTAL_TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi = {0};
    memcpy(wifi.ap.ssid, request->source_ssid, request->source_ssid_length);
    wifi.ap.ssid_len = request->source_ssid_length;
    wifi.ap.channel = request->local_channel;
    wifi.ap.max_connection = 4U;
    wifi.ap.authmode = WIFI_AUTH_OPEN;
    PORTAL_TRY(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
        wifi_event, NULL, &portal_wifi_handler));
    handler_registered = true;
    PORTAL_TRY(esp_wifi_set_mode(WIFI_MODE_AP));
    PORTAL_TRY(esp_wifi_set_config(WIFI_IF_AP, &wifi));
    PORTAL_TRY(esp_wifi_start());
    wifi_started = true;
    PORTAL_TRY(esp_wifi_get_mac(WIFI_IF_AP, portal_status.local_bssid));
    if (memcmp(portal_status.local_bssid, request->source_bssid, 6U) == 0) {
        error = ESP_ERR_INVALID_STATE;
        goto failed;
    }
    dns_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (dns_socket < 0) { error = ESP_FAIL; goto failed; }
    struct timeval timeout = {.tv_sec = 0, .tv_usec = 250000};
    if (setsockopt(dns_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        error = ESP_FAIL; goto failed;
    }
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(53)};
    address.sin_addr.s_addr = inet_addr(PORTAL_IP);
    if (bind(dns_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        error = ESP_FAIL; goto failed;
    }
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.stack_size = 8192U;
    http_config.max_uri_handlers = 4U;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    PORTAL_TRY(httpd_start(&portal_http, &http_config));
    httpd_uri_t submit = {.uri = "/submit", .method = HTTP_POST, .handler = submit_post};
    httpd_uri_t landing = {.uri = "/*", .method = HTTP_GET, .handler = landing_get};
    PORTAL_TRY(httpd_register_uri_handler(portal_http, &submit));
    PORTAL_TRY(httpd_register_uri_handler(portal_http, &landing));
    dns_done = xSemaphoreCreateBinary();
    if (!dns_done) { error = ESP_ERR_NO_MEM; goto failed; }
    started_ms = now_ms();
    if (xTaskCreate(dns_worker, "portal_dns", 4096U, NULL, 3U, &dns_task) != pdPASS) {
        error = ESP_ERR_NO_MEM; goto failed;
    }
    portENTER_CRITICAL(&portal_lock);
    portal_status.running = true;
    portEXIT_CRITICAL(&portal_lock);
    ESP_LOGI(TAG, "portal ready at http://%s/ on S3 channel %u", PORTAL_IP, request->local_channel);
#undef PORTAL_TRY
    return ESP_OK;
failed:
    ESP_LOGW(TAG, "portal start failed: %s", esp_err_to_name(error));
    esp_err_t cleanup = cleanup_resources();
    if (dns_done) { vSemaphoreDelete(dns_done); dns_done = NULL; }
    portENTER_CRITICAL(&portal_lock);
    portal_status.running = false;
    portal_status.resources_held = cleanup != ESP_OK;
    portal_status.error_code = cleanup != ESP_OK ? cleanup : error;
    portEXIT_CRITICAL(&portal_lock);
    if (cleanup == ESP_OK) {
        memset(&active_request, 0, sizeof(active_request));
        memset(html_template, 0, sizeof(html_template));
        html_template_length = 0U;
    }
    return cleanup != ESP_OK ? cleanup : error;
}

esp_err_t cerberus_s3_portal_stop(void)
{
    if (dns_done) {
        atomic_store(&stop_requested, true);
        if (xSemaphoreTake(dns_done, pdMS_TO_TICKS(5000U)) != pdTRUE)
            return ESP_ERR_TIMEOUT;
        vSemaphoreDelete(dns_done);
        dns_done = NULL;
    }
    esp_err_t error = cleanup_resources();
    portENTER_CRITICAL(&portal_lock);
    if (error != ESP_OK || portal_status.resources_held) {
        portal_status.running = false;
        portal_status.resources_held = error != ESP_OK;
        if (error == ESP_OK && started_ms) portal_status.report_pending = true;
        portal_status.error_code = error;
        if (started_ms) portal_status.elapsed_ms = now_ms() - started_ms;
    }
    portEXIT_CRITICAL(&portal_lock);
    return error;
}

void cerberus_s3_portal_get_status(cerberus_s3_portal_status_t *out)
{
    if (!out) return;
    portENTER_CRITICAL(&portal_lock);
    *out = portal_status;
    if (out->running) out->elapsed_ms = now_ms() - started_ms;
    portEXIT_CRITICAL(&portal_lock);
}

esp_err_t cerberus_s3_portal_note_c5_observation(uint32_t scan_generation,
    const uint8_t source_bssid[6], uint32_t observed_scans)
{
    if (!source_bssid || !scan_generation) return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&portal_lock);
    bool match = portal_status.running && scan_generation == active_request.c5_scan_generation &&
        memcmp(source_bssid, active_request.source_bssid, 6U) == 0 &&
        observed_scans >= portal_status.c5_observed_scans;
    if (match) {
        portal_status.c5_observed_scans = observed_scans;
        portal_status.c5_observation_available = true;
    }
    portEXIT_CRITICAL(&portal_lock);
    return match ? ESP_OK : ESP_ERR_INVALID_STATE;
}

static bool append(char *output, size_t capacity, size_t *length, const char *format, ...)
{
    if (*length >= capacity) return false;
    va_list arguments;
    va_start(arguments, format);
    int count = vsnprintf(output + *length, capacity - *length, format, arguments);
    va_end(arguments);
    if (count < 0 || (size_t)count >= capacity - *length) return false;
    *length += (size_t)count;
    return true;
}

static bool append_csv_cell(char *output, size_t capacity, size_t *length, const char *value)
{
    if (!append(output, capacity, length, "\"")) return false;
    if (*value == '=' || *value == '+' || *value == '-' || *value == '@')
        if (!append(output, capacity, length, "'")) return false;
    while (*value) {
        if (*value == '"' && !append(output, capacity, length, "\"")) return false;
        if (!append(output, capacity, length, "%c", *value++)) return false;
    }
    return append(output, capacity, length, "\"");
}

esp_err_t cerberus_s3_portal_save(char *file_name, size_t file_name_capacity)
{
    if (!file_name || file_name_capacity == 0U) return ESP_ERR_INVALID_ARG;
    if (dns_done) {
        if (portal_status.running) return ESP_ERR_INVALID_STATE;
        esp_err_t stopped = cerberus_s3_portal_stop();
        if (stopped != ESP_OK) return stopped;
    }
    cerberus_s3_portal_status_t snapshot;
    portal_submission_t rows[CERBERUS_PORTAL_SUBMISSIONS];
    uint8_t row_count;
    portENTER_CRITICAL(&portal_lock);
    snapshot = portal_status;
    row_count = submission_count;
    memcpy(rows, submissions, sizeof(rows));
    portEXIT_CRITICAL(&portal_lock);
    if (snapshot.running || snapshot.resources_held || !snapshot.report_pending)
        return ESP_ERR_INVALID_STATE;
    char *report = calloc(1U, PORTAL_REPORT_MAX);
    if (!report) return ESP_ERR_NO_MEM;
    size_t length = 0U;
    char ssid[33];
    for (size_t index = 0U; index < active_request.source_ssid_length; ++index) {
        uint8_t byte = active_request.source_ssid[index];
        ssid[index] = byte >= 0x20U && byte <= 0x7EU ? (char)byte : '?';
    }
    ssid[active_request.source_ssid_length] = '\0';
    bool okay = append(report, PORTAL_REPORT_MAX, &length,
        "kind,key,value\nmeta,source_ssid,") &&
        append_csv_cell(report, PORTAL_REPORT_MAX, &length, ssid) &&
        append(report, PORTAL_REPORT_MAX, &length,
            "\nmeta,source_bssid,%02X:%02X:%02X:%02X:%02X:%02X"
            "\nmeta,local_bssid,%02X:%02X:%02X:%02X:%02X:%02X"
            "\nmeta,source_channel,%u\nmeta,local_channel,%u"
            "\nmeta,c5_scan_generation,%lu\nmeta,c5_observed_scans,%lu"
            "\nmeta,client_joins,%lu"
            "\nmeta,dns_queries,%lu\nmeta,http_requests,%lu"
            "\nmeta,form_submissions,%lu\nmeta,forms_dropped,%lu"
            "\nmeta,elapsed_ms,%lu\n",
            snapshot.source_bssid[0], snapshot.source_bssid[1], snapshot.source_bssid[2],
            snapshot.source_bssid[3], snapshot.source_bssid[4], snapshot.source_bssid[5],
            snapshot.local_bssid[0], snapshot.local_bssid[1], snapshot.local_bssid[2],
            snapshot.local_bssid[3], snapshot.local_bssid[4], snapshot.local_bssid[5],
            snapshot.source_channel, snapshot.local_channel,
            (unsigned long)snapshot.c5_scan_generation,
            (unsigned long)snapshot.c5_observed_scans,
            (unsigned long)snapshot.client_joins,
            (unsigned long)snapshot.dns_queries, (unsigned long)snapshot.http_requests,
            (unsigned long)snapshot.form_submissions, (unsigned long)snapshot.forms_dropped,
            (unsigned long)snapshot.elapsed_ms);
    for (uint8_t row = 0U; okay && row < row_count; ++row)
        for (uint8_t field = 0U; okay && field < rows[row].field_count; ++field)
            okay = append(report, PORTAL_REPORT_MAX, &length, "form%u,", row + 1U) &&
                append_csv_cell(report, PORTAL_REPORT_MAX, &length, rows[row].fields[field].name) &&
                append(report, PORTAL_REPORT_MAX, &length, ",") &&
                append_csv_cell(report, PORTAL_REPORT_MAX, &length, rows[row].fields[field].value) &&
                append(report, PORTAL_REPORT_MAX, &length, "\n");
    memset(rows, 0, sizeof(rows));
    esp_err_t error = okay ? cerberus_storage_write_portal_report(
        report, length, file_name, file_name_capacity) : ESP_ERR_INVALID_SIZE;
    memset(report, 0, PORTAL_REPORT_MAX);
    free(report);
    if (error == ESP_OK) (void)cerberus_s3_portal_discard();
    return error;
}

esp_err_t cerberus_s3_portal_discard(void)
{
    if (dns_done) {
        if (portal_status.running) return ESP_ERR_INVALID_STATE;
        esp_err_t stopped = cerberus_s3_portal_stop();
        if (stopped != ESP_OK) return stopped;
    }
    portENTER_CRITICAL(&portal_lock);
    if (portal_status.running || portal_status.resources_held) {
        portEXIT_CRITICAL(&portal_lock);
        return ESP_ERR_INVALID_STATE;
    }
    memset(submissions, 0, sizeof(submissions));
    submission_count = 0U;
    portal_status.report_pending = false;
    portEXIT_CRITICAL(&portal_lock);
    memset(&active_request, 0, sizeof(active_request));
    memset(html_template, 0, sizeof(html_template));
    html_template_length = 0U;
    started_ms = 0U;
    return ESP_OK;
}
