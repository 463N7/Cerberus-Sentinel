#include "s3_ble_central.h"
#include "s3_ble_host.h"
#include "s3_ble_scanner.h"
#include "s3_wifi_owner.h"
#include "sdkconfig.h"

#if defined(CONFIG_BT_NIMBLE_ENABLED) && defined(CONFIG_BT_NIMBLE_ROLE_CENTRAL)
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "os/os_mbuf.h"
#include <string.h>

#define CONNECT_TIMEOUT_MS 15000U
#define SOUND_GATT_TIMEOUT_MS 6000U
#define SOUND_DISCONNECT_TIMEOUT_MS 5000U
#define SOUND_FMNA_RESPONSE_MS 2000U
#define SOUND_DULT_RESPONSE_MS 3000U
#define NO_CONNECTION 0xffffU
static const char *TAG = "s3_ble_central";
static SemaphoreHandle_t mutex;
static struct ble_npl_event connect_event, disconnect_event, service_event, characteristic_event, read_event;
static struct ble_npl_event sound_service_event, sound_characteristic_event,
    sound_descriptor_event, sound_subscribe_event, sound_write_event;
static bool events_ready;
static unsigned lease_epoch;
static cerberus_ble_connection_status_payload_t connection = {.connection_handle = NO_CONNECTION};
static cerberus_ble_gatt_service_status_payload_t service_status;
static cerberus_c5_ble_service_t services[CERBERUS_BLE_GATT_SERVICE_MAX_RESULTS];
static cerberus_ble_gatt_characteristic_status_payload_t characteristic_status;
static cerberus_c5_ble_characteristic_t characteristics[CERBERUS_BLE_GATT_CHARACTERISTIC_MAX_RESULTS];
static cerberus_ble_gatt_read_status_payload_t read_status;
static uint8_t read_value[CERBERUS_BLE_GATT_READ_MAX_LENGTH];
static cerberus_s3_ble_sound_status_t sound_status;
static cerberus_s3_ble_sound_state_t sound_final_state;
static ble_uuid_any_t sound_service_uuid, sound_characteristic_uuid;
static uint8_t sound_connection_generation;
static uint16_t sound_service_start, sound_service_end, sound_value_handle;
static uint16_t sound_next_definition, sound_cccd_handle;
static uint8_t sound_subscribe_value;
static bool sound_command_submitted;
static uint32_t sound_stage_started_ms;

static bool active_state(uint8_t state);
static void start_sound_service(struct ble_npl_event *event);
static void start_sound_characteristic(struct ble_npl_event *event);
static void start_sound_descriptor(struct ble_npl_event *event);
static void start_sound_subscribe(struct ble_npl_event *event);
static void start_sound_write(struct ble_npl_event *event);

static uint32_t sound_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static bool sound_active(cerberus_s3_ble_sound_state_t state)
{
    return state >= CERBERUS_S3_BLE_SOUND_CONNECTING &&
        state <= CERBERUS_S3_BLE_SOUND_STOPPING;
}

static void sound_state_locked(cerberus_s3_ble_sound_state_t state)
{
    sound_status.state = state;
    sound_stage_started_ms = sound_now_ms();
}

static bool sound_current_locked(uint8_t generation,
    cerberus_s3_ble_sound_state_t state, uint16_t handle)
{
    return sound_status.generation == generation && sound_status.state == state &&
        sound_connection_generation == connection.generation &&
        connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        connection.connection_handle == handle &&
        lease_epoch == cerberus_s3_ble_host_epoch();
}

/* A finished sound job keeps its result until the next start. The connection
 * owner must be gone before COMPLETE, STOPPED, or ERROR is exposed. */
static void sound_reconcile_locked(void)
{
    if (sound_status.state == CERBERUS_S3_BLE_SOUND_STOPPING &&
        cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_CENTRAL &&
        !active_state(connection.state) && connection.connection_handle == NO_CONNECTION) {
        sound_status.state = sound_final_state;
        if (sound_final_state != CERBERUS_S3_BLE_SOUND_ERROR)
            sound_status.error_code = 0;
    } else if (sound_active(sound_status.state) &&
        sound_status.state != CERBERUS_S3_BLE_SOUND_STOPPING &&
        cerberus_s3_ble_host_owner() != CERBERUS_S3_BLE_OWNER_CENTRAL) {
        sound_status.failed_stage = sound_status.state;
        if (lease_epoch == cerberus_s3_ble_host_epoch() &&
            connection.error_code != 0) {
            sound_status.last_host_rc = connection.error_code;
            sound_status.error_code = ESP_FAIL;
        } else sound_status.error_code = ESP_ERR_INVALID_STATE;
        sound_status.state = CERBERUS_S3_BLE_SOUND_ERROR;
    }
}

static void sound_finish_locked(cerberus_s3_ble_sound_state_t result, int error)
{
    sound_final_state = result;
    sound_status.error_code = error;
    sound_state_locked(CERBERUS_S3_BLE_SOUND_STOPPING);
}

static bool active_state(uint8_t state)
{
    return state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
}

static void clear_gatt_locked(void)
{
    uint8_t service_generation = service_status.generation;
    uint8_t characteristic_generation = characteristic_status.generation;
    uint8_t read_generation = read_status.generation;
    memset(services, 0, sizeof(services));
    memset(characteristics, 0, sizeof(characteristics));
    memset(read_value, 0, sizeof(read_value));
    service_status = (cerberus_ble_gatt_service_status_payload_t){
        .state = CERBERUS_BLE_GATT_SERVICE_IDLE,
        .generation = service_generation,
        .connection_generation = connection.generation,
    };
    characteristic_status = (cerberus_ble_gatt_characteristic_status_payload_t){
        .state = CERBERUS_BLE_GATT_CHARACTERISTIC_IDLE,
        .generation = characteristic_generation,
        .connection_generation = connection.generation,
    };
    read_status = (cerberus_ble_gatt_read_status_payload_t){
        .state = CERBERUS_BLE_GATT_READ_IDLE,
        .generation = read_generation,
        .connection_generation = connection.generation,
    };
}

static void reconcile_host_locked(void)
{
    if ((active_state(connection.state) || connection.connection_handle != NO_CONNECTION) &&
        lease_epoch != cerberus_s3_ble_host_epoch()) {
        connection.state = CERBERUS_BLE_CONNECTION_ERROR;
        connection.connection_handle = NO_CONNECTION;
        connection.error_code = ESP_ERR_INVALID_STATE;
        clear_gatt_locked();
    }
}

