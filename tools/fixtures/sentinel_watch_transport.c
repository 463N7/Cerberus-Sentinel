#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include "c5_spi_link.h"
#include "cerberus_watch_capture.h"

#define pdTRUE 1
#define pdMS_TO_TICKS(n) (n)
static void *link_mutex = (void *)1;
static bool link_held, fail_lock;
static int xSemaphoreTake(void *mutex, unsigned timeout)
{ (void)timeout; assert(mutex == link_mutex && !link_held); if (fail_lock) return 0; link_held = true; return 1; }
static void xSemaphoreGive(void *mutex) { assert(mutex == link_mutex && link_held); link_held = false; }
static uint8_t sequence;
static uint32_t remote_version = 31, remote_caps = CERBERUS_C5_CAPABILITIES;
static unsigned fault, status_reads, data_reads, activity_reads;
static cerberus_watch_capture_t remote[2];
static cerberus_ble_activity_capture_t activity;
static esp_err_t get_status_v2_locked(uint8_t request_sequence, cerberus_c5_status_t *out)
{
    (void)request_sequence; assert(link_held);
    out->firmware_version = remote_version; out->capabilities = remote_caps;
    return fault == 20 ? ESP_FAIL : ESP_OK;
}
static esp_err_t exchange_v2_locked(cerberus_spi_command_t command, const void *request,
    size_t request_length, void *response, size_t response_length)
{
    assert(link_held);
    if (command == CERBERUS_SPI_COMMAND_WATCH_SCAN_STATUS) {
        assert(request_length == sizeof(cerberus_watch_scan_request_t));
        assert(response_length == sizeof(cerberus_watch_scan_status_payload_t));
        const cerberus_watch_scan_request_t *q = request;
        cerberus_watch_scan_status_payload_t *out = response;
        assert(q->radio >= 1 && q->radio <= 2); *out = remote[q->radio - 1].status; ++status_reads;
        if (fault == 1) out->generation++;
        if (fault == 2) out->radio++;
        if (fault == 3) out->state = CERBERUS_WATCH_RUNNING;
        if (fault == 4) out->stored_count = 13;
        if (fault == 5) out->flags = 0x100;
        if (fault == 6) out->flags = CERBERUS_WATCH_CAPTURE_UNAVAILABLE;
        if (fault == 7 && status_reads == 2) out->reports++;
        if (fault == 8) return ESP_FAIL;
        return ESP_OK;
    }
    if (command == CERBERUS_SPI_COMMAND_BLE_ACTIVITY) {
        assert(request_length == sizeof(cerberus_watch_scan_request_t));
        assert(response_length == sizeof(cerberus_ble_activity_payload_t));
        cerberus_ble_activity_payload_t *out = response; *out = activity.status; ++activity_reads;
        if (fault == 1) out->generation++;
        if (fault == 2) out->state = CERBERUS_WATCH_RUNNING;
        if (fault == 3) out->flags = 8;
        if (fault == 4) out->valid_reports = out->reports + 1;
        if (fault == 5) out->identities = 129;
        if (fault == 6) out->peak_reports_per_second = (uint16_t)(out->reports + 1);
        if (fault == 7) out->reserved[0] = 1;
        if (fault == 8) return ESP_FAIL;
        return ESP_OK;
    }
    assert(command == CERBERUS_SPI_COMMAND_WATCH_SCAN_DATA); /* No RF commands allowed. */
    assert(request_length == sizeof(cerberus_watch_scan_data_request_t));
    assert(response_length == sizeof(cerberus_watch_scan_data_payload_t));
    const cerberus_watch_scan_data_request_t *q = request;
    cerberus_watch_scan_data_payload_t *out = response; ++data_reads;
    if (fault == 9 && q->chunk == 1) remote[q->radio - 1].status.generation++;
    bool ok = cerberus_watch_capture_get_chunk(&remote[q->radio - 1], q->radio, q->generation, q->index, q->chunk, out);
    if (fault == 10) out->radio++;
    if (fault == 11) out->generation++;
    if (fault == 12) out->index++;
    if (fault == 13) out->chunk++;
    if (fault == 14) out->byte_count = 18;
    if (fault == 15) out->byte_count = 0;
    if (fault == 16 && q->chunk == 2) out->byte_count = 11;
    if (fault == 17 && q->chunk == 2) return ESP_FAIL;
    return ok ? ESP_OK : ESP_ERR_INVALID_STATE;
}
#include "c5_watch_link.h"

