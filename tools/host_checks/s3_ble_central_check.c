#include "fake_ble_central_platform.h"
#include "s3_ble_central.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "s3_wifi_owner.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cerberus_s3_ble_owner_t owner;
static unsigned epoch;
static cerberus_s3_wifi_owner_t wifi_owner;
static struct ble_npl_eventq queue_object;
static struct ble_npl_event *queue[16];
static unsigned queue_count;
static ble_gap_event_fn gap_callback;
static void *gap_arg;
static bool initiating;
static uint8_t connected_address[6];
static uint16_t connection_handle = 37U;
static ble_gatt_svc_fn service_callback;
static void *service_arg;
static ble_gatt_chr_fn characteristic_callback;
static void *characteristic_arg;
static ble_gatt_read_fn read_callback;
static void *read_arg;
static uint16_t last_value_handle;
static unsigned scanner_stops;
static uint32_t fake_now_ms = 1000U;
static ble_uuid_t requested_service_uuid;
static ble_gatt_dsc_fn descriptor_callback;
static void *descriptor_arg;
static uint16_t descriptor_first, descriptor_last;
static ble_gatt_read_fn write_callback;
static void *write_arg;
static uint16_t write_handle, write_length;
static uint8_t write_data[8];
static unsigned write_count;
static unsigned mutex_creates, mutex_takes, mutex_gives, mutex_depth;
static unsigned connect_calls, cancel_calls, terminate_calls, claim_calls, release_calls;
static unsigned event_puts, gatt_calls;
static int cancel_rc, terminate_rc;
static bool defer_cancel, defer_disconnect;
static unsigned epoch_reads, reset_on_epoch_read;
static bool reset_every_epoch_read;

SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++mutex_creates; return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t sem, unsigned delay)
{ CHECK(sem && delay == portMAX_DELAY && mutex_depth == 0U); ++mutex_takes; ++mutex_depth; return 1; }
int xSemaphoreGive(SemaphoreHandle_t sem)
{ CHECK(sem && mutex_depth == 1U); ++mutex_gives; --mutex_depth; return 1; }
void vTaskDelay(unsigned ticks) { (void)ticks; }
int64_t esp_timer_get_time(void) { return (int64_t)fake_now_ms * 1000LL; }
size_t heap_caps_get_free_size(unsigned caps) { (void)caps; return 120000U; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "fake"; }
void ble_npl_event_init(struct ble_npl_event *event,
    void (*fn)(struct ble_npl_event *), void *arg)
{ event->fn = fn; event->arg = arg; event->queued = false; }
void ble_npl_event_set_arg(struct ble_npl_event *event, void *arg) { event->arg = arg; }
void *ble_npl_event_get_arg(struct ble_npl_event *event) { return event->arg; }
void ble_npl_eventq_put(struct ble_npl_eventq *eventq, struct ble_npl_event *event)
{
    CHECK(eventq == &queue_object);
    ++event_puts;
    if (!event->queued) { CHECK(queue_count < 16U); queue[queue_count++] = event; event->queued = true; }
}
struct ble_npl_eventq *nimble_port_get_dflt_eventq(void) { return &queue_object; }
static void drain(void)
{
    while (queue_count) {
        struct ble_npl_event *event = queue[0];
        memmove(queue, queue + 1, --queue_count * sizeof(queue[0]));
        event->queued = false;
        event->fn(event);
    }
}
int ble_hs_id_infer_auto(int privacy, uint8_t *type)
{ CHECK(!privacy); *type = 0; return 0; }
int ble_gap_connect(uint8_t own, const ble_addr_t *peer, int32_t timeout,
    const void *parameters, ble_gap_event_fn callback, void *arg)
{
    (void)own; (void)parameters;
    ++connect_calls;
    CHECK(timeout == 15000);
    CHECK(peer->type <= 1U);
    memcpy(connected_address, peer->val, 6U);
    gap_callback = callback; gap_arg = arg; initiating = true;
    return 0;
}
int ble_gap_conn_cancel(void)
{
    ++cancel_calls;
    if (cancel_rc != 0) return cancel_rc;
    if (!initiating) return 7;
    if (defer_cancel) return 0;
    initiating = false;
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_CONNECT, .connect.status = 7};
    gap_callback(&event, gap_arg);
    return 0;
}
int ble_gap_conn_active(void) { return initiating; }
int ble_gap_terminate(uint16_t handle, uint8_t reason)
{
    ++terminate_calls;
    CHECK(handle == connection_handle && reason == BLE_ERR_REM_USER_CONN_TERM);
    if (terminate_rc != 0) return terminate_rc;
    if (defer_disconnect) return 0;
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_DISCONNECT};
    event.disconnect.conn.conn_handle = handle;
    event.disconnect.reason = reason;
    gap_callback(&event, gap_arg);
    return 0;
}
int os_mbuf_copydata(const struct os_mbuf *mbuf, int offset, int length, void *target)
{
    CHECK(offset >= 0 && length >= 0 && (size_t)(offset + length) <= mbuf->len);
    memcpy(target, mbuf->data + offset, (size_t)length);
    return 0;
}
int ble_uuid_from_str(ble_uuid_any_t *uuid, const char *value)
{
    static const char *const known[] = {
        "7dfc9000-7d1c-4951-86aa-8d9728f8d66c",
        "7dfc9001-7d1c-4951-86aa-8d9728f8d66c",
        "0000fd44-0000-1000-8000-00805f9b34fb",
        "4f860003-943b-49ef-bed4-2f730304427a",
        "15190001-12f4-c226-88ed-2ac5579f2a85",
        "8e0c0001-1d68-fb92-bf61-48377421680e",
    };
    for (unsigned i = 0; i < sizeof(known) / sizeof(known[0]); ++i) {
        if (strcmp(value, known[i]) == 0) {
            memset(uuid, 0, sizeof(*uuid));
            uuid->u.type = BLE_UUID_TYPE_128;
            uuid->u.v128[0] = (uint8_t)(i + 1U);
            return 0;
        }
    }
    return BLE_HS_EINVAL;
}
int ble_uuid_cmp(const ble_uuid_t *first, const ble_uuid_t *second)
{
    if (first->type != second->type) return 1;
    if (first->type == BLE_UUID_TYPE_16) return first->v16 != second->v16;
    return memcmp(first->v128, second->v128, sizeof(first->v128));
}
int ble_gattc_disc_all_svcs(uint16_t handle, ble_gatt_svc_fn callback, void *arg)
{ ++gatt_calls; CHECK(handle == connection_handle); service_callback = callback; service_arg = arg; return 0; }
int ble_gattc_disc_svc_by_uuid(uint16_t handle, const ble_uuid_t *uuid,
    ble_gatt_svc_fn callback, void *arg)
{
    ++gatt_calls;
    CHECK(handle == connection_handle && uuid);
    requested_service_uuid = *uuid;
    service_callback = callback; service_arg = arg;
    return 0;
}
int ble_gattc_disc_all_chrs(uint16_t handle, uint16_t first, uint16_t last,
    ble_gatt_chr_fn callback, void *arg)
{
    ++gatt_calls;
    CHECK(handle == connection_handle && first <= last);
    characteristic_callback = callback; characteristic_arg = arg; return 0;
}
int ble_gattc_disc_all_dscs(uint16_t handle, uint16_t first, uint16_t last,
    ble_gatt_dsc_fn callback, void *arg)
{
    ++gatt_calls;
    CHECK(handle == connection_handle && first < last);
    descriptor_first = first; descriptor_last = last;
    descriptor_callback = callback; descriptor_arg = arg;
    return 0;
}
int ble_gattc_read(uint16_t handle, uint16_t value_handle,
    ble_gatt_read_fn callback, void *arg)
{
    ++gatt_calls;
    CHECK(handle == connection_handle);
    last_value_handle = value_handle;
    read_callback = callback; read_arg = arg; return 0;
}
int ble_gattc_write_flat(uint16_t handle, uint16_t attribute,
    const void *data, uint16_t length, ble_gatt_read_fn callback, void *arg)
{
    ++gatt_calls;
    CHECK(handle == connection_handle && data && length <= sizeof(write_data));
    write_handle = attribute; write_length = length;
    memcpy(write_data, data, length);
    write_callback = callback; write_arg = arg;
    ++write_count;
    return 0;
}
esp_err_t cerberus_s3_ble_host_claim(cerberus_s3_ble_owner_t value)
{ ++claim_calls; if (owner != CERBERUS_S3_BLE_OWNER_NONE) return ESP_ERR_INVALID_STATE; owner = value; return ESP_OK; }
esp_err_t cerberus_s3_ble_host_release(cerberus_s3_ble_owner_t value)
{ ++release_calls; if (owner != value) return ESP_ERR_INVALID_STATE; owner = CERBERUS_S3_BLE_OWNER_NONE; return ESP_OK; }
cerberus_s3_ble_owner_t cerberus_s3_ble_host_owner(void) { return owner; }
bool cerberus_s3_ble_host_initialized(void) { return true; }
bool cerberus_s3_ble_host_synced(void) { return true; }
unsigned cerberus_s3_ble_host_epoch(void)
{
    ++epoch_reads;
    if (reset_every_epoch_read || (reset_on_epoch_read && epoch_reads == reset_on_epoch_read)) {
        ++epoch;
        owner = CERBERUS_S3_BLE_OWNER_NONE;
    }
    return epoch;
}
cerberus_s3_wifi_owner_t cerberus_s3_wifi_owner(void) { return wifi_owner; }
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status)
{ *status = (cerberus_ble_scan_status_payload_t){.state = CERBERUS_BLE_SCAN_RUNNING}; return ESP_OK; }
esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *status)
{ ++scanner_stops; owner = CERBERUS_S3_BLE_OWNER_NONE; status->state = CERBERUS_BLE_SCAN_IDLE; return ESP_OK; }

