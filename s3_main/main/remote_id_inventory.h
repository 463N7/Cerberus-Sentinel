#pragma once

#include "cerberus_protocol.h"
#include "sentinel_watch_protocol.h"
#include "watch_receive_timing.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Caller-owned pure state. Allocate on demand outside this module. One owner
 * serializes publication, page copies and selection; there are no locks/tasks,
 * allocations, radio calls, or cross-clock RF-age claims in this backend. */
#define CERBERUS_REMOTE_ID_RADIOS 2U
#define CERBERUS_REMOTE_ID_CAPACITY (CERBERUS_REMOTE_ID_RADIOS * CERBERUS_WATCH_MAX_OBSERVATIONS)
#define CERBERUS_REMOTE_ID_PAGE_ROWS 4U

typedef struct {
    uint64_t sweep_token;
    uint64_t receipt_ms; /* Full S3 completed-sweep receipt time, NOT RF time. */
    cerberus_watch_observation_t observation;
    uint8_t radio; /* CERBERUS_WATCH_RADIO_WIFI or CERBERUS_WATCH_RADIO_BLE. */
    cerberus_watch_receive_timing_t timing; /* Source callback age, not field/RF time. */
    sentinel_watch_motion_t motion;
    cerberus_watch_receive_timing_t motion_timing; /* Location callback only. */
} cerberus_remote_id_record_t;

typedef struct {
    cerberus_remote_id_record_t frozen; /* Exact painted selection, immutable. */
    cerberus_remote_id_record_t latest; /* Last matching sweep, retained if absent. */
    uint64_t checked_sweep_token;
    uint64_t checked_receipt_ms; /* Latest checked selected-radio sweep only. */
    uint32_t matching_sweeps; /* Known matches since frozen sweep; saturates. */
    bool valid;
    bool present; /* Exact source exists in latest committed selected-radio bank. */
    bool radio_unavailable; /* Latest bank reports capture unavailable, not absence. */
} cerberus_remote_id_selection_t;

typedef struct {
    uint64_t sweep_token, receipt_ms;
    cerberus_watch_scan_status_payload_t status; /* Includes Pwn rows/drops/flags. */
    cerberus_watch_observation_t rows[CERBERUS_WATCH_MAX_OBSERVATIONS]; /* DRONE only. */
    cerberus_watch_receive_timing_t timings[CERBERUS_WATCH_MAX_OBSERVATIONS];
    sentinel_watch_motion_t motions[CERBERUS_WATCH_MAX_OBSERVATIONS];
    cerberus_watch_receive_timing_t motion_timings[CERBERUS_WATCH_MAX_OBSERVATIONS];
    uint8_t count;
    bool published;
} cerberus_remote_id_bank_t;

typedef struct {
    /* Stable page order: Wi-Fi bank first, then BLE; source order within each. */
    cerberus_remote_id_bank_t banks[CERBERUS_REMOTE_ID_RADIOS];
    /* Optional reusable transport staging; never read by page/selection APIs. */
    cerberus_watch_observation_t staging[CERBERUS_WATCH_MAX_OBSERVATIONS];
    cerberus_watch_receive_timing_t staging_timings[CERBERUS_WATCH_MAX_OBSERVATIONS];
    sentinel_watch_motion_t staging_motions[CERBERUS_WATCH_MAX_OBSERVATIONS];
    cerberus_watch_receive_timing_t staging_motion_timings[CERBERUS_WATCH_MAX_OBSERVATIONS];
    cerberus_remote_id_selection_t selected;
} cerberus_remote_id_inventory_t;

typedef struct {
    cerberus_remote_id_record_t rows[CERBERUS_REMOTE_ID_PAGE_ROWS];
    uint8_t total, copied;
    size_t offset;
} cerberus_remote_id_page_t;

typedef enum {
    CERBERUS_REMOTE_ID_PUBLISHED,
    CERBERUS_REMOTE_ID_DUPLICATE,
    CERBERUS_REMOTE_ID_STALE,
    CERBERUS_REMOTE_ID_INVALID,
} cerberus_remote_id_publish_result_t;

