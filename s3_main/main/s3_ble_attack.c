#include "s3_ble_attack.h"
#include "s3_ble_host.h"
#include "s3_wifi_owner.h"
#include "sdkconfig.h"

#include <string.h>

#ifdef CONFIG_BT_NIMBLE_ENABLED
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "nimble/ble.h"

#define ATTACK_LEASE_MS 5000U
#define ATTACK_WORKER_STACK_BYTES 6144U
#define ATTACK_DEFAULT_INTERVAL_UNITS 160U /* Valid nonconnectable baseline: 100 ms. */
#define ATTACK_STOP_TIMEOUT_MS 1000U
#define ATTACK_CONNECTION_STOP_TIMEOUT_MS 1500U

static SemaphoreHandle_t attack_mutex;
static cerberus_s3_ble_attack_request_t active_request;
static cerberus_s3_ble_attack_status_t active_status;
static bool worker_running, stop_requested, attack_owns_host;
static bool connection_active;
static uint16_t connection_handle;
static unsigned claimed_host_epoch;
static uint32_t started_ms, last_touch_ms;

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }
static void delay_ms(uint32_t ms)
{
    TickType_t ticks = pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1U);
}
static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) ++*value;
}

static bool lease_or_stop(bool *lease_expired)
{
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    bool expired = (uint32_t)(now_ms() - last_touch_ms) > ATTACK_LEASE_MS;
    if (expired) stop_requested = true;
    bool stop = stop_requested;
    if (stop && active_status.state != CERBERUS_S3_BLE_ATTACK_ERROR)
        active_status.state = CERBERUS_S3_BLE_ATTACK_STOPPING;
    xSemaphoreGive(attack_mutex);
    if (lease_expired) *lease_expired |= expired;
    return stop;
}

static void record_heap(void)
{
    uint32_t free_kib = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U;
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    if (free_kib < active_status.minimum_free_heap_kib)
        active_status.minimum_free_heap_kib = (uint16_t)free_kib;
    active_status.elapsed_ms = (uint32_t)(now_ms() - started_ms);
    xSemaphoreGive(attack_mutex);
}

static int gap_event(struct ble_gap_event *event, void *unused)
{
    (void)unused;
#ifdef CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
    if (!event) return 0;
    if (event->type == BLE_GAP_EVENT_CONNECT && event->connect.status == 0) {
        /* A connectable announcement can end while its link is still live.
         * Keep the host lease until the disconnect callback confirms closure. */
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        connection_active = true;
        connection_handle = event->connect.conn_handle;
        active_status.connection_active = true;
        increment(&active_status.connections_accepted);
        xSemaphoreGive(attack_mutex);
        int rc = ble_gap_terminate(event->connect.conn_handle,
            BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0 && rc != BLE_HS_ENOTCONN) {
            xSemaphoreTake(attack_mutex, portMAX_DELAY);
            active_status.last_controller_rc = rc;
            increment(&active_status.connection_terminate_errors);
            xSemaphoreGive(attack_mutex);
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        if (connection_active &&
            connection_handle == event->disconnect.conn.conn_handle) {
            connection_active = false;
            active_status.connection_active = false;
            increment(&active_status.connections_closed);
        }
        /* A disconnect can arrive after the worker's bounded Stop wait.
         * Release the retained host then, so another BLE owner can proceed. */
        if (!connection_active && !worker_running && attack_owns_host &&
            !ble_gap_adv_active()) {
            cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
            if (owner == CERBERUS_S3_BLE_OWNER_ADVERTISER &&
                cerberus_s3_ble_host_epoch() == claimed_host_epoch &&
                cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_ADVERTISER) == ESP_OK)
                attack_owns_host = false;
            else if (owner == CERBERUS_S3_BLE_OWNER_NONE ||
                     cerberus_s3_ble_host_epoch() != claimed_host_epoch)
                attack_owns_host = false;
        }
        xSemaphoreGive(attack_mutex);
    }
#else
    (void)event;
#endif
    return 0;
}

