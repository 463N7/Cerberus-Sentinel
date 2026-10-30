#include "csi_motion_model.h"

#include <math.h>
#include <string.h>

#define CSI_PI 3.14159265358979323846f
#define CSI_MIN_TONES 40U
#define CSI_MIN_SAMPLES 5U

static uint32_t increment(uint32_t value)
{ return value == UINT32_MAX ? value : value + 1U; }

static float bounded(float value, float low, float high)
{ return value < low ? low : value > high ? high : value; }

static uint8_t percent(float value)
{ return (uint8_t)(bounded(value, 0.0f, 100.0f) + 0.5f); }

bool cerberus_csi_motion_carrier(uint8_t secondary, size_t index,
    uint8_t *offset, int8_t *carrier)
{
    if (secondary > 2U || index >= CERBERUS_CSI_MOTION_CARRIERS || !offset || !carrier) return false;
    size_t native;
    int number;
    if (secondary == 0U) {
        native = index < 26U ? index + 27U : index - 24U;
        number = index < 26U ? (int)index - 26 : (int)index - 24;
    } else {
        native = index + 2U;
        number = secondary == 1U ? (int)index - 51 : (int)index + 2;
    }
    *offset = (uint8_t)(native * 2U);
    *carrier = (int8_t)number;
    return true;
}

static void empty_window(cerberus_csi_motion_model_t *model)
{
    memset(model->amplitude_mean, 0, sizeof(model->amplitude_mean));
    memset(model->amplitude_m2, 0, sizeof(model->amplitude_m2));
    memset(model->pair_sum, 0, sizeof(model->pair_sum));
    model->rssi_mean = model->rssi_m2 = 0.0f;
    model->window_samples = model->window_pairs = 0U;
    model->previous_valid = false;
}

void cerberus_csi_motion_reset(cerberus_csi_motion_model_t *model, uint64_t source_us)
{
    if (!model) return;
    memset(model, 0, sizeof(*model));
    model->started_us = model->window_us = model->latest_us = source_us;
    model->initialized = true;
    model->view.state = CERBERUS_CSI_MOTION_CALIBRATING;
    model->view.rssi = 127;
    memset(model->view.history, CERBERUS_CSI_MOTION_HISTORY_INVALID, sizeof(model->view.history));
}

static void append_history(cerberus_csi_motion_model_t *model, uint8_t value)
{
    model->view.history[model->view.history_head] = value;
    model->view.history_head = (uint16_t)((model->view.history_head + 1U) % CERBERUS_CSI_MOTION_HISTORY);
    if (model->view.history_count < CERBERUS_CSI_MOTION_HISTORY) ++model->view.history_count;
}

static float median(float values[CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS], size_t count)
{
    for (size_t i = 1U; i < count; ++i) {
        float value = values[i];
        size_t j = i;
        while (j && values[j - 1U] > value) { values[j] = values[j - 1U]; --j; }
        values[j] = value;
    }
    return (values[(count - 1U) / 2U] + values[count / 2U]) * 0.5f;
}

static void calibrate(cerberus_csi_motion_model_t *model)
{
    /* Floors cover quantization and floating-point noise; this is an original
     * heuristic model, not an environment-independent detection threshold. */
    const float floors[CERBERUS_CSI_MOTION_FEATURES] = {0.01f, 0.0004f, 0.025f, 0.015f};
    float work[CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS];
    for (size_t f = 0; f < CERBERUS_CSI_MOTION_FEATURES; ++f) {
        for (size_t w = 0; w < CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS; ++w) work[w] = model->calibration[w][f];
        model->baseline[f] = median(work, CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS);
        for (size_t w = 0; w < CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS; ++w)
            work[w] = fabsf(model->calibration[w][f] - model->baseline[f]);
        model->scale[f] = fmaxf(floors[f], fmaxf(model->baseline[f] * 0.05f,
            median(work, CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS) * 1.4826f));
    }
    model->view.calibrated = true;
    model->view.calibration_percent = 100U;
}