static esp_err_t ensure_mutex(void)
{
    if (!mutex) mutex = xSemaphoreCreateMutex(); /* First call comes from the UI task. */
    return mutex ? ESP_OK : ESP_ERR_NO_MEM;
}

static void release_central(void)
{
    esp_err_t err = cerberus_s3_ble_host_release(CERBERUS_S3_BLE_OWNER_CENTRAL);
    if (err != ESP_OK && cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_CENTRAL)
        ESP_LOGE(TAG, "central host release failed: %s", esp_err_to_name(err));
}

static void connection_failed(uint8_t generation, int reason)
{
    bool release = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (connection.generation == generation &&
        connection.state == CERBERUS_BLE_CONNECTION_CONNECTING) {
        connection.state = CERBERUS_BLE_CONNECTION_ERROR;
        connection.connection_handle = NO_CONNECTION;
        connection.error_code = reason;
        clear_gatt_locked();
        release = true;
    }
    xSemaphoreGive(mutex);
    if (release) release_central();
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool release = false;
    bool terminate = false;
    bool start_sound = false;
    uint8_t sound_generation = 0U;
    uint16_t handle = NO_CONNECTION;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (connection.generation != generation || lease_epoch != cerberus_s3_ble_host_epoch()) {
        xSemaphoreGive(mutex);
        return 0;
    }
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (connection.state == CERBERUS_BLE_CONNECTION_CONNECTING ||
            connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING) {
            if (event->connect.status == 0) {
                connection.connection_handle = event->connect.conn_handle;
                connection.error_code = 0;
                if (connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING) {
                    terminate = true;
                    handle = event->connect.conn_handle;
                } else {
                    connection.state = CERBERUS_BLE_CONNECTION_CONNECTED;
                    if (sound_status.state == CERBERUS_S3_BLE_SOUND_CONNECTING &&
                        sound_connection_generation == connection.generation) {
                        sound_state_locked(CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE);
                        sound_generation = sound_status.generation;
                        ble_npl_event_set_arg(&sound_service_event,
                            (void *)(uintptr_t)sound_generation);
                        start_sound = true;
                    }
                }
            } else {
                connection.state = connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING
                    ? CERBERUS_BLE_CONNECTION_IDLE : CERBERUS_BLE_CONNECTION_ERROR;
                connection.connection_handle = NO_CONNECTION;
                connection.error_code = connection.state == CERBERUS_BLE_CONNECTION_IDLE
                    ? 0 : event->connect.status;
                clear_gatt_locked();
                release = true;
            }
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        if (active_state(connection.state) &&
            (connection.connection_handle == NO_CONNECTION ||
             connection.connection_handle == event->disconnect.conn.conn_handle)) {
            bool requested = connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
            connection.state = requested ? CERBERUS_BLE_CONNECTION_IDLE : CERBERUS_BLE_CONNECTION_ERROR;
            connection.connection_handle = NO_CONNECTION;
            connection.error_code = requested ? 0 : event->disconnect.reason;
            clear_gatt_locked();
            release = true;
        }
        break;
    case BLE_GAP_EVENT_TERM_FAILURE:
        if (connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING)
            connection.error_code = event->term_failure.status;
        break;
    case BLE_GAP_EVENT_NOTIFY_RX:
        if (connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
            event->notify_rx.conn_handle == connection.connection_handle &&
            event->notify_rx.attr_handle == sound_value_handle &&
            sound_command_submitted && sound_status.write_accepted &&
            sound_status.state == CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE &&
            sound_connection_generation == connection.generation &&
            sound_status.profile != CERBERUS_FINDMY_PROFILE_AIRTAG) {
            sound_status.response_received = true;
            if (sound_status.profile == CERBERUS_FINDMY_PROFILE_DULT &&
                event->notify_rx.om && OS_MBUF_PKTLEN(event->notify_rx.om) >= 2U) {
                uint8_t response[2];
                if (os_mbuf_copydata(event->notify_rx.om, 0, 2, response) == 0 &&
                    response[0] == 0x02U && response[1] == 0x03U)
                    sound_status.completion_reported = true;
            }
        }
        break;
    default:
        break;
    }
    xSemaphoreGive(mutex);
    if (terminate) {
        int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            xSemaphoreTake(mutex, portMAX_DELAY);
            if (connection.generation == generation &&
                lease_epoch == cerberus_s3_ble_host_epoch() &&
                connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING &&
                connection.connection_handle == handle)
                connection.error_code = rc;
            xSemaphoreGive(mutex);
            ESP_LOGW(TAG, "termination after connect: %d", rc);
        }
    }
    if (release) release_central();
    if (start_sound) ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
        &sound_service_event);
    return 0;
}

static void start_connect(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    ble_addr_t peer = {0};
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTING &&
        connection.generation == generation;
    if (current) {
        peer.type = connection.address_type;
        memcpy(peer.val, connection.address, sizeof(peer.val));
    }
    xSemaphoreGive(mutex);
    if (!current) return;
    uint8_t own_type = 0;
    int rc = ble_hs_id_infer_auto(0, &own_type);
    if (rc == 0) rc = ble_gap_connect(own_type, &peer, CONNECT_TIMEOUT_MS, NULL,
        gap_event, (void *)(uintptr_t)generation);
    if (rc != 0) connection_failed(generation, rc);
}

static void start_disconnect(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING &&
        connection.generation == generation;
    uint16_t handle = connection.connection_handle;
    xSemaphoreGive(mutex);
    if (!current) return;
    int rc = handle == NO_CONNECTION ? ble_gap_conn_cancel() :
        ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    if (rc != 0 && handle == NO_CONNECTION && !ble_gap_conn_active()) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        bool release = connection.generation == generation &&
            connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
        if (release) {
            connection.state = CERBERUS_BLE_CONNECTION_IDLE;
            connection.connection_handle = NO_CONNECTION;
            connection.error_code = 0;
        }
        xSemaphoreGive(mutex);
        if (release) release_central();
    } else if (rc != 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (connection.generation == generation &&
            connection.state == CERBERUS_BLE_CONNECTION_DISCONNECTING)
            connection.error_code = rc;
        xSemaphoreGive(mutex);
        ESP_LOGW(TAG, "disconnect request failed: %d", rc);
    }
}

