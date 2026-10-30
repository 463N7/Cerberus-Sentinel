#include "network_scanner.h"
#include "wifi_scanner.h"
#include "esp_timer.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/etharp.h"
#include "lwip/tcpip.h"
#include <errno.h>
#include <fcntl.h>
#include <string.h>

static SemaphoreHandle_t scan_lock;
static TaskHandle_t scan_task;
static cerberus_net_scan_plan_t active_plan;
static cerberus_net_scan_status_t scan_status;
static cerberus_net_scan_result_t results[CERBERUS_NET_SCAN_MAX_RESULTS];
static uint32_t active_epoch, started_ms, contact_ms;
static bool stop_requested;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }
static esp_err_t abort_reason(uint32_t epoch)
{
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    bool stopped = stop_requested || (uint32_t)(now_ms() - contact_ms) > 5000U;
    xSemaphoreGive(scan_lock);
    if (stopped) return ESP_ERR_NOT_FINISHED;
    cerberus_wifi_connection_status_payload_t link = {0};
    uint32_t current = cerberus_wifi_connection_snapshot(&link);
    return current != epoch || link.state != CERBERUS_WIFI_CONNECTION_CONNECTED ?
        ESP_ERR_INVALID_STATE : ESP_OK;
}

/* Every select is <=100 ms so cancel, lease expiry and link loss close the
 * socket promptly. A timeout is not reported as a closed port. */
static int wait_socket(int fd, bool writing, uint32_t deadline, uint32_t epoch, esp_err_t *error)
{
    while ((int32_t)(deadline - now_ms()) > 0) {
        *error = abort_reason(epoch);
        if (*error != ESP_OK) return -1;
        uint32_t left = deadline - now_ms();
        if ((int32_t)left <= 0) break;
        if (left > 100U) left = 100U;
        struct timeval timeout = {.tv_sec = 0, .tv_usec = (long)left * 1000};
        fd_set set;
        FD_ZERO(&set); FD_SET(fd, &set);
        int ready = select(fd + 1, writing ? NULL : &set, writing ? &set : NULL, NULL, &timeout);
        if (ready > 0) return 1;
        if (ready < 0 && errno != EINTR) { *error = ESP_FAIL; return -1; }
    }
    return 0;
}

/* Returns 1=open, 0=refused, 2=timeout/unreachable, -1=fatal/cancelled.
 * SSH identification only reads a server banner; it never sends credentials. */
static int probe(cerberus_net_scan_result_t *out, uint16_t timeout_ms, uint32_t epoch, esp_err_t *error)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) { *error = ESP_ERR_NO_MEM; return -1; }
    int outcome = -1;
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { *error = ESP_FAIL; goto done; }
    struct sockaddr_in target = {.sin_family = AF_INET, .sin_port = htons(out->port)};
    memcpy(&target.sin_addr.s_addr, out->ipv4, 4);
    uint32_t began = now_ms();
    int connected = connect(fd, (struct sockaddr *)&target, sizeof(target));
    int connection_error = connected == 0 ? 0 : errno;
    if (connected < 0 && (connection_error == EINPROGRESS || connection_error == EWOULDBLOCK)) {
        int ready = wait_socket(fd, true, began + timeout_ms, epoch, error);
        if (ready < 0) goto done;
        if (!ready) { outcome = 2; goto done; }
        socklen_t length = sizeof(connection_error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &connection_error, &length) < 0) {
            *error = ESP_FAIL; goto done;
        }
    }
    if (connection_error) {
        if (connection_error == ECONNREFUSED) outcome = 0;
        else if (connection_error == ETIMEDOUT || connection_error == EHOSTUNREACH ||
                 connection_error == ENETUNREACH || connection_error == ECONNRESET) outcome = 2;
        else *error = ESP_FAIL;
        goto done;
    }
    outcome = 1;
    out->connect_ms = (uint16_t)(now_ms() - began);
    if (out->port == 22 || out->port == 2222) {
        char banner[CERBERUS_NET_SCAN_BANNER_BYTES] = {0};
        size_t used = 0;
        uint32_t deadline = now_ms() + 300U;
        while (used < sizeof(banner)) {
            int ready = wait_socket(fd, false, deadline, epoch, error);
            if (ready < 0) { outcome = -1; break; }
            if (!ready) break;
            int got = recv(fd, banner + used, sizeof(banner) - used, 0);
            if (got <= 0) {
                if (got < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) continue;
                break;
            }
            used += (size_t)got;
            if (memchr(banner, '\n', used)) break;
        }
        if (used >= 4 && !memcmp(banner, "SSH-", 4)) out->flags |= CERBERUS_NET_SCAN_SSH_BANNER;
        if (used == sizeof(banner)) out->flags |= CERBERUS_NET_SCAN_BANNER_TRUNCATED;
        for (size_t i = 0; i < used; ++i)
            out->banner[i] = banner[i] >= 32 && banner[i] <= 126 ? banner[i] : '.';
    }
