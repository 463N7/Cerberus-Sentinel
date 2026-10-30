#pragma once

#include "cerberus_protocol.h"
#include "s3_wifi_observation.h"
#include "watch_receive_timing.h"
#include "wifi_probe_observation.h"
#include "s3_wifi_relation.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t c5_spi_link_test_start(const cerberus_radio_test_config_t *config,cerberus_radio_test_status_t *status);
esp_err_t c5_spi_link_test_status(uint16_t lease_job_id,cerberus_radio_test_status_t *status);
esp_err_t c5_spi_link_ap_start(const cerberus_wifi_ap_job_t *request,cerberus_radio_test_status_t *status);
esp_err_t c5_spi_link_ap_validate(const cerberus_wifi_ap_job_t *request);
esp_err_t c5_spi_link_ap_stage(const cerberus_wifi_ap_job_t *request,
    const cerberus_wifi_ap_station_set_t *stations);
esp_err_t c5_spi_link_test_stop(uint16_t job_id,cerberus_radio_test_status_t *status);

typedef struct {
    uint32_t firmware_version;
    uint32_t uptime_ms;
    uint32_t capabilities;
    uint8_t protocol_version;
    uint8_t reset_reason;
    uint8_t boot_count;
    uint8_t ble_phase;
    uint16_t free_heap_kib;
    uint16_t minimum_free_heap_kib;
    uint8_t radio_state;
} cerberus_c5_status_t;

/* Passive ordinary-scan relationships. No station-target bank is read or
 * changed. Proof has full source-bank epoch plus peer identity and a local
 * request-start anchor; all row timestamps remain C5 callback uptime. */
typedef cerberus_s3_wifi_relation_meta_t cerberus_c5_wifi_relation_bank_t;
typedef bool (*cerberus_c5_relation_continue_fn)(void *context);
typedef struct {
    cerberus_c5_wifi_relation_bank_t pending;
    cerberus_c5_wifi_relation_bank_t expected;
    cerberus_wifi_relation_row_t row;
    cerberus_wifi_relation_row_t *rows;
    uint64_t frames, last_local_us, read_clock_us;
    uint32_t deadline_ms;
    esp_err_t error;
    uint8_t generation, capacity, phase, index, chunk;
    bool anchored, check_only, expect_bank;
} cerberus_c5_wifi_relation_reader_t;

/* Reader storage must initially be zeroed. Begin/cancel issue no SPI. Each
 * step performs at most one physical request/response attempt, with at most
 * 20 ms mutex wait. An in-flight READY exchange can overrun the deadline once;
 * afterward failure clears staging. Continue callbacks execute outside locks.
 * ESP_OK+complete=false means progress; only complete=true publishes proof.
 * Any error is terminal until begin, and clears all bounded caller rows.
 * Keep rows alive until cancel or terminal completion; no allocation occurs. */
esp_err_t c5_spi_link_wifi_relation_begin(cerberus_c5_wifi_relation_reader_t *,
    uint8_t generation, uint32_t deadline_ms, cerberus_wifi_relation_row_t *rows,
    size_t capacity);
/* UI AP publication must carry a proof obtained before AP staging and checked
 * again after its final scan-status fence. This start pins that exact bank,
 * including its original clock/anchor; gen8 alone never authorizes a bind. */
esp_err_t c5_spi_link_wifi_relation_begin_expected(cerberus_c5_wifi_relation_reader_t *,
    const cerberus_c5_wifi_relation_bank_t *expected, uint32_t deadline_ms,
    cerberus_wifi_relation_row_t *rows, size_t capacity);
esp_err_t c5_spi_link_wifi_relation_step(cerberus_c5_wifi_relation_reader_t *,
    cerberus_c5_wifi_relation_bank_t *proof, bool *complete,
    cerberus_c5_relation_continue_fn, void *context);
void c5_spi_link_wifi_relation_cancel(cerberus_c5_wifi_relation_reader_t *);
/* Worker-only wrappers drive the same incremental reader. UI callers use
 * step once per tick. NOT_SUPPORTED occurs only on initial old-peer proof;
 * a supported peer changing/rebooting/malforming never permits fallback. */