static void copy_uuid(uint8_t *length, uint8_t bytes[16], const ble_uuid_t *uuid)
{
    if (uuid->type == BLE_UUID_TYPE_16) {
        uint16_t v = BLE_UUID16(uuid)->value;
        *length = 2U; bytes[0] = (uint8_t)v; bytes[1] = (uint8_t)(v >> 8U);
    } else if (uuid->type == BLE_UUID_TYPE_32) {
        uint32_t v = BLE_UUID32(uuid)->value;
        *length = 4U;
        for (unsigned i = 0; i < 4U; ++i) bytes[i] = (uint8_t)(v >> (i * 8U));
    } else if (uuid->type == BLE_UUID_TYPE_128) {
        *length = 16U; memcpy(bytes, BLE_UUID128(uuid)->value, 16U);
    }
}

static int discovered_service(uint16_t handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        connection.connection_handle == handle &&
        service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING &&
        service_status.generation == generation &&
        service_status.connection_generation == connection.generation;
    if (current && error->status == 0 && service) {
        if (service_status.total_count < UINT16_MAX) ++service_status.total_count;
        if (service_status.stored_count < CERBERUS_BLE_GATT_SERVICE_MAX_RESULTS) {
            cerberus_c5_ble_service_t *item = &services[service_status.stored_count++];
            *item = (cerberus_c5_ble_service_t){
                .start_handle = service->start_handle, .end_handle = service->end_handle};
            copy_uuid(&item->uuid_length, item->uuid, &service->uuid.u);
        }
    } else if (current) {
        service_status.state = error->status == BLE_HS_EDONE ?
            CERBERUS_BLE_GATT_SERVICE_COMPLETE : CERBERUS_BLE_GATT_SERVICE_ERROR;
        service_status.error_code = error->status == BLE_HS_EDONE ? 0 : error->status;
    }
    xSemaphoreGive(mutex);
    return 0;
}

static int discovered_characteristic(uint16_t handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *characteristic, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        connection.connection_handle == handle &&
        characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
        characteristic_status.generation == generation &&
        characteristic_status.connection_generation == connection.generation;
    if (current && error->status == 0 && characteristic) {
        if (characteristic_status.total_count < UINT16_MAX) ++characteristic_status.total_count;
        if (characteristic_status.stored_count < CERBERUS_BLE_GATT_CHARACTERISTIC_MAX_RESULTS) {
            cerberus_c5_ble_characteristic_t *item =
                &characteristics[characteristic_status.stored_count++];
            *item = (cerberus_c5_ble_characteristic_t){
                .definition_handle = characteristic->def_handle,
                .value_handle = characteristic->val_handle,
                .properties = characteristic->properties};
            copy_uuid(&item->uuid_length, item->uuid, &characteristic->uuid.u);
        }
    } else if (current) {
        characteristic_status.state = error->status == BLE_HS_EDONE ?
            CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE : CERBERUS_BLE_GATT_CHARACTERISTIC_ERROR;
        characteristic_status.error_code = error->status == BLE_HS_EDONE ? 0 : error->status;
    }
    xSemaphoreGive(mutex);
    return 0;
}

static int read_characteristic(uint16_t handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        connection.connection_handle == handle &&
        read_status.state == CERBERUS_BLE_GATT_READ_RUNNING &&
        read_status.generation == generation &&
        read_status.connection_generation == connection.generation;
    if (current) {
        if (error->status == 0 && attribute && attribute->om) {
            uint16_t total = OS_MBUF_PKTLEN(attribute->om);
            uint16_t stored = total < sizeof(read_value) ? total : sizeof(read_value);
            int rc = os_mbuf_copydata(attribute->om, 0, stored, read_value);
            read_status.total_length = total;
            read_status.stored_length = rc == 0 ? stored : 0;
            read_status.state = rc == 0 ? CERBERUS_BLE_GATT_READ_COMPLETE :
                CERBERUS_BLE_GATT_READ_ERROR;
            read_status.error_code = rc;
        } else {
            read_status.state = CERBERUS_BLE_GATT_READ_ERROR;
            read_status.error_code = error->status ? error->status : BLE_HS_EINVAL;
        }
    }
    xSemaphoreGive(mutex);
    return 0;
}

static void start_service(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING &&
        service_status.generation == generation;
    uint16_t handle = connection.connection_handle;
    xSemaphoreGive(mutex);
    if (!current) return;
    int rc = ble_gattc_disc_all_svcs(handle, discovered_service, (void *)(uintptr_t)generation);
    if (rc != 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (service_status.state == CERBERUS_BLE_GATT_SERVICE_RUNNING &&
            service_status.generation == generation) {
            service_status.state = CERBERUS_BLE_GATT_SERVICE_ERROR;
            service_status.error_code = rc;
        }
        xSemaphoreGive(mutex);
    }
}

static void start_characteristic(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
        characteristic_status.generation == generation;
    uint16_t handle = connection.connection_handle;
    uint16_t first = characteristic_status.start_handle, last = characteristic_status.end_handle;
    xSemaphoreGive(mutex);
    if (!current) return;
    int rc = ble_gattc_disc_all_chrs(handle, first, last,
        discovered_characteristic, (void *)(uintptr_t)generation);
    if (rc != 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
            characteristic_status.generation == generation) {
            characteristic_status.state = CERBERUS_BLE_GATT_CHARACTERISTIC_ERROR;
            characteristic_status.error_code = rc;
        }
        xSemaphoreGive(mutex);
    }
}

static void start_read(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool current = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        read_status.state == CERBERUS_BLE_GATT_READ_RUNNING &&
        read_status.generation == generation;
    uint16_t handle = connection.connection_handle, value_handle = read_status.value_handle;
    xSemaphoreGive(mutex);
    if (!current) return;
    int rc = ble_gattc_read(handle, value_handle, read_characteristic,
        (void *)(uintptr_t)generation);
    if (rc != 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (read_status.state == CERBERUS_BLE_GATT_READ_RUNNING &&
            read_status.generation == generation) {
            read_status.state = CERBERUS_BLE_GATT_READ_ERROR;
            read_status.error_code = rc;
        }
        xSemaphoreGive(mutex);
    }
}