done:
    close(fd);
    return outcome;
}

/* The socket belongs to this worker and is closed on every exit. No separate
 * ping task/session is allocated. Ignore unrelated, stale or malformed replies
 * until the original deadline, rather than renewing it on received traffic. */
static int probe_ping(cerberus_net_scan_result_t *out, const uint8_t own[4],
    uint16_t sequence, uint32_t nonce, uint16_t timeout_ms, uint32_t epoch, esp_err_t *error)
{
    int fd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (fd < 0) { *error = ESP_ERR_NO_MEM; return -1; }
    int outcome = -1;
    struct sockaddr_in local = {.sin_family = AF_INET};
    memcpy(&local.sin_addr.s_addr, own, 4);
    if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 || bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        *error = ESP_FAIL; goto done;
    }
    struct sockaddr_in target = {.sin_family = AF_INET};
    memcpy(&target.sin_addr.s_addr, out->ipv4, 4);
    uint8_t request[CERBERUS_NET_ICMP_BYTES];
    cerberus_net_icmp_request(request, out->job_id, sequence, nonce);
    uint32_t began = now_ms(), deadline = began + timeout_ms;
    *error = abort_reason(epoch);
    if (*error != ESP_OK) goto done;
    if (sendto(fd, request, sizeof(request), 0, (struct sockaddr *)&target, sizeof(target)) != sizeof(request)) {
        if (errno == EHOSTUNREACH || errno == ENETUNREACH) outcome = 2;
        else *error = ESP_FAIL;
        goto done;
    }
    for (;;) {
        int ready = wait_socket(fd, false, deadline, epoch, error);
        if (ready < 0) break;
        if (!ready) { outcome = 2; break; }
        uint8_t packet[60 + CERBERUS_NET_ICMP_BYTES];
        struct sockaddr_in from = {0};
        socklen_t length = sizeof(from);
        int received = recvfrom(fd, packet, sizeof(packet), 0, (struct sockaddr *)&from, &length);
        if (received < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR) continue;
            *error = ESP_FAIL; break;
        }
        if (from.sin_family != AF_INET || memcmp(&from.sin_addr.s_addr, out->ipv4, 4)) continue;
        if (!cerberus_net_icmp_reply(packet, (size_t)received, out->ipv4, own, out->job_id, sequence, nonce)) continue;
        out->connect_ms = (uint16_t)(now_ms() - began);
        out->flags = CERBERUS_NET_SCAN_ICMP_REPLY;
        outcome = 1;
        break;
    }
done:
    close(fd);
    return outcome;
}

typedef struct {
    uint8_t interface_index, own[4], target[4], mac[6];
    bool request, found, cached;
    esp_err_t error;
} arp_operation_t;

/* Runs only on the lwIP core, including both lookup and request. Never mutate
 * or clear the global ARP table: existing connections share it. */