static uint8_t reliability(const cerberus_csi_motion_model_t *model)
{
    const uint32_t n = model->window_samples;
    if (n < CSI_MIN_SAMPLES || model->window_pairs + 1U < n) return 0U;
    float quality = n >= 20U ? 45.0f : n >= 10U ? 38.0f : 30.0f;
    quality += model->rssi_mean >= -75.0f ? 25.0f : model->rssi_mean >= -85.0f ? 18.0f :
        model->rssi_mean >= -95.0f ? 8.0f : 0.0f;
    const float deviation = sqrtf(fmaxf(0.0f, model->rssi_m2 / (float)n));
    quality += deviation <= 3.0f ? 20.0f : deviation <= 7.0f ? 10.0f : 0.0f;
    const double used = model->view.samples_used;
    const double discarded = (double)model->view.dropped + model->view.samples_rejected;
    const float delivered = used + discarded > 0.0 ? (float)(used / (used + discarded)) : 0.0f;
    quality += delivered * 10.0f - (1.0f - delivered) * 60.0f;
    return percent(quality);
}

static void low_data(cerberus_csi_motion_model_t *model)
{
    model->view.state = CERBERUS_CSI_MOTION_LOW_DATA;
    model->view.score = 0U;
    model->view.reliability = 0U;
    memset(model->view.features, 0, sizeof(model->view.features));
    memset(model->view.feature_scores, 0, sizeof(model->view.feature_scores));
    model->smoothed_score = 0.0f;
    model->motion_votes = model->quiet_votes = 0U;
}

static void close_window(cerberus_csi_motion_model_t *model)
{
    model->view.samples_per_second = (uint16_t)model->window_samples;
    model->view.rssi = model->window_samples ? (int8_t)lroundf(model->rssi_mean) : 127;
    model->view.reliability = reliability(model);
    bool adequate = model->window_samples >= CSI_MIN_SAMPLES &&
        model->window_pairs >= CSI_MIN_SAMPLES - 1U && model->view.reliability >= 50U;
    if (adequate) {
        float variance = 0.0f;
        for (size_t i = 0; i < CERBERUS_CSI_MOTION_CARRIERS; ++i)
            variance += fmaxf(0.0f, model->amplitude_m2[i] / (float)model->window_samples);
        model->view.features[0] = sqrtf(variance / CERBERUS_CSI_MOTION_CARRIERS);
        for (size_t i = 0; i < 3U; ++i)
            model->view.features[i + 1U] = model->pair_sum[i] / (float)model->window_pairs;
        if (!model->view.calibrated && model->view.calibration_windows < CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS) {
            memcpy(model->calibration[model->view.calibration_windows], model->view.features, sizeof(model->view.features));
            ++model->view.calibration_windows;
        }
        if (!model->view.calibrated && model->view.calibration_windows == CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS)
            calibrate(model);
        if (model->view.calibrated) {
            float highest = 0.0f, second = 0.0f;
            for (size_t i = 0; i < CERBERUS_CSI_MOTION_FEATURES; ++i) {
                float score = bounded((model->view.features[i] - model->baseline[i]) * 12.5f / model->scale[i], 0.0f, 100.0f);
                model->view.feature_scores[i] = percent(score);
                if (score > highest) { second = highest; highest = score; }
                else if (score > second) second = score;
            }
            model->smoothed_score += (0.5f * (highest + second) - model->smoothed_score) * 0.5f;
            model->view.score = percent(model->smoothed_score);
            if (model->view.score >= 28U) {
                if (model->motion_votes < 3U) ++model->motion_votes;
                model->quiet_votes = 0U;
                model->view.state = model->motion_votes >= 2U ? CERBERUS_CSI_MOTION_DETECTED : CERBERUS_CSI_MOTION_POSSIBLE;
            } else {
                model->motion_votes = 0U;
                if (model->view.score >= 12U) { model->quiet_votes = 0U; model->view.state = CERBERUS_CSI_MOTION_POSSIBLE; }
                else {
                    if (model->quiet_votes < 2U) ++model->quiet_votes;
                    model->view.state = model->quiet_votes >= 2U || model->view.state == CERBERUS_CSI_MOTION_CALIBRATING ?
                        CERBERUS_CSI_MOTION_STILL : CERBERUS_CSI_MOTION_POSSIBLE;
                }
            }
            append_history(model, model->view.score);
        } else {
            model->view.state = CERBERUS_CSI_MOTION_CALIBRATING;
            append_history(model, CERBERUS_CSI_MOTION_HISTORY_INVALID);
        }
    } else {
        low_data(model);
        append_history(model, CERBERUS_CSI_MOTION_HISTORY_INVALID);
    }
    if (!model->view.calibrated)
        model->view.calibration_percent = (uint8_t)(model->view.calibration_windows * 100U / CERBERUS_CSI_MOTION_CALIBRATION_WINDOWS);
    empty_window(model);
}

