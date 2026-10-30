#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "c5_spi_link.h"
#include "c5_ready_wait.h"

#define DRAM_ATTR
#define IRAM_ATTR
#define SPI3_HOST 3
#define CERBERUS_ONBOARD_C5_READY_GPIO 44
#define CERBERUS_ONBOARD_C5_SPI_MISO_GPIO 13
#define GPIO_MODE_INPUT 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_ENABLE 1
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define ESP_INTR_FLAG_IRAM 4
#define pdTRUE 1
#define pdFALSE 0
#define pdMS_TO_TICKS(ms) (ms)
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(lock) ((void)(lock))
#define portEXIT_CRITICAL(lock) ((void)(lock))
#define ESP_LOGW(tag,...) ((void)(tag))
#define ESP_LOGE(tag,...) ((void)(tag))
#define ESP_LOGI(tag,...) ((void)(tag))
#define ESP_RETURN_ON_ERROR(expression,tag,...) do { \
    esp_err_t checked = (expression); (void)(tag); if (checked != ESP_OK) return checked; \
} while (0)
typedef int BaseType_t;
typedef unsigned TickType_t;
typedef void *SemaphoreHandle_t;
typedef void *spi_device_handle_t;
typedef int portMUX_TYPE;
typedef struct {unsigned length; const void *tx_buffer; void *rx_buffer;} spi_transaction_t;
typedef struct {uint64_t pin_bit_mask; int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
static int64_t esp_timer_get_time(void);
static void esp_rom_delay_us(uint32_t);
static int gpio_get_level(int);
static esp_err_t spi_device_polling_transmit(spi_device_handle_t,spi_transaction_t *);
static BaseType_t xSemaphoreTake(SemaphoreHandle_t,TickType_t);
static BaseType_t xSemaphoreGive(SemaphoreHandle_t);
static esp_err_t radio_bus_init(void);
static spi_device_handle_t radio_bus_onboard(void);
static SemaphoreHandle_t radio_bus_mutex(void);
static esp_err_t spi_device_get_actual_freq(spi_device_handle_t,int *);
static esp_err_t gpio_pullup_en(int);
static esp_err_t gpio_config(const gpio_config_t *);
static esp_err_t gpio_install_isr_service(int);
static esp_err_t gpio_set_intr_type(int,int);
static esp_err_t gpio_isr_handler_add(int,void(*)(void *),void *);
static esp_err_t gpio_intr_disable(int);
#include "c5_spi_transport_impl.h"
#include "c5_watch_link.h"

static uint32_t clock_us, low_at, high_at;
static bool high_level, scheduled, irq_enabled, fail_mutex, held;
static unsigned transfers, requests, releases, install_calls, handler_calls;
static unsigned corrupt_crc, wrong_sequence, wrong_command, wrong_length;
static uint32_t mutex_wait_us;
static unsigned watch_status_calls, corrupt_watch_crc;
static bool watch_unsupported, watch_status_changes, watch_invalid_status;
static bool watch_invalid_chunk, watch_invalid_record, activity_invalid;
static esp_err_t service_error, handler_error, spi_error;
static void (*irq_handler)(void *);
static cerberus_spi_frame_t reply;
enum pulse_kind {EARLY, DELAYED, NONE, NO_ISR, STUCK_LOW, REQUEST_ONLY, LEGACY};
static enum pulse_kind pulse;

static void falling(void)
{
    high_level = false;
    if (irq_enabled && irq_handler) irq_handler(NULL);
}
static int64_t esp_timer_get_time(void) { return clock_us; }
static void esp_rom_delay_us(uint32_t us)
{
    clock_us += us;
    if (scheduled && (int32_t)(clock_us - low_at) >= 0 && high_level) falling();
    if (scheduled && (int32_t)(clock_us - high_at) >= 0) {
        high_level = true;
        scheduled = false;
    }
}
static int gpio_get_level(int pin) {assert(pin==44);return high_level;}
static BaseType_t xSemaphoreTake(SemaphoreHandle_t mutex,TickType_t ticks)
{
    assert(mutex==(void *)1&&!held);
    if (fail_mutex) {clock_us += ticks*1000U;return pdFALSE;}
    clock_us += mutex_wait_us;held=true;return pdTRUE;
}
static BaseType_t xSemaphoreGive(SemaphoreHandle_t mutex)
{assert(mutex==(void *)1&&held);held=false;++releases;return pdTRUE;}
static esp_err_t radio_bus_init(void) {return ESP_OK;}
static spi_device_handle_t radio_bus_onboard(void) {return (void *)2;}
static SemaphoreHandle_t radio_bus_mutex(void) {return (void *)1;}
static esp_err_t spi_device_get_actual_freq(spi_device_handle_t device,int *khz)
{assert(device==(void *)2);*khz=40000;return ESP_OK;}
static esp_err_t gpio_pullup_en(int pin) {assert(pin==13);return ESP_OK;}
static esp_err_t gpio_config(const gpio_config_t *config)
{assert(config->intr_type==GPIO_INTR_DISABLE);return ESP_OK;}
static esp_err_t gpio_install_isr_service(int flags)
{assert(flags==ESP_INTR_FLAG_IRAM);++install_calls;return service_error;}
static esp_err_t gpio_set_intr_type(int pin,int type)
{assert(pin==44&&type==GPIO_INTR_NEGEDGE);return ESP_OK;}
static esp_err_t gpio_isr_handler_add(int pin,void(*handler)(void *),void *arg)
{
    assert(pin==44&&arg==NULL);++handler_calls;
    if (handler_error!=ESP_OK) return handler_error;
    irq_handler=handler;irq_enabled=true;return ESP_OK;
}
static esp_err_t gpio_intr_disable(int pin)
{assert(pin==44);irq_enabled=false;return ESP_OK;}

static size_t model_reply_payload(const cerberus_spi_frame_t *frame, uint8_t *payload)
{
    if (frame->command == CERBERUS_SPI_COMMAND_STATUS) {
        cerberus_status_payload_t value = {
            .ready = 1U, .firmware_version = CERBERUS_WATCH_MIN_FIRMWARE,
            .capabilities = watch_unsupported ? 0U :
                CERBERUS_C5_CAP_WATCH_OBSERVATIONS | CERBERUS_C5_CAP_BLE_ACTIVITY,
        };
        memcpy(payload, &value, sizeof(value)); return sizeof(value);
    }
    if (frame->command == CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS) {
        cerberus_watch_scan_request_t query;
        assert(frame->payload_length == sizeof(query));
        memcpy(&query, frame->payload, sizeof(query));
        cerberus_watch_scan_status_payload_t value = {
            .radio = query.radio, .generation = query.generation,
            .state = CERBERUS_WATCH_COMPLETE, .stored_count = 1U,
            .reports = 10U, .duration_ms = 1000U,
        };
        if (watch_invalid_status) ++value.generation;
        if (++watch_status_calls > 1U && watch_status_changes) ++value.reports;
        memcpy(payload, &value, sizeof(value)); return sizeof(value);
    }
    if (frame->command == CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA) {
        cerberus_watch_scan_data_request_t query;
        assert(frame->payload_length == sizeof(query));
        memcpy(&query, frame->payload, sizeof(query));
        cerberus_watch_observation_t record = {
            .address = {2U, 0U, 0U, 0U, 0U, 1U}, .rssi = -60,
            .channel = 8U, .kind = SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI,
            .transport = SENTINEL_WATCH_TRANSPORT_WIFI_BEACON,
            .flags = SENTINEL_WATCH_PROTOCOL_HAS_ID, .id = "node",
        };
        if (watch_invalid_record) record.flags = 0x80U;
        size_t offset = query.chunk * CERBERUS_WATCH_DATA_CHUNK_SIZE;
        assert(offset < sizeof(record));
        size_t count = sizeof(record) - offset;
        if (count > CERBERUS_WATCH_DATA_CHUNK_SIZE) count = CERBERUS_WATCH_DATA_CHUNK_SIZE;
        cerberus_watch_scan_data_payload_t value = {
            .radio = query.radio, .generation = query.generation,
            .index = query.index, .chunk = query.chunk, .byte_count = (uint8_t)count,
        };
        if (watch_invalid_chunk) ++value.generation;
        memcpy(value.bytes, (const uint8_t *)&record + offset, count);
        memcpy(payload, &value, sizeof(value)); return sizeof(value);
    }
    if (frame->command == CERBERUS_SPI_COMMAND_BLE_ACTIVITY) {
        cerberus_watch_scan_request_t query;
        assert(frame->payload_length == sizeof(query));
        memcpy(&query, frame->payload, sizeof(query));
        cerberus_ble_activity_payload_t value = {
            .generation = query.generation, .state = CERBERUS_WATCH_COMPLETE,
            .duration_ms = 1000U, .reports = 12U, .valid_reports = 10U,
            .identities = 3U, .peak_reports_per_second = 5U,
        };
        if (activity_invalid) value.valid_reports = 13U;
        memcpy(payload, &value, sizeof(value)); return sizeof(value);
    }
    cerberus_wifi_scan_status_payload_t value = {.generation = 7, .state = CERBERUS_WIFI_SCAN_RUNNING};
    memcpy(payload, &value, sizeof(value)); return sizeof(value);
}

static esp_err_t spi_device_polling_transmit(spi_device_handle_t device,spi_transaction_t *tx)
{
    assert(device==(void *)2&&tx->length==CERBERUS_SPI_TRANSFER_SIZE*8U&&held);
    ++transfers;clock_us+=3U;
    bool request=tx->tx_buffer!=NULL;
    if (request) {
        ++requests;
        const cerberus_spi_frame_t *frame=tx->tx_buffer;
        assert(cerberus_protocol_frame_is_valid(frame));
        uint8_t value[CERBERUS_SPI_FRAME_PAYLOAD_SIZE] = {0};
        size_t value_size = model_reply_payload(frame, value);
        assert(cerberus_protocol_prepare_frame(&reply,frame->command,frame->sequence,value,value_size));
        if (wrong_sequence) {--wrong_sequence;
            assert(cerberus_protocol_prepare_frame(&reply,frame->command,(uint8_t)(frame->sequence-1U),value,value_size));}
        if (wrong_command) {--wrong_command;
            assert(cerberus_protocol_prepare_frame(&reply,(uint8_t)(frame->command+1U),frame->sequence,value,value_size));}
        if (wrong_length) {--wrong_length;
            assert(cerberus_protocol_prepare_frame(&reply,frame->command,frame->sequence,value,value_size-1U));}
        if (corrupt_crc) {--corrupt_crc;reply.payload[0]^=1U;}
        if (frame->command == CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS && corrupt_watch_crc) {
            --corrupt_watch_crc; reply.payload[0] ^= 1U;
        }
    } else memcpy(tx->rx_buffer,&reply,sizeof(reply));
    if (spi_error!=ESP_OK) return spi_error;
    if (pulse==EARLY || (pulse==REQUEST_ONLY&&request)) {falling();high_level=true;}
    else if (pulse==DELAYED) {scheduled=true;low_at=clock_us+40U;high_at=clock_us+80U;}
    else if (pulse==NO_ISR) high_level=false;
    else if (pulse==STUCK_LOW) falling();
    return ESP_OK;
}

static void reset_case(enum pulse_kind kind)
{
    memset(&transport_diagnostics,0,sizeof(transport_diagnostics));
    atomic_store(&ready_fall_epoch,0U);sequence=0U;mutex_acquired_us=0U;
    clock_us=low_at=high_at=0U;high_level=kind!=LEGACY;scheduled=false;
    fail_mutex=held=false;transfers=requests=releases=0U;mutex_wait_us=0U;
    corrupt_crc=wrong_sequence=wrong_command=wrong_length=0U;spi_error=ESP_OK;pulse=kind;
    watch_status_calls = corrupt_watch_crc = 0U;
    watch_unsupported = watch_status_changes = watch_invalid_status = false;
    watch_invalid_chunk = watch_invalid_record = activity_invalid = false;
}
static void scan_ok(void)
{
    cerberus_wifi_scan_status_payload_t status={0};
    assert(c5_spi_link_get_wifi_scan_status(&status)==ESP_OK);
    assert(status.generation==7&&status.state==CERBERUS_WIFI_SCAN_RUNNING&&!held);
}

static bool bytes_zero(const void *value, size_t length)
{
    const uint8_t *bytes = value;
    for (size_t i = 0U; i < length; ++i) if (bytes[i]) return false;
    return true;
}

static void watch_lock_checks(void)
{
    cerberus_watch_scan_status_payload_t status;
    cerberus_watch_observation_t record;
    cerberus_ble_activity_payload_t activity;

    for (unsigned route = 0U; route < 3U; ++route) {
        reset_case(EARLY); fail_mutex = true;
        memset(&status, 0xa5, sizeof(status));
        memset(&record, 0xa5, sizeof(record));
        memset(&activity, 0xa5, sizeof(activity));
        esp_err_t error = route == 0U ? c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI, 7U, &status) :
            route == 1U ? c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI, 7U, 0U, &record) :
            c5_spi_link_get_ble_activity(7U, &activity);
        assert(error == ESP_ERR_TIMEOUT && !held && releases == 0U && transfers == 0U);
        assert(transport_diagnostics.mutex_timeouts == 1U && transport_diagnostics.max_mutex_wait_us == 250000U);
        assert(route == 0U ? bytes_zero(&status, sizeof(status)) :
            route == 1U ? bytes_zero(&record, sizeof(record)) : bytes_zero(&activity, sizeof(activity)));
    }

    reset_case(EARLY); mutex_wait_us = 70000U;
    assert(c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI, 7U, &status) == ESP_OK);
    assert(status.generation == 7U && status.reports == 10U && status.stored_count == 1U);
    assert(requests == 2U && releases == 1U && !held && transport_diagnostics.max_mutex_wait_us == 70000U);
    assert(transport_diagnostics.max_mutex_hold_us == 12U && transport_diagnostics.exchanges == 2U);

    reset_case(EARLY); mutex_wait_us = 80000U;
    assert(c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI, 7U, 0U, &record) == ESP_OK);
    assert(record.channel == 8U && record.rssi == -60 && strcmp(record.id, "node") == 0);
    unsigned result_exchanges = 3U + (sizeof(record) + CERBERUS_WATCH_DATA_CHUNK_SIZE - 1U) / CERBERUS_WATCH_DATA_CHUNK_SIZE;
    assert(requests == result_exchanges && releases == 1U && !held);
    assert(transport_diagnostics.max_mutex_wait_us == 80000U &&
        transport_diagnostics.max_mutex_hold_us == result_exchanges * 6U);

    reset_case(EARLY); mutex_wait_us = 90000U;
    assert(c5_spi_link_get_ble_activity(7U, &activity) == ESP_OK);
    assert(activity.generation == 7U && activity.reports == 12U && activity.valid_reports == 10U);
    assert(releases == 1U && !held && transport_diagnostics.max_mutex_wait_us == 90000U);
    assert(transport_diagnostics.max_mutex_hold_us == 12U);

    for (unsigned route = 0U; route < 3U; ++route) {
        reset_case(EARLY); watch_unsupported = true;
        esp_err_t error = route == 0U ? c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI, 7U, &status) :
            route == 1U ? c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI, 7U, 0U, &record) :
            c5_spi_link_get_ble_activity(7U, &activity);
        assert(error == ESP_ERR_NOT_SUPPORTED && releases == 1U && !held && requests == 1U);
        assert(transport_diagnostics.max_mutex_hold_us == 6U);
        assert(route == 0U ? bytes_zero(&status, sizeof(status)) :
            route == 1U ? bytes_zero(&record, sizeof(record)) : bytes_zero(&activity, sizeof(activity)));
    }

    reset_case(EARLY); watch_invalid_status = true;
    assert(c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI, 7U, &status) == ESP_ERR_INVALID_RESPONSE);
    assert(bytes_zero(&status, sizeof(status)) && !held && releases == 1U);
    reset_case(EARLY); corrupt_watch_crc = 3U;
    assert(c5_spi_link_get_watch_status(CERBERUS_WATCH_RADIO_WIFI, 7U, &status) == ESP_ERR_INVALID_RESPONSE);
    assert(bytes_zero(&status, sizeof(status)) && !held && releases == 1U && requests == 4U);
    for (unsigned failure = 0U; failure < 3U; ++failure) {
        reset_case(EARLY);
        watch_invalid_chunk = failure == 0U;
        watch_status_changes = failure == 1U;
        watch_invalid_record = failure == 2U;
        assert(c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI, 7U, 0U, &record) == ESP_ERR_INVALID_RESPONSE);
        assert(bytes_zero(&record, sizeof(record)) && !held && releases == 1U);
    }
    reset_case(EARLY); activity_invalid = true;
    assert(c5_spi_link_get_ble_activity(7U, &activity) == ESP_ERR_INVALID_RESPONSE);
    assert(bytes_zero(&activity, sizeof(activity)) && !held && releases == 1U);
    reset_case(EARLY);
    assert(c5_spi_link_get_watch_result(CERBERUS_WATCH_RADIO_WIFI, 7U, 1U, &record) == ESP_ERR_INVALID_ARG);
    assert(bytes_zero(&record, sizeof(record)) && !held && releases == 1U && requests == 2U);
    reset_case(EARLY);
    assert(c5_spi_link_get_watch_status(0U, 7U, &status) == ESP_ERR_INVALID_ARG);
    assert(c5_spi_link_get_watch_result(0U, 7U, 0U, &record) == ESP_ERR_INVALID_ARG);
    assert(c5_spi_link_get_ble_activity(7U, NULL) == ESP_ERR_INVALID_ARG);
    assert(!transfers && !releases && !held && !transport_diagnostics.mutex_timeouts);
}

