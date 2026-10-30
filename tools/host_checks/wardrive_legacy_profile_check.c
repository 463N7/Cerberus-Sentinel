#ifdef NDEBUG
#undef NDEBUG
#endif
#include "field_survey.h"
#include "../../expansion/shared/cerberus_config_proof.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define portMAX_DELAY UINT32_MAX
static int state_lock;
static int xSemaphoreTake(int ignored, TickType_t timeout)
{ (void)ignored; (void)timeout; return 1; }
static field_survey_snapshot_t state;
static bool may_run[6], profile_attempted[6], profile_proof_seen[6];
static TickType_t profile_attempt_tick[6], next_idle_probe[6];
static uint32_t dropped[6], scan_rejection_baseline[6];
static bool scan_rejection_baseline_known[6];
static bool module_proof, reject_config;
static unsigned enables, stops;

static bool exchange(unsigned slot, unsigned command, bool enabled,
    const cerberus_scan_config_t *profile, expansion_status_t *status,
    expansion_record_t records[3], uint8_t *count)
{
    (void)records; (void)count;
    assert(slot == 0U && command == EXP_CONFIG);
    if (!enabled) {
        ++stops;
        status->enabled = false;
        status->config_proof_supported = module_proof;
        return true;
    }
    ++enables;
    assert(profile != NULL && profile->scan_enabled);
    status->enabled = !reject_config;
    status->config_proof_supported = module_proof;
    status->config_result = reject_config ? CERBERUS_CONFIG_RESULT_REJECTED :
        CERBERUS_CONFIG_RESULT_APPLIED;
    status->config_fingerprint = reject_config ? 0U :
        cerberus_config_fingerprint(profile);
    return true;
}

typedef struct { bool read, ok, rejected; expansion_status_t status; } step_result_t;
static step_result_t step(expansion_status_t status, TickType_t now, bool ok)
{
    unsigned slot = 0U;
    uint8_t bit = 1U;
    bool planned = true, assigned = true, wanted = true;
    cerberus_scan_config_t config = {
        .protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION,
        .firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION,
        .node_id = 1U, .scan_enabled = 1U, .region = CERBERUS_REGION_US,
        .channels_2g_mask = CERBERUS_2G_CHANNEL_BIT(1),
        .dwell_time_ms = 120U, .passive_dwell_time_ms = 240U,
    };
    expansion_record_t records[3] = {0};
    uint8_t count = 0U;
#include "wardrive_legacy_profile_impl.h"
    return (step_result_t){read, ok, profile_config_failed, status};
}

static void reset(void)
{
    memset(&state, 0, sizeof(state));
    memset(may_run, 0, sizeof(may_run));
    memset(profile_attempted, 0, sizeof(profile_attempted));
    memset(profile_proof_seen, 0, sizeof(profile_proof_seen));
    memset(profile_attempt_tick, 0, sizeof(profile_attempt_tick));
    memset(next_idle_probe, 0, sizeof(next_idle_probe));
    memset(dropped, 0, sizeof(dropped));
    memset(scan_rejection_baseline, 0, sizeof(scan_rejection_baseline));
    memset(scan_rejection_baseline_known, 0, sizeof(scan_rejection_baseline_known));
    module_proof = reject_config = false;
    enables = stops = 0U;
    state.requested = true;
}

int main(void)
{
    expansion_status_t legacy_off = {0};
    reset();
    step_result_t r = step(legacy_off, 100U, true);
    assert(r.ok && r.read && enables == 1U && stops == 0U);
    assert(r.status.enabled && state.profile_unverified_mask == 1U);
    assert(state.profile_verified_mask == 0U && !profile_proof_seen[0]);
    r = step(r.status, 200U, true);
    assert(r.read && enables == 1U && stops == 0U); /* no CONFIG loop */
    r.status.enabled = false;
    r = step(r.status, 500U, true);
    assert(!r.read && enables == 1U); /* paced retry */
    r = step(r.status, 10100U, true);
    assert(r.read && enables == 2U && stops == 0U);

    reset();
    expansion_status_t legacy_on = {.enabled = true};
    r = step(legacy_on, 100U, true);
    assert(r.read && enables == 1U && stops == 0U); /* replace stale allocation */

    reset();
    module_proof = true;
    expansion_status_t proof_off = {.config_proof_supported = true};
    r = step(proof_off, 100U, true);
    assert(r.read && enables == 1U && stops == 0U);
    assert(profile_proof_seen[0] && state.profile_verified_mask == 1U);
    assert(state.profile_unverified_mask == 0U);
    r.status.config_fingerprint ^= 1U;
    r = step(r.status, 200U, true);
    assert(!r.read && stops == 1U && state.profile_verified_mask == 0U);
    r = step(r.status, 10200U, true);
    assert(r.read && enables == 2U && state.profile_verified_mask == 1U);

    reset();
    module_proof = true;
    reject_config = true;
    r = step(proof_off, 100U, true);
    assert(!r.read && r.rejected && state.profile_rejected_mask == 1U);
    assert(state.profile_verified_mask == 0U && state.profile_unverified_mask == 0U);

    reset();
    profile_proof_seen[0] = true;
    r = step(legacy_on, 100U, true);
    assert(!r.read && stops == 1U && state.profile_unverified_mask == 0U);
    /* Failed communication removes the old identity and requires a fresh
     * profile on the next status from a replacement module. */
    state.present_mask = 1U;
    profile_attempted[0] = true;
    profile_attempt_tick[0] = 100U;
    r = step(legacy_on, 200U, false);
    assert(!r.ok && !r.read && !profile_proof_seen[0] && !profile_attempted[0]);
    assert(profile_attempt_tick[0] == 0U && state.present_mask == 0U);
    module_proof = false;
    r = step(legacy_on, 300U, true);
    assert(r.read && enables == 1U && stops == 1U);
    assert(state.profile_unverified_mask == 1U);
    puts("wardrive legacy expansion profile: fallback, proof guard, retry, replacement passed");
    return 0;
}