static void advance_windows(cerberus_csi_motion_model_t *model, uint64_t source_us)
{
    uint64_t windows = (source_us - model->window_us) / CERBERUS_CSI_MOTION_WINDOW_US;
    if (!windows) return;
    close_window(model);
    model->window_us += CERBERUS_CSI_MOTION_WINDOW_US;
    --windows;
    if (windows >= CERBERUS_CSI_MOTION_HISTORY) {
        memset(model->view.history, CERBERUS_CSI_MOTION_HISTORY_INVALID, sizeof(model->view.history));
        model->view.history_head = (uint16_t)((model->view.history_head + windows % CERBERUS_CSI_MOTION_HISTORY) % CERBERUS_CSI_MOTION_HISTORY);
        model->view.history_count = CERBERUS_CSI_MOTION_HISTORY;
        model->window_us = source_us - (source_us - model->window_us) % CERBERUS_CSI_MOTION_WINDOW_US;
        model->view.samples_per_second = 0U;
        model->view.rssi = 127;
        low_data(model);
    } else {
        while (windows--) { close_window(model); model->window_us += CERBERUS_CSI_MOTION_WINDOW_US; }
    }
}

bool cerberus_csi_motion_advance(cerberus_csi_motion_model_t *model,
    uint64_t source_us, uint32_t dropped)
{
    if (!model || !model->initialized) return false;
    if (source_us < model->latest_us || dropped < model->view.dropped) {
        low_data(model); model->previous_valid = false; return false;
    }
    model->view.dropped = dropped;
    advance_windows(model, source_us);
    model->latest_us = source_us;
    return true;
}

static bool reject(cerberus_csi_motion_model_t *model)
{
    model->view.samples_rejected = increment(model->view.samples_rejected);
    model->previous_valid = false;
    return false;
}

