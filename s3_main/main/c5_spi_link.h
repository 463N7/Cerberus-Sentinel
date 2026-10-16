#pragma once

#include "cerberus_protocol.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

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

typedef struct {
    char ssid[CERBERUS_WIFI_SSID_MAX_LENGTH + 1U];
    uint8_t bssid[6];
    int8_t rssi;
    uint8_t channel;
    uint8_t security;
} cerberus_c5_wifi_result_t;

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
esp_err_t c5_spi_link_get_status(cerberus_c5_status_t *status);
esp_err_t c5_spi_link_start_wifi_scan(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_stop_wifi_scan(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_scan_status(cerberus_wifi_scan_status_payload_t *status);
esp_err_t c5_spi_link_get_wifi_radio_stats(cerberus_wifi_radio_stats_payload_t *stats);
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