/* Compile the real promiscuous receive callback against a packet/lock fixture. */
typedef enum { WIFI_PKT_MGMT, WIFI_PKT_CTRL, WIFI_PKT_DATA, WIFI_PKT_MISC } wifi_promiscuous_pkt_type_t;
typedef struct { uint16_t sig_len; uint8_t rx_state, channel; int8_t rssi, noise_floor; } wifi_pkt_rx_ctrl_t;
typedef struct { wifi_pkt_rx_ctrl_t rx_ctrl; uint8_t payload[2308]; } wifi_promiscuous_pkt_t;
static bool critical_held, defense_capture_active, watch_capture_active;
static unsigned defense_capture_mux;
static uint32_t defense_management_frames, defense_control_frames, defense_data_frames;
static uint32_t defense_beacon_frames, defense_deauth_frames, defense_disassoc_frames, defense_noise_samples;
static int64_t defense_noise_sum;
static int8_t defense_strongest_rssi;
static cerberus_watch_capture_t watch_capture;
static void critical_enter(unsigned *mux) { assert(mux == &defense_capture_mux && !critical_held); critical_held = true; }
static void critical_exit(unsigned *mux) { assert(mux == &defense_capture_mux && critical_held); critical_held = false; }
#define portENTER_CRITICAL(m) critical_enter(m)
#define portEXIT_CRITICAL(m) critical_exit(m)
/* ACTUAL_WIFI_CALLBACK */