static void connected(void)
{
    CHECK(initiating);
    initiating = false;
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_CONNECT};
    event.connect.conn_handle = connection_handle;
    gap_callback(&event, gap_arg);
}

static unsigned work_calls(void)
{
    return connect_calls + cancel_calls + terminate_calls + claim_calls +
        release_calls + event_puts + gatt_calls + write_count + scanner_stops;
}

static cerberus_s3_ble_central_diagnostics_t diagnostic(void)
{
    unsigned before = work_calls(), creates = mutex_creates;
    cerberus_s3_ble_central_diagnostics_t result;
    memset(&result, 0xa5, sizeof(result));
    CHECK(cerberus_s3_ble_central_get_diagnostics(&result) == ESP_OK);
    CHECK(result.reserved == 0U && result.host_epoch == epoch &&
        result.host_owner == (uint8_t)owner);
    CHECK(work_calls() == before && mutex_creates == creates && mutex_depth == 0U);
    return result;
}

static void check_diagnostics_before_init(void)
{
    CHECK(sizeof(cerberus_s3_ble_central_diagnostics_t) == 16U);
    CHECK(cerberus_s3_ble_central_get_diagnostics(NULL) == ESP_ERR_INVALID_ARG);
    for (unsigned i = CERBERUS_S3_BLE_OWNER_NONE; i <= CERBERUS_S3_BLE_OWNER_ADVERTISER; ++i) {
        owner = (cerberus_s3_ble_owner_t)i;
        cerberus_s3_ble_central_diagnostics_t result = diagnostic();
        CHECK(result.state == CERBERUS_BLE_CONNECTION_IDLE && !result.handle_present &&
            result.generation == 0U && result.error_code == 0);
        CHECK(result.service_state == CERBERUS_BLE_GATT_SERVICE_IDLE &&
            result.characteristic_state == CERBERUS_BLE_GATT_CHARACTERISTIC_IDLE &&
            result.read_state == CERBERUS_BLE_GATT_READ_IDLE);
    }
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    CHECK(mutex_creates == 0U && mutex_takes == 0U && mutex_gives == 0U && work_calls() == 0U);
}

static void disconnected(uint16_t handle)
{
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_DISCONNECT};
    event.disconnect.conn.conn_handle = handle;
    event.disconnect.reason = BLE_ERR_REM_USER_CONN_TERM;
    gap_callback(&event, gap_arg);
}

