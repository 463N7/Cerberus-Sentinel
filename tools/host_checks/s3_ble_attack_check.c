#ifdef NDEBUG
#undef NDEBUG
#endif
#include "fake_ble_attack_platform.h"
#include "s3_ble_attack.h"
#include "s3_ble_host.h"
#include "s3_wifi_owner.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t tick_ms, stop_at_ms, touch_at_ms, adv_expires_ms;
static uint32_t connect_at_ms, disconnect_at_ms, reset_at_ms;
static unsigned starts, stops, accepted_data, random_identity_sets, terminate_calls;
static unsigned host_epoch = 1U, connections_to_simulate;
static unsigned free_bytes = 128U * 1024U;
static int held, fail_start, stop_failures, fail_claim, terminate_failures;
static bool adv_active, host_initialized, host_synced = true, run_on_delay, freeze_expiry;
static bool connected, freeze_disconnect, saw_connectable, saw_nonconnectable;
static uint16_t connected_handle = 0x40U;
static int (*gap_callback)(struct ble_gap_event *, void *);
static void *gap_arg;
static void (*queued_worker)(void *);
static cerberus_s3_ble_owner_t owner;
static cerberus_s3_wifi_owner_t wifi_owner;
static uint16_t stopped_job, touched_job;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t mutex, unsigned ticks)
{ (void)mutex; (void)ticks; assert(!held); held = 1; return 1; }
int xSemaphoreGive(SemaphoreHandle_t mutex)
{ (void)mutex; assert(held); held = 0; return 1; }
BaseType_t xTaskCreate(void (*fn)(void *), const char *name, unsigned stack,
    void *arg, unsigned priority, void *handle)
{
    (void)name; (void)arg; (void)priority; (void)handle;
    assert(stack == 6144U && !queued_worker);
    queued_worker = fn;
    return pdPASS;
}
void vTaskDelete(void *task) { (void)task; }
int64_t esp_timer_get_time(void) { return (int64_t)tick_ms * 1000LL; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return free_bytes; }
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void) { return wifi_owner; }
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void) { return owner; }
bool cerberus_s3_ble_host_initialized(void) { return host_initialized; }
bool cerberus_s3_ble_host_synced(void) { return host_synced; }
unsigned cerberus_s3_ble_host_epoch(void) { return host_epoch; }
esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t requested)
{
    if (fail_claim) return ESP_ERR_INVALID_STATE;
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);
    assert(requested == CERBERUS_S3_BLE_OWNER_ADVERTISER);
    owner = requested; host_initialized = true;
    return ESP_OK;
}
esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t requested)
{
    assert(owner == requested && !adv_active && !connected);
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    return ESP_OK;
}
int ble_hs_id_set_rnd(const uint8_t *address)
{
    assert(!connected);
    assert((address[5] & 0xC0U) == 0xC0U);
    ++random_identity_sets;
    return 0;
}
int ble_gap_adv_set_data(const uint8_t *data, int length)
{
    assert(!connected);
    assert(length > 0 && length <= 31);
    assert(cerberus_ble_attack_ad_valid(data, (uint8_t)length));
    ++accepted_data;
    return 0;
}
int ble_gap_adv_start(uint8_t own_addr, const void *peer, int32_t duration,
    const struct ble_gap_adv_params *parameters,
    int (*callback)(struct ble_gap_event *, void *), void *arg)
{
    assert(owner == CERBERUS_S3_BLE_OWNER_ADVERTISER && !adv_active && !connected);
    assert(own_addr == BLE_OWN_ADDR_RANDOM && !peer);
    assert(parameters->conn_mode == BLE_GAP_CONN_MODE_NON ||
        parameters->conn_mode == BLE_GAP_CONN_MODE_UND);
    assert(parameters->disc_mode == BLE_GAP_DISC_MODE_GEN);
    assert(parameters->itvl_min == parameters->itvl_max);
    if (parameters->conn_mode == BLE_GAP_CONN_MODE_UND) {
        assert(parameters->itvl_min == 32U);
        saw_connectable = true;
        if (connections_to_simulate) connect_at_ms = tick_ms + 30U;
    } else {
        assert(parameters->itvl_min == 160U);
        saw_nonconnectable = true;
    }
    assert(duration >= 450 && duration <= 750);
    ++starts;
    if (fail_start) return 8;
    adv_active = true;
    adv_expires_ms = tick_ms + (uint32_t)duration;
    gap_callback = callback;
    gap_arg = arg;
    return 0;
}
int ble_gap_adv_stop(void)
{
    ++stops;
    if (stop_failures > 0) { --stop_failures; return 9; }
    adv_active = false;
    return 0;
}
int ble_gap_adv_active(void) { return adv_active; }
int ble_gap_terminate(uint16_t handle, uint8_t reason)
{
    ++terminate_calls;
    assert(handle == connected_handle && reason == BLE_ERR_REM_USER_CONN_TERM);
    if (!connected) return BLE_HS_ENOTCONN;
    if (terminate_failures > 0) { --terminate_failures; return 9; }
    if (!disconnect_at_ms) disconnect_at_ms = tick_ms + 60U;
    return 0;
}
void vTaskDelay(TickType_t ticks)
{
    tick_ms += ticks;
    if (reset_at_ms && tick_ms >= reset_at_ms) {
        reset_at_ms = 0U;
        host_synced = false;
        ++host_epoch;
        owner = CERBERUS_S3_BLE_OWNER_NONE;
        adv_active = connected = false;
        connect_at_ms = disconnect_at_ms = 0U;
    }
    if (connect_at_ms && tick_ms >= connect_at_ms && adv_active) {
        connect_at_ms = 0U;
        --connections_to_simulate;
        adv_active = false;
        connected = true;
        struct ble_gap_event event = {.type = BLE_GAP_EVENT_CONNECT,
            .connect = {.status = 0, .conn_handle = connected_handle}};
        assert(gap_callback && gap_callback(&event, gap_arg) == 0);
    }
    if (disconnect_at_ms && tick_ms >= disconnect_at_ms && !freeze_disconnect) {
        disconnect_at_ms = 0U;
        connected = false;
        struct ble_gap_event event = {.type = BLE_GAP_EVENT_DISCONNECT,
            .disconnect = {.conn = {.conn_handle = connected_handle}}};
        assert(gap_callback && gap_callback(&event, gap_arg) == 0);
    }
    if (adv_active && !freeze_expiry && tick_ms >= adv_expires_ms) adv_active = false;
    if (touch_at_ms && tick_ms >= touch_at_ms) {
        touch_at_ms = 0U;
        assert(cerberus_s3_ble_attack_touch(touched_job) == ESP_OK);
    }
    if (stop_at_ms && tick_ms >= stop_at_ms) {
        stop_at_ms = 0U;
        assert(cerberus_s3_ble_attack_stop(stopped_job) == ESP_OK);
    }
    if (run_on_delay) {
        run_on_delay = false;
        assert(queued_worker);
        void (*fn)(void *) = queued_worker;
        queued_worker = NULL;
        fn(NULL);
    }
}
static void run_worker(void)
{
    assert(queued_worker && !held);
    void (*fn)(void *) = queued_worker;
    queued_worker = NULL;
    fn(NULL);
    assert(!held);
}
static cerberus_s3_ble_attack_request_t request(uint16_t id, cerberus_ble_attack_mode_t mode)
{
    cerberus_s3_ble_attack_request_t value = {.job_id = id, .mode = mode, .seed = 0x1234U};
    return value;
}
static cerberus_s3_ble_attack_status_t start(cerberus_s3_ble_attack_request_t *value)
{
    cerberus_s3_ble_attack_status_t status;
    assert(cerberus_s3_ble_attack_start(value, &status) == ESP_OK);
    assert(status.job_id == value->job_id);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STARTING);
    assert(!status.packet_count_available);
    return status;
}
static uint32_t seed_with_connectable(cerberus_ble_attack_mode_t mode)
{
    for (uint32_t seed = 1U; seed < 10000U; ++seed) {
        cerberus_ble_attack_plan_t plan;
        assert(cerberus_ble_attack_plan_init(&plan, mode, seed, NULL) ==
            CERBERUS_BLE_ATTACK_OK);
        for (unsigned i = 0; i < 6U; ++i) {
            cerberus_ble_attack_frame_t frame;
            assert(cerberus_ble_attack_next(&plan, i * 250U, &frame) ==
                CERBERUS_BLE_ATTACK_OK);
            if (frame.connectable) return seed;
        }
    }
    assert(!"no connectable Apple Juice frame in initial cycle");
    return 0U;
}