static void sound_fail(uint8_t generation, int error_code, int host_rc)
{
    bool disconnect = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_status.generation == generation && sound_active(sound_status.state) &&
        sound_status.state != CERBERUS_S3_BLE_SOUND_STOPPING) {
        sound_status.failed_stage = sound_status.state;
        sound_status.last_host_rc = host_rc;
        sound_finish_locked(CERBERUS_S3_BLE_SOUND_ERROR,
            host_rc ? ESP_FAIL : error_code);
        disconnect = true;
    }
    xSemaphoreGive(mutex);
    if (disconnect) {
        cerberus_ble_connection_status_payload_t ignored;
        (void)cerberus_s3_ble_central_disconnect(&ignored);
    }
}

static int sound_service_found(uint16_t handle, const struct ble_gatt_error *error,
    const struct ble_gatt_svc *service, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool next = false;
    int failure = 0;
    int host_rc = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE, handle)) {
        if (!error) failure = ESP_ERR_INVALID_RESPONSE;
        else if (error->status == 0 && service) {
            if (ble_uuid_cmp(&service->uuid.u, &sound_service_uuid.u) == 0 &&
                service->start_handle != 0U &&
                service->start_handle <= service->end_handle) {
                sound_service_start = service->start_handle;
                sound_service_end = service->end_handle;
            }
        } else if (error->status == BLE_HS_EDONE) {
            if (sound_service_start != 0U) {
                sound_state_locked(CERBERUS_S3_BLE_SOUND_DISCOVERING_CHARACTERISTIC);
                ble_npl_event_set_arg(&sound_characteristic_event,
                    (void *)(uintptr_t)generation);
                next = true;
            } else failure = ESP_ERR_NOT_FOUND;
        } else {
            host_rc = error->status;
            failure = host_rc ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(mutex);
    if (next) ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
        &sound_characteristic_event);
    if (failure) sound_fail(generation, failure, host_rc);
    return 0;
}

static void start_sound_service(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    uint16_t handle = NO_CONNECTION;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE,
            connection.connection_handle)) handle = connection.connection_handle;
    xSemaphoreGive(mutex);
    if (handle == NO_CONNECTION) return;
    int rc = ble_gattc_disc_svc_by_uuid(handle, &sound_service_uuid.u,
        sound_service_found, (void *)(uintptr_t)generation);
    if (rc != 0) sound_fail(generation, ESP_FAIL, rc);
}

static int sound_characteristic_found(uint16_t handle,
    const struct ble_gatt_error *error, const struct ble_gatt_chr *characteristic,
    void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool next = false;
    struct ble_npl_event *next_event = NULL;
    int failure = 0;
    int host_rc = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_CHARACTERISTIC, handle)) {
        if (!error) failure = ESP_ERR_INVALID_RESPONSE;
        else if (error->status == 0 && characteristic) {
            if (sound_value_handle == 0U &&
                ble_uuid_cmp(&characteristic->uuid.u,
                    &sound_characteristic_uuid.u) == 0) {
                sound_value_handle = characteristic->val_handle;
                sound_subscribe_value =
                    (characteristic->properties & BLE_GATT_CHR_PROP_INDICATE) ? 2U :
                    (characteristic->properties & BLE_GATT_CHR_PROP_NOTIFY) ? 1U : 0U;
                if (!(characteristic->properties & BLE_GATT_CHR_PROP_WRITE))
                    sound_value_handle = 0U;
            } else if (sound_value_handle != 0U &&
                       characteristic->def_handle > sound_value_handle &&
                       (sound_next_definition == 0U ||
                        characteristic->def_handle < sound_next_definition)) {
                sound_next_definition = characteristic->def_handle;
            }
        } else if (error->status == BLE_HS_EDONE) {
            if (sound_value_handle == 0U) failure = ESP_ERR_NOT_SUPPORTED;
            else if (sound_status.profile == CERBERUS_FINDMY_PROFILE_AIRTAG) {
                sound_state_locked(CERBERUS_S3_BLE_SOUND_WRITING);
                next_event = &sound_write_event;
                next = true;
            } else if (sound_subscribe_value == 0U) {
                failure = ESP_ERR_NOT_SUPPORTED;
            } else if (sound_next_definition != 0U &&
                       sound_next_definition <= sound_value_handle + 1U) {
                failure = ESP_ERR_NOT_FOUND;
            } else {
                sound_state_locked(CERBERUS_S3_BLE_SOUND_DISCOVERING_CCCD);
                next_event = &sound_descriptor_event;
                next = true;
            }
            if (next) ble_npl_event_set_arg(next_event,
                (void *)(uintptr_t)generation);
        } else {
            host_rc = error->status;
            failure = host_rc ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(mutex);
    if (next) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), next_event);
    if (failure) sound_fail(generation, failure, host_rc);
    return 0;
}

static void start_sound_characteristic(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    uint16_t handle = NO_CONNECTION, first = 0U, last = 0U;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_CHARACTERISTIC,
            connection.connection_handle)) {
        handle = connection.connection_handle;
        first = sound_service_start;
        last = sound_service_end;
    }
    xSemaphoreGive(mutex);
    if (handle == NO_CONNECTION) return;
    int rc = ble_gattc_disc_all_chrs(handle, first, last,
        sound_characteristic_found, (void *)(uintptr_t)generation);
    if (rc != 0) sound_fail(generation, ESP_FAIL, rc);
}

static int sound_descriptor_found(uint16_t handle,
    const struct ble_gatt_error *error, uint16_t chr_val_handle,
    const struct ble_gatt_dsc *descriptor, void *arg)
{
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool next = false;
    int failure = 0;
    int host_rc = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_CCCD, handle)) {
        if (!error) failure = ESP_ERR_INVALID_RESPONSE;
        else if (error->status == 0 && descriptor) {
            if (chr_val_handle == sound_value_handle &&
                descriptor->handle > sound_value_handle &&
                descriptor->uuid.u.type == BLE_UUID_TYPE_16 &&
                BLE_UUID16(&descriptor->uuid.u)->value == 0x2902U)
                sound_cccd_handle = descriptor->handle;
        } else if (error->status == BLE_HS_EDONE) {
            if (sound_cccd_handle != 0U) {
                sound_state_locked(CERBERUS_S3_BLE_SOUND_SUBSCRIBING);
                ble_npl_event_set_arg(&sound_subscribe_event,
                    (void *)(uintptr_t)generation);
                next = true;
            } else failure = ESP_ERR_NOT_FOUND;
        } else {
            host_rc = error->status;
            failure = host_rc ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(mutex);
    if (next) ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
        &sound_subscribe_event);
    if (failure) sound_fail(generation, failure, host_rc);
    return 0;
}