static void empty(const void *memory, size_t length)
{ for (size_t i = 0; i < length; ++i) assert(((const uint8_t *)memory)[i] == 0); }
static sentinel_watch_protocol_record_t drone(void)
{
    sentinel_watch_protocol_record_t p = {.kind = SENTINEL_WATCH_PROTOCOL_DRONE,
        .transport = SENTINEL_WATCH_TRANSPORT_BLE, .flags = SENTINEL_WATCH_PROTOCOL_HAS_ID};
    memcpy(p.id, "DRONE-SERIAL-123", 16); return p;
}
static void reset(void)
{
    assert(!link_held && !critical_held); link_mutex = (void *)1; fail_lock = false;
    remote_version = 31; remote_caps = CERBERUS_C5_CAPABILITIES;
    fault = status_reads = data_reads = activity_reads = 0;
    cerberus_watch_capture_begin(&remote[0], CERBERUS_WATCH_RADIO_BLE, 255, 1000);
    cerberus_watch_capture_begin(&remote[1], CERBERUS_WATCH_RADIO_WIFI, 255, 1000);
    cerberus_ble_activity_begin(&activity, 255, 1000, true);
}
static void test_capture(void)
{
    reset(); uint8_t addr[6] = {1,2,3,4,5,6}; sentinel_watch_protocol_record_t p = drone();
    cerberus_watch_capture_observe(&remote[0], NULL, addr, 1, -70, 0);
    assert(remote[0].status.reports == 1 && !remote[0].status.stored_count);
    cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -60, 0);
    assert(remote[0].status.stored_count == 1);
    sentinel_watch_protocol_record_t loc = {.kind = p.kind, .transport = p.transport,
        .flags = SENTINEL_WATCH_PROTOCOL_HAS_POSITION | SENTINEL_WATCH_PROTOCOL_HAS_ALTITUDE,
        .latitude_e7 = 123456789, .longitude_e7 = -987654321, .altitude_dm = 1234,
        .altitude_kind = SENTINEL_WATCH_ALTITUDE_GEODETIC};
    cerberus_watch_capture_observe(&remote[0], &loc, addr, 1, -50, 0);
    assert(remote[0].status.stored_count == 1 && remote[0].records[0].flags == 7);
    assert(!strcmp(remote[0].records[0].id, p.id) && remote[0].records[0].altitude_dm == 1234);
    memcpy(p.id, "OTHER-SERIAL-999", 16);
    cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -40, 0);
    assert(remote[0].records[0].flags == 1 && !remote[0].records[0].latitude_e7 && !remote[0].records[0].altitude_dm);
    cerberus_watch_capture_observe(&remote[0], &loc, addr, 0, -80, 0);
    assert(remote[0].status.stored_count == 2 && remote[0].records[1].flags == 6);
    for (unsigned i = 2; i < 15; ++i) { addr[0] = (uint8_t)i; cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -60, 0); }
    assert(remote[0].status.stored_count == 12 && remote[0].status.dropped_records == 3);
    assert(remote[0].status.flags == CERBERUS_WATCH_CAPTURE_OVERFLOW);
    remote[0].status.dropped_records = UINT16_MAX; remote[0].status.reports = UINT32_MAX;
    cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -60, 0);
    assert(remote[0].status.dropped_records == UINT16_MAX && remote[0].status.reports == UINT32_MAX);
    cerberus_watch_scan_data_payload_t chunk;
    assert(!cerberus_watch_capture_get_chunk(&remote[0], 1, 255, 0, 0, &chunk)); empty(&chunk, sizeof(chunk));
    cerberus_watch_capture_finish(&remote[0], CERBERUS_WATCH_COMPLETE, 9000);
    assert(remote[0].status.duration_ms == 8000);
    assert(cerberus_watch_capture_get_chunk(&remote[0], 1, 255, 0, 2, &chunk) && chunk.byte_count == 12);
    assert(!cerberus_watch_capture_get_chunk(&remote[0], 1, 0, 0, 0, &chunk)); empty(&chunk, sizeof(chunk));
    assert(!cerberus_watch_capture_get_chunk(&remote[0], 2, 255, 0, 0, &chunk));
    assert(!cerberus_watch_capture_get_chunk(&remote[0], 1, 255, 12, 0, &chunk));
    assert(!cerberus_watch_capture_get_chunk(&remote[0], 1, 255, 0, 3, &chunk));
    reset(); cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -60, 0);
    cerberus_watch_capture_finish(&remote[0], CERBERUS_WATCH_IDLE, 1500);
    assert(!remote[0].status.stored_count); empty(remote[0].records, sizeof(remote[0].records));
    cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -60, 0); assert(remote[0].status.reports == 1);
    cerberus_watch_capture_begin(&remote[0], 1, 0, UINT32_MAX - 20U);
    cerberus_watch_capture_finish(&remote[0], CERBERUS_WATCH_COMPLETE, 10U);
    assert(remote[0].status.duration_ms == 31U && !remote[0].status.stored_count);
}
static void test_activity(void)
{
    reset(); uint8_t addr[6] = {1,2,3,4,5,6}, valid[] = {2,1,6}, invalid[] = {3,1,6};
    for (unsigned i = 0; i < 500; ++i) cerberus_ble_activity_observe(&activity, addr, 1, valid, 3, true, 1999);
    assert(activity.status.reports == 500 && activity.status.valid_reports == 500 && activity.status.identities == 1);
    assert(!activity.status.peak_reports_per_second);
    cerberus_ble_activity_observe(&activity, addr, 1, invalid, 3, true, 2000);
    assert(activity.status.peak_reports_per_second == 500 && activity.status.valid_reports == 500);
    for (unsigned i = 2; i <= 130; ++i) { addr[0] = (uint8_t)i; cerberus_ble_activity_observe(&activity, addr, 1, valid, 3, false, 2001); }
    assert(activity.status.identities == 128 && (activity.status.flags & CERBERUS_BLE_ACTIVITY_IDENTITIES_SATURATED));
    assert(activity.status.valid_reports == 500); /* Incomplete extended packets count only reports. */
    cerberus_ble_activity_finish(&activity, CERBERUS_WATCH_COMPLETE, 2500);
    assert(activity.status.duration_ms == 1500 && activity.status.peak_reports_per_second == 500);
    uint32_t reports = activity.status.reports;
    cerberus_ble_activity_observe(&activity, addr, 1, valid, 3, true, 2501); assert(activity.status.reports == reports);
    reset(); activity.status.reports = activity.status.valid_reports = UINT32_MAX;
    cerberus_ble_activity_observe(&activity, addr, 1, valid, 3, true, 1500);
    assert(activity.status.reports == UINT32_MAX && (activity.status.flags & CERBERUS_BLE_ACTIVITY_COUNTER_SATURATED));
    reset(); cerberus_ble_activity_observe(&activity, NULL, 255, valid, 3, true, 1001);
    assert(activity.status.reports == 1 && activity.status.identities == 0);
    cerberus_ble_activity_begin(&activity, 0, UINT32_MAX - 999U, false);
    cerberus_ble_activity_observe(&activity, addr, 1, valid, 3, true, UINT32_MAX - 1U);
    cerberus_ble_activity_finish(&activity, CERBERUS_WATCH_COMPLETE, 0);
    assert(activity.status.duration_ms == 1000 && activity.status.peak_reports_per_second == 1);
}
static void seed(void)
{
    reset(); sentinel_watch_protocol_record_t p = drone(); uint8_t addr[6] = {1,2,3,4,5,6};
    cerberus_watch_capture_observe(&remote[0], &p, addr, 1, -50, 0);
    p.transport = SENTINEL_WATCH_TRANSPORT_WIFI_BEACON;
    cerberus_watch_capture_observe(&remote[1], &p, addr, 0, -60, 6);
    cerberus_watch_capture_finish(&remote[0], CERBERUS_WATCH_COMPLETE, 9000);
    cerberus_watch_capture_finish(&remote[1], CERBERUS_WATCH_COMPLETE, 9000);
    uint8_t ad[] = {2,1,6}; cerberus_ble_activity_observe(&activity, addr, 1, ad, 3, true, 1500);
    cerberus_ble_activity_finish(&activity, CERBERUS_WATCH_COMPLETE, 9000);
}
static void test_transfer(void)
{
    for (unsigned radio = 1; radio <= 2; ++radio) {
        seed(); cerberus_watch_observation_t result;
        assert(c5_spi_link_get_watch_result(radio, 255, 0, &result) == ESP_OK);
        assert(!memcmp(&result, &remote[radio - 1].records[0], sizeof(result)));
        assert(status_reads == 2 && data_reads == 3);
        for (unsigned f = 1; f <= 17; ++f) {
            seed(); fault = f; memset(&result, 0xa5, sizeof(result));
            assert(c5_spi_link_get_watch_result(radio, 255, 0, &result) != ESP_OK);
            empty(&result, sizeof(result)); assert(!link_held);
        }
    }
    for (unsigned f = 1; f <= 8; ++f) {
        seed(); fault = f; cerberus_ble_activity_payload_t output;
        memset(&output, 0xa5, sizeof(output)); assert(c5_spi_link_get_ble_activity(255, &output) != ESP_OK);
        empty(&output, sizeof(output)); assert(!link_held);
    }
    seed(); cerberus_ble_activity_payload_t metrics;
    assert(c5_spi_link_get_ble_activity(255, &metrics) == ESP_OK && metrics.reports == 1 && metrics.identities == 1);
    for (unsigned mode = 0; mode < 2; ++mode) {
        seed(); if (mode) remote_caps = 0; else remote_version = 30;
        cerberus_watch_observation_t out; cerberus_watch_scan_status_payload_t status;
        assert(c5_spi_link_get_watch_status(1, 255, &status) == ESP_ERR_NOT_SUPPORTED); empty(&status, sizeof(status));
        assert(c5_spi_link_get_watch_result(1, 255, 0, &out) == ESP_ERR_NOT_SUPPORTED); empty(&out, sizeof(out));
        assert(c5_spi_link_get_ble_activity(255, &metrics) == ESP_ERR_NOT_SUPPORTED); empty(&metrics, sizeof(metrics));
        assert(!status_reads && !data_reads && !activity_reads);
    }
    seed(); cerberus_watch_observation_t out;
    assert(c5_spi_link_get_watch_result(3, 255, 0, &out) == ESP_ERR_INVALID_ARG); empty(&out, sizeof(out));
    assert(c5_spi_link_get_watch_result(1, 255, 12, &out) == ESP_ERR_INVALID_ARG);
    fail_lock = true; assert(c5_spi_link_get_watch_result(1, 255, 0, &out) == ESP_ERR_TIMEOUT);
    fail_lock = false; link_mutex = NULL;
    assert(c5_spi_link_get_watch_result(1, 255, 0, &out) == ESP_ERR_INVALID_STATE); empty(&out, sizeof(out));
    for (unsigned f = 0; f < 8; ++f) {
        seed(); cerberus_watch_observation_t *r = &remote[0].records[0];
        if (f == 0) r->flags = 8;
        if (f == 1) r->channel = 6;
        if (f == 2) r->address_type = 4;
        if (f == 3) r->id[20] = 'Z';
        if (f == 4) { r->flags |= 2; r->latitude_e7 = 900000001; }
        if (f == 5) r->altitude_kind = 1;
        if (f == 6) r->kind = SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI;
        if (f == 7) { r->flags |= 4; r->altitude_kind = 1; r->altitude_dm = 317676; }
        assert(c5_spi_link_get_watch_result(1, 255, 0, &out) == ESP_ERR_INVALID_RESPONSE); empty(&out, sizeof(out));
    }
}
static void test_wifi_callback(void)
{
    reset(); wifi_promiscuous_pkt_t packet = {0}; packet.rx_ctrl.channel = 6; packet.rx_ctrl.rssi = -55;
    packet.payload[0] = 0x80;
    const uint8_t source[6] = {0xde,0xad,0xbe,0xef,0xde,0xad}; memcpy(packet.payload + 10, source, 6);
    const char json[] = "{\"name\":\"test-pwn\",\"identity\":\"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\",\"grid_version\":\"1.10.3\"}";
    packet.payload[36] = 222; packet.payload[37] = sizeof(json) - 1;
    memcpy(packet.payload + 38, json, sizeof(json) - 1); packet.rx_ctrl.sig_len = 38 + sizeof(json) - 1 + 4;
    /* Nonzero FCS would break TLV parsing unless callback really removes it. */
    memset(packet.payload + packet.rx_ctrl.sig_len - 4, 0xf1, 4);
    cerberus_watch_capture_begin(&watch_capture, 2, 255, 0); watch_capture_active = true;
    for (unsigned i = 1; i <= 2; ++i) { packet.payload[16] = (uint8_t)i; wifi_promiscuous_callback(&packet, WIFI_PKT_MGMT); }
    assert(watch_capture.status.stored_count == 2 && watch_capture.status.reports == 2);
    assert(watch_capture.records[0].address[0] == 1 && watch_capture.records[1].address[0] == 2);
    assert(watch_capture.records[0].kind == SENTINEL_WATCH_PROTOCOL_PWNAGOTCHI);
    packet.rx_ctrl.rx_state = 1; wifi_promiscuous_callback(&packet, WIFI_PKT_MGMT); assert(watch_capture.status.reports == 2);
    packet.rx_ctrl.rx_state = 0; packet.payload[37] = 255;
    wifi_promiscuous_callback(&packet, WIFI_PKT_MGMT);
    assert(watch_capture.status.stored_count == 2 && watch_capture.status.reports == 3);
    watch_capture_active = false; wifi_promiscuous_callback(&packet, WIFI_PKT_MGMT); assert(watch_capture.status.reports == 3);
    defense_capture_active = true; packet.payload[0] = 0xc0;
    wifi_promiscuous_callback(&packet, WIFI_PKT_MGMT);
    assert(defense_management_frames == 1 && defense_deauth_frames == 1);
    assert(watch_capture.status.reports == 3 && !critical_held);
}
int main(void)
{
    test_capture(); test_activity(); test_transfer(); test_wifi_callback();
    puts("PASS: Watch bounded captures, ID/location merge and identity change, 128-address lower bound, complete-bucket rates, overflow and rollover, actual Wi-Fi Pwn callback/FCS/source identity, SPI capability gates, all chunk and generation failures, zero-on-failure, and defensive survey coexistence.");
    return 0;
}
