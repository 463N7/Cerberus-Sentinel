#include "wardrive_plan.h"

#include <string.h>

static bool dfs_channel(uint8_t channel)
{
    return channel >= 52U && channel <= 144U;
}

uint16_t cerberus_wardrive_region_2g_mask(uint8_t region)
{
    switch (region) {
    case CERBERUS_REGION_US: return CERBERUS_2G_CHANNEL_MASK_US;
    case CERBERUS_REGION_ETSI: return CERBERUS_2G_CHANNEL_MASK_ETSI;
    case CERBERUS_REGION_JP: return CERBERUS_2G_CHANNEL_MASK_JP;
    default: return 0U;
    }
}

uint32_t cerberus_wardrive_region_5g_mask(uint8_t region)
{
    if (region > CERBERUS_REGION_JP) return 0U;
    uint32_t mask = 0U;
    /* The preserved region plan uses 36-64 in all three regions, 100-144
     * except 144 in ETSI, and 149-165 only in the US. */
    for (uint8_t bit = 1U; bit <= 20U; ++bit) {
        if (region == CERBERUS_REGION_ETSI && bit == 20U) continue;
        mask |= 1UL << bit;
    }
    if (region == CERBERUS_REGION_US)
        for (uint8_t bit = 21U; bit <= 25U; ++bit) mask |= 1UL << bit;
    return mask;
}

static uint8_t choose_radio(const cerberus_wardrive_plan_t *plan)
{
    uint8_t chosen = CERBERUS_WARDRIVE_RADIO_COUNT;
    for (uint8_t slot = 0U; slot < CERBERUS_WARDRIVE_RADIO_COUNT; ++slot) {
        if (!plan->radio[slot].available) continue;
        if (chosen == CERBERUS_WARDRIVE_RADIO_COUNT ||
            plan->radio[slot].planned_dwell_ms < plan->radio[chosen].planned_dwell_ms ||
            (plan->radio[slot].planned_dwell_ms == plan->radio[chosen].planned_dwell_ms &&
             plan->radio[slot].channel_count < plan->radio[chosen].channel_count)) {
            chosen = slot;
        }
    }
    return chosen;
}

static void assign_2g(cerberus_wardrive_plan_t *plan, uint16_t allowed)
{
    /* Give the common non-overlapping channels first, then fill the region.
     * Every channel is still assigned exactly once. */
    static const uint8_t order[] = {1U, 6U, 11U, 2U, 3U, 4U, 5U,
        7U, 8U, 9U, 10U, 12U, 13U, 14U};
    for (unsigned i = 0U; i < sizeof(order); ++i) {
        uint16_t bit = (uint16_t)(1U << order[i]);
        if (!(allowed & bit)) continue;
        uint8_t slot = choose_radio(plan);
        cerberus_wardrive_radio_plan_t *radio = &plan->radio[slot];
        radio->channels_2g_mask |= bit;
        radio->channel_count++;
        radio->planned_dwell_ms += plan->active_dwell_ms;
        plan->assigned_2g_mask |= bit;
    }
}

static void assign_5g(cerberus_wardrive_plan_t *plan, uint32_t allowed)
{
    /* Assign the longer DFS dwells first. Greedy minimum load then bounds the
     * planned dwell spread by at most one channel dwell. */
    for (uint8_t pass = 0U; pass < 2U; ++pass) {
        for (uint8_t bit = 1U; bit <= 25U; ++bit) {
            uint32_t mask = 1UL << bit;
            if (!(allowed & mask)) continue;
            uint8_t channel = cerberus_5g_bit_to_channel(bit);
            bool passive = dfs_channel(channel);
            if ((pass == 0U && !passive) || (pass == 1U && passive)) continue;
            uint8_t slot = choose_radio(plan);
            cerberus_wardrive_radio_plan_t *radio = &plan->radio[slot];
            radio->channels_5g_mask |= mask;
            radio->channel_count++;
            radio->planned_dwell_ms += passive ? plan->passive_dwell_ms :
                plan->active_dwell_ms;
            plan->assigned_5g_mask |= mask;
        }
    }
}

bool cerberus_wardrive_plan_build(uint8_t available_mask, uint8_t region,
    cerberus_wardrive_band_t band, cerberus_wardrive_timing_t timing,
    cerberus_wardrive_plan_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!(available_mask & 1U) || (available_mask & 0x80U) ||
        region > CERBERUS_REGION_JP || band > CERBERUS_WARDRIVE_BAND_5G ||
        timing > CERBERUS_WARDRIVE_TIMING_THOROUGH) return false;
    out->available_mask = available_mask;
    out->region = region;
    out->band = band;
    out->timing = timing;
    static const uint16_t dwell[][2] = {
        {100U, 180U}, {120U, 220U}, {140U, 240U},
    };
    out->active_dwell_ms = dwell[timing][0];
    out->passive_dwell_ms = dwell[timing][1];
    for (uint8_t slot = 0U; slot < CERBERUS_WARDRIVE_RADIO_COUNT; ++slot) {
        out->radio[slot].slot = slot;
        out->radio[slot].available = (available_mask & (1U << slot)) != 0U;
    }
    if (band != CERBERUS_WARDRIVE_BAND_5G)
        assign_2g(out, cerberus_wardrive_region_2g_mask(region));
    if (band != CERBERUS_WARDRIVE_BAND_2G)
        assign_5g(out, cerberus_wardrive_region_5g_mask(region));
    return true;
}

bool cerberus_wardrive_plan_expansion_config(const cerberus_wardrive_plan_t *plan,
    uint8_t slot, bool enable, cerberus_scan_config_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!plan || slot == 0U || slot >= CERBERUS_WARDRIVE_RADIO_COUNT ||
        !(plan->available_mask & (1U << slot)) ||
        !plan->radio[slot].channel_count) return false;
    const cerberus_wardrive_radio_plan_t *radio = &plan->radio[slot];
    out->protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION;
    out->node_id = slot;
    out->firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION;
    out->scan_interval_ms = 3000U;
    out->dwell_time_ms = plan->active_dwell_ms;
    out->passive_dwell_time_ms = plan->passive_dwell_ms;
    out->channels_2g_mask = radio->channels_2g_mask;
    out->channels_5g_mask = radio->channels_5g_mask;
    out->ble_scan_interval_ms = 100U;
    out->ble_scan_window_ms = 100U;
    out->min_rssi = -95;
    out->channel_count = radio->channel_count;
    out->radio_mode = CERBERUS_RADIO_MODE_WIFI;
    out->region = plan->region;
    out->scan_enabled = enable ? 1U : 0U;
    uint8_t index = 0U;
    for (uint8_t channel = 1U; channel <= 14U && index < sizeof(out->channels); ++channel)
        if (radio->channels_2g_mask & (1U << channel)) out->channels[index++] = channel;
    for (uint8_t bit = 1U; bit <= 28U && index < sizeof(out->channels); ++bit)
        if (radio->channels_5g_mask & (1UL << bit))
            out->channels[index++] = cerberus_5g_bit_to_channel(bit);
    return true;
}