static void start_sound_descriptor(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    uint16_t handle = NO_CONNECTION, first = 0U, last = 0U;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_DISCOVERING_CCCD,
            connection.connection_handle)) {
        handle = connection.connection_handle;
        first = sound_value_handle;
        last = sound_next_definition != 0U ?
            sound_next_definition - 1U : sound_service_end;
    }
    xSemaphoreGive(mutex);
    if (handle == NO_CONNECTION) return;
    if (last <= first) { sound_fail(generation, ESP_ERR_NOT_FOUND, 0); return; }
    int rc = ble_gattc_disc_all_dscs(handle, first, last,
        sound_descriptor_found, (void *)(uintptr_t)generation);
    if (rc != 0) sound_fail(generation, ESP_FAIL, rc);
}

static int sound_subscribed(uint16_t handle,
    const struct ble_gatt_error *error, struct ble_gatt_attr *attribute,
    void *arg)
{
    (void)attribute;
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool next = false;
    int failure = 0;
    int host_rc = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_SUBSCRIBING, handle)) {
        if (error && error->status == 0) {
            sound_state_locked(CERBERUS_S3_BLE_SOUND_WRITING);
            ble_npl_event_set_arg(&sound_write_event,
                (void *)(uintptr_t)generation);
            next = true;
        } else {
            host_rc = error ? error->status : 0;
            failure = host_rc ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(mutex);
    if (next) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &sound_write_event);
    if (failure) sound_fail(generation, failure, host_rc);
    return 0;
}

static void start_sound_subscribe(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    int rc = 0;
    uint8_t subscription[2] = {0U, 0U};
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation,
            CERBERUS_S3_BLE_SOUND_SUBSCRIBING,
            connection.connection_handle)) {
        subscription[0] = sound_subscribe_value;
        rc = ble_gattc_write_flat(connection.connection_handle, sound_cccd_handle,
            subscription, sizeof(subscription), sound_subscribed,
            (void *)(uintptr_t)generation);
    }
    xSemaphoreGive(mutex);
    if (rc != 0) sound_fail(generation, ESP_FAIL, rc);
}

static int sound_written(uint16_t handle, const struct ble_gatt_error *error,
    struct ble_gatt_attr *attribute, void *arg)
{
    (void)attribute;
    uint8_t generation = (uint8_t)(uintptr_t)arg;
    bool disconnect = false;
    int failure = 0;
    int host_rc = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation, CERBERUS_S3_BLE_SOUND_WRITING, handle)) {
        if (error && error->status == 0) {
            sound_status.write_accepted = true;
            if (sound_status.profile == CERBERUS_FINDMY_PROFILE_AIRTAG) {
                sound_finish_locked(CERBERUS_S3_BLE_SOUND_COMPLETE, 0);
                disconnect = true;
            } else sound_state_locked(CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE);
        } else {
            host_rc = error ? error->status : 0;
            failure = host_rc ? ESP_FAIL : ESP_ERR_INVALID_RESPONSE;
        }
    }
    xSemaphoreGive(mutex);
    if (failure) sound_fail(generation, failure, host_rc);
    if (disconnect) {
        cerberus_ble_connection_status_payload_t ignored;
        (void)cerberus_s3_ble_central_disconnect(&ignored);
    }
    return 0;
}

static void start_sound_write(struct ble_npl_event *event)
{
    uint8_t generation = (uint8_t)(uintptr_t)ble_npl_event_get_arg(event);
    int rc = 0;
    uint8_t command[3] = {0U, 0U, 0U};
    uint16_t command_length = 0U;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    if (sound_current_locked(generation, CERBERUS_S3_BLE_SOUND_WRITING,
            connection.connection_handle)) {
        switch (sound_status.profile) {
        case CERBERUS_FINDMY_PROFILE_AIRTAG:
            command[0] = 0xAFU; command_length = 1U; break;
        case CERBERUS_FINDMY_PROFILE_FMNA:
            command[0] = 0x01U; command[1] = 0x00U; command[2] = 0x03U;
            command_length = 3U; break;
        case CERBERUS_FINDMY_PROFILE_DULT:
            command[0] = 0x00U; command[1] = 0x03U;
            command_length = 2U; break;
        default: break;
        }
        if (command_length != 0U) {
            rc = ble_gattc_write_flat(connection.connection_handle,
                sound_value_handle, command, command_length, sound_written,
                (void *)(uintptr_t)generation);
            sound_command_submitted = rc == 0;
        }
    }
    xSemaphoreGive(mutex);
    if (command_length == 0U) { sound_fail(generation, ESP_ERR_INVALID_ARG, 0); return; }
    if (rc != 0) sound_fail(generation, ESP_FAIL, rc);
}

static void init_events(void)
{
    if (events_ready) return;
    ble_npl_event_init(&connect_event, start_connect, NULL);
    ble_npl_event_init(&disconnect_event, start_disconnect, NULL);
    ble_npl_event_init(&service_event, start_service, NULL);
    ble_npl_event_init(&characteristic_event, start_characteristic, NULL);
    ble_npl_event_init(&read_event, start_read, NULL);
    ble_npl_event_init(&sound_service_event, start_sound_service, NULL);
    ble_npl_event_init(&sound_characteristic_event, start_sound_characteristic, NULL);
    ble_npl_event_init(&sound_descriptor_event, start_sound_descriptor, NULL);
    ble_npl_event_init(&sound_subscribe_event, start_sound_subscribe, NULL);
    ble_npl_event_init(&sound_write_event, start_sound_write, NULL);
    events_ready = true;
}