static bool has_connection(void)
{
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    bool active = connection_active;
    xSemaphoreGive(attack_mutex);
    return active;
}

static esp_err_t stop_connection(unsigned host_epoch)
{
    uint32_t started = now_ms();
    uint32_t last_terminate_ms = 0U;
    while (has_connection()) {
        if (!cerberus_s3_ble_host_synced() ||
            cerberus_s3_ble_host_epoch() != host_epoch) {
            /* A host reset destroys every controller link. The shared host
             * owner is also reset, so never release a different new owner. */
            xSemaphoreTake(attack_mutex, portMAX_DELAY);
            connection_active = false;
            active_status.connection_active = false;
            xSemaphoreGive(attack_mutex);
            return ESP_FAIL;
        }
        uint32_t now = now_ms();
        if (last_terminate_ms == 0U || (uint32_t)(now - last_terminate_ms) >= 100U) {
            xSemaphoreTake(attack_mutex, portMAX_DELAY);
            uint16_t handle = connection_handle;
            xSemaphoreGive(attack_mutex);
            int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
            if (rc != 0 && rc != BLE_HS_ENOTCONN) {
                xSemaphoreTake(attack_mutex, portMAX_DELAY);
                active_status.last_controller_rc = rc;
                increment(&active_status.connection_terminate_errors);
                xSemaphoreGive(attack_mutex);
            }
            last_terminate_ms = now ? now : 1U;
        }
        if ((uint32_t)(now - started) >= ATTACK_CONNECTION_STOP_TIMEOUT_MS)
            return ESP_ERR_TIMEOUT;
        delay_ms(20U);
    }
    return ESP_OK;
}

static uint16_t interval_units(const cerberus_ble_attack_frame_t *frame)
{
    uint16_t proposed = frame->interval_units ? frame->interval_units : ATTACK_DEFAULT_INTERVAL_UNITS;
    /* Use the 100 ms fast-advertising baseline for nonconnectable payloads.
     * A shorter interval may be accepted by the controller, but needs
     * independent RF and coexistence measurements before enabling it. */
    return !frame->connectable && proposed < ATTACK_DEFAULT_INTERVAL_UNITS ?
        ATTACK_DEFAULT_INTERVAL_UNITS : proposed;
}

static uint32_t dwell_ms(const cerberus_ble_attack_frame_t *frame, uint16_t interval)
{
    /* Give even a 10 ms payload plan time for two scheduled advertisements.
     * This is controller scheduling opportunity, not a claim of RF delivery. */
    uint32_t minimum = ((uint32_t)interval * 625U * 2U + 999U) / 1000U;
    return frame->active_ms > minimum ? frame->active_ms : minimum;
}

static esp_err_t stop_advertiser(void)
{
    if (!ble_gap_adv_active()) return ESP_OK;
    uint32_t started = now_ms();
    int rc = ble_gap_adv_stop();
    while (ble_gap_adv_active()) {
        /* A failed stop must retain ownership. Bound the wait so navigation
         * can report a controller fault instead of hanging its caller. */
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        active_status.state = CERBERUS_S3_BLE_ATTACK_STOPPING;
        active_status.last_controller_rc = rc;
        xSemaphoreGive(attack_mutex);
        if ((uint32_t)(now_ms() - started) >= ATTACK_STOP_TIMEOUT_MS)
            return ESP_ERR_TIMEOUT;
        delay_ms(20U);
        rc = ble_gap_adv_stop();
    }
    return (rc == 0 || rc == BLE_HS_EALREADY) ? ESP_OK : ESP_FAIL;
}

