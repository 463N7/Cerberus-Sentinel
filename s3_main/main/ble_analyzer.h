#pragma once

#include "sentinel_watch_detect.h"
#include "ble_meta_hints.h"
#include "ibeacon.h"
#include "ble_flock_hints.h"
#include "ble_hc_hints.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A bounded S3 BLE event-rate view. Each bucket counts advertising callbacks,
 * not unique devices or every over-the-air packet. A scanner owner serializes
 * observe, advance and snapshot; this core takes no locks or heap. */
enum {
    CERBERUS_BLE_ANALYZER_BUCKET_MS = 200U,
    CERBERUS_BLE_ANALYZER_HISTORY = 64U,
    CERBERUS_BLE_ANALYZER_NAME_MAX = 32U,
    CERBERUS_BLE_ANALYZER_FRESH_MS = 3000U,
};

typedef enum {
    CERBERUS_BLE_ANALYZER_EVIDENCE_NONE = 0,
    CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,
    CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT,
    CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT,
    CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT,
    CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT,
} cerberus_ble_analyzer_evidence_kind_t;

typedef struct {
    uint8_t address[6]; /* NimBLE byte order; display code reverses it. */
    uint8_t address_type;
    int8_t rssi_dbm;
    const uint8_t *advertisement;
    size_t advertisement_length;
    uint8_t advertisement_flags; /* Nonzero means incomplete/truncated. */
    bool complete; /* One complete report, never joined with scan response. */
    bool scan_response; /* Source of this individual event; no ADV association. */
} cerberus_ble_analyzer_report_t;

typedef struct {
    uint16_t history[CERBERUS_BLE_ANALYZER_HISTORY];
    uint16_t current_events;
    uint8_t history_count;
    uint32_t total_events;
    uint32_t unclassified_reports;
    uint32_t malformed_reports;
    uint32_t watch_match_events, ibeacon_format_events;
    uint32_t meta_hint_events, hc_name_hint_events;
    uint32_t flock_hint_events; /* Events with a packet hint or typed-address OUI clue, counted once. */
    uint32_t last_seen_age_ms;
    uint8_t last_address[6];
    uint8_t last_address_type;
    int8_t last_rssi_dbm;
    uint8_t last_name[CERBERUS_BLE_ANALYZER_NAME_MAX];
    uint8_t last_name_length;
    cerberus_ble_meta_evidence_t last_meta;
    cerberus_ble_flock_hints_t last_flock;
    bool last_scan_response, last_report_valid;
    cerberus_ibeacon_t last_ibeacon;
    bool last_ibeacon_valid;
    bool last_flock_oui_clue; /* Address-only context; never attributed to AD fields. */
    uint8_t last_hc_name; /* Exact name code from this latest valid event, not identity. */
    cerberus_ble_analyzer_evidence_kind_t evidence_kind;
    sentinel_watch_confidence_t evidence_confidence;
    const char *evidence_label; /* Static classifier-owned text. */
    const char *evidence_reason; /* Static classifier-owned text. */
    bool active;
    bool has_last;
    bool last_fresh;
} cerberus_ble_analyzer_view_t;

/* Statistics over completed visible 200 ms buckets only. Silence contributes
 * zero buckets; the partial current bucket and never-started history do not
 * enter the denominator. A UINT16_MAX bucket makes these stored totals lower
 * bounds, since callback counters saturate. No extra history is retained. */
typedef struct {
    uint32_t events;
    uint16_t peak;
    uint8_t buckets;
    bool capped;
} cerberus_ble_analyzer_window_t;

bool cerberus_ble_analyzer_window(const cerberus_ble_analyzer_view_t *view,
    cerberus_ble_analyzer_window_t *out);

typedef struct {
    uint16_t history[CERBERUS_BLE_ANALYZER_HISTORY];
    uint16_t current_events;
    uint8_t next_history, history_count;
    uint32_t bucket_start_ms, total_events, unclassified_reports;
    uint32_t malformed_reports, watch_match_events, ibeacon_format_events;
    uint32_t meta_hint_events, hc_name_hint_events, last_seen_ms;
    uint32_t flock_hint_events;
    uint8_t last_address[6], last_address_type;
    int8_t last_rssi_dbm;
    uint8_t last_name[CERBERUS_BLE_ANALYZER_NAME_MAX], last_name_length;
    cerberus_ble_meta_evidence_t last_meta;
    cerberus_ble_flock_hints_t last_flock;
    bool last_scan_response, last_report_valid;
    cerberus_ibeacon_t last_ibeacon;
    bool last_ibeacon_valid;
    bool last_flock_oui_clue;
    uint8_t last_hc_name;
    cerberus_ble_analyzer_evidence_kind_t evidence_kind;
    sentinel_watch_confidence_t evidence_confidence;
    const char *evidence_label, *evidence_reason;
    bool active, has_last;
} cerberus_ble_analyzer_t;

void cerberus_ble_analyzer_begin(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms);
void cerberus_ble_analyzer_stop(cerberus_ble_analyzer_t *analyzer);
void cerberus_ble_analyzer_advance(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms);
bool cerberus_ble_analyzer_observe(cerberus_ble_analyzer_t *analyzer,
    const cerberus_ble_analyzer_report_t *report, uint32_t now_ms);
void cerberus_ble_analyzer_snapshot(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms, cerberus_ble_analyzer_view_t *out);