int main(void)
{
    cerberus_s3_ble_attack_status_t status;
    cerberus_s3_ble_attack_request_t value = request(1U, CERBERUS_BLE_ATTACK_FIND_MY_SOUND);
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_NOT_SUPPORTED);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_GATT_REQUIRED);
    value = request(4U, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG);
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_INVALID_ARG);
    value.has_capture = true;
    value.capture.length = 3U;
    value.capture.data[0] = 2U;
    value.capture.data[1] = 1U;
    value.capture.data[2] = 6U;
    value.capture.address_type = 0U;
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_NOT_SUPPORTED);
    value.capture.address_type = 1U;
    value.capture.address[5] = 0x40U;
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_NOT_SUPPORTED);

    value = request(5U, CERBERUS_BLE_ATTACK_SWIFT_PAIR);
    wifi_owner = CERBERUS_S3_WIFI_RECEIVER;
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_INVALID_STATE);
    wifi_owner = CERBERUS_S3_WIFI_NONE;
    owner = CERBERUS_S3_BLE_OWNER_SCANNER;
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_INVALID_STATE);
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    free_bytes = 64000U;
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_NO_MEM);
    free_bytes = 128U * 1024U;
    fail_claim = 1;
    start(&value); run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_ERROR);
    assert(status.error == ESP_ERR_INVALID_STATE && !starts);
    fail_claim = 0;

    value.job_id = 6U;
    start(&value);
    assert(cerberus_s3_ble_attack_start(&value, &status) == ESP_ERR_INVALID_STATE);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 500U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED && status.error == ESP_OK);
    assert(status.planned_bursts > 0 && status.controller_accepted_bursts > 0);
    assert(status.controller_rejected_bursts == 0 && status.completed_bursts > 0);
    assert(starts == status.controller_accepted_bursts);
    assert(random_identity_sets == accepted_data && accepted_data == starts);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && !adv_active);
    assert(!status.packet_count_available);

    value.job_id = 7U;
    start(&value);
    uint32_t before = tick_ms;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_LEASE_EXPIRED);
    assert(tick_ms - before >= 5000U && tick_ms - before < 5300U);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && !adv_active);

    value.job_id = 8U;
    start(&value);
    touched_job = value.job_id;
    stopped_job = value.job_id;
    touch_at_ms = tick_ms + 4500U;
    stop_at_ms = tick_ms + 8000U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED);
    assert(status.elapsed_ms >= 8000U && status.controller_accepted_bursts > 0U);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);

    value.job_id = 13U;
    start(&value);
    fail_start = 1;
    run_worker();
    fail_start = 0;
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_ERROR);
    assert(status.last_controller_rc == 8 && status.controller_rejected_bursts == 1U);
    assert(status.controller_accepted_bursts == 0U && owner == CERBERUS_S3_BLE_OWNER_NONE);

    value.job_id = 9U;
    start(&value);
    stop_failures = 2;
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 250U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE && stops >= 3U);

    value.job_id = 10U;
    start(&value);
    assert(cerberus_s3_ble_attack_quiesce(0U) == ESP_ERR_TIMEOUT);
    run_on_delay = true;
    assert(cerberus_s3_ble_attack_quiesce(100U) == ESP_OK);
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);

    value = request(11U, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG);
    value.has_capture = true;
    value.capture.length = 3U;
    value.capture.data[0] = 2U;
    value.capture.data[1] = 1U;
    value.capture.data[2] = 6U;
    value.capture.address_type = 1U;
    value.capture.address[5] = 0xC0U;
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 250U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED);
    assert(status.last_payload == CERBERUS_BLE_PAYLOAD_CAPTURED_TRACKER);
    assert(status.controller_accepted_bursts > 0U);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);

    value = request(14U, CERBERUS_BLE_ATTACK_APPLE_JUICE);
    value.seed = seed_with_connectable(value.mode);
    connections_to_simulate = 1U;
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 1000U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED && status.error == ESP_OK);
    assert(saw_connectable && saw_nonconnectable && terminate_calls > 0U);
    assert(status.connections_accepted == 1U && status.connections_closed == 1U);
    assert(!status.connection_active && !connected && owner == CERBERUS_S3_BLE_OWNER_NONE);

    value = request(15U, CERBERUS_BLE_ATTACK_ALL);
    value.seed = seed_with_connectable(value.mode);
    connections_to_simulate = 1U;
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 3000U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED && status.error == ESP_OK);
    assert(status.connections_accepted == 1U && status.connections_closed == 1U);
    assert(status.controller_accepted_bursts >= 5U);
    assert(!status.connection_active && !connected && owner == CERBERUS_S3_BLE_OWNER_NONE);

    value = request(16U, CERBERUS_BLE_ATTACK_APPLE_JUICE);
    value.seed = seed_with_connectable(value.mode);
    connections_to_simulate = 1U;
    terminate_failures = 1;
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 300U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_STOPPED);
    assert(status.connection_terminate_errors == 1U && status.connections_closed == 1U);
    assert(owner == CERBERUS_S3_BLE_OWNER_NONE);

    value = request(17U, CERBERUS_BLE_ATTACK_APPLE_JUICE);
    value.seed = seed_with_connectable(value.mode);
    connections_to_simulate = 1U;
    start(&value);
    reset_at_ms = tick_ms + 70U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_ERROR && status.error == ESP_FAIL);
    assert(!status.connection_active && !connected && owner == CERBERUS_S3_BLE_OWNER_NONE);
    host_synced = true;

    value = request(18U, CERBERUS_BLE_ATTACK_APPLE_JUICE);
    value.seed = seed_with_connectable(value.mode);
    connections_to_simulate = 1U;
    freeze_disconnect = true;
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 150U;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_ERROR && status.error == ESP_ERR_TIMEOUT);
    assert(status.connection_active && connected);
    assert(owner == CERBERUS_S3_BLE_OWNER_ADVERTISER);
    assert(cerberus_s3_ble_attack_quiesce(50U) == ESP_ERR_TIMEOUT);
    freeze_disconnect = false;
    vTaskDelay(1U);
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.connections_accepted == 1U && status.connections_closed == 1U);
    assert(!status.connection_active && !connected && owner == CERBERUS_S3_BLE_OWNER_NONE);
    assert(cerberus_s3_ble_attack_quiesce(0U) == ESP_OK);

    value = request(12U, CERBERUS_BLE_ATTACK_SAMSUNG);
    start(&value);
    stopped_job = value.job_id;
    stop_at_ms = tick_ms + 250U;
    stop_failures = 1000;
    freeze_expiry = true;
    run_worker();
    cerberus_s3_ble_attack_get_status(&status);
    assert(status.state == CERBERUS_S3_BLE_ATTACK_ERROR);
    assert(status.error == ESP_ERR_TIMEOUT);
    assert(owner == CERBERUS_S3_BLE_OWNER_ADVERTISER && adv_active);
    assert(cerberus_s3_ble_attack_quiesce(50U) == ESP_ERR_TIMEOUT);
    puts("S3 BLE attack runtime checks passed: gating, connectable links, disconnect before release, late release, reset, Stop, lease and controller faults.");
}
