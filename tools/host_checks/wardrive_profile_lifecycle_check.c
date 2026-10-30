#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "field_survey.h"
#include "field_survey_stream.h"

typedef uint32_t TickType_t;
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(value) (value)
static int lock_token;
static SemaphoreHandle_t state_lock = &lock_token;
static field_survey_snapshot_t state;
static bool may_run[6], worker_busy, profile_active, profile_attempted[6];
static bool profile_proof_seen[6];
static bool idle_probe_enabled = true;
static cerberus_scan_config_t profile_config[6];
static TickType_t next_idle_probe[6], profile_attempt_tick[6];
static uint32_t scan_rejection_baseline[6];
static bool scan_rejection_baseline_known[6];
static field_survey_stream_t *wardrive_stream;
static unsigned waits;
static int xSemaphoreTake(SemaphoreHandle_t unused, TickType_t timeout)
{ (void)unused; (void)timeout; return 1; }
static void xSemaphoreGive(SemaphoreHandle_t unused) { (void)unused; }
static void vTaskDelay(TickType_t ticks) { (void)ticks; ++waits; }
static bool cerberus_config_region_masks_valid(const cerberus_scan_config_t *config)
{ return config && (config->channels_2g_mask || config->channels_5g_mask); }
#include "wardrive_profile_lifecycle_impl.h"

int main(void)
{
    assert(field_survey_set_idle_probe_enabled(false) == ESP_OK);
    assert(!idle_probe_enabled);
    assert(field_survey_set_idle_probe_enabled(true) == ESP_OK);
    assert(idle_probe_enabled);
    state.requested = true;
    assert(field_survey_set_idle_probe_enabled(false) == ESP_ERR_INVALID_STATE);
    assert(idle_probe_enabled);
    state.requested = false;
    may_run[0] = true;
    assert(field_survey_set_idle_probe_enabled(false) == ESP_ERR_INVALID_STATE);
    may_run[0] = false;
    worker_busy = true;
    waits = 0U;
    assert(field_survey_set_idle_probe_enabled(false) == ESP_ERR_TIMEOUT && waits == 100U);
    assert(!idle_probe_enabled);
    worker_busy = false;
    assert(field_survey_set_idle_probe_enabled(false) == ESP_OK);
    assert(field_survey_set_idle_probe_enabled(true) == ESP_OK);
    cerberus_wardrive_plan_t plan;
    assert(cerberus_wardrive_plan_build(0x7fU, CERBERUS_REGION_US,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_FAST, &plan));
    assert(field_survey_set_expansion_plan(&plan) == ESP_OK);
    assert(profile_active && state.profile_mask == 0x3fU);
    assert(state.profile_generation == 1U && profile_config[0].node_id == 1U);
    assert(profile_config[5].node_id == 6U && profile_config[5].scan_enabled == 1U);
    state.requested = true;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_INVALID_STATE);
    state.requested = false;
    state.scanning_mask = 1U;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_INVALID_STATE);
    state.scanning_mask = 0U;
    state.uncertain_mask = 1U;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_INVALID_STATE);
    state.uncertain_mask = 0U;
    may_run[0] = true;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_INVALID_STATE);
    may_run[0] = false;
    field_survey_stream_t stream;
    wardrive_stream = &stream;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_INVALID_STATE);
    wardrive_stream = NULL;
    worker_busy = true;
    waits = 0U;
    assert(field_survey_set_expansion_plan(NULL) == ESP_ERR_TIMEOUT && waits == 30U);
    worker_busy = false;
    assert(field_survey_set_expansion_plan(NULL) == ESP_OK);
    assert(!profile_active && state.profile_mask == 0U && state.profile_generation == 2U);
    assert(cerberus_wardrive_plan_build(0x01U, CERBERUS_REGION_ETSI,
        CERBERUS_WARDRIVE_BAND_5G, CERBERUS_WARDRIVE_TIMING_BALANCED, &plan));
    assert(field_survey_set_expansion_plan(&plan) == ESP_OK);
    assert(state.profile_mask == 0U && profile_active);
    plan.available_mask = 0U;
    assert(field_survey_set_expansion_plan(&plan) == ESP_ERR_INVALID_ARG);
    assert(state.profile_generation == 3U);
    puts("wardrive expansion profile lifecycle: Stop/drain gate and optional slots passed");
    return 0;
}