static void finish_worker(esp_err_t error, bool lease_expired, bool claimed,
    unsigned host_epoch)
{
    if (claimed) {
        if (ble_gap_adv_active()) {
            esp_err_t stop_error = stop_advertiser();
            if (error == ESP_OK) error = stop_error;
        }
        esp_err_t connection_error = stop_connection(host_epoch);
        if (error == ESP_OK) error = connection_error;
        if (ble_gap_adv_active() || has_connection()) {
            /* Do not transfer the host to a scanner or central while the
             * controller still advertises or holds a link. */
            if (error == ESP_OK) error = ESP_ERR_TIMEOUT;
        } else {
            cerberus_s3_ble_owner_t owner = cerberus_s3_ble_host_owner();
            esp_err_t release_error = owner == CERBERUS_S3_BLE_OWNER_ADVERTISER ?
                cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_ADVERTISER) :
                owner == CERBERUS_S3_BLE_OWNER_NONE ? ESP_OK : ESP_ERR_INVALID_STATE;
            xSemaphoreTake(attack_mutex, portMAX_DELAY);
            attack_owns_host = false;
            xSemaphoreGive(attack_mutex);
            if (release_error != ESP_OK && error == ESP_OK) error = release_error;
        }
    }
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    active_status.error = error;
    active_status.elapsed_ms = (uint32_t)(now_ms() - started_ms);
    active_status.state = error != ESP_OK ? CERBERUS_S3_BLE_ATTACK_ERROR :
        lease_expired ? CERBERUS_S3_BLE_ATTACK_LEASE_EXPIRED : CERBERUS_S3_BLE_ATTACK_STOPPED;
    worker_running = false;
    xSemaphoreGive(attack_mutex);
}

static void attack_worker(void *unused)
{
    (void)unused;
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    cerberus_s3_ble_attack_request_t request = active_request;
    xSemaphoreGive(attack_mutex);
    cerberus_ble_attack_plan_t plan;
    cerberus_ble_attack_result_t result = cerberus_ble_attack_plan_init(&plan,
        request.mode, request.seed, request.has_capture ? &request.capture : NULL);
    esp_err_t error = result == CERBERUS_BLE_ATTACK_OK ? ESP_OK : ESP_ERR_INVALID_ARG;
    bool lease_expired = false;
    bool claimed = false;
    if (error == ESP_OK && !lease_or_stop(&lease_expired)) {
        error = cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_ADVERTISER);
        claimed = error == ESP_OK;
        if (claimed) {
            xSemaphoreTake(attack_mutex, portMAX_DELAY);
            attack_owns_host = true;
            claimed_host_epoch = cerberus_s3_ble_host_epoch();
            xSemaphoreGive(attack_mutex);
        }
    }
    unsigned host_epoch = cerberus_s3_ble_host_epoch();
    if (claimed && cerberus_s3_wifi_owner() != CERBERUS_S3_WIFI_RECEIVER &&
        cerberus_s3_wifi_owner() != CERBERUS_S3_WIFI_PORTAL) {
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        active_status.state = CERBERUS_S3_BLE_ATTACK_RUNNING;
        xSemaphoreGive(attack_mutex);
    } else if (claimed) error = ESP_ERR_INVALID_STATE;

    while (claimed && error == ESP_OK && !lease_or_stop(&lease_expired)) {
        if (!cerberus_s3_ble_host_synced() || cerberus_s3_ble_host_epoch() != host_epoch) {
            error = ESP_FAIL;
            break;
        }
        cerberus_ble_attack_frame_t frame;
        result = cerberus_ble_attack_next(&plan, now_ms(), &frame);
        if (result != CERBERUS_BLE_ATTACK_OK ||
            !cerberus_ble_attack_ad_valid(frame.data, frame.length)) {
            error = result == CERBERUS_BLE_ATTACK_NEEDS_GATT ?
                ESP_ERR_NOT_SUPPORTED : ESP_ERR_INVALID_ARG;
            break;
        }
        if (frame.address_type != 1U && frame.address_type != 3U) {
            error = ESP_ERR_NOT_SUPPORTED; /* Public identity cannot be cloned. */
            break;
        }
        if ((frame.address[5] & 0xC0U) != 0xC0U) {
            error = ESP_ERR_NOT_SUPPORTED; /* RPA cannot be replayed as a static identity. */
            break;
        }
#ifndef CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
        if (frame.connectable) {
            error = ESP_ERR_NOT_SUPPORTED;
            break;
        }
#endif
        uint16_t interval = interval_units(&frame);
        uint32_t dwell = dwell_ms(&frame, interval);
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        increment(&active_status.planned_bursts);
        active_status.last_payload = frame.kind;
        active_status.effective_interval_units = interval;
        xSemaphoreGive(attack_mutex);
        int rc = ble_hs_id_set_rnd(frame.address);
        if (rc == 0) rc = ble_gap_adv_set_data(frame.data, frame.length);
        struct ble_gap_adv_params parameters = {
            .conn_mode = frame.connectable ? BLE_GAP_CONN_MODE_UND : BLE_GAP_CONN_MODE_NON,
            .disc_mode = BLE_GAP_DISC_MODE_GEN,
            .itvl_min = interval,
            .itvl_max = interval,
        };
        if (rc == 0) rc = ble_gap_adv_start(BLE_OWN_ADDR_RANDOM, NULL,
            (int32_t)(dwell + 250U), &parameters, gap_event, NULL);
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        active_status.last_controller_rc = rc;
        if (rc == 0) increment(&active_status.controller_accepted_bursts);
        else increment(&active_status.controller_rejected_bursts);
        xSemaphoreGive(attack_mutex);
        if (rc != 0) {
            error = ESP_FAIL;
            break;
        }
        uint32_t burst_started = now_ms();
        while ((uint32_t)(now_ms() - burst_started) < dwell &&
               ble_gap_adv_active() && !lease_or_stop(&lease_expired)) {
            record_heap();
            delay_ms(10U);
        }
        esp_err_t stop_error = stop_advertiser();
        if (stop_error != ESP_OK) {
            error = stop_error;
            break;
        }
        stop_error = stop_connection(host_epoch);
        if (stop_error != ESP_OK) {
            error = stop_error;
            break;
        }
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        increment(&active_status.completed_bursts);
        xSemaphoreGive(attack_mutex);
    }
    finish_worker(error, lease_expired, claimed, host_epoch);
    vTaskDelete(NULL);
}