esp_err_t c5_spi_link_copy_wifi_relations(uint8_t generation, uint32_t deadline_ms,
    cerberus_c5_wifi_relation_bank_t *proof, cerberus_wifi_relation_row_t *rows,
    size_t capacity, cerberus_c5_relation_continue_fn, void *context);
esp_err_t c5_spi_link_get_wifi_relation_bank(uint8_t generation,
    uint32_t deadline_ms, cerberus_c5_wifi_relation_bank_t *proof);
esp_err_t c5_spi_link_check_wifi_relation_bank(
    const cerberus_c5_wifi_relation_bank_t *proof, uint32_t deadline_ms);

/* Completed passive probe bank plus peer identity. All callers own their
 * storage; these read-only queries allocate nothing and never start a scan. */
typedef struct {
    cerberus_wifi_probe_status_t status;
    cerberus_c5_status_t peer;
} cerberus_c5_wifi_probe_bank_t;
typedef bool (*cerberus_c5_probe_continue_fn)(void *context);

/* Absolute S3 uptime-ms deadline, less than 2^31 ms ahead. Cancellation is
 * checked outside the link mutex before/after each transaction and returns
 * NOT_FINISHED. An in-flight transaction is allowed to finish, then timeout
 * or cancellation clears all outputs. Callers must not publish rows before
 * success; no whole-bank temporary allocation is made. capacity is 0..16;
 * NULL rows are permitted only at capacity0. Failure clears the bounded rows
 * and proof, including unsupported old peers; no malformed-peer fallback.
 * COMPLETE but unavailable banks succeed with AVAILABLE clear and count0. */
esp_err_t c5_spi_link_copy_wifi_probe_results(uint8_t generation,
    uint32_t deadline_ms, cerberus_c5_wifi_probe_bank_t *proof,
    cerberus_wifi_probe_row_t *rows, size_t capacity,
    cerberus_c5_probe_continue_fn continue_fn, void *context);
esp_err_t c5_spi_link_get_wifi_probe_bank(uint8_t generation,
    uint32_t deadline_ms, cerberus_c5_wifi_probe_bank_t *proof);
/* Recheck after other staged reads, before any publication/SD writes. */
esp_err_t c5_spi_link_check_wifi_probe_bank(const cerberus_c5_wifi_probe_bank_t *proof,
    uint32_t deadline_ms);