void cerberus_remote_id_inventory_init(cerberus_remote_id_inventory_t *inventory);

/* before/after must describe the same complete snapshot; every input row is
 * validated before Pwn rows are ignored. Tokens are nonzero, monotonically
 * increasing per radio, assigned only to witnessed new sweeps by the caller.
 * A duplicate token never republishes or refreshes any time; older tokens and
 * backwards receipt time fail closed. Failures leave committed/selected state
 * unchanged. rows may point to inventory->staging; count must equal stored_count.
 * All-zero/group Wi-Fi source addresses are rejected; BLE uses typed-address
 * identity without applying IEEE unicast/local-bit rules to its byte array. */
cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows, size_t count, uint64_t receipt_ms);

/* Timing is paired with each input row before filtering. NULL means unavailable
 * for every row and preserves the original untimed API. All timing is validated
 * before commit; repeated tokens never refresh it. Millisecond receipt rounding
 * permits request_started_us / 1000 == receipt_ms. */
cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish_observed(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows,
    const cerberus_watch_receive_timing_t *timings, size_t count, uint64_t receipt_ms);

/* Additive atomic Location evidence. Both motion arrays must be supplied or
 * both NULL (legacy/unavailable). A source-only update cannot date retained
 * motion. Complete detailed duplicate tokens must retain identical values;
 * no duplicate refreshes either age anchor. New untimed/legacy publications
 * clear unavailable extra fields instead of retaining a prior sweep's data. */
cerberus_remote_id_publish_result_t cerberus_remote_id_inventory_publish_detailed(
    cerberus_remote_id_inventory_t *inventory, uint64_t sweep_token,
    const cerberus_watch_scan_status_payload_t *before,
    const cerberus_watch_scan_status_payload_t *after,
    const cerberus_watch_observation_t *rows,
    const cerberus_watch_receive_timing_t *timings,
    const sentinel_watch_motion_t *motions,
    const cerberus_watch_receive_timing_t *motion_timings, size_t count, uint64_t receipt_ms);

/* Return NULL for an unsupported radio. Bank metadata is observational; the
 * caller must not modify a committed bank. Empty unavailable banks remain
 * published so UI can distinguish unavailable capture from no matches. */
const cerberus_remote_id_bank_t *cerberus_remote_id_inventory_bank(
    const cerberus_remote_id_inventory_t *inventory, uint8_t radio);
bool cerberus_remote_id_inventory_page(const cerberus_remote_id_inventory_t *inventory,
    size_t offset, cerberus_remote_id_page_t *page);

bool cerberus_remote_id_source_equal(const cerberus_remote_id_record_t *a,
    const cerberus_remote_id_record_t *b);

/* Accept a private record copied from a previously painted page. The current
 * bank may already have advanced; it must not redirect that frozen identity.
 * Caller guarantees provenance for older painted copies (history is bounded).
 * The same-token row must still match its committed bank exactly. Selection
 * starts with one known matching sweep; a newer matching bank adds one.
 * Current/absent state is checked against the latest bank immediately. */
bool cerberus_remote_id_inventory_select(cerberus_remote_id_inventory_t *inventory,
    const cerberus_remote_id_record_t *painted);
void cerberus_remote_id_inventory_clear_selection(cerberus_remote_id_inventory_t *inventory);

typedef struct {
    uint8_t transmitter[6];
    uint8_t transport;
    uint8_t channel; /* Zero requests a search without a fresh channel hint. */
} cerberus_remote_id_follow_hint_t;

/* Preserve the exact frozen Wi-Fi source even when absent. Only its present,
 * matching latest row with a conservative callback-age bound below 30 seconds
 * supplies a channel. Receipt time and BLE evidence never supply freshness.
 * This is a hint: C5 must independently bind its own source/capture evidence. */
bool cerberus_remote_id_inventory_follow_hint(const cerberus_remote_id_inventory_t *inventory,
    uint64_t now_us, cerberus_remote_id_follow_hint_t *out);
