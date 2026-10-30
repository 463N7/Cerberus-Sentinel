#pragma once

#include "csi_protocol.h"

#define CERBERUS_CSI_MOTION_CARRIERS 51U
#define CERBERUS_CSI_MOTION_FEATURES 4U
#define CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS 30U
#define CERBERUS_CSI_MOTION_HISTORY 160U
#define CERBERUS_CSI_MOTION_HISTORY_INVALID 255U
#define CERBERUS_CSI_MOTION_WINDOW_US UINT64_C(1000000)

typedef enum {
    CERBERUS_CSI_MOTION_CALIBRATING = 0,
    CERBERUS_CSI_MOTION_LOW_DATA,
    CERBERUS_CSI_MOTION_STILL,
    CERBERUS_CSI_MOTION_POSSIBLE,
    CERBERUS_CSI_MOTION_DETECTED,
} cerberus_csi_motion_state_t;

/* Feature order: normalized amplitude deviation, shape decorrelation,
 * detrended inter-frame phase change, temporal amplitude change. These are
 * channel-change indicators, not a person/identity/distance measurement. */
typedef struct {
    float features[CERBERUS_CSI_MOTION_FEATURES];
    uint32_t samples_used, samples_rejected, dropped, layout_resets;
    uint16_t samples_per_second, history_head, history_count;
    uint8_t feature_scores[CERBERUS_CSI_MOTION_FEATURES];
    uint8_t history[CERBERUS_CSI_MOTION_HISTORY];
    uint8_t state, score, reliability, calibration_percent, calibration_windows;
    int8_t rssi;
    bool calibrated;
} cerberus_csi_motion_view_t;

/* Caller-owned bounded state, suitable for one on-demand PSRAM allocation.
 * No allocator, platform clock, radio call or task is used by this model. */
typedef struct {
    cerberus_csi_motion_view_t view;
    float previous_real[CERBERUS_CSI_MOTION_CARRIERS];
    float previous_imag[CERBERUS_CSI_MOTION_CARRIERS];
    float previous_amplitude[CERBERUS_CSI_MOTION_CARRIERS];
    float amplitude_mean[CERBERUS_CSI_MOTION_CARRIERS];
    float amplitude_m2[CERBERUS_CSI_MOTION_CARRIERS];
    float pair_sum[3];
    float calibration[CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS][CERBERUS_CSI_MOTION_FEATURES];
    float baseline[CERBERUS_CSI_MOTION_FEATURES], scale[CERBERUS_CSI_MOTION_FEATURES];
    float rssi_mean, rssi_m2, smoothed_score;
    uint64_t started_us, window_us, latest_us;
    uint32_t last_sequence, window_samples, window_pairs;
    uint8_t channel, secondary, motion_votes, quiet_votes;
    bool initialized, layout_valid, previous_valid;
} cerberus_csi_motion_model_t;

/* Source time is full-width C5 uptime, never S3 delivery time. Reset also
 * recalibrates and clears all history/counters for a new capture epoch. */
void cerberus_csi_motion_reset(cerberus_csi_motion_model_t *model, uint64_t source_us);
/* Accept exactly one full124-byte fixed-format record. Transport owns exact
 * job/epoch/source binding; model additionally rejects repeated sequences,
 * backward source time and malformed layout. Boundary windows close BEFORE
 * this sample. PHY1..7/11 are documented OFDM families; secondary0/1/2 means
 * none/above/below. Always omit the first2complex tones, even if their flag is
 * clear, plus DC by identity: the common51-carrier mask never mixes changing
 * first-word validity. Channel/secondary changes restart calibration/history.
 * False never means motion or stillness. */
bool cerberus_csi_motion_observe(cerberus_csi_motion_model_t *model,
    const cerberus_csi_sample_t *sample, size_t sample_bytes);
/* Call after draining samples through a coherent empty-ring source clock.
 * Dropped is the cumulative per-capture invalid/overflow count (saturating),
 * not a per-window RF-loss estimate; known cumulative delivery loss reduces
 * reliability. Backward clocks/counters reject the update. Empty windows are
 * LOW DATA with invalid history. Arbitrarily large jumps cost at most161
 * window operations. */
bool cerberus_csi_motion_advance(cerberus_csi_motion_model_t *model,
    uint64_t source_us, uint32_t dropped);
void cerberus_csi_motion_view(const cerberus_csi_motion_model_t *model,
    cerberus_csi_motion_view_t *view);

/* Pure carrier identity helper for validation/fixtures; invalid arguments
 * return false without writing outputs. Retained index0..50 is sorted by
 * physical subcarrier number, not the vendor buffer's wrap order. */
bool cerberus_csi_motion_carrier(uint8_t secondary, size_t retained_index,
    uint8_t *byte_offset, int8_t *subcarrier);
