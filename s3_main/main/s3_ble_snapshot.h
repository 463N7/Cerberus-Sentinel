#pragma once

#include "cerberus_watch_capture.h"
#include "ibeacon.h"
#include "ble_findmy_hints.h"
#include "ble_flipper_hints.h"
#include "ble_meta_hints.h"
#include "ble_flock_hints.h"
#include "ble_hc_hints.h"

enum {
    CERBERUS_S3_BLE_FOCUS_IBEACON = 1U << 0,
    CERBERUS_S3_BLE_FOCUS_FIND_MY = 1U << 1,
    CERBERUS_S3_BLE_FOCUS_FLIPPER = 1U << 2,
    CERBERUS_S3_BLE_FOCUS_FLOCK = 1U << 3,
    CERBERUS_S3_BLE_FOCUS_META_HINT = 1U << 4,
    CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT = 1U << 5,
};

typedef enum {
    CERBERUS_S3_BLE_FILTER_ALL = 0,
    CERBERUS_S3_BLE_FILTER_IBEACON,
    CERBERUS_S3_BLE_FILTER_FIND_MY,
    CERBERUS_S3_BLE_FILTER_FLIPPER,
    CERBERUS_S3_BLE_FILTER_FLOCK,
    CERBERUS_S3_BLE_FILTER_META_HINT,
    CERBERUS_S3_BLE_FILTER_HC_NAME_HINT,
    CERBERUS_S3_BLE_FILTER_COUNT,
} cerberus_s3_ble_filter_t;

enum {
    CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE = 1U << 0,
    CERBERUS_S3_BLE_EVIDENCE_ADV_VALID = 1U << 1,
    CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE = 1U << 2,
    CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID = 1U << 3,
    CERBERUS_S3_BLE_EVIDENCE_SID_VALID = 1U << 4,
    CERBERUS_S3_BLE_EVIDENCE_SID_INVALID = 1U << 5,
};

/* Current original report plus its latest complete, valid, matched response.
 * Source identity/time is separate from the bounded raw ADV copy. A newer
 * incomplete/malformed original clears hints even when raw ADV/time stay old.
 * RESPONSE_VALID implies complete source bytes and exact same-sweep binding;
 * responses never update original time/sequence/raw bytes. The passive hint
 * families share this one source binding; neither supplies an active profile. */
typedef struct {
    uint32_t advertisement_sequence, advertisement_observed_ms;
    uint32_t scan_response_sequence, scan_response_observed_ms;
    uint8_t address[6], address_type, generation;
    cerberus_ble_findmy_hints_t findmy_advertisement, findmy_scan_response;
    cerberus_ble_flipper_hints_t flipper_advertisement, flipper_scan_response;
    cerberus_ble_meta_evidence_t meta_advertisement, meta_scan_response;
    cerberus_ble_flock_hints_t flock_advertisement, flock_scan_response;
    uint8_t sid, flags;
    uint8_t hc_advertisement, hc_scan_response; /* Exact current-source HC name codes, not identity. */
} cerberus_s3_ble_discovery_evidence_t;

typedef struct {
    /* Complete packets add evidence; matched responses may add name/ID hints.
     * A conflicting Meta identifier suppresses that hint for this sweep. */
    uint8_t category_bits;
    bool ibeacon_valid : 1;
    bool meta_suppressed : 1; /* A valid blocked ID occurred in this sweep, possibly earlier. */
    bool ibeacon_scan_response : 1;
    bool flock_oui_clue : 1; /* Typed-address clue after a valid original, not packet/SID/time evidence. */
    /* Latest qualifying same-sweep report, never a first-match cache. These
     * own source values survive ordinary/invalid replacements; identity and
     * generation remain those of this typed-address snapshot record. */
    cerberus_ibeacon_t ibeacon;
    uint32_t ibeacon_observed_ms, ibeacon_sequence;
    /* Static classifier text, never a pointer into received radio data. */
    const char *findmy_evidence;
    const char *flipper_evidence;
    const char *flock_evidence;
    const char *meta_evidence;
    cerberus_s3_ble_discovery_evidence_t discovery;
} cerberus_s3_ble_focus_t;

/* Current means the retained match still belongs to the current valid ADV or
 * latest accepted valid matched response. It is independent of freshness and
 * does not claim the newest callback itself contained an iBeacon. */
static inline bool cerberus_s3_ble_ibeacon_is_current(const cerberus_s3_ble_focus_t *focus)
{
    if (!focus || !focus->ibeacon_valid || !focus->ibeacon_sequence) return false;
    const cerberus_s3_ble_discovery_evidence_t *source = &focus->discovery;
    const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
    if ((source->flags & required) != required ||
        (source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_INVALID)) return false;
    if (focus->ibeacon_scan_response)
        return (source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) &&
            source->scan_response_sequence == focus->ibeacon_sequence &&
            source->scan_response_observed_ms == focus->ibeacon_observed_ms;
    return source->advertisement_sequence == focus->ibeacon_sequence &&
        source->advertisement_observed_ms == focus->ibeacon_observed_ms;
}