static esp_err_t wait_for_scanner(void)
{
    uint32_t began = (uint32_t)(esp_timer_get_time() / 1000LL);
    bool stop_sent = false;
    while (cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_SCANNER) {
        cerberus_ble_scan_status_payload_t scan = {0};
        esp_err_t err = cerberus_s3_ble_scanner_get_status(&scan);
        if (err != ESP_OK) return err;
        if (!stop_sent && scan.state == CERBERUS_BLE_SCAN_RUNNING) {
            err = cerberus_s3_ble_scanner_stop(&scan);
            if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return err;
            stop_sent = true;
        }
        if ((uint32_t)(esp_timer_get_time() / 1000LL) - began >= 1000U)
            return ESP_ERR_TIMEOUT;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_NONE ?
        ESP_OK : ESP_ERR_INVALID_STATE;
}

static esp_err_t connect_internal(const cerberus_c5_ble_result_t *peer,
    cerberus_ble_connection_status_payload_t *status,
    cerberus_findmy_profile_t sound_profile,
    cerberus_s3_ble_sound_status_t *sound_result)
{
    if (!peer || !status || peer->address_type > BLE_ADDR_RANDOM_ID ||
        !(peer->flags & CERBERUS_BLE_RESULT_CONNECTABLE)) return ESP_ERR_INVALID_ARG;
    ble_uuid_any_t service_uuid = {0}, characteristic_uuid = {0};
    if (sound_profile != CERBERUS_FINDMY_PROFILE_NONE) {
        const char *service = NULL, *characteristic = NULL;
        switch (sound_profile) {
        case CERBERUS_FINDMY_PROFILE_AIRTAG:
            service = "7dfc9000-7d1c-4951-86aa-8d9728f8d66c";
            characteristic = "7dfc9001-7d1c-4951-86aa-8d9728f8d66c";
            break;
        case CERBERUS_FINDMY_PROFILE_FMNA:
            service = "0000fd44-0000-1000-8000-00805f9b34fb";
            characteristic = "4f860003-943b-49ef-bed4-2f730304427a";
            break;
        case CERBERUS_FINDMY_PROFILE_DULT:
            service = "15190001-12f4-c226-88ed-2ac5579f2a85";
            characteristic = "8e0c0001-1d68-fb92-bf61-48377421680e";
            break;
        default: return ESP_ERR_INVALID_ARG;
        }
        if (ble_uuid_from_str(&service_uuid, service) != 0 ||
            ble_uuid_from_str(&characteristic_uuid, characteristic) != 0)
            return ESP_ERR_INVALID_STATE;
    }
    if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER) return ESP_ERR_INVALID_STATE;
    esp_err_t init_err = ensure_mutex();
    if (init_err != ESP_OK) return init_err;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    sound_reconcile_locked();
    bool busy = active_state(connection.state) || connection.connection_handle != NO_CONNECTION;
    xSemaphoreGive(mutex);
    if (busy) return ESP_ERR_INVALID_STATE;
    esp_err_t err = wait_for_scanner();
    if (err != ESP_OK) return err;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) <
        (cerberus_s3_ble_host_initialized() ? 32768U : 98304U)) return ESP_ERR_NO_MEM;
    err = cerberus_s3_ble_host_claim(CERBERUS_S3_BLE_OWNER_CENTRAL);
    if (err != ESP_OK) return err;
    if (cerberus_s3_wifi_owner() == CERBERUS_S3_WIFI_RECEIVER) {
        release_central();
        return ESP_ERR_INVALID_STATE;
    }
    init_events();
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (active_state(connection.state) || connection.connection_handle != NO_CONNECTION) {
        xSemaphoreGive(mutex);
        release_central();
        return ESP_ERR_INVALID_STATE;
    }
    lease_epoch = cerberus_s3_ble_host_epoch();
    ++connection.generation;
    connection.state = CERBERUS_BLE_CONNECTION_CONNECTING;
    connection.address_type = peer->address_type;
    memcpy(connection.address, peer->address, sizeof(connection.address));
    connection.connection_handle = NO_CONNECTION;
    connection.error_code = 0;
    clear_gatt_locked();
    if (sound_profile != CERBERUS_FINDMY_PROFILE_NONE) {
        uint8_t generation = sound_status.generation + 1U;
        sound_status = (cerberus_s3_ble_sound_status_t){
            .state = CERBERUS_S3_BLE_SOUND_CONNECTING,
            .profile = sound_profile,
            .generation = generation,
            .address_type = peer->address_type,
        };
        memcpy(sound_status.address, peer->address, sizeof(sound_status.address));
        sound_connection_generation = connection.generation;
        sound_service_uuid = service_uuid;
        sound_characteristic_uuid = characteristic_uuid;
        sound_service_start = sound_service_end = sound_value_handle = 0U;
        sound_next_definition = sound_cccd_handle = 0U;
        sound_subscribe_value = 0U;
        sound_command_submitted = false;
        sound_final_state = CERBERUS_S3_BLE_SOUND_IDLE;
        sound_stage_started_ms = sound_now_ms();
        if (sound_result) *sound_result = sound_status;
    }
    *status = connection;
    ble_npl_event_set_arg(&connect_event, (void *)(uintptr_t)connection.generation);
    xSemaphoreGive(mutex);
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &connect_event);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_central_connect(const cerberus_c5_ble_result_t *peer,
    cerberus_ble_connection_status_payload_t *status)
{
    return connect_internal(peer, status, CERBERUS_FINDMY_PROFILE_NONE, NULL);
}

esp_err_t cerberus_s3_ble_central_disconnect(cerberus_ble_connection_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = active_state(connection.state) ||
        (connection.state == CERBERUS_BLE_CONNECTION_ERROR &&
         connection.connection_handle != NO_CONNECTION);
    if (valid) {
        connection.state = CERBERUS_BLE_CONNECTION_DISCONNECTING;
        connection.error_code = 0;
        clear_gatt_locked();
        ble_npl_event_set_arg(&disconnect_event, (void *)(uintptr_t)connection.generation);
    }
    *status = connection;
    xSemaphoreGive(mutex);
    if (valid) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &disconnect_event);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_get_status(cerberus_ble_connection_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = connection; return ESP_OK; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    *status = connection;
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_central_get_diagnostics(
    cerberus_s3_ble_central_diagnostics_t *diagnostics)
{
    if (!diagnostics) return ESP_ERR_INVALID_ARG;
    memset(diagnostics, 0, sizeof(*diagnostics));
    /* A first diagnostic read must not create a mutex or initialize NimBLE.
     * The UI publishes the mutex before it can change connection state. */
    SemaphoreHandle_t existing_mutex = mutex;
    if (existing_mutex) xSemaphoreTake(existing_mutex, portMAX_DELAY);
    esp_err_t result = ESP_ERR_INVALID_STATE;
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        unsigned observed_epoch = cerberus_s3_ble_host_epoch();
        cerberus_s3_ble_central_diagnostics_t snapshot = {
            .host_owner = (uint8_t)cerberus_s3_ble_host_owner(),
            .host_epoch = observed_epoch,
        };
        if (existing_mutex) {
            snapshot.state = connection.state;
            snapshot.generation = connection.generation;
            snapshot.handle_present = connection.connection_handle != NO_CONNECTION;
            snapshot.error_code = connection.error_code;
            snapshot.service_state = service_status.state;
            snapshot.characteristic_state = characteristic_status.state;
            snapshot.read_state = read_status.state;
            if ((active_state(snapshot.state) || snapshot.handle_present) &&
                lease_epoch != observed_epoch) {
                snapshot.state = CERBERUS_BLE_CONNECTION_ERROR;
                snapshot.handle_present = false;
                snapshot.error_code = ESP_ERR_INVALID_STATE;
                snapshot.service_state = CERBERUS_BLE_GATT_SERVICE_IDLE;
                snapshot.characteristic_state = CERBERUS_BLE_GATT_CHARACTERISTIC_IDLE;
                snapshot.read_state = CERBERUS_BLE_GATT_READ_IDLE;
            }
        }
        if (observed_epoch == cerberus_s3_ble_host_epoch()) {
            *diagnostics = snapshot;
            result = ESP_OK;
            break;
        }
    }
    if (existing_mutex) xSemaphoreGive(existing_mutex);
    return result;
}

