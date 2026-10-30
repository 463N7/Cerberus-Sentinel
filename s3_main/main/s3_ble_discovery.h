#pragma once

#include "c5_spi_link.h"
#include "s3_ble_snapshot.h"

/* A local scan identity never aliases the public generation byte. Revision
 * changes only when the producer observes a report or a lifecycle transition;
 * neither copying nor rendering refreshes any source timestamp. */
typedef struct {
    uint64_t scan_epoch;
    uint64_t revision;
    uint64_t started_ms;
    uint64_t source_now_ms; /* Same S3 clock; copy anchor, never raw observation time. */
    cerberus_ble_scan_status_payload_t status;
    /* stored_count includes admitted rows whose current family hints cleared;
     * consumers derive matches and retire those typed nonmatching records. */
    cerberus_s3_ble_filter_t family;
    uint16_t replacements;
    uint16_t binding_overflows;
    bool attached;
    bool owned;
    bool stopping;
} cerberus_s3_ble_discovery_meta_t;

typedef struct {
    cerberus_c5_ble_result_t peer;
    cerberus_c5_ble_advertisement_t advertisement;
    cerberus_s3_ble_focus_t focus;
    uint32_t observed_ms; /* Exact original raw advertisement time; zero valid. */
    uint8_t generation;
    uint8_t index;
} cerberus_s3_ble_discovery_record_t;

typedef struct {
    cerberus_s3_ble_discovery_meta_t meta;
    cerberus_s3_ble_discovery_record_t records[CERBERUS_BLE_SCAN_MAX_RESULTS];
} cerberus_s3_ble_discovery_bank_t;

/* Initial Start requires expected_previous_epoch=0 and no attachment. Exact
 * same-family rearm is allowed only after the previous finite procedure and
 * queued cleanup have released. Rejected starts clear meta (no foreign token).
 * Accepted Start returns its epoch before the asynchronous SDK start executes.
 * A focused attachment owns one reusable PSRAM binding cache until release. */
esp_err_t cerberus_s3_ble_scanner_start_discovery(cerberus_s3_ble_filter_t family,
    uint64_t expected_previous_epoch, cerberus_s3_ble_discovery_meta_t *meta);

/* Scoped read/Stop/release return NOT_FOUND only for authoritative absence of
 * this exact attachment. TIMEOUT means ownership is unknown and must be kept.
 * All outputs clear on failure. Reads never initialize, allocate, or start RF.
 * Copies reject source clocks before any retained evidence and intervals
 * spanning a full32-bit timer revolution; no ambiguous source age is guessed.
 * Active or COMPLETE bank copies include every row under one bounded mutex. */
esp_err_t cerberus_s3_ble_scanner_discovery_status(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_meta_t *meta);
esp_err_t cerberus_s3_ble_scanner_copy_discovery(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_bank_t *bank);
esp_err_t cerberus_s3_ble_scanner_stop_discovery(uint64_t expected_epoch,
    cerberus_s3_ble_discovery_meta_t *meta);
esp_err_t cerberus_s3_ble_scanner_release_discovery(uint64_t expected_epoch);

/* Unscoped aggregate diagnostics only; never a control/adoption token. An
 * unattached or never-initialized scanner returns canonical zero/ESP_OK.
 * Attached state uses the same bounded lock and source-clock validation. */
esp_err_t cerberus_s3_ble_scanner_discovery_diagnostics(cerberus_s3_ble_discovery_meta_t *meta);
