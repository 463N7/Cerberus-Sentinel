#pragma once

#include "c5_spi_link.h" /* Existing normalized BLE inventory and raw packet shapes. */
#include "s3_ble_snapshot.h"
#include "s3_ble_tracker.h"
#include "ble_analyzer.h"
#include "watch_receive_timing.h"
#include "s3_ble_wardrive.h"
#include "s3_ble_mac_monitor.h"
#include "s3_ble_discovery.h"

/* Local S3 observer. Every sweep is bounded to eight seconds; the caller must
 * consume a completed generation before starting the next one. The default
 * remains passive for Watch, Wardriving, Fox Hunt, and Analyzer. */
esp_err_t cerberus_s3_ble_scanner_start(cerberus_ble_scan_status_payload_t *status);
/* Active 1M scan for the device list. Scan responses can add display fields
 * and separately proven passive hints to an exact same-sweep original report;
 * they never change that advertisement's raw bytes/time or active eligibility. */
esp_err_t cerberus_s3_ble_scanner_start_active(cerberus_ble_scan_status_payload_t *status);
esp_err_t cerberus_s3_ble_scanner_stop(cerberus_ble_scan_status_payload_t *status);
esp_err_t cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *status);
/* Atomic COMPLETE-bank status and local replacement count for this exact
 * generation. Both outputs are mandatory and cleared on failure. Replacement
 * discards retained inventory, while dropped_reports counts rejected reports;
 * neither is an RF packet-loss measurement. No RF or allocation is performed. */
esp_err_t cerberus_s3_ble_scanner_get_inventory_status(uint8_t generation,
    cerberus_ble_scan_status_payload_t *status, uint16_t *replacements);
esp_err_t cerberus_s3_ble_scanner_get_result(uint8_t generation, uint8_t index,
    cerberus_c5_ble_result_t *result);
/* One existing mutex protects the entire completed bank and its evidence.
 * Count must equal stored_count, and count <= capacity <= MAX_RESULTS. All
 * supplied capacity entries are cleared on failure (bounded to MAX_RESULTS
 * even for an invalid capacity). Null buffers are allowed only at capacity 0.
 * This getter never starts RF, allocates storage or changes source times. */
esp_err_t cerberus_s3_ble_scanner_copy_wardrive(uint8_t generation, uint8_t count,
    cerberus_c5_ble_result_t *rows, cerberus_s3_ble_wardrive_evidence_t *evidence,
    size_t capacity);
esp_err_t cerberus_s3_ble_scanner_get_advertisement(uint8_t generation, uint8_t index,
    cerberus_c5_ble_advertisement_t *result);
/* Time of that exact raw ADV, not the time a completed row was opened. */
esp_err_t cerberus_s3_ble_scanner_get_advertisement_time(uint8_t generation,
    uint8_t index, uint32_t *observed_ms);
esp_err_t cerberus_s3_ble_scanner_get_focus(uint8_t generation, uint8_t index,
    cerberus_s3_ble_focus_t *result);
esp_err_t cerberus_s3_ble_scanner_get_watch_status(uint8_t generation,
    cerberus_watch_scan_status_payload_t *status);
esp_err_t cerberus_s3_ble_scanner_get_watch_result(uint8_t generation, uint8_t index,
    cerberus_watch_observation_t *result);
/* One locked completed-row copy with optional qualifying-callback age. The
 * request anchor is sampled before waiting for the lock, never at UI paint. */
esp_err_t cerberus_s3_ble_scanner_get_watch_observed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result,
    cerberus_watch_receive_timing_t *timing);
/* Copy the row and its independent source/Location timing under one lock.
 * All outputs are mandatory and cleared together on failure. */
esp_err_t cerberus_s3_ble_scanner_get_watch_detailed_result(uint8_t generation,
    uint8_t index, cerberus_watch_observation_t *result,
    cerberus_watch_receive_timing_t *timing, sentinel_watch_motion_t *motion,
    cerberus_watch_receive_timing_t *motion_timing);
esp_err_t cerberus_s3_ble_scanner_get_activity(uint8_t generation,
    cerberus_ble_activity_payload_t *status);
/* Configure or clear an exact selected identity between bounded sweeps.
 * Measurements come directly from GAP reports, not strongest-RSSI inventory. */
esp_err_t cerberus_s3_ble_scanner_track_begin(const uint8_t address[6],
    uint8_t address_type);
void cerberus_s3_ble_scanner_track_end(void);
esp_err_t cerberus_s3_ble_scanner_track_status(
    cerberus_s3_ble_track_status_t *status);
/* Analyzer shares the same bounded passive sweeps. Its 64-bin event history
 * is allocated in PSRAM only while this view is active. */
esp_err_t cerberus_s3_ble_scanner_analyzer_begin(void);
void cerberus_s3_ble_scanner_analyzer_end(void);
esp_err_t cerberus_s3_ble_scanner_analyzer_status(
    cerberus_ble_analyzer_view_t *view);