int main(void)
{
    service_error=ESP_ERR_NO_MEM;
    assert(c5_spi_link_init()==ESP_ERR_NO_MEM&&!link_mutex&&handler_calls==0U);
    service_error=ESP_ERR_INVALID_STATE;handler_error=ESP_FAIL;
    assert(c5_spi_link_init()==ESP_FAIL&&!link_mutex&&!irq_enabled);
    handler_error=ESP_OK;
    assert(c5_spi_link_init()==ESP_OK&&link_mutex&&irq_enabled);
    unsigned installs=install_calls;assert(c5_spi_link_init()==ESP_OK&&install_calls==installs);

    reset_case(EARLY);scan_ok();scan_ok();
    assert(transfers==4U&&requests==2U&&clock_us==12U);
    assert(transport_diagnostics.ready_latched_pulses==4U);
    assert(!transport_diagnostics.ready_fall_timeouts&&!transport_diagnostics.legacy_fallbacks);
    assert(transport_diagnostics.max_exchange_us==6U&&transport_diagnostics.max_mutex_hold_us==6U);

    reset_case(DELAYED);scan_ok();
    assert(clock_us==166U&&!transport_diagnostics.ready_latched_pulses);
    assert(!transport_diagnostics.ready_fall_timeouts);
    reset_case(REQUEST_ONLY);scan_ok();
    assert(transport_diagnostics.ready_fall_timeouts==1U&&clock_us==100006U);
    /* A request edge cannot satisfy the response transfer's new baseline. */
    assert(transport_diagnostics.ready_latched_pulses==1U);

    reset_case(NONE);scan_ok();
    assert(clock_us==110006U&&transport_diagnostics.legacy_fallbacks==1U);
    assert(transport_diagnostics.ready_fall_timeouts==1U);
    reset_case(NO_ISR);scan_ok();
    assert(transport_diagnostics.ready_fall_timeouts==1U);
    /* Merely low, with its ISR still pending, must never count as consumed. */
    reset_case(STUCK_LOW);scan_ok();
    assert(transport_diagnostics.ready_high_timeouts==1U&&clock_us==110006U);
    reset_case(LEGACY);scan_ok();
    assert(clock_us==110006U&&transport_diagnostics.ready_initial_timeouts==1U);

    reset_case(EARLY);wrong_sequence=1U;scan_ok();assert(requests==2U&&sequence==2U);
    reset_case(EARLY);corrupt_crc=1U;scan_ok();assert(requests==2U&&sequence==2U);
    reset_case(EARLY);wrong_command=1U;scan_ok();assert(requests==2U&&sequence==2U);
    reset_case(EARLY);wrong_length=1U;scan_ok();assert(requests==2U&&sequence==2U);
    reset_case(EARLY);wrong_sequence=3U;
    cerberus_wifi_scan_status_payload_t status={0};
    assert(c5_spi_link_get_wifi_scan_status(&status)==ESP_ERR_INVALID_RESPONSE);
    assert(requests==3U&&sequence==3U&&!held&&releases==1U);
    reset_case(EARLY);spi_error=ESP_FAIL;
    assert(c5_spi_link_get_wifi_scan_status(&status)==ESP_ERR_INVALID_RESPONSE);
    assert(transport_diagnostics.spi_errors==6U&&!held);

    reset_case(EARLY);fail_mutex=true;
    assert(c5_spi_link_get_wifi_scan_status(&status)==ESP_ERR_TIMEOUT);
    assert(!transfers&&transport_diagnostics.mutex_timeouts==1U);
    assert(transport_diagnostics.max_mutex_wait_us==100000U);
    reset_case(EARLY);mutex_wait_us=70000U;scan_ok();
    assert(transport_diagnostics.max_mutex_wait_us==70000U);
    assert(transport_diagnostics.max_mutex_hold_us==6U);

    reset_case(DELAYED);clock_us=UINT32_MAX-50U;
    atomic_store(&ready_fall_epoch,UINT32_MAX);scan_ok();
    assert(ready_epoch()==1U&&transport_diagnostics.max_exchange_us==166U);
    cerberus_c5_transport_diagnostics_t snapshot={0};
    unsigned before=transfers;c5_spi_link_get_transport_diagnostics(&snapshot);
    assert(transfers==before&&snapshot.ready_fall_edges==1U&&snapshot.exchanges==1U);
    watch_lock_checks();
    puts("C5 READY: pulses/faults/framing/metrics/init/wrap; Watch status/chunks/activity lock metrics, release paths and immutable validated snapshots passed.");
    return 0;
}