bool cerberus_csi_motion_observe(cerberus_csi_motion_model_t *model,
    const cerberus_csi_sample_t *sample, size_t sample_bytes)
{
    if (!model || !model->initialized) return false;
    if (!sample || sample_bytes != sizeof(*sample)) return reject(model);
    if (sample->format != CERBERUS_CSI_FORMAT_LLTF_I8 ||
        !(sample->flags & CERBERUS_CSI_SAMPLE_ESTIMATE_VALID) || (sample->flags & ~3U) ||
        sample->secondary > 2U || !sample->channel || sample->channel > 177U ||
        !((sample->phy >= 1U && sample->phy <= 7U) || sample->phy == 11U) ||
        sample->rssi < -127 || sample->rssi > 20 || !sample->sequence ||
        sample->sequence <= model->last_sequence) return reject(model);
    if (sample->source_us < model->latest_us) { low_data(model); return reject(model); }

    float real[CERBERUS_CSI_MOTION_CARRIERS], imag[CERBERUS_CSI_MOTION_CARRIERS];
    float amplitude[CERBERUS_CSI_MOTION_CARRIERS];
    float energy = 0.0f;
    unsigned usable = 0U;
    for (size_t i = 0; i < CERBERUS_CSI_MOTION_CARRIERS; ++i) {
        uint8_t offset; int8_t carrier;
        (void)cerberus_csi_motion_carrier(sample->secondary, i, &offset, &carrier);
        imag[i] = sample->data[offset]; real[i] = sample->data[offset + 1U];
        const float squared = real[i] * real[i] + imag[i] * imag[i];
        energy += squared; amplitude[i] = sqrtf(squared);
        if (squared >= 1.0f) ++usable;
    }
    if (usable < CSI_MIN_TONES) return reject(model);
    advance_windows(model, sample->source_us);
    if (model->layout_valid && (sample->secondary != model->secondary || sample->channel != model->channel)) {
        const uint32_t used = model->view.samples_used, rejected = model->view.samples_rejected;
        const uint32_t dropped = model->view.dropped, resets = increment(model->view.layout_resets);
        cerberus_csi_motion_reset(model, sample->source_us);
        model->view.samples_used = used; model->view.samples_rejected = rejected;
        model->view.dropped = dropped; model->view.layout_resets = resets;
    }
    model->layout_valid = true; model->channel = sample->channel; model->secondary = sample->secondary;
    model->latest_us = sample->source_us; model->last_sequence = sample->sequence;
    if (model->window_samples == UINT16_MAX) return reject(model);
    const float normalization = sqrtf((float)CERBERUS_CSI_MOTION_CARRIERS / energy);
    ++model->window_samples;
    model->view.samples_used = increment(model->view.samples_used);
    const float n = (float)model->window_samples;
    const float rssi_delta = sample->rssi - model->rssi_mean;
    model->rssi_mean += rssi_delta / n;
    model->rssi_m2 += rssi_delta * (sample->rssi - model->rssi_mean);
    for (size_t i = 0; i < CERBERUS_CSI_MOTION_CARRIERS; ++i) {
        amplitude[i] *= normalization;
        const float delta = amplitude[i] - model->amplitude_mean[i];
        model->amplitude_mean[i] += delta / n;
        model->amplitude_m2[i] += delta * (amplitude[i] - model->amplitude_mean[i]);
    }
    if (model->previous_valid) {
        float phase[CERBERUS_CSI_MOTION_CARRIERS], sum_x = 0.0f, sum_y = 0.0f, sum_xx = 0.0f, sum_xy = 0.0f;
        float dot = 0.0f, temporal = 0.0f, last = 0.0f;
        unsigned pairs = 0U;
        for (size_t i = 0; i < CERBERUS_CSI_MOTION_CARRIERS; ++i) {
            dot += amplitude[i] * model->previous_amplitude[i];
            temporal += fabsf(amplitude[i] - model->previous_amplitude[i]);
            phase[i] = NAN;
            if (amplitude[i] == 0.0f || model->previous_amplitude[i] == 0.0f) continue;
            float angle = atan2f(imag[i] * model->previous_real[i] - real[i] * model->previous_imag[i],
                real[i] * model->previous_real[i] + imag[i] * model->previous_imag[i]);
            if (pairs) {
                /* Differences are bounded; unwrap relative to the previous
                 * known carrier without an unbounded iterative loop. */
                angle += roundf((last - angle) / (2.0f * CSI_PI)) * (2.0f * CSI_PI);
            }
            last = phase[i] = angle;
            uint8_t offset; int8_t carrier;
            (void)cerberus_csi_motion_carrier(sample->secondary, i, &offset, &carrier);
            const float x = carrier;
            sum_x += x; sum_y += angle; sum_xx += x * x; sum_xy += x * angle; ++pairs;
        }
        if (pairs >= CSI_MIN_TONES) {
            const float determinant = pairs * sum_xx - sum_x * sum_x;
            const float slope = determinant > 0.0f ? (pairs * sum_xy - sum_x * sum_y) / determinant : 0.0f;
            const float intercept = (sum_y - slope * sum_x) / pairs;
            float residual_energy = 0.0f;
            for (size_t i = 0; i < CERBERUS_CSI_MOTION_CARRIERS; ++i) if (isfinite(phase[i])) {
                uint8_t offset; int8_t carrier;
                (void)cerberus_csi_motion_carrier(sample->secondary, i, &offset, &carrier);
                float residual = phase[i] - intercept - slope * carrier;
                residual_energy += residual * residual;
            }
            model->pair_sum[0] += bounded(1.0f - dot / CERBERUS_CSI_MOTION_CARRIERS, 0.0f, 1.0f);
            model->pair_sum[1] += sqrtf(residual_energy / pairs);
            model->pair_sum[2] += temporal / CERBERUS_CSI_MOTION_CARRIERS;
            ++model->window_pairs;
        }
    }
    memcpy(model->previous_real, real, sizeof(real));
    memcpy(model->previous_imag, imag, sizeof(imag));
    memcpy(model->previous_amplitude, amplitude, sizeof(amplitude));
    model->previous_valid = true;
    return true;
}

void cerberus_csi_motion_view(const cerberus_csi_motion_model_t *model,
    cerberus_csi_motion_view_t *view)
{
    if (!view) return;
    if (model && model->initialized) *view = model->view;
    else { memset(view, 0, sizeof(*view)); view->state = CERBERUS_CSI_MOTION_LOW_DATA; view->rssi = 127;
        memset(view->history, CERBERUS_CSI_MOTION_HISTORY_INVALID, sizeof(view->history)); }
}
