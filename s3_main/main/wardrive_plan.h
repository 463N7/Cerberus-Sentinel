#pragma once

/* Pure seven-radio channel planner. R0 is the onboard C5; R1-R6 are optional
 * expansion C5s. This module creates profiles but does not start a radio. */
#include "../../expansion/shared/cerberus_protocol.h"
#include <stdbool.h>
#include <stdint.h>

enum { CERBERUS_WARDRIVE_RADIO_COUNT = 7 };

typedef enum {
    CERBERUS_WARDRIVE_BAND_MIXED = 0,
    CERBERUS_WARDRIVE_BAND_2G,
    CERBERUS_WARDRIVE_BAND_5G,
} cerberus_wardrive_band_t;

typedef enum {
    CERBERUS_WARDRIVE_TIMING_FAST = 0,
    CERBERUS_WARDRIVE_TIMING_BALANCED,
    CERBERUS_WARDRIVE_TIMING_THOROUGH,
} cerberus_wardrive_timing_t;

typedef struct {
    uint8_t slot; /* 0 = onboard; 1..6 = expansion chip select. */
    bool available;
    uint16_t channels_2g_mask;
    uint32_t channels_5g_mask;
    uint8_t channel_count;
    uint32_t planned_dwell_ms; /* Sum of channel dwells, excluding driver overhead. */
} cerberus_wardrive_radio_plan_t;

typedef struct {
    uint8_t available_mask; /* bit 0 must name the onboard C5. */
    uint8_t region; /* CERBERUS_REGION_US / ETSI / JP. */
    cerberus_wardrive_band_t band;
    cerberus_wardrive_timing_t timing;
    uint16_t active_dwell_ms;
    uint16_t passive_dwell_ms;
    uint16_t assigned_2g_mask;
    uint32_t assigned_5g_mask;
    cerberus_wardrive_radio_plan_t radio[CERBERUS_WARDRIVE_RADIO_COUNT];
} cerberus_wardrive_plan_t;

uint16_t cerberus_wardrive_region_2g_mask(uint8_t region);
uint32_t cerberus_wardrive_region_5g_mask(uint8_t region);

/* Partitions every allowed channel once across available radios. Rebuild when
 * an expansion appears or disappears; an absent slot receives no channels. */
bool cerberus_wardrive_plan_build(uint8_t available_mask, uint8_t region,
    cerberus_wardrive_band_t band, cerberus_wardrive_timing_t timing,
    cerberus_wardrive_plan_t *out);

/* Maps R1-R6 profiles to the preserved expansion protocol-v3 config shape.
 * It must never be sent to onboard Sentinel-v2 C5. The expansion scanner
 * consumes channel masks; channels[14] is only a legacy preview. */
bool cerberus_wardrive_plan_expansion_config(const cerberus_wardrive_plan_t *plan,
    uint8_t slot, bool enable, cerberus_scan_config_t *out);
