#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_plan.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned bits16(uint16_t value)
{
    unsigned n = 0U;
    while (value) { n += value & 1U; value >>= 1U; }
    return n;
}

static unsigned bits32(uint32_t value)
{
    unsigned n = 0U;
    while (value) { n += value & 1U; value >>= 1U; }
    return n;
}

static uint32_t expected_dwell(const cerberus_wardrive_plan_t *plan,
    const cerberus_wardrive_radio_plan_t *radio)
{
    uint32_t dwell = bits16(radio->channels_2g_mask) * plan->active_dwell_ms;
    for (uint8_t bit = 1U; bit <= 25U; ++bit) {
        if (!(radio->channels_5g_mask & (1UL << bit))) continue;
        uint8_t channel = cerberus_5g_bit_to_channel(bit);
        dwell += channel >= 52U && channel <= 144U ?
            plan->passive_dwell_ms : plan->active_dwell_ms;
    }
    return dwell;
}

static void validate_plan(uint8_t available, uint8_t region,
    cerberus_wardrive_band_t band, cerberus_wardrive_timing_t timing)
{
    cerberus_wardrive_plan_t plan;
    assert(cerberus_wardrive_plan_build(available, region, band, timing, &plan));
    assert(plan.available_mask == available);
    assert(plan.region == region && plan.band == band && plan.timing == timing);
    uint16_t wanted_2g = band == CERBERUS_WARDRIVE_BAND_5G ? 0U :
        cerberus_wardrive_region_2g_mask(region);
    uint32_t wanted_5g = band == CERBERUS_WARDRIVE_BAND_2G ? 0U :
        cerberus_wardrive_region_5g_mask(region);
    uint16_t found_2g = 0U;
    uint32_t found_5g = 0U;
    uint32_t minimum = UINT32_MAX, maximum = 0U;
    for (uint8_t slot = 0U; slot < CERBERUS_WARDRIVE_RADIO_COUNT; ++slot) {
        const cerberus_wardrive_radio_plan_t *radio = &plan.radio[slot];
        assert(radio->slot == slot);
        assert(radio->available == ((available & (1U << slot)) != 0U));
        assert(!(found_2g & radio->channels_2g_mask));
        assert(!(found_5g & radio->channels_5g_mask));
        found_2g |= radio->channels_2g_mask;
        found_5g |= radio->channels_5g_mask;
        assert(radio->channel_count == bits16(radio->channels_2g_mask) +
            bits32(radio->channels_5g_mask));
        assert(radio->planned_dwell_ms == expected_dwell(&plan, radio));
        if (!radio->available) {
            assert(!radio->channel_count && !radio->planned_dwell_ms);
            continue;
        }
        assert(radio->channel_count);
        if (radio->planned_dwell_ms < minimum) minimum = radio->planned_dwell_ms;
        if (radio->planned_dwell_ms > maximum) maximum = radio->planned_dwell_ms;
        if (slot == 0U) continue; /* Onboard uses Sentinel V2, not this wire config. */
        cerberus_scan_config_t config;
        assert(cerberus_wardrive_plan_expansion_config(&plan, slot, true, &config));
        assert(config.protocol_version == CERBERUS_EXPANSION_PROTOCOL_VERSION);
        assert(config.node_id == slot && config.firmware_version == CERBERUS_EXPANSION_C5_FIRMWARE_VERSION);
        assert(config.region == region && config.radio_mode == CERBERUS_RADIO_MODE_WIFI);
        assert(config.scan_enabled == 1U && config.scan_interval_ms >= 50U);
        assert(config.channels_2g_mask == radio->channels_2g_mask);
        assert(config.channels_5g_mask == radio->channels_5g_mask);
        assert(config.channel_count == radio->channel_count);
        assert(config.dwell_time_ms == plan.active_dwell_ms);
        assert(config.passive_dwell_time_ms == plan.passive_dwell_ms);
        assert(config.dwell_time_ms >= 20U && config.dwell_time_ms <= 1500U);
        assert(config.passive_dwell_time_ms >= 20U && config.passive_dwell_time_ms <= 1500U);
        cerberus_scan_config_t off;
        assert(cerberus_wardrive_plan_expansion_config(&plan, slot, false, &off));
        assert(off.scan_enabled == 0U && off.channel_count == config.channel_count);
        assert(!memcmp(&off.channels_2g_mask, &config.channels_2g_mask,
            sizeof(config.channels_2g_mask)));
    }
    assert(found_2g == wanted_2g && found_5g == wanted_5g);
    assert(plan.assigned_2g_mask == wanted_2g && plan.assigned_5g_mask == wanted_5g);
    assert(maximum - minimum <= plan.passive_dwell_ms);
}

int main(void)
{
    assert(bits16(cerberus_wardrive_region_2g_mask(CERBERUS_REGION_US)) == 11U);
    assert(bits16(cerberus_wardrive_region_2g_mask(CERBERUS_REGION_ETSI)) == 13U);
    assert(bits16(cerberus_wardrive_region_2g_mask(CERBERUS_REGION_JP)) == 14U);
    assert(bits32(cerberus_wardrive_region_5g_mask(CERBERUS_REGION_US)) == 25U);
    assert(bits32(cerberus_wardrive_region_5g_mask(CERBERUS_REGION_ETSI)) == 19U);
    assert(bits32(cerberus_wardrive_region_5g_mask(CERBERUS_REGION_JP)) == 20U);
    assert(!cerberus_wardrive_region_2g_mask(255U));
    assert(!cerberus_wardrive_region_5g_mask(255U));
    for (uint8_t available = 1U; available < 0x80U; available += 2U)
        for (uint8_t region = CERBERUS_REGION_US; region <= CERBERUS_REGION_JP; ++region)
            for (cerberus_wardrive_band_t band = CERBERUS_WARDRIVE_BAND_MIXED;
                 band <= CERBERUS_WARDRIVE_BAND_5G; ++band)
                for (cerberus_wardrive_timing_t timing = CERBERUS_WARDRIVE_TIMING_FAST;
                     timing <= CERBERUS_WARDRIVE_TIMING_THOROUGH; ++timing)
                    validate_plan(available, region, band, timing);
    cerberus_wardrive_plan_t plan;
    assert(!cerberus_wardrive_plan_build(0U, CERBERUS_REGION_US,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_FAST, &plan));
    assert(!cerberus_wardrive_plan_build(0x80U, CERBERUS_REGION_US,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_FAST, &plan));
    assert(!cerberus_wardrive_plan_build(1U, 255U,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_FAST, &plan));
    assert(!cerberus_wardrive_plan_build(1U, CERBERUS_REGION_US,
        (cerberus_wardrive_band_t)255, CERBERUS_WARDRIVE_TIMING_FAST, &plan));
    assert(!cerberus_wardrive_plan_build(1U, CERBERUS_REGION_US,
        CERBERUS_WARDRIVE_BAND_MIXED, (cerberus_wardrive_timing_t)255, &plan));
    assert(cerberus_wardrive_plan_build(1U, CERBERUS_REGION_US,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_BALANCED, &plan));
    assert(plan.radio[0].channel_count == 36U); /* Beyond legacy channels[14]. */
    cerberus_scan_config_t invalid;
    assert(!cerberus_wardrive_plan_expansion_config(&plan, 0U, true, &invalid));
    assert(!cerberus_wardrive_plan_expansion_config(&plan, 1U, true, &invalid));
    assert(!cerberus_wardrive_plan_expansion_config(&plan, 7U, true, &invalid));
    puts("wardrive plan: 1,728 masks/region/band/timing profiles passed");
    return 0;
}
