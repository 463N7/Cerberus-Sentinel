#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wardrive_gps_core.h"
#include "s3_ble_wardrive.h"
#include "wifi_probe_observation.h"
#include "wifi_relation_observation.h"

#define CERBERUS_WARDRIVE_DEDUPE_CAPACITY 160U
#define CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY 16U
#define CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY 32U
#define CERBERUS_WARDRIVE_LABEL_CAPACITY 32U
#define CERBERUS_WARDRIVE_RELOG_MS 300000U
#define CERBERUS_WARDRIVE_RSSI_STEP_DB 3
#define CERBERUS_WARDRIVE_UNLOCATED_HEADER \
    "# Cerberus unlocated v4; RX!=UTC\n" \
    "uptime_ms,session_ms,record_type,radio,address,address_type,channel,rssi_dbm,security,ssid_hex,ble_name_hex,manufacturer_id,gps_state_at_log,latest_rmc_utc,flock_oui_clue,flock_adv_level,flock_adv_facts_hex,flock_adv_sequence,flock_adv_observed_ms,flock_rsp_level,flock_rsp_facts_hex,flock_rsp_sequence,flock_rsp_observed_ms,flock_sid,requested_ssid_hex,probe_name_bits,probe_oui_clue,probe_observed_ms,probe_count,bssid,relation_ds,relation_rx_us,uplink_count,downlink_count\n"

_Static_assert(sizeof(CERBERUS_WARDRIVE_UNLOCATED_HEADER) - 1U <= 512U,
    "Unlocated header must fit the production storage gate");

typedef enum {
    CERBERUS_WARDRIVE_WIFI = 1,
    CERBERUS_WARDRIVE_BLE = 2,
    CERBERUS_WARDRIVE_WIFI_PROBE = 3,
    CERBERUS_WARDRIVE_WIFI_RELATION = 4,
} cerberus_wardrive_record_type_t;

typedef enum {
    CERBERUS_WARDRIVE_RADIO_C5 = 0,
    CERBERUS_WARDRIVE_RADIO_S3 = 1,
    CERBERUS_WARDRIVE_RADIO_R1 = 2,
    CERBERUS_WARDRIVE_RADIO_R6 = 7,
} cerberus_wardrive_radio_t;

/* Split the full source callback uptime into aligned words so this shares
 * the existing 28-byte BLE slot without raising observation alignment. */
typedef struct {
    uint32_t observed_ms_low, observed_ms_high, count;
    uint8_t name_bits, oui_clue, reserved[14];
} cerberus_wardrive_probe_evidence_t;

_Static_assert(sizeof(cerberus_wardrive_probe_evidence_t) == 28U,
    "Probe evidence must reuse the existing BLE evidence slot");

static inline uint64_t cerberus_wardrive_probe_observed_ms(
    const cerberus_wardrive_probe_evidence_t *evidence)
{
    return evidence ? (uint64_t)evidence->observed_ms_low |
        ((uint64_t)evidence->observed_ms_high << 32U) : 0U;
}

/* Latest directional callback only. The main address is the station; signal
 * belongs to that station for TO_AP and to this BSSID for FROM_AP. Counts
 * retain both directions without inventing an authenticated association. */
typedef struct {
    uint32_t callback_us_low, callback_us_high;
    uint32_t to_ap_frames, from_ap_frames;
    uint8_t bssid[6], latest_direction, rssi_valid, reserved[4];
} cerberus_wardrive_relation_evidence_t;

_Static_assert(sizeof(cerberus_wardrive_relation_evidence_t) == 28U,
    "Relation evidence must reuse the existing BLE evidence slot");

static inline uint64_t cerberus_wardrive_relation_callback_us(
    const cerberus_wardrive_relation_evidence_t *evidence)
{
    return evidence ? (uint64_t)evidence->callback_us_low |
        ((uint64_t)evidence->callback_us_high << 32U) : 0U;
}

typedef struct {
    cerberus_wardrive_record_type_t type;
    uint8_t radio;
    uint8_t address[6]; /* AP BSSID, probe transmitter, relation station or BLE display address. */
    uint8_t address_type; /* BLE only; prevents public/random collisions. */
    uint8_t channel; /* Wi-Fi channel; zero for BLE. */
    int8_t rssi_dbm;
    uint8_t security; /* Wi-Fi security enum; zero for BLE. */
    uint16_t manufacturer_id; /* BLE only. */
    uint8_t label[CERBERUS_WARDRIVE_LABEL_CAPACITY]; /* AP/requested SSID or BLE name. */
    uint8_t label_length;
    union {
        cerberus_s3_ble_wardrive_evidence_t ble_evidence;
        cerberus_wardrive_probe_evidence_t probe_evidence;
        cerberus_wardrive_relation_evidence_t relation_evidence;
    };
} cerberus_wardrive_observation_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type, type, channel;
    int8_t best_logged_rssi;
    uint32_t last_logged_ms;
    uint32_t ble_evidence_key_low, ble_evidence_key_high;
    bool occupied;
} cerberus_wardrive_dedupe_entry_t;