static void check_generic_recovery(void)
{
    cerberus_c5_ble_result_t peer = {.address = {4, 3, 2, 1, 9, 8},
        .address_type = 1U, .flags = CERBERUS_BLE_RESULT_CONNECTABLE};
    cerberus_ble_connection_status_payload_t status;
    cerberus_s3_ble_central_diagnostics_t d;
    unsigned writes_before = write_count, gatt_before = gatt_calls;

    /* A rejected cancel remains owned; only a new explicit request retries. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_CONNECTING && !d.handle_present);
    drain();
    cancel_rc = 22;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    unsigned cancel_before = cancel_calls, release_before = release_calls;
    for (unsigned i = 0U; i < 4U; ++i) {
        d = diagnostic();
        CHECK(d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING && d.error_code == 22 &&
            !d.handle_present && d.host_owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
        CHECK(cerberus_s3_ble_central_get_status(&status) == ESP_OK && status.error_code == 22);
    }
    CHECK(cancel_calls == cancel_before && release_calls == release_before);
    cancel_rc = 0; defer_cancel = true;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK && status.error_code == 0);
    drain();
    CHECK(cancel_calls == cancel_before + 1U && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING && !d.handle_present);
    initiating = false;
    struct ble_gap_event cancelled = {.type = BLE_GAP_EVENT_CONNECT, .connect.status = 7};
    gap_callback(&cancelled, gap_arg);
    defer_cancel = false;
    d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_IDLE && !d.handle_present &&
        owner == CERBERUS_S3_BLE_OWNER_NONE && release_calls == release_before + 1U);

    /* An accepted termination is still pending until the matching GAP event.
     * Both immediate rejection and TERM_FAILURE remain visible and retryable. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    drain(); connected();
    terminate_rc = 55;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    unsigned terminate_before = terminate_calls;
    release_before = release_calls;
    for (unsigned i = 0U; i < 4U; ++i) {
        d = diagnostic();
        CHECK(d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING && d.handle_present &&
            d.error_code == 55 && d.host_owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    }
    CHECK(terminate_calls == terminate_before && release_calls == release_before);
    terminate_rc = 0; defer_disconnect = true;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING && d.error_code == 0 && d.handle_present);
    struct ble_gap_event failed = {.type = BLE_GAP_EVENT_TERM_FAILURE, .term_failure.status = 9};
    gap_callback(&failed, gap_arg);
    d = diagnostic();
    CHECK(d.error_code == 9 && d.host_owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK && status.error_code == 0);
    drain();
    CHECK(terminate_calls == terminate_before + 2U && release_calls == release_before);
    disconnected((uint16_t)(connection_handle + 1U));
    CHECK(owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    disconnected(connection_handle);
    CHECK(owner == CERBERUS_S3_BLE_OWNER_NONE && release_calls == release_before + 1U);
    defer_disconnect = false;

    /* Connect can succeed after cancellation was sent. Rejected immediate
     * cleanup must retain the new handle and surface its error, not just log. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    drain(); defer_cancel = true;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    terminate_rc = 37;
    connected();
    d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING && d.error_code == 37 &&
        d.handle_present && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    terminate_before = terminate_calls;
    (void)diagnostic(); (void)diagnostic();
    CHECK(terminate_calls == terminate_before);
    ble_gap_event_fn old_callback = gap_callback; void *old_arg = gap_arg;
    terminate_rc = 0; defer_cancel = false; defer_disconnect = true;
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    CHECK(terminate_calls == terminate_before + 1U && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    disconnected(connection_handle);

    /* An old termination callback cannot overwrite the new generation. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    drain(); connected();
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain(); old_callback(&failed, old_arg);
    d = diagnostic();
    CHECK(d.error_code == 0 && d.state == CERBERUS_BLE_CONNECTION_DISCONNECTING);
    disconnected(connection_handle); defer_disconnect = false;

    /* Reset between snapshot epoch reads retries locally, never controller
     * work. A stale lease projects an error, not CONNECTED with a new owner. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    drain(); connected();
    reset_on_epoch_read = epoch_reads + 2U;
    d = diagnostic(); reset_on_epoch_read = 0U;
    CHECK(d.state == CERBERUS_BLE_CONNECTION_ERROR && d.error_code == ESP_ERR_INVALID_STATE &&
        !d.handle_present && d.host_owner == CERBERUS_S3_BLE_OWNER_NONE &&
        d.service_state == CERBERUS_BLE_GATT_SERVICE_IDLE &&
        d.characteristic_state == CERBERUS_BLE_GATT_CHARACTERISTIC_IDLE &&
        d.read_state == CERBERUS_BLE_GATT_READ_IDLE);
    unsigned work_before = work_calls(), epoch_before = epoch_reads;
    reset_every_epoch_read = true;
    memset(&d, 0xa5, sizeof(d));
    CHECK(cerberus_s3_ble_central_get_diagnostics(&d) == ESP_ERR_INVALID_STATE);
    reset_every_epoch_read = false;
    const cerberus_s3_ble_central_diagnostics_t zero = {0};
    CHECK(memcmp(&d, &zero, sizeof(d)) == 0 && epoch_reads == epoch_before + 6U &&
        work_calls() == work_before && mutex_depth == 0U);
    CHECK(cerberus_s3_ble_central_connect(&peer, &status) == ESP_OK);
    drain(); connected();
    CHECK(cerberus_s3_ble_central_disconnect(&status) == ESP_OK);
    drain();
    CHECK(owner == CERBERUS_S3_BLE_OWNER_NONE && write_count == writes_before &&
        gatt_calls == gatt_before && mutex_takes == mutex_gives);
    puts("Generic BLE cancel/terminate errors, explicit retry, asynchronous release and read-only diagnostics passed.");
}

static cerberus_c5_ble_result_t sound_peer(void)
{
    cerberus_c5_ble_result_t peer = {.address = {9, 8, 7, 6, 5, 4},
        .address_type = 1, .flags = CERBERUS_BLE_RESULT_CONNECTABLE};
    return peer;
}

static cerberus_s3_ble_sound_status_t sound_begin(cerberus_findmy_profile_t profile)
{
    cerberus_s3_ble_sound_status_t sound;
    cerberus_c5_ble_result_t peer = sound_peer();
    CHECK(cerberus_s3_ble_central_sound_start(&peer, profile, &sound) == ESP_OK);
    CHECK(sound.state == CERBERUS_S3_BLE_SOUND_CONNECTING &&
        sound.profile == profile && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    drain(); connected(); drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE);
    return sound;
}

static void sound_present_service(uint8_t marker)
{
    struct ble_gatt_error ok = {0}, done = {.status = BLE_HS_EDONE};
    CHECK(requested_service_uuid.type == BLE_UUID_TYPE_128 &&
        requested_service_uuid.v128[0] == marker);
    struct ble_gatt_svc service = {.start_handle = 1U, .end_handle = 20U};
    service.uuid.u = requested_service_uuid;
    service_callback(connection_handle, &ok, &service, service_arg);
    service_callback(connection_handle, &done, NULL, service_arg);
    drain();
}

static void sound_present_characteristic(uint8_t marker, uint8_t properties,
    bool following_characteristic)
{
    struct ble_gatt_error ok = {0}, done = {.status = BLE_HS_EDONE};
    struct ble_gatt_chr characteristic = {.def_handle = 2U,
        .val_handle = 3U, .properties = properties};
    characteristic.uuid.u.type = BLE_UUID_TYPE_128;
    characteristic.uuid.u.v128[0] = marker;
    characteristic_callback(connection_handle, &ok, &characteristic,
        characteristic_arg);
    if (following_characteristic) {
        characteristic.def_handle = 8U;
        characteristic.val_handle = 9U;
        characteristic.uuid.u.v128[0] = 77U;
        characteristic_callback(connection_handle, &ok, &characteristic,
            characteristic_arg);
    }
    characteristic_callback(connection_handle, &done, NULL, characteristic_arg);
    drain();
}

static void sound_present_cccd(void)
{
    struct ble_gatt_error ok = {0}, done = {.status = BLE_HS_EDONE};
    struct ble_gatt_dsc descriptor = {.handle = 4U};
    descriptor.uuid.u.type = BLE_UUID_TYPE_16;
    descriptor.uuid.u.v16 = 0x2902U;
    CHECK(descriptor_first == 3U && descriptor_last == 7U);
    descriptor_callback(connection_handle, &ok, 3U, &descriptor, descriptor_arg);
    descriptor_callback(connection_handle, &done, 3U, NULL, descriptor_arg);
    drain();
}

static void sound_ack_write(void)
{
    struct ble_gatt_error ok = {0};
    CHECK(write_callback);
    write_callback(connection_handle, &ok, NULL, write_arg);
    drain();
}

static void sound_notify(uint16_t attribute, const uint8_t *data, size_t length)
{
    struct os_mbuf mbuf = {.len = length};
    CHECK(length <= sizeof(mbuf.data));
    memcpy(mbuf.data, data, length);
    struct ble_gap_event event = {.type = BLE_GAP_EVENT_NOTIFY_RX};
    event.notify_rx.conn_handle = connection_handle;
    event.notify_rx.attr_handle = attribute;
    event.notify_rx.om = &mbuf;
    gap_callback(&event, gap_arg);
}

static void check_sound_jobs(void)
{
    cerberus_s3_ble_sound_status_t sound;
    cerberus_c5_ble_result_t peer = sound_peer();
    CHECK(cerberus_s3_ble_central_sound_start(&peer,
        CERBERUS_FINDMY_PROFILE_NONE, &sound) == ESP_ERR_INVALID_ARG);
    peer.flags = 0U;
    CHECK(cerberus_s3_ble_central_sound_start(&peer,
        CERBERUS_FINDMY_PROFILE_AIRTAG, &sound) == ESP_ERR_INVALID_ARG);

    /* AirTag: exact UUID pair, one-byte write, ATT ACK, then host release. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_AIRTAG);
    cerberus_ble_gatt_service_status_payload_t generic_services;
    CHECK(cerberus_s3_ble_central_start_service_discovery(&generic_services) ==
        ESP_ERR_INVALID_STATE);
    sound_present_service(1U);
    sound_present_characteristic(2U, BLE_GATT_CHR_PROP_WRITE, false);
    CHECK(write_count == 1U && write_handle == 3U &&
        write_length == 1U && write_data[0] == 0xAFU);
    sound_ack_write();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_COMPLETE && sound.write_accepted &&
        !sound.response_received && owner == CERBERUS_S3_BLE_OWNER_NONE);

    /* FMNA: CCCD indications must be ACKed before the command write. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_FMNA);
    sound_present_service(3U);
    sound_present_characteristic(4U,
        BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_INDICATE, true);
    sound_present_cccd();
    CHECK(write_count == 2U && write_handle == 4U &&
        write_length == 2U && write_data[0] == 2U && write_data[1] == 0U);
    sound_ack_write();
    CHECK(write_count == 3U && write_handle == 3U && write_length == 3U &&
        write_data[0] == 1U && write_data[1] == 0U && write_data[2] == 3U);
    sound_ack_write();
    uint8_t response[] = {0x99U};
    sound_notify(10U, response, sizeof(response)); /* Wrong handle ignored. */
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        !sound.response_received);
    sound_notify(3U, response, sizeof(response));
    fake_now_ms += 2000U;
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPING);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_COMPLETE && sound.write_accepted &&
        sound.response_received && !sound.completion_reported);

    /* A same-handle indication before the command ACK is not a response.
     * If the ATT write then fails, no response evidence may survive. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_FMNA);
    sound_present_service(3U);
    sound_present_characteristic(4U,
        BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_INDICATE, true);
    sound_present_cccd();
    sound_ack_write();
    CHECK(write_handle == 3U && write_length == 3U);
    sound_notify(3U, response, sizeof(response));
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_WRITING &&
        !sound.write_accepted && !sound.response_received);
    struct ble_gatt_error rejected_write = {.status = 0x0EU};
    write_callback(connection_handle, &rejected_write, NULL, write_arg);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.failed_stage == CERBERUS_S3_BLE_SOUND_WRITING &&
        sound.last_host_rc == 0x0EU && !sound.write_accepted &&
        !sound.response_received && owner == CERBERUS_S3_BLE_OWNER_NONE);

    /* DULT: notification subscription and exact completion response. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_DULT);
    sound_present_service(5U);
    sound_present_characteristic(6U,
        BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_NOTIFY, true);
    sound_present_cccd();
    CHECK(write_handle == 4U && write_length == 2U && write_data[0] == 1U);
    sound_ack_write();
    CHECK(write_handle == 3U && write_length == 2U &&
        write_data[0] == 0U && write_data[1] == 3U);
    sound_ack_write();
    uint8_t completion[] = {2U, 3U};
    sound_notify(3U, completion, sizeof(completion));
    fake_now_ms += 3000U;
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_COMPLETE &&
        sound.response_received && sound.completion_reported);

    /* Missing service and missing write property fail without a write. */
    unsigned before = write_count;
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_AIRTAG);
    struct ble_gatt_error done = {.status = BLE_HS_EDONE};
    service_callback(connection_handle, &done, NULL, service_arg);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.error_code == ESP_ERR_NOT_FOUND &&
        sound.failed_stage == CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE &&
        write_count == before);
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_AIRTAG);
    sound_present_service(1U);
    sound_present_characteristic(2U, BLE_GATT_CHR_PROP_READ, false);
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR && write_count == before);

    /* Stop during discovery invalidates a queued callback and releases host. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_FMNA);
    ble_gatt_svc_fn stale_service = service_callback;
    void *stale_arg = service_arg;
    CHECK(cerberus_s3_ble_central_sound_stop(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPING);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPED &&
        owner == CERBERUS_S3_BLE_OWNER_NONE);
    struct ble_gatt_svc stale = {.start_handle = 1U, .end_handle = 20U};
    stale.uuid.u = requested_service_uuid;
    struct ble_gatt_error ok = {0};
    stale_service(connection_handle, &ok, &stale, stale_arg);
    CHECK(write_count == before);

    /* Stop before GAP connect reaches the host task. */
    peer = sound_peer();
    CHECK(cerberus_s3_ble_central_sound_start(&peer,
        CERBERUS_FINDMY_PROFILE_AIRTAG, &sound) == ESP_OK);
    CHECK(cerberus_s3_ble_central_sound_stop(&sound) == ESP_OK);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPED &&
        owner == CERBERUS_S3_BLE_OWNER_NONE && write_count == before);

    /* A missing CCCD cannot fall through to a command write. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_FMNA);
    sound_present_service(3U);
    sound_present_characteristic(4U,
        BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_NOTIFY, true);
    descriptor_callback(connection_handle, &done, 3U, NULL, descriptor_arg);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.error_code == ESP_ERR_NOT_FOUND && write_count == before);

    /* Stop after CCCD ACK but before its queued command: no command submitted. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_FMNA);
    sound_present_service(3U);
    sound_present_characteristic(4U,
        BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_NOTIFY, true);
    sound_present_cccd();
    CHECK(write_count == before + 1U && write_handle == 4U);
    write_callback(connection_handle, &ok, NULL, write_arg);
    CHECK(cerberus_s3_ble_central_sound_stop(&sound) == ESP_OK);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPED &&
        write_count == before + 1U);
    before = write_count;

    /* A stalled GATT service discovery times out and releases the host. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_DULT);
    fake_now_ms += 6000U;
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPING);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.error_code == ESP_ERR_TIMEOUT &&
        sound.failed_stage == CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE &&
        write_count == before);

    /* Host status can numerically overlap ESP_ERR_NOT_FOUND: keep it separate. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_DULT);
    struct ble_gatt_error att_error = {.status = ESP_ERR_NOT_FOUND};
    service_callback(connection_handle, &att_error, NULL, service_arg);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.error_code == ESP_FAIL &&
        sound.last_host_rc == ESP_ERR_NOT_FOUND);

    /* GAP connect failure also releases ownership and records the host RC. */
    peer = sound_peer();
    CHECK(cerberus_s3_ble_central_sound_start(&peer,
        CERBERUS_FINDMY_PROFILE_AIRTAG, &sound) == ESP_OK);
    drain();
    initiating = false;
    struct ble_gap_event failed_connect = {.type = BLE_GAP_EVENT_CONNECT};
    failed_connect.connect.status = 22;
    gap_callback(&failed_connect, gap_arg);
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.failed_stage == CERBERUS_S3_BLE_SOUND_CONNECTING &&
        sound.error_code == ESP_FAIL && sound.last_host_rc == 22 &&
        owner == CERBERUS_S3_BLE_OWNER_NONE);

    /* A controller reset invalidates the sound lease and callbacks. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_AIRTAG);
    ble_gatt_svc_fn pre_reset = service_callback;
    void *pre_reset_arg = service_arg;
    owner = CERBERUS_S3_BLE_OWNER_NONE;
    ++epoch;
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_ERROR &&
        sound.error_code == ESP_ERR_INVALID_STATE);
    pre_reset(connection_handle, &ok, &stale, pre_reset_arg);
    CHECK(write_count == before);

    /* A newly selected peer can run after the reset. */
    (void)sound_begin(CERBERUS_FINDMY_PROFILE_AIRTAG);
    CHECK(cerberus_s3_ble_central_sound_stop(&sound) == ESP_OK);
    drain();
    CHECK(cerberus_s3_ble_central_sound_get_status(&sound) == ESP_OK &&
        sound.state == CERBERUS_S3_BLE_SOUND_STOPPED && write_count == before);
    puts("S3 Find My Sound exact GATT sequence, response evidence and Stop cleanup passed.");
}