esp_err_t cerberus_s3_ble_attack_start(const cerberus_s3_ble_attack_request_t *request,
    cerberus_s3_ble_attack_status_t *status)
{
    if (!request || !status || !request->job_id) return ESP_ERR_INVALID_ARG;
    *status = (cerberus_s3_ble_attack_status_t){
        .job_id = request->job_id, .mode = request->mode,
        .state = CERBERUS_S3_BLE_ATTACK_ERROR,
        .error = ESP_ERR_INVALID_ARG,
        .packet_count_available = false,
    };
    cerberus_ble_attack_plan_t plan;
    cerberus_ble_attack_result_t result = cerberus_ble_attack_plan_init(&plan,
        request->mode, request->seed, request->has_capture ? &request->capture : NULL);
    if (result == CERBERUS_BLE_ATTACK_NEEDS_GATT) {
        status->state = CERBERUS_S3_BLE_ATTACK_GATT_REQUIRED;
        status->error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (result != CERBERUS_BLE_ATTACK_OK) return ESP_ERR_INVALID_ARG;
#ifndef CONFIG_BT_NIMBLE_ROLE_PERIPHERAL
    if (request->mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ||
        request->mode == CERBERUS_BLE_ATTACK_ALL) {
        status->error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
#endif
    if (request->mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG &&
        (((request->capture.address_type != 1U) &&
          (request->capture.address_type != 3U)) ||
         (request->capture.address[5] & 0xC0U) != 0xC0U)) {
        status->error = ESP_ERR_NOT_SUPPORTED;
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER ||
        cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_PORTAL ||
        cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_NONE)
    {
        status->error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        (cerberus_s3_ble_host_initialized() ? 32768U : 98304U)) {
        status->error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    if (!attack_mutex) attack_mutex = xSemaphoreCreateMutex(); /* UI owner creates once. */
    if (!attack_mutex) {
        status->error = ESP_ERR_NO_MEM;
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    if (worker_running || attack_owns_host) {
        xSemaphoreGive(attack_mutex);
        status->error = ESP_ERR_INVALID_STATE;
        return ESP_ERR_INVALID_STATE;
    }
    active_request = *request;
    started_ms = last_touch_ms = now_ms();
    stop_requested = false;
    connection_active = false;
    connection_handle = 0U;
    worker_running = true;
    active_status = (cerberus_s3_ble_attack_status_t){
        .job_id = request->job_id, .mode = request->mode,
        .state = CERBERUS_S3_BLE_ATTACK_STARTING,
        .minimum_free_heap_kib = (uint16_t)(heap_caps_get_free_size(
            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U),
        .packet_count_available = false,
    };
    BaseType_t created = xTaskCreate(attack_worker, "s3_ble_attack",
        ATTACK_WORKER_STACK_BYTES, NULL, 3, NULL);
    if (created != pdPASS) {
        worker_running = false;
        active_status.state = CERBERUS_S3_BLE_ATTACK_ERROR;
        active_status.error = ESP_ERR_NO_MEM;
    }
    *status = active_status;
    xSemaphoreGive(attack_mutex);
    return created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t cerberus_s3_ble_attack_touch(uint16_t job_id)
{
    if (!attack_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    bool valid = worker_running && active_status.job_id == job_id && !stop_requested &&
        (uint32_t)(now_ms() - last_touch_ms) <= ATTACK_LEASE_MS;
    if (valid) last_touch_ms = now_ms();
    xSemaphoreGive(attack_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_attack_stop(uint16_t job_id)
{
    if (!attack_mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    bool valid = active_status.job_id == job_id && job_id != 0U;
    if (valid && worker_running) {
        stop_requested = true;
        active_status.state = CERBERUS_S3_BLE_ATTACK_STOPPING;
    }
    xSemaphoreGive(attack_mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

void cerberus_s3_ble_attack_get_status(cerberus_s3_ble_attack_status_t *status)
{
    if (!status) return;
    memset(status, 0, sizeof(*status));
    if (!attack_mutex) return;
    xSemaphoreTake(attack_mutex, portMAX_DELAY);
    *status = active_status;
    if (worker_running) status->elapsed_ms = (uint32_t)(now_ms() - started_ms);
    xSemaphoreGive(attack_mutex);
}

esp_err_t cerberus_s3_ble_attack_quiesce(uint32_t timeout_ms)
{
    cerberus_s3_ble_attack_status_t status;
    cerberus_s3_ble_attack_get_status(&status);
    if (status.state == CERBERUS_S3_BLE_ATTACK_STARTING ||
        status.state == CERBERUS_S3_BLE_ATTACK_RUNNING ||
        status.state == CERBERUS_S3_BLE_ATTACK_STOPPING)
        (void)cerberus_s3_ble_attack_stop(status.job_id);
    uint32_t started = now_ms();
    for (;;) {
        if (!attack_mutex) return ESP_OK;
        xSemaphoreTake(attack_mutex, portMAX_DELAY);
        bool settled = !worker_running && !attack_owns_host;
        xSemaphoreGive(attack_mutex);
        if (settled)
            return ESP_OK;
        if ((uint32_t)(now_ms() - started) >= timeout_ms) break;
        delay_ms(10U);
    }
    return ESP_ERR_TIMEOUT;
}

#else
esp_err_t cerberus_s3_ble_attack_start(const cerberus_s3_ble_attack_request_t *request,
    cerberus_s3_ble_attack_status_t *status)
{ (void)request; if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_attack_touch(uint16_t job_id)
{ (void)job_id; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_attack_stop(uint16_t job_id)
{ (void)job_id; return ESP_ERR_NOT_SUPPORTED; }
void cerberus_s3_ble_attack_get_status(cerberus_s3_ble_attack_status_t *status)
{ if (status) memset(status, 0, sizeof(*status)); }
esp_err_t cerberus_s3_ble_attack_quiesce(uint32_t timeout_ms)
{ (void)timeout_ms; return ESP_ERR_NOT_SUPPORTED; }
#endif
