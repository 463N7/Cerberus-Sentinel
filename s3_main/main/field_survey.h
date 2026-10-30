#pragma once
#include "esp_err.h"
#include "expansion_protocol.h"
#include "wardrive_plan.h"
#define FIELD_SURVEY_CAPACITY 32
#define FIELD_SURVEY_STREAM_BATCH 8U
typedef struct {
    bool requested;
    uint8_t present_mask, scanning_mask, uncertain_mask;
    uint32_t request_epoch, worker_seen_epoch;
    uint16_t count;
    uint32_t observations, evicted, errors, radio_dropped;
    uint32_t radio_dropped_by_slot[6];
    /* Profile bit 0 maps to R1. Unverified means an older protocol-v3 image
     * lacks config proof; rejected means CONFIG or country/driver admission
     * failed. Missing slots remain optional and are excluded from both. */
    uint8_t profile_mask, profile_verified_mask, profile_unverified_mask;
    uint8_t profile_rejected_mask, profile_driver_rejected_mask;
    uint32_t profile_generation, scan_rejections_by_slot[6];
    uint32_t last_scan_error_by_slot[6];
    uint8_t last_rejected_channel[6];
    expansion_record_t records[FIELD_SURVEY_CAPACITY];
    /* Changes on every fresh radio report, even when the record bytes match.
     * Readers can distinguish a cached entry from a new observation. */
    uint32_t record_update[FIELD_SURVEY_CAPACITY];
} field_survey_snapshot_t;
esp_err_t field_survey_init(void);
esp_err_t field_survey_set_running(bool run);
/* Before onboard-only Wardriving takes the shared SPI bus, disable idle
 * presence probes and wait for an in-flight probe to finish. Pending R1-R6
 * STOP work is never suspended. Re-enable after Wardriving releases the bus. */
esp_err_t field_survey_set_idle_probe_enabled(bool enabled);
/* Call only after Stop has drained all R1-R6 radios and before opening the
 * Wardriving stream. A NULL plan restores the existing manual survey. */
esp_err_t field_survey_set_expansion_plan(const cerberus_wardrive_plan_t *plan);
void field_survey_snapshot(field_survey_snapshot_t *out);
esp_err_t field_survey_save(char *name, size_t capacity);
/* The Wardriving owner opens this optional stream before starting R1-R6 and
 * drains it before closing after Stop. The field worker never writes SD. */
esp_err_t field_survey_stream_open(void);
void field_survey_stream_close(void);
size_t field_survey_stream_read(expansion_record_t *out, size_t capacity,
    uint32_t *overflow_dropped, uint32_t *invalid_dropped,
    uint16_t *queued, uint32_t overflow_by_radio[6]);