/* Return total matches and fill one page with original scan indexes. */
size_t cerberus_s3_ble_focus_page(const uint8_t *category_bits, size_t count,
    cerberus_s3_ble_filter_t filter, size_t offset, uint8_t *indexes,
    size_t capacity);

/* A bounded, generation-tagged passive BLE sweep. The caller serializes
 * observe/finish/read access; no NimBLE or ESP-IDF types enter this module. */
typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi; /* Valid original-report sweep peak; 127 if no measurement. */
    uint8_t name[CERBERUS_BLE_NAME_MAX_LENGTH];
    uint8_t name_length;
    bool name_complete;
    uint8_t uuid[CERBERUS_BLE_UUID_MAX_LENGTH];
    uint8_t uuid_length;
    uint16_t manufacturer_id;
    uint8_t flags;
    uint8_t advertisement[CERBERUS_BLE_ADVERTISEMENT_MAX_LENGTH];
    uint8_t advertisement_length;
    uint8_t advertisement_event_type;
    uint8_t advertisement_flags;
    uint16_t reports_seen;
    uint32_t last_advertisement_ms;
    uint8_t advertisement_sid;
    bool advertisement_sid_valid;
    cerberus_s3_ble_focus_t focus;
} cerberus_s3_ble_record_t;

typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    int8_t rssi;
    uint8_t event_type; /* Original 0..3; legacy response 4; extended response 0/3. */
    bool connectable;
    bool scan_response;
    bool complete;
    uint8_t sid;
    bool sid_valid;
    bool sid_invalid; /* Nonlegacy extended report with unavailable/invalid SID. */
    const uint8_t *data;
    size_t length;
} cerberus_s3_ble_report_t;

#define CERBERUS_S3_BLE_DISCOVERY_BINDINGS 8U

/* Only qualifying sources enter focused discovery's public inventory; an
 * admitted row remains visible to consumers after dequalification until a
 * new qualifying source replaces it. These private originals allow a later
 * exact matched response to qualify without crowding the admitted bank.
 * The caller supplies this bounded storage only for an explicit focused scan. */
typedef struct {
    cerberus_s3_ble_record_t records[CERBERUS_S3_BLE_DISCOVERY_BINDINGS];
    uint8_t count;
    uint16_t overflows; /* Discarded correlation records, not RF packet loss. */
} cerberus_s3_ble_binding_cache_t;

typedef struct {
    cerberus_ble_scan_status_payload_t status;
    cerberus_s3_ble_record_t records[CERBERUS_BLE_SCAN_MAX_RESULTS];
    cerberus_watch_capture_t watch;
    cerberus_ble_activity_capture_t activity;
    /* Never wraps within a sweep: exhausted sequences disable new hint
     * provenance until begin resets the snapshot. No token can be reused. */
    uint32_t report_sequence;
    /* Accepted new identities replacing the oldest original in a full bank.
     * Saturates independently of rejected reports; reset by begin. Rows may
     * change only while RUNNING, never in a published COMPLETE bank. */
    uint16_t inventory_replacements;
    cerberus_s3_ble_filter_t family;
    cerberus_s3_ble_binding_cache_t *bindings;
    uint64_t revision;
} cerberus_s3_ble_snapshot_t;

void cerberus_s3_ble_snapshot_begin(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended);
/* Nonzero boot-lifetime capture identity, independent of the public byte.
 * The legacy begin/observe APIs deliberately supply no Watch timing. */
void cerberus_s3_ble_snapshot_begin_with_epoch(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended, uint64_t capture_epoch);
/* ALL requires no binding cache. Other families require caller-owned cache.
 * Rejected arguments leave both objects unchanged. Family is immutable until
 * the next begin. Legacy begin APIs always retain their ALL semantics. */
bool cerberus_s3_ble_snapshot_begin_discovery(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended, uint64_t capture_epoch,
    cerberus_s3_ble_filter_t family, cerberus_s3_ble_binding_cache_t *bindings);
void cerberus_s3_ble_snapshot_observe(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now_ms);
/* callback_us is captured before locks/decoding; now_ms retains the existing
 * raw/hint/tracker clock contract. Responses never enter the Watch collector. */
void cerberus_s3_ble_snapshot_observe_timed(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now_ms, uint64_t callback_us);
void cerberus_s3_ble_snapshot_finish(cerberus_s3_ble_snapshot_t *snapshot,
    bool cancelled, int32_t error_code, uint32_t now_ms);