static void arp_on_core(void *argument)
{
    arp_operation_t *op = argument;
    op->found = false;
    struct netif *interface = netif_get_by_index(op->interface_index);
    if (!interface || !netif_is_up(interface) || !netif_is_link_up(interface) ||
        memcmp(&netif_ip4_addr(interface)->addr, op->own, 4)) {
        op->error = ESP_ERR_INVALID_STATE; return;
    }
    ip4_addr_t ip;
    memcpy(&ip.addr, op->target, 4);
    struct eth_addr *mac = NULL;
    const ip4_addr_t *resolved = NULL;
    if (etharp_find_addr(interface, &ip, &mac, &resolved) >= 0 && mac && resolved &&
        !memcmp(&resolved->addr, op->target, 4)) {
        memcpy(op->mac, mac->addr, 6);
        op->found = true;
    }
    if (op->request) {
        op->cached = op->found;
        err_t err = etharp_request(interface, &ip);
        if (err != ERR_OK) op->error = err == ERR_MEM ? ESP_ERR_NO_MEM : ESP_FAIL;
    }
}
static int probe_arp(cerberus_net_scan_result_t *out, const uint8_t own[4],
    uint16_t timeout_ms, uint32_t epoch, esp_err_t *error)
{
    esp_netif_t *station = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    int index = station ? esp_netif_get_netif_impl_index(station) : -1;
    if (index < 1 || index > UINT8_MAX) { *error = ESP_ERR_INVALID_STATE; return -1; }
    arp_operation_t op = {.interface_index = (uint8_t)index, .request = true};
    memcpy(op.own, own, 4); memcpy(op.target, out->ipv4, 4);
    uint32_t began = now_ms();
    for (;;) {
        *error = abort_reason(epoch);
        if (*error != ESP_OK) return -1;
        /* Synchronous completion keeps this stack context alive for the whole
         * callback, even when the user cancels during a core operation. */
        err_t err = tcpip_callback_wait(arp_on_core, &op);
        if (err != ERR_OK) { *error = err == ERR_MEM ? ESP_ERR_NO_MEM : ESP_FAIL; return -1; }
        if (op.error != ESP_OK) { *error = op.error; return -1; }
        *error = abort_reason(epoch);
        if (*error != ESP_OK) return -1;
        if (op.found) {
            out->flags = CERBERUS_NET_SCAN_ARP_RESOLVED | (op.cached ? CERBERUS_NET_SCAN_ARP_CACHED : 0);
            memcpy(out->banner, op.mac, 6);
            out->connect_ms = (uint16_t)(now_ms() - began);
            return 1;
        }
        if ((uint32_t)(now_ms() - began) >= timeout_ms) return 2;
        op.request = false;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void worker(void *unused)
{
    (void)unused;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    cerberus_net_scan_plan_t plan = active_plan;
    uint32_t epoch = active_epoch;
    xSemaphoreGive(scan_lock);
    const uint8_t own[] = {(uint8_t)(plan.own_host >> 24), (uint8_t)(plan.own_host >> 16),
        (uint8_t)(plan.own_host >> 8), (uint8_t)plan.own_host};
    uint32_t nonce = plan.request.mode == CERBERUS_NET_SCAN_PING_LAN ? esp_random() : 0;
    esp_err_t error = ESP_OK;
    for (uint32_t i = 0; i < plan.total; ++i) {
        error = abort_reason(epoch);
        if (error != ESP_OK) break;
        cerberus_net_scan_result_t item = {.job_id = plan.request.job_id};
        uint16_t port;
        if (!cerberus_net_scan_probe(&plan, (uint16_t)i, item.ipv4, &port)) { error = ESP_FAIL; break; }
        item.port = port;
        int outcome = plan.request.mode == CERBERUS_NET_SCAN_PING_LAN ?
            probe_ping(&item, own, (uint16_t)(i + 1U), nonce, plan.request.timeout_ms, epoch, &error) :
            plan.request.mode == CERBERUS_NET_SCAN_ARP_LAN ?
            probe_arp(&item, own, plan.request.timeout_ms, epoch, &error) :
            probe(&item, plan.request.timeout_ms, epoch, &error);
        if (outcome < 0) break;
        error = abort_reason(epoch);
        if (error != ESP_OK) break;
        xSemaphoreTake(scan_lock, portMAX_DELAY);
        ++scan_status.tested;
        if (outcome == 1) {
            ++scan_status.open;
            if (scan_status.result_count < CERBERUS_NET_SCAN_MAX_RESULTS) {
                item.index = scan_status.result_count;
                results[scan_status.result_count++] = item;
            }
        } else if (outcome == 0) ++scan_status.closed;
        else ++scan_status.filtered;
        xSemaphoreGive(scan_lock);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    scan_status.state = error == ESP_ERR_NOT_FINISHED ? CERBERUS_NET_SCAN_CANCELLED :
        error == ESP_OK ? CERBERUS_NET_SCAN_COMPLETE : CERBERUS_NET_SCAN_ERROR;
    scan_status.error_code = error == ESP_ERR_NOT_FINISHED ? ESP_OK : error;
    scan_status.elapsed_ms = now_ms() - started_ms;
    scan_task = NULL;
    xSemaphoreGive(scan_lock);
    vTaskDelete(NULL);
}

esp_err_t cerberus_network_scanner_init(void)
{
    if (scan_lock) return ESP_OK;
    scan_lock = xSemaphoreCreateMutex();
    return scan_lock ? ESP_OK : ESP_ERR_NO_MEM;
}
esp_err_t cerberus_network_scanner_start(const cerberus_net_scan_request_t *request)
{
    if (!scan_lock || !request) return ESP_ERR_INVALID_ARG;
    cerberus_wifi_connection_status_payload_t link = {0};
    uint32_t epoch = cerberus_wifi_connection_snapshot(&link);
    cerberus_net_scan_plan_t plan;
    if (link.state != CERBERUS_WIFI_CONNECTION_CONNECTED ||
        link.generation != request->connection_generation || link.index != request->connection_index)
        return ESP_ERR_INVALID_STATE;
    if (!cerberus_net_scan_plan(&plan, request, link.ipv4, link.netmask)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    if (scan_status.job_id == request->job_id) {
        bool same = !memcmp(request, &active_plan.request, sizeof(*request));
        if (same) contact_ms = now_ms();
        xSemaphoreGive(scan_lock);
        return same ? ESP_OK : ESP_ERR_INVALID_ARG;
    }
    if (scan_task) { xSemaphoreGive(scan_lock); return ESP_ERR_INVALID_STATE; }
    active_plan = plan; active_epoch = epoch;
    memset(results, 0, sizeof(results));
    scan_status = (cerberus_net_scan_status_t){.job_id = request->job_id,
        .state = CERBERUS_NET_SCAN_RUNNING, .total = plan.total};
    stop_requested = false;
    contact_ms = started_ms = now_ms();
    if (xTaskCreate(worker, "network_scan", 5120, NULL, 3, &scan_task) != pdPASS) {
        scan_task = NULL; scan_status.state = CERBERUS_NET_SCAN_ERROR;
        scan_status.error_code = ESP_ERR_NO_MEM;
    }
    esp_err_t error = scan_status.error_code;
    xSemaphoreGive(scan_lock);
    return error;
}
esp_err_t cerberus_network_scanner_stop(uint16_t job_id)
{
    if (!scan_lock || !job_id) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    bool match = job_id == scan_status.job_id;
    if (match && scan_task) {
        stop_requested = true;
        scan_status.state = CERBERUS_NET_SCAN_STOPPING;
    }
    xSemaphoreGive(scan_lock);
    return match ? ESP_OK : ESP_ERR_NOT_FOUND;
}
void cerberus_network_scanner_status(cerberus_net_scan_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!scan_lock) { status->state = CERBERUS_NET_SCAN_ERROR; status->error_code = ESP_ERR_INVALID_STATE; return; }
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    contact_ms = now_ms();
    *status = scan_status;
    if (scan_task) status->elapsed_ms = now_ms() - started_ms;
    xSemaphoreGive(scan_lock);
}
esp_err_t cerberus_network_scanner_result(const cerberus_net_scan_query_t *query,
    cerberus_net_scan_result_t *result)
{
    if (!scan_lock || !query || !result) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(scan_lock, portMAX_DELAY);
    bool valid = query->job_id == scan_status.job_id && query->index < scan_status.result_count;
    if (valid) { *result = results[query->index]; contact_ms = now_ms(); }
    xSemaphoreGive(scan_lock);
    return valid ? ESP_OK : ESP_ERR_NOT_FOUND;
}
