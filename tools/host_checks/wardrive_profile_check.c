#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wardrive_plan.h"
#include "expansion_protocol.h"
#include "../../expansion/shared/cerberus_spi_protocol.h"
#include "../../expansion/shared/cerberus_config_proof.h"

static void make_status(uint8_t packet[64], uint8_t sequence, uint8_t node,
    const cerberus_scan_config_t *config, uint8_t result)
{
    cerberus_spi_status_response_t status = {0};
    status.magic = CERBERUS_SPI_MAGIC;
    status.response = CERBERUS_SPI_RESP_STATUS;
    status.node_id = node;
    status.protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION;
    status.scan_enabled = config->scan_enabled;
    status.sequence = sequence;
    status.firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION;
    status.reserved[CERBERUS_CONFIG_PROOF_TAG0_AT] = CERBERUS_CONFIG_PROOF_TAG0;
    status.reserved[CERBERUS_CONFIG_PROOF_TAG1_AT] = CERBERUS_CONFIG_PROOF_TAG1;
    status.reserved[CERBERUS_CONFIG_PROOF_VERSION_AT] = CERBERUS_CONFIG_PROOF_VERSION;
    status.reserved[CERBERUS_CONFIG_PROOF_RESULT_AT] = result;
    cerberus_config_proof_store_u32(status.reserved + CERBERUS_CONFIG_PROOF_FINGERPRINT_AT,
        cerberus_config_fingerprint(config));
    cerberus_config_proof_store_u32(status.reserved + CERBERUS_CONFIG_PROOF_SCAN_REJECTIONS_AT,
        7U);
    status.reserved[CERBERUS_CONFIG_PROOF_LAST_REJECTED_CHANNEL_AT] = 144U;
    cerberus_config_proof_store_u32(status.reserved + CERBERUS_CONFIG_PROOF_LAST_SCAN_ERROR_AT,
        0x3002U);
    memcpy(packet, &status, sizeof(status));
}

int main(void)
{
    assert(sizeof(cerberus_spi_status_response_t) == 64U);
    for (uint8_t region = CERBERUS_REGION_US; region <= CERBERUS_REGION_JP; ++region) {
        assert(cerberus_config_region_2g_mask(region) ==
            cerberus_wardrive_region_2g_mask(region));
        assert(cerberus_config_region_5g_mask(region) ==
            cerberus_wardrive_region_5g_mask(region));
    }
    cerberus_wardrive_plan_t plan;
    assert(cerberus_wardrive_plan_build(0x03U, CERBERUS_REGION_ETSI,
        CERBERUS_WARDRIVE_BAND_MIXED, CERBERUS_WARDRIVE_TIMING_THOROUGH, &plan));
    cerberus_scan_config_t config;
    assert(cerberus_wardrive_plan_expansion_config(&plan, 1U, true, &config));
    assert(cerberus_config_payload_valid(&config, 1U));
    uint8_t packet[64] = {0};
    expansion_config_profile(packet, 19U, &config);
    cerberus_spi_config_request_t wire;
    memcpy(&wire, packet, sizeof(wire));
    assert(wire.footer.command == EXP_CONFIG && wire.footer.sequence == 19U);
    assert(wire.config_guard == CERBERUS_SPI_CONFIG_GUARD);
    assert(wire.config.channels_2g_mask == config.channels_2g_mask);
    assert(wire.config.channels_5g_mask == config.channels_5g_mask);
    assert(wire.config.dwell_time_ms == config.dwell_time_ms);

    expansion_status_t parsed;
    make_status(packet, 19U, 1U, &config, CERBERUS_CONFIG_RESULT_APPLIED);
    assert(expansion_parse_status(packet, 19U, &parsed));
    assert(parsed.node_id == 1U && parsed.config_proof_supported);
    assert(parsed.scan_rejections == 7U && parsed.last_rejected_channel == 144U &&
        parsed.last_scan_error == 0x3002U);
    assert(expansion_status_matches_profile(&parsed, &config));
    uint8_t shifted[64] = {0};
    memcpy(shifted + 4U, packet, 46U);
    assert(expansion_parse_status(shifted, 19U, &parsed));
    assert(expansion_status_matches_profile(&parsed, &config));
    cerberus_scan_config_t changed = config;
    changed.dwell_time_ms++;
    assert(!expansion_status_matches_profile(&parsed, &changed));
    changed = config;
    changed.channels_5g_mask ^= 1UL << 1U;
    assert(!expansion_status_matches_profile(&parsed, &changed));
    changed = config;
    changed.channels[0] = 0U;
    changed.channel_count = 0U;
    assert(expansion_status_matches_profile(&parsed, &changed));
    packet[29U + CERBERUS_CONFIG_PROOF_RESULT_AT] = CERBERUS_CONFIG_RESULT_REJECTED;
    assert(expansion_parse_status(packet, 19U, &parsed));
    assert(!expansion_status_matches_profile(&parsed, &config));
    packet[29U + CERBERUS_CONFIG_PROOF_TAG0_AT] = 0U;
    assert(expansion_parse_status(packet, 19U, &parsed));
    assert(!parsed.config_proof_supported && !expansion_status_matches_profile(&parsed, &config));

    changed = config;
    changed.scan_enabled = 0U;
    expansion_config_profile(packet, 20U, &changed);
    memcpy(&wire, packet, sizeof(wire));
    assert(wire.config.scan_enabled == 0U && wire.config_guard == CERBERUS_SPI_CONFIG_GUARD);
    make_status(packet, 20U, 1U, &changed, CERBERUS_CONFIG_RESULT_APPLIED);
    assert(expansion_parse_status(packet, 20U, &parsed));
    assert(expansion_status_matches_profile(&parsed, &changed));
    assert(!expansion_status_matches_profile(&parsed, &config));

    changed = config;
    changed.channels_2g_mask |= CERBERUS_2G_CHANNEL_BIT(14);
    assert(!cerberus_config_payload_valid(&changed, 1U));
    changed = config;
    changed.channels_5g_mask |= cerberus_5g_channel_bit(144U);
    assert(!cerberus_config_payload_valid(&changed, 1U));
    changed = config;
    changed.node_id = 2U;
    assert(!cerberus_config_payload_valid(&changed, 1U));
    changed = config;
    changed.dwell_time_ms = 19U;
    assert(!cerberus_config_payload_valid(&changed, 1U));
    changed = config;
    changed.channels_2g_mask = 0U;
    changed.channels_5g_mask = 0U;
    assert(!cerberus_config_payload_valid(&changed, 1U));
    changed = config;
    changed.scan_enabled = 2U;
    assert(!cerberus_config_payload_valid(&changed, 1U));
    puts("wardrive expansion profile: config, proof, fallback and region validation passed");
    return 0;
}