int main(void)
{
    check_diagnostics_before_init();
    cerberus_c5_ble_result_t peer = {.address = {1, 2, 3, 4, 5, 6},
        .address_type = 0, .flags = CERBERUS_BLE_RESULT_CONNECTABLE};
    cerberus_ble_connection_status_payload_t connection;
    owner = CERBERUS_S3_BLE_OWNER_SCANNER;
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_OK);
    CHECK(scanner_stops == 1U && owner == CERBERUS_S3_BLE_OWNER_CENTRAL);
    CHECK(connection.state == CERBERUS_BLE_CONNECTION_CONNECTING);
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_ERR_INVALID_STATE);
    drain();
    CHECK(memcmp(connected_address, peer.address, 6U) == 0);
    connected();
    CHECK(cerberus_s3_ble_central_get_status(&connection) == ESP_OK);
    CHECK(connection.state == CERBERUS_BLE_CONNECTION_CONNECTED);
    CHECK(cerberus_s3_ble_central_start_service_discovery(NULL) == ESP_ERR_INVALID_ARG);
    cerberus_ble_gatt_service_status_payload_t services;
    CHECK(cerberus_s3_ble_central_start_service_discovery(&services) == ESP_OK);
    CHECK(diagnostic().service_state == CERBERUS_BLE_GATT_SERVICE_RUNNING);
    drain();
    struct ble_gatt_svc svc = {.start_handle = 1U, .end_handle = 20U};
    svc.uuid.u.type = BLE_UUID_TYPE_16; svc.uuid.u.v16 = 0x181AU;
    struct ble_gatt_error ok = {0}, done = {.status = BLE_HS_EDONE};
    service_callback(connection_handle, &ok, &svc, service_arg);
    service_callback(connection_handle, &done, NULL, service_arg);
    CHECK(cerberus_s3_ble_central_get_service_status(&services) == ESP_OK);
    CHECK(services.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE &&
        services.total_count == 1U && services.stored_count == 1U);
    cerberus_c5_ble_service_t saved_service;
    CHECK(cerberus_s3_ble_central_get_service_result(services.generation, 0U, &saved_service) == ESP_OK);
    CHECK(saved_service.start_handle == 1U && saved_service.end_handle == 20U &&
        saved_service.uuid_length == 2U && saved_service.uuid[0] == 0x1AU);
    CHECK(cerberus_s3_ble_central_get_service_result((uint8_t)(services.generation - 1U),
        0U, &saved_service) == ESP_ERR_INVALID_STATE);
    ble_gatt_svc_fn stale_service_callback = service_callback;
    void *stale_service_arg = service_arg;
    CHECK(cerberus_s3_ble_central_start_service_discovery(&services) == ESP_OK);
    drain();
    stale_service_callback(connection_handle, &ok, &svc, stale_service_arg);
    CHECK(cerberus_s3_ble_central_get_service_status(&services) == ESP_OK &&
        services.total_count == 0U);
    for (unsigned i = 0; i < 30U; ++i)
        service_callback(connection_handle, &ok, &svc, service_arg);
    service_callback(connection_handle, &done, NULL, service_arg);
    CHECK(cerberus_s3_ble_central_get_service_status(&services) == ESP_OK &&
        services.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE &&
        services.total_count == 30U && services.stored_count == 24U);
    CHECK(cerberus_s3_ble_central_get_service_result(services.generation,
        0U, &saved_service) == ESP_OK);
    cerberus_ble_gatt_characteristic_status_payload_t characteristics;
    CHECK(cerberus_s3_ble_central_start_characteristic_discovery(&saved_service,
        &characteristics) == ESP_OK);
    CHECK(diagnostic().characteristic_state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING);
    drain();
    struct ble_gatt_chr chr = {.def_handle = 2U, .val_handle = 3U,
        .properties = BLE_GATT_CHR_PROP_READ};
    chr.uuid.u.type = BLE_UUID_TYPE_16; chr.uuid.u.v16 = 0x2A6EU;
    for (unsigned i = 0; i < 40U; ++i)
        characteristic_callback(connection_handle, &ok, &chr, characteristic_arg);
    characteristic_callback(connection_handle, &done, NULL, characteristic_arg);
    CHECK(cerberus_s3_ble_central_get_characteristic_status(&characteristics) == ESP_OK);
    CHECK(characteristics.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE &&
        characteristics.total_count == 40U && characteristics.stored_count == 32U);
    cerberus_c5_ble_characteristic_t saved_characteristic;
    CHECK(cerberus_s3_ble_central_get_characteristic_result(characteristics.generation,
        0U, &saved_characteristic) == ESP_OK);
    CHECK(saved_characteristic.value_handle == 3U && saved_characteristic.uuid[0] == 0x6EU);
    cerberus_ble_gatt_read_status_payload_t read;
    CHECK(cerberus_s3_ble_central_start_read(&saved_characteristic, &read) == ESP_OK);
    CHECK(diagnostic().read_state == CERBERUS_BLE_GATT_READ_RUNNING);
    drain();
    CHECK(last_value_handle == 3U);
    struct os_mbuf mbuf = {.len = 300U};
    for (unsigned i = 0; i < 300U; ++i) mbuf.data[i] = (uint8_t)i;
    struct ble_gatt_attr attr = {.om = &mbuf};
    read_callback(connection_handle, &ok, &attr, read_arg);
    CHECK(cerberus_s3_ble_central_get_read_status(&read) == ESP_OK);
    CHECK(read.state == CERBERUS_BLE_GATT_READ_COMPLETE &&
        read.total_length == 300U && read.stored_length == 256U);
    uint8_t value[256]; size_t length;
    CHECK(cerberus_s3_ble_central_get_read_value(read.generation, value,
        sizeof(value), &length) == ESP_OK);
    CHECK(length == 256U && value[0] == 0U && value[255] == 255U);
    CHECK(cerberus_s3_ble_central_get_read_value(read.generation, value,
        255U, &length) == ESP_ERR_INVALID_STATE);
    cerberus_s3_ble_central_diagnostics_t d = diagnostic();
    CHECK(d.state == CERBERUS_BLE_CONNECTION_CONNECTED && d.handle_present &&
        d.host_owner == CERBERUS_S3_BLE_OWNER_CENTRAL &&
        d.service_state == CERBERUS_BLE_GATT_SERVICE_COMPLETE &&
        d.characteristic_state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE &&
        d.read_state == CERBERUS_BLE_GATT_READ_COMPLETE);
    CHECK(cerberus_s3_ble_central_disconnect(&connection) == ESP_OK);
    drain();
    CHECK(owner == CERBERUS_S3_BLE_OWNER_NONE);
    CHECK(cerberus_s3_ble_central_get_status(&connection) == ESP_OK &&
        connection.state == CERBERUS_BLE_CONNECTION_IDLE);

    /* Cancel before the queued GAP connect starts and then recover. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_OK);
    CHECK(cerberus_s3_ble_central_disconnect(&connection) == ESP_OK);
    drain();
    CHECK(owner == CERBERUS_S3_BLE_OWNER_NONE);
    CHECK(cerberus_s3_ble_central_get_status(&connection) == ESP_OK &&
        connection.state == CERBERUS_BLE_CONNECTION_IDLE);

    /* A controller reset invalidates the lease and old completed results. */
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_OK);
    drain(); connected();
    owner = CERBERUS_S3_BLE_OWNER_NONE; ++epoch;
    CHECK(cerberus_s3_ble_central_get_status(&connection) == ESP_OK &&
        connection.state == CERBERUS_BLE_CONNECTION_ERROR);
    CHECK(cerberus_s3_ble_central_get_service_result(services.generation,
        0U, &saved_service) == ESP_ERR_INVALID_STATE);
    wifi_owner = CERBERUS_S3_WIFI_RECEIVER;
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_ERR_INVALID_STATE);
    wifi_owner = CERBERUS_S3_WIFI_NONE;
    CHECK(cerberus_s3_ble_central_connect(&peer, &connection) == ESP_OK);
    drain(); connected();
    CHECK(cerberus_s3_ble_central_disconnect(&connection) == ESP_OK);
    drain();
    check_generic_recovery();
    check_sound_jobs();
    puts("S3 BLE central lifecycle, bounded GATT results, cancellation, reset, and receiver gate passed.");
    printf("%u production central fixture checks; diagnostic value %zu bytes.\n",
        checks, sizeof(cerberus_s3_ble_central_diagnostics_t));
    return 0;
}