typedef struct {
    cerberus_wardrive_dedupe_entry_t entries[CERBERUS_WARDRIVE_DEDUPE_CAPACITY];
    uint16_t used;
    uint32_t evicted;
} cerberus_wardrive_dedupe_t;

typedef struct {
    uint32_t last_logged_ms;
    uint8_t address[6], ssid[CERBERUS_WARDRIVE_LABEL_CAPACITY];
    uint8_t ssid_length, channel;
    int8_t best_logged_rssi;
    uint8_t name_bits, oui_clue;
    bool occupied;
} cerberus_wardrive_probe_dedupe_entry_t;

typedef struct {
    cerberus_wardrive_probe_dedupe_entry_t entries[CERBERUS_WARDRIVE_PROBE_DEDUPE_CAPACITY];
    uint16_t used;
    uint32_t evicted;
} cerberus_wardrive_probe_dedupe_t;

typedef struct {
    uint32_t last_logged_ms;
    uint8_t bssid[6], station[6], channel, directions, valid_rssi;
    int8_t best_logged_rssi[2];
    bool occupied;
} cerberus_wardrive_relation_dedupe_entry_t;

typedef struct {
    cerberus_wardrive_relation_dedupe_entry_t entries[CERBERUS_WARDRIVE_RELATION_DEDUPE_CAPACITY];
    uint16_t used;
    uint32_t evicted;
} cerberus_wardrive_relation_dedupe_t;

typedef struct {
    bool should_log;
    uint16_t slot;
} cerberus_wardrive_decision_t;

void cerberus_wardrive_dedupe_reset(cerberus_wardrive_dedupe_t *state);
bool cerberus_wardrive_observation_valid(const cerberus_wardrive_observation_t *observation);
/* Transactional conversion of a complete, clue-matching C5 source row. */
bool cerberus_wardrive_observation_from_probe(const cerberus_wifi_probe_row_t *probe,
    cerberus_wardrive_observation_t *observation);
bool cerberus_wardrive_observation_from_relation(const cerberus_wifi_relation_row_t *relation,
    cerberus_wardrive_observation_t *observation);
void cerberus_wardrive_relation_dedupe_reset(cerberus_wardrive_relation_dedupe_t *state);
cerberus_wardrive_decision_t cerberus_wardrive_relation_decide(
    const cerberus_wardrive_relation_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms);
/* Exact BSSID/station/channel key; compare signal only with the same direction.
 * Callback/count changes alone do not relog. Commit only after accepted append. */
void cerberus_wardrive_relation_commit(cerberus_wardrive_relation_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms);
void cerberus_wardrive_probe_dedupe_reset(cerberus_wardrive_probe_dedupe_t *state);
cerberus_wardrive_decision_t cerberus_wardrive_probe_decide(
    const cerberus_wardrive_probe_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms);
/* As with AP/BLE, commit only after accepted SD append. Exact transmitter and
 * requested SSID bytes define the key. Callback/count changes alone do not
 * relog; channel, signal, clue changes or the ordinary interval can do so. */
void cerberus_wardrive_probe_commit(cerberus_wardrive_probe_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms);
cerberus_wardrive_decision_t cerberus_wardrive_decide(
    const cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms);
/* Unlocated CSV also records changed hint facts. The ordinary decision keeps
 * the established WiGLE RSSI/channel/time policy. Callback time alone never
 * forces a duplicate row. Neither history claims globally unique devices. */
cerberus_wardrive_decision_t cerberus_wardrive_decide_evidence(
    const cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation, uint32_t uptime_ms);
/* Call only after a complete CSV row has been accepted by storage. A failed
 * append must not suppress the first valid observation on the next sweep. */
void cerberus_wardrive_commit(cerberus_wardrive_dedupe_t *state,
    const cerberus_wardrive_observation_t *observation,
    cerberus_wardrive_decision_t decision, uint32_t uptime_ms);
/* All variable RF text is hexadecimal. The GPS fields describe the receiver
 * at row append time, not the RF observation time. The file has no location
 * coordinates. UTC is the last validated RMC timestamp, never fabricated.
 * Returns zero if capacity is too small. */
size_t cerberus_wardrive_format_unlocated_row(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    uint64_t uptime_ms, uint64_t session_ms, char *out, size_t capacity);
