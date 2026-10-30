#pragma once

/* Pure presentation evidence for one ordinary AP publication. No allocation,
 * RF, station-target updates or direct comparison of C5 and S3 uptimes. */
#include "wifi_relation_observation.h"

typedef struct {
    cerberus_wifi_relation_status_t status;
    uint64_t request_started_us, source_now_us;
    uint32_t firmware_version, capabilities, peer_uptime_ms;
    uint8_t protocol_version, reset_reason, boot_count;
    bool clock_valid;
} cerberus_s3_wifi_relation_meta_t;

typedef enum {
    CERBERUS_S3_WIFI_RELATION_UNKNOWN = 0,
    CERBERUS_S3_WIFI_RELATION_LOADING,
    CERBERUS_S3_WIFI_RELATION_READY,
    CERBERUS_S3_WIFI_RELATION_UNAVAILABLE,
    CERBERUS_S3_WIFI_RELATION_UNSUPPORTED,
    CERBERUS_S3_WIFI_RELATION_FAILED,
    CERBERUS_S3_WIFI_RELATION_CANCELLED,
} cerberus_s3_wifi_relation_outcome_t;

typedef struct {
    cerberus_s3_wifi_relation_meta_t meta;
    cerberus_wifi_relation_row_t rows[CERBERUS_WIFI_RELATION_LIMIT];
    uint64_t ap_publication, receipt_us;
    int32_t error;
    cerberus_s3_wifi_relation_outcome_t outcome;
    uint8_t generation;
} cerberus_s3_wifi_relation_bank_t;

typedef struct {
    cerberus_s3_wifi_relation_bank_t bank;
    uint8_t bssid[6], channel, count;
    bool valid;
} cerberus_s3_wifi_relation_selection_t;

typedef enum {
    CERBERUS_S3_WIFI_RELATION_REJECTED = -1,
    CERBERUS_S3_WIFI_RELATION_DUPLICATE = 0,
    CERBERUS_S3_WIFI_RELATION_APPLIED = 1,
} cerberus_s3_wifi_relation_apply_t;

bool cerberus_s3_wifi_relation_meta_valid(const cerberus_s3_wifi_relation_meta_t *);
/* Preflight every row, exact key and saturated count before replacing bank.
 * Duplicate publication/epoch/content preserves the original request anchor.
 * Malformed input leaves bank byte-for-byte unchanged. */
cerberus_s3_wifi_relation_apply_t cerberus_s3_wifi_relation_apply(
    cerberus_s3_wifi_relation_bank_t *, uint64_t ap_publication,
    const cerberus_s3_wifi_relation_meta_t *, const cerberus_wifi_relation_row_t *,
    size_t count, uint64_t receipt_us);
/* Explicit current-publication state replaces old rows rather than silently
 * showing a stale bank as the new scan. A frozen selection is independent. */
bool cerberus_s3_wifi_relation_set_outcome(cerberus_s3_wifi_relation_bank_t *,
    uint64_t ap_publication, uint8_t generation,
    cerberus_s3_wifi_relation_outcome_t, int32_t error);
/* A valid selected AP with a different publication gets UNKNOWN with no rows.
 * READY/count0 means proven zero matching rows; unavailable is distinct. */
bool cerberus_s3_wifi_relation_select(const cerberus_s3_wifi_relation_bank_t *,
    uint64_t expected_ap_publication, const uint8_t bssid[6], uint8_t channel,
    cerberus_s3_wifi_relation_selection_t *);
/* Per-direction callback age upper bound, not association/packet/position age.
 * A zero callback timestamp is valid when that direction is present. */
bool cerberus_s3_wifi_relation_age_ms(const cerberus_s3_wifi_relation_selection_t *,
    size_t index, uint8_t direction, uint64_t now_us, uint64_t *upper_bound_ms);