esp_err_t cerberus_s3_ble_central_start_service_discovery(
    cerberus_ble_gatt_service_status_payload_t *status)
{
    if (!status || !mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        !sound_active(sound_status.state) &&
        service_status.state != CERBERUS_BLE_GATT_SERVICE_RUNNING &&
        characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
        read_status.state != CERBERUS_BLE_GATT_READ_RUNNING;
    if (valid) {
        uint8_t generation = service_status.generation + 1U;
        clear_gatt_locked();
        service_status.generation = generation;
        service_status.state = CERBERUS_BLE_GATT_SERVICE_RUNNING;
        ble_npl_event_set_arg(&service_event, (void *)(uintptr_t)generation);
    }
    *status = service_status;
    xSemaphoreGive(mutex);
    if (valid) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &service_event);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_get_service_status(
    cerberus_ble_gatt_service_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = service_status; return ESP_OK; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked(); *status = service_status;
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_central_get_service_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_service_t *result)
{
    if (!result || !mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE &&
        service_status.generation == generation && index < service_status.stored_count;
    if (valid) *result = services[index];
    xSemaphoreGive(mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_start_characteristic_discovery(
    const cerberus_c5_ble_service_t *service,
    cerberus_ble_gatt_characteristic_status_payload_t *status)
{
    if (!service || !status || !mutex || !service->start_handle ||
        service->start_handle > service->end_handle) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool known = false;
    if (connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        service_status.state == CERBERUS_BLE_GATT_SERVICE_COMPLETE) {
        for (uint8_t i = 0; i < service_status.stored_count; ++i)
            if (services[i].start_handle == service->start_handle &&
                services[i].end_handle == service->end_handle) { known = true; break; }
    }
    bool valid = known && !sound_active(sound_status.state) &&
        characteristic_status.state != CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING &&
        read_status.state != CERBERUS_BLE_GATT_READ_RUNNING;
    if (valid) {
        uint8_t generation = characteristic_status.generation + 1U;
        memset(characteristics, 0, sizeof(characteristics));
        memset(read_value, 0, sizeof(read_value));
        characteristic_status = (cerberus_ble_gatt_characteristic_status_payload_t){
            .state = CERBERUS_BLE_GATT_CHARACTERISTIC_RUNNING,
            .generation = generation,
            .connection_generation = connection.generation,
            .start_handle = service->start_handle,
            .end_handle = service->end_handle};
        read_status = (cerberus_ble_gatt_read_status_payload_t){
            .state = CERBERUS_BLE_GATT_READ_IDLE,
            .connection_generation = connection.generation};
        ble_npl_event_set_arg(&characteristic_event, (void *)(uintptr_t)generation);
    }
    *status = characteristic_status;
    xSemaphoreGive(mutex);
    if (valid) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &characteristic_event);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_get_characteristic_status(
    cerberus_ble_gatt_characteristic_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = characteristic_status; return ESP_OK; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked(); *status = characteristic_status;
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_central_get_characteristic_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_characteristic_t *result)
{
    if (!result || !mutex) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE &&
        characteristic_status.generation == generation &&
        index < characteristic_status.stored_count;
    if (valid) *result = characteristics[index];
    xSemaphoreGive(mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_start_read(
    const cerberus_c5_ble_characteristic_t *characteristic,
    cerberus_ble_gatt_read_status_payload_t *status)
{
    if (!characteristic || !status || !mutex || !characteristic->value_handle ||
        !(characteristic->properties & BLE_GATT_CHR_PROP_READ)) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool known = false;
    if (connection.state == CERBERUS_BLE_CONNECTION_CONNECTED &&
        characteristic_status.state == CERBERUS_BLE_GATT_CHARACTERISTIC_COMPLETE) {
        for (uint8_t i = 0; i < characteristic_status.stored_count; ++i)
            if (characteristics[i].value_handle == characteristic->value_handle &&
                (characteristics[i].properties & BLE_GATT_CHR_PROP_READ)) { known = true; break; }
    }
    bool valid = known && !sound_active(sound_status.state) &&
        read_status.state != CERBERUS_BLE_GATT_READ_RUNNING;
    if (valid) {
        uint8_t generation = read_status.generation + 1U;
        memset(read_value, 0, sizeof(read_value));
        read_status = (cerberus_ble_gatt_read_status_payload_t){
            .state = CERBERUS_BLE_GATT_READ_RUNNING,
            .generation = generation,
            .connection_generation = connection.generation,
            .value_handle = characteristic->value_handle};
        ble_npl_event_set_arg(&read_event, (void *)(uintptr_t)generation);
    }
    *status = read_status;
    xSemaphoreGive(mutex);
    if (valid) ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &read_event);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_get_read_status(cerberus_ble_gatt_read_status_payload_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = read_status; return ESP_OK; }
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked(); *status = read_status;
    xSemaphoreGive(mutex);
    return ESP_OK;
}

esp_err_t cerberus_s3_ble_central_get_read_value(uint8_t generation,
    uint8_t *value, size_t capacity, size_t *length)
{
    if (!value || !length || !mutex) return ESP_ERR_INVALID_ARG;
    *length = 0;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    bool valid = read_status.state == CERBERUS_BLE_GATT_READ_COMPLETE &&
        read_status.generation == generation && capacity >= read_status.stored_length;
    if (valid) {
        memcpy(value, read_value, read_status.stored_length);
        *length = read_status.stored_length;
    }
    xSemaphoreGive(mutex);
    return valid ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_sound_start(const cerberus_c5_ble_result_t *peer,
    cerberus_findmy_profile_t profile, cerberus_s3_ble_sound_status_t *status)
{
    if (!status || profile < CERBERUS_FINDMY_PROFILE_AIRTAG ||
        profile > CERBERUS_FINDMY_PROFILE_DULT) return ESP_ERR_INVALID_ARG;
    cerberus_ble_connection_status_payload_t connection_status;
    return connect_internal(peer, &connection_status, profile, status);
}

esp_err_t cerberus_s3_ble_central_sound_stop(cerberus_s3_ble_sound_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = sound_status; return ESP_ERR_INVALID_STATE; }
    bool disconnect = false, had_job = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    sound_reconcile_locked();
    if (sound_active(sound_status.state) &&
        sound_status.state != CERBERUS_S3_BLE_SOUND_STOPPING) {
        sound_finish_locked(CERBERUS_S3_BLE_SOUND_STOPPED, 0);
        disconnect = true;
    }
    had_job = sound_status.state != CERBERUS_S3_BLE_SOUND_IDLE;
    *status = sound_status;
    xSemaphoreGive(mutex);
    if (disconnect) {
        cerberus_ble_connection_status_payload_t ignored;
        (void)cerberus_s3_ble_central_disconnect(&ignored);
    }
    return had_job ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t cerberus_s3_ble_central_sound_get_status(cerberus_s3_ble_sound_status_t *status)
{
    if (!status) return ESP_ERR_INVALID_ARG;
    if (!mutex) { *status = sound_status; return ESP_OK; }
    bool disconnect = false;
    xSemaphoreTake(mutex, portMAX_DELAY);
    reconcile_host_locked();
    sound_reconcile_locked();
    uint32_t elapsed = (uint32_t)(sound_now_ms() - sound_stage_started_ms);
    uint32_t timeout = 0U;
    if (sound_status.state == CERBERUS_S3_BLE_SOUND_CONNECTING)
        timeout = CONNECT_TIMEOUT_MS + 1000U;
    else if (sound_status.state >= CERBERUS_S3_BLE_SOUND_DISCOVERING_SERVICE &&
             sound_status.state <= CERBERUS_S3_BLE_SOUND_WRITING)
        timeout = SOUND_GATT_TIMEOUT_MS;
    else if (sound_status.state == CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE)
        timeout = sound_status.profile == CERBERUS_FINDMY_PROFILE_DULT ?
            SOUND_DULT_RESPONSE_MS : SOUND_FMNA_RESPONSE_MS;
    if (timeout && elapsed >= timeout) {
        if (sound_status.state == CERBERUS_S3_BLE_SOUND_WAITING_RESPONSE)
            sound_finish_locked(CERBERUS_S3_BLE_SOUND_COMPLETE, 0);
        else {
            sound_status.failed_stage = sound_status.state;
            sound_finish_locked(CERBERUS_S3_BLE_SOUND_ERROR, ESP_ERR_TIMEOUT);
        }
        disconnect = true;
    } else if (sound_status.state == CERBERUS_S3_BLE_SOUND_STOPPING &&
               elapsed >= SOUND_DISCONNECT_TIMEOUT_MS &&
               cerberus_s3_ble_host_owner() == CERBERUS_S3_BLE_OWNER_CENTRAL) {
        sound_status.error_code = ESP_ERR_TIMEOUT;
        sound_stage_started_ms = sound_now_ms();
        disconnect = true; /* Retry a termination request; never claim release. */
    }
    *status = sound_status;
    xSemaphoreGive(mutex);
    if (disconnect) {
        cerberus_ble_connection_status_payload_t ignored;
        (void)cerberus_s3_ble_central_disconnect(&ignored);
    }
    return ESP_OK;
}

#else
#include <string.h>
esp_err_t cerberus_s3_ble_central_connect(const cerberus_c5_ble_result_t *peer, cerberus_ble_connection_status_payload_t *status)
{ (void)peer; (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_disconnect(cerberus_ble_connection_status_payload_t *status)
{ (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_status(cerberus_ble_connection_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_diagnostics(cerberus_s3_ble_central_diagnostics_t *diagnostics)
{
    if (!diagnostics) return ESP_ERR_INVALID_ARG;
    memset(diagnostics, 0, sizeof(*diagnostics));
    return ESP_ERR_NOT_SUPPORTED;
}
esp_err_t cerberus_s3_ble_central_start_service_discovery(cerberus_ble_gatt_service_status_payload_t *status)
{ (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_service_status(cerberus_ble_gatt_service_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_service_result(uint8_t generation, uint8_t index, cerberus_c5_ble_service_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_start_characteristic_discovery(const cerberus_c5_ble_service_t *service, cerberus_ble_gatt_characteristic_status_payload_t *status)
{ (void)service; (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_characteristic_status(cerberus_ble_gatt_characteristic_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_characteristic_result(uint8_t generation, uint8_t index, cerberus_c5_ble_characteristic_t *result)
{ (void)generation; (void)index; (void)result; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_start_read(const cerberus_c5_ble_characteristic_t *characteristic, cerberus_ble_gatt_read_status_payload_t *status)
{ (void)characteristic; (void)status; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_read_status(cerberus_ble_gatt_read_status_payload_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_get_read_value(uint8_t generation, uint8_t *value, size_t capacity, size_t *length)
{ (void)generation; (void)value; (void)capacity; if (length) *length = 0; return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_sound_start(const cerberus_c5_ble_result_t *peer,
    cerberus_findmy_profile_t profile, cerberus_s3_ble_sound_status_t *status)
{ (void)peer; (void)profile; if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_sound_stop(cerberus_s3_ble_sound_status_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
esp_err_t cerberus_s3_ble_central_sound_get_status(cerberus_s3_ble_sound_status_t *status)
{ if (status) memset(status, 0, sizeof(*status)); return ESP_ERR_NOT_SUPPORTED; }
#endif