typedef struct {
    char ssid[CERBERUS_WIFI_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
} cerberus_c5_wifi_result_t;

/* Optional presence-age evidence from the same result response. Old firmware
 * still returns its inventory with observation.valid=false; no extra SPI query. */
esp_err_t c5_spi_link_get_wifi_scan_observed_result(uint8_t generation,
    uint8_t index, cerberus_c5_wifi_result_t *result,
    cerberus_s3_wifi_observation_t *observation);

typedef struct {
    char name[CERBERUS_BLE_NAME_MAX_LENGTH + 1U];
    uint8_t address[6];
    int8_t rssi;
    uint8_t address_type;
    uint16_t manufacturer_id;
    uint8_t flags;
    uint8_t uuid_length;
    uint8_t uuid[CERBERUS_BLE_UUID_MAX_LENGTH];
} cerberus_c5_ble_result_t;

/* Latest received legacy advertising packet for one completed-scan record.
   No scan-response merging. reports_seen counts delivered advertising
   reports; flags describe truncation or malformed AD TLVs in this packet. */
typedef struct {
    uint8_t data[CERBERUS_BLE_ADVERTISEMENT_MAX_LENGTH];
    uint8_t length;
    uint8_t event_type;
    uint8_t flags;
    uint16_t reports_seen;
    uint8_t address[6];
    uint8_t address_type;
} cerberus_c5_ble_advertisement_t;

typedef struct {
    uint8_t uuid_length;
    uint8_t uuid[16];
    uint16_t start_handle;
    uint16_t end_handle;
} cerberus_c5_ble_service_t;

typedef struct {
    uint8_t uuid_length;
    uint8_t uuid[16];
    uint16_t definition_handle;
    uint16_t value_handle;
    uint8_t properties;
} cerberus_c5_ble_characteristic_t;

esp_err_t c5_spi_link_init(void);
uint32_t c5_spi_link_clock_hz(void);
typedef struct {
    uint32_t exchanges;
    uint32_t mutex_timeouts;
    uint32_t ready_initial_timeouts;
    uint32_t ready_fall_timeouts;
    uint32_t ready_high_timeouts;
    uint32_t ready_latched_pulses;
    uint32_t legacy_fallbacks;
    uint32_t spi_errors;
    uint32_t max_mutex_wait_us;
    uint32_t max_mutex_hold_us;
    uint32_t max_exchange_us;
    uint32_t ready_fall_edges;
} cerberus_c5_transport_diagnostics_t;
/* Local snapshot; performs no SPI transaction. Mutex hold covers onboard C5
 * calls only, while wait duration also reflects expansion-bus contention. */
void c5_spi_link_get_transport_diagnostics(cerberus_c5_transport_diagnostics_t *out);
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *status);
esp_err_t c5_spi_link_start_wifi_scan(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_stop_wifi_scan(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_radio_stats(cerberus_wifi_radio_stats_payload_t *stats);
esp_err_t c5_spi_link_wifi_channel_survey_start(uint8_t band,
    cerberus_wifi_channel_survey_status_payload_t *status);
esp_err_t c5_spi_link_wifi_channel_survey_stop(
    cerberus_wifi_channel_survey_status_payload_t *status);
esp_err_t c5_spi_link_wifi_channel_survey_status(
    cerberus_wifi_channel_survey_status_payload_t *status);
esp_err_t c5_spi_link_wifi_channel_survey_result(uint8_t generation, uint8_t index,
    cerberus_wifi_channel_survey_result_payload_t *result);
esp_err_t c5_spi_link_wifi_packet_monitor_start(uint8_t channel,
    cerberus_wifi_packet_monitor_status_payload_t *status);
esp_err_t c5_spi_link_wifi_packet_monitor_stop(uint8_t generation,
    cerberus_wifi_packet_monitor_status_payload_t *status);
esp_err_t c5_spi_link_wifi_packet_monitor_status(
    cerberus_wifi_packet_monitor_status_payload_t *status);
esp_err_t c5_spi_link_wifi_packet_monitor_sample(uint8_t generation,
    uint16_t sequence, cerberus_wifi_packet_monitor_sample_payload_t *sample);
/* Wi-Fi capture needs C5 firmware revision 45+. `read_frame` verifies CRC32
 * into caller storage and deliberately leaves the record pending until ACK. */
esp_err_t c5_spi_link_wifi_capture_start(
    const cerberus_wifi_capture_start_request_t *request,
    cerberus_wifi_capture_status_payload_t *status);
esp_err_t c5_spi_link_wifi_capture_status(cerberus_wifi_capture_status_payload_t *status);
esp_err_t c5_spi_link_wifi_capture_peek(uint32_t generation,
    cerberus_wifi_capture_peek_payload_t *peek);
esp_err_t c5_spi_link_wifi_capture_read(uint32_t generation,uint32_t sequence,
    uint16_t offset,cerberus_wifi_capture_read_payload_t *chunk);
esp_err_t c5_spi_link_wifi_capture_read_frame(
    const cerberus_wifi_capture_peek_payload_t *peek,uint8_t *frame,size_t capacity);
esp_err_t c5_spi_link_wifi_capture_ack(uint32_t generation,uint32_t sequence);
esp_err_t c5_spi_link_wifi_capture_stop(uint32_t generation,
    cerberus_wifi_capture_status_payload_t *status);
uint64_t c5_spi_link_wifi_capture_monotonic_us(
    const cerberus_wifi_capture_peek_payload_t *peek);
esp_err_t c5_spi_link_start_wifi_defense_survey(cerberus_wifi_defense_status_payload_t *status);
esp_err_t c5_spi_link_stop_wifi_defense_survey(cerberus_wifi_defense_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_defense_status(cerberus_wifi_defense_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_defense_result(
    uint8_t generation,
    uint8_t index,
    cerberus_wifi_defense_result_payload_t *result);
esp_err_t c5_spi_link_get_wifi_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_wifi_result_t *result);
esp_err_t c5_spi_link_get_wifi_scan_metadata(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_wifi_result_t *result);
esp_err_t c5_spi_link_connect_wifi(
    uint8_t generation,
    uint8_t index,
    const char *password,
    cerberus_wifi_connection_status_payload_t *status);
esp_err_t c5_spi_link_disconnect_wifi(cerberus_wifi_connection_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_connection_status(cerberus_wifi_connection_status_payload_t *status);
esp_err_t c5_spi_link_network_start(const cerberus_net_scan_request_t *request, cerberus_net_scan_status_t *status);
esp_err_t c5_spi_link_station_start(const cerberus_wifi_station_request_t *request,cerberus_wifi_station_status_t *status);
esp_err_t c5_spi_link_station_status(uint16_t lease_job_id,cerberus_wifi_station_status_t *status);
esp_err_t c5_spi_link_station_stop(uint16_t job_id,cerberus_wifi_station_status_t *status);
esp_err_t c5_spi_link_station_result(uint16_t job_id,uint8_t index,cerberus_wifi_station_result_t *result);
esp_err_t c5_spi_link_network_status(cerberus_net_scan_status_t *status);
esp_err_t c5_spi_link_network_stop(uint16_t job_id, cerberus_net_scan_status_t *status);
esp_err_t c5_spi_link_network_result(uint16_t job_id, uint8_t index, cerberus_net_scan_result_t *result);
esp_err_t c5_spi_link_get_wifi_dns_status(cerberus_wifi_dns_status_payload_t *status);
esp_err_t c5_spi_link_start_network_diagnostics(
    cerberus_network_diagnostics_status_payload_t *status);
esp_err_t c5_spi_link_get_network_diagnostics_status(
    cerberus_network_diagnostics_status_payload_t *status);
esp_err_t c5_spi_link_start_mdns_scan(cerberus_mdns_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_mdns_scan_status(cerberus_mdns_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_mdns_result(
    uint8_t generation,
    uint8_t index,
    cerberus_mdns_result_payload_t *result);
esp_err_t c5_spi_link_start_ble_scan(cerberus_ble_scan_status_payload_t *status);
esp_err_t c5_spi_link_stop_ble_scan(cerberus_ble_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_scan_status(cerberus_ble_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_result_t *result);
/* Requires revision 30+ and CERBERUS_C5_CAP_BLE_RAW_ADVERTISEMENT. Returns
   ESP_ERR_NOT_SUPPORTED for older firmware; output is empty on any failure. */
esp_err_t c5_spi_link_get_ble_advertisement(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_advertisement_t *result);
/* Revision 31+ capability-gated read-only APIs. Completed generations only;
   outputs are cleared on every failure, including mid-transfer generation
   changes. Watch records are independent of ordinary scan inventories. */
esp_err_t c5_spi_link_get_watch_status(uint8_t radio, uint8_t generation,
    cerberus_watch_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_watch_result(uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result);
/* Atomically retain the row and conservative callback-age evidence. Legacy
 * Watch firmware returns a valid row with zero/unavailable timing. NULL timing
 * preserves the original untimed getter's transactions. */
esp_err_t c5_spi_link_get_watch_observed_result(uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result,
    cerberus_watch_receive_timing_t *timing);
/* Atomic optional motion/Location-age extension. All four outputs are required
 * and cleared on failure. Peers before fw56 return canonical absent motion and
 * unavailable Location timing without issuing the new commands. */
esp_err_t c5_spi_link_get_watch_detailed_result(uint8_t radio, uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result,
    cerberus_watch_receive_timing_t *timing, sentinel_watch_motion_t *motion,
    cerberus_watch_receive_timing_t *motion_timing);
/* Revision 54+ bounded passive priority job. STATUS ID0 discovers the retained
 * high-water job; all mutations require an exact nonzero caller-chosen ID.
 * ESP_OK means a validated C5 reply, including REJECTED/error states. Only
 * preflight version/capability rejection returns NOT_SUPPORTED, without a job
 * command. Other failures clear output and may leave command delivery uncertain.
 * The caller retains ownership until a matching reply confirms release.
 * Follow requests require firmware55 before delivery. Requested-only STARTING
 * is pending; requested-only RUNNING is explicit SEARCH. FOLLOW_BOUND records
 * the C5-validated profile and survives terminal/cleanup replies. Generic
 * firmware54 requests keep the original zero flags and seven zero bytes. */
esp_err_t c5_spi_link_remote_id_start(const cerberus_remote_id_start_request_t *request,
    cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_status(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_stop(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_renew(uint64_t job_id,
    cerberus_remote_id_status_payload_t *status);
/* Bound variants additionally require the caller's expected peer snapshot.
 * This error is emitted only before any job command when fresh peer identity
 * differs; it is an authoritative no-delivery result, never a legacy fallback.
 * A peer change after delivery remains INVALID_RESPONSE/uncertain instead. */
#define CERBERUS_REMOTE_ID_ERR_PEER_CHANGED ((esp_err_t)-2101)
esp_err_t c5_spi_link_remote_id_start_for_peer(const cerberus_remote_id_start_request_t *request,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_status_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_stop_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_remote_id_renew_for_peer(uint64_t job_id,
    const cerberus_c5_status_t *expected_peer, cerberus_remote_id_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_activity(uint8_t generation,
    cerberus_ble_activity_payload_t *activity);
esp_err_t c5_spi_link_connect_ble(
    const cerberus_c5_ble_result_t *result,
    cerberus_ble_connection_status_payload_t *status);
esp_err_t c5_spi_link_disconnect_ble(cerberus_ble_connection_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_connection_status(cerberus_ble_connection_status_payload_t *status);
esp_err_t c5_spi_link_start_ble_service_discovery(
    cerberus_ble_gatt_service_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_service_status(
    cerberus_ble_gatt_service_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_service_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_service_t *result);
esp_err_t c5_spi_link_start_ble_characteristic_discovery(
    const cerberus_c5_ble_service_t *service,
    cerberus_ble_gatt_characteristic_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_characteristic_status(
    cerberus_ble_gatt_characteristic_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_characteristic_result(
    uint8_t generation,
    uint8_t index,
    cerberus_c5_ble_characteristic_t *result);
esp_err_t c5_spi_link_start_ble_characteristic_read(
    const cerberus_c5_ble_characteristic_t *characteristic,
    cerberus_ble_gatt_read_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_characteristic_read_status(
    cerberus_ble_gatt_read_status_payload_t *status);
esp_err_t c5_spi_link_get_ble_characteristic_read_data(
    uint8_t generation,
    uint8_t chunk,
    cerberus_ble_gatt_read_data_payload_t *result);
esp_err_t c5_spi_link_start_ieee802154_energy_scan(
    cerberus_ieee802154_energy_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_energy_scan_status(
    cerberus_ieee802154_energy_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_energy_scan_result(
    uint8_t generation,
    cerberus_ieee802154_energy_scan_result_payload_t *result);
esp_err_t c5_spi_link_start_ieee802154_beacon_scan(
    cerberus_ieee802154_beacon_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_beacon_scan_status(
    cerberus_ieee802154_beacon_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_beacon_scan_result(
    uint8_t generation,
    uint8_t index,
    cerberus_ieee802154_beacon_scan_result_payload_t *result);
esp_err_t c5_spi_link_start_ieee802154_activity_scan(
    cerberus_ieee802154_activity_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_activity_scan_status(
    cerberus_ieee802154_activity_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_ieee802154_activity_scan_result(
    uint8_t generation,
    uint8_t result_index,
    cerberus_ieee802154_activity_scan_result_payload_t *result);
esp_err_t c5_spi_link_get_event_queue_status(
    cerberus_c5_event_queue_status_payload_t *status);
esp_err_t c5_spi_link_pop_event(cerberus_c5_event_payload_t *event);
esp_err_t c5_spi_link_pop_event_batch(
    uint8_t max_events,
    cerberus_c5_event_batch_frame_t *batch);
esp_err_t c5_spi_link_configure_event_flow_control(
    uint8_t low_watermark,
    uint8_t high_watermark,
    bool enabled,
    cerberus_c5_event_queue_status_payload_t *status);
esp_err_t c5_spi_link_read_fragmented_object(
    uint8_t object_type,
    void *buffer,
    size_t capacity,
    uint16_t *length,
    uint32_t *crc32);

#ifdef __cplusplus
}
#endif
