#include "expansion_protocol.h"
/* Kept private: this wire protocol is incompatible with onboard Sentinel V2. */
#include "../../expansion/shared/cerberus_spi_protocol.h"
#include "../../expansion/shared/cerberus_config_proof.h"
#include <string.h>

static uint32_t le32(const uint8_t *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
void expansion_request(uint8_t packet[64], uint8_t command, uint8_t sequence)
{
    memset(packet, 0, 64);
    cerberus_spi_request_footer_t footer = {
        .request_magic = CERBERUS_SPI_REQUEST_MAGIC, .command = command,
        .sequence = sequence, .request_guard = CERBERUS_SPI_REQUEST_GUARD_VALUE(command, sequence),
    };
    memcpy(packet + CERBERUS_SPI_REQUEST_OFFSET, &footer, sizeof(footer));
}
void expansion_config(uint8_t packet[64], uint8_t sequence, unsigned slot, bool enabled)
{
    cerberus_scan_config_t config = {0};
    config.protocol_version = CERBERUS_EXPANSION_PROTOCOL_VERSION;
    config.firmware_version = CERBERUS_EXPANSION_C5_FIRMWARE_VERSION;
    config.node_id = slot + 1;
    config.scan_interval_ms = 1000 + slot * 100;
    config.dwell_time_ms = 120;
    config.passive_dwell_time_ms = 240;
    config.channels_2g_mask = CERBERUS_2G_CHANNEL_MASK_US;
    /* Conservative US non-DFS plan. Each radio covers the full plan so an
     * absent expansion cannot silently remove a group of channels. */
    const uint8_t channels[] = {36, 40, 44, 48, 149, 153, 157, 161, 165};
    for (unsigned i = 0; i < sizeof(channels); ++i)
        config.channels_5g_mask |= cerberus_5g_channel_bit(channels[i]);
    config.ble_scan_interval_ms = 100;
    config.ble_scan_window_ms = 50;
    config.min_rssi = -100;
    config.region = CERBERUS_REGION_US;
    config.scan_enabled = enabled;
    expansion_config_profile(packet, sequence, &config);
}
void expansion_config_profile(uint8_t packet[64], uint8_t sequence,
    const cerberus_scan_config_t *profile)
{
    expansion_request(packet, EXP_CONFIG, sequence);
    if (!profile) return;
    memcpy(packet, profile, sizeof(*profile));
    uint32_t guard = CERBERUS_SPI_CONFIG_GUARD;
    memcpy(packet + sizeof(*profile), &guard, sizeof(guard));
}
static const uint8_t *find(const uint8_t *packet, uint8_t response, uint8_t seq, size_t required)
{
    for (size_t offset = 0; offset + required <= 64; ++offset) {
        const uint8_t *p = packet + offset;
        if (le32(p) == CERBERUS_SPI_MAGIC && p[4] == response &&
            p[6] == CERBERUS_EXPANSION_PROTOCOL_VERSION && p[response == 1 ? 8 : 7] == seq)
            return p;
    }
    return NULL;
}
bool expansion_parse_status(const uint8_t packet[64], uint8_t sequence, expansion_status_t *status)
{
    if (!packet || !status) return false;
    memset(status, 0, sizeof(*status));
    const uint8_t *p = find(packet, 1, sequence, 29);
    if (!p || le32(p + 12) != CERBERUS_EXPANSION_C5_FIRMWARE_VERSION || p[7] > 1) return false;
    status->node_id = p[5]; status->enabled = p[7];
    status->queued = p[9]; status->channel = p[11];
    status->firmware = le32(p + 12); status->dropped = le32(p + 24);
    if ((size_t)(p - packet) + 29U +
        CERBERUS_CONFIG_PROOF_LAST_SCAN_ERROR_AT + sizeof(uint32_t) <= 64U) {
        const uint8_t *proof = p + 29;
        if (proof[CERBERUS_CONFIG_PROOF_TAG0_AT] == CERBERUS_CONFIG_PROOF_TAG0 &&
            proof[CERBERUS_CONFIG_PROOF_TAG1_AT] == CERBERUS_CONFIG_PROOF_TAG1 &&
            proof[CERBERUS_CONFIG_PROOF_VERSION_AT] == CERBERUS_CONFIG_PROOF_VERSION) {
            status->config_proof_supported = true;
            status->config_result = proof[CERBERUS_CONFIG_PROOF_RESULT_AT];
            status->config_fingerprint = cerberus_config_proof_load_u32(
                proof + CERBERUS_CONFIG_PROOF_FINGERPRINT_AT);
            status->scan_rejections = cerberus_config_proof_load_u32(
                proof + CERBERUS_CONFIG_PROOF_SCAN_REJECTIONS_AT);
            status->last_rejected_channel =
                proof[CERBERUS_CONFIG_PROOF_LAST_REJECTED_CHANNEL_AT];
            status->last_scan_error = cerberus_config_proof_load_u32(
                proof + CERBERUS_CONFIG_PROOF_LAST_SCAN_ERROR_AT);
        }
    }
    return true;
}
bool expansion_status_matches_profile(const expansion_status_t *status,
    const cerberus_scan_config_t *profile)
{
    return status && profile && status->config_proof_supported &&
        status->config_result == CERBERUS_CONFIG_RESULT_APPLIED &&
        status->enabled == (profile->scan_enabled != 0U) &&
        status->config_fingerprint == cerberus_config_fingerprint(profile);
}
bool expansion_parse_records(const uint8_t packet[64], uint8_t sequence, unsigned slot,
    expansion_record_t records[EXP_BATCH_MAX], uint8_t *count)
{
    if (!packet || !records || !count || slot >= 6) return false;
    *count = 0;
    memset(records, 0, sizeof(*records) * EXP_BATCH_MAX);
    const uint8_t *p = find(packet, 3, sequence, 12);
    if (!p) return find(packet, 0, sequence, 9) != NULL;
    size_t available = 64 - (size_t)(p - packet);
    unsigned n = p[9], bytes = p[10];
    if (n > EXP_BATCH_MAX || bytes > CERBERUS_SPI_BATCH_PAYLOAD_SIZE || 12 + bytes > available) return false;
    expansion_record_t parsed[EXP_BATCH_MAX] = {0};
    size_t at = 0;
    for (unsigned i = 0; i < n; ++i) {
        if (at + 14 > bytes) return false;
        const uint8_t *wire = p + 12 + at;
        unsigned length = wire[13];
        if (length > 32 || at + 14 + length > bytes || wire[12] > CERBERUS_AUTH_WPA2_WPA3) return false;
        if (!(wire[10] >= 1 && wire[10] <= 14) && !cerberus_5g_channel_to_bit(wire[10])) return false;
        memcpy(parsed[i].bssid, wire + 4, 6);
        parsed[i].channel = wire[10]; parsed[i].rssi = (int8_t)wire[11];
        parsed[i].security = wire[12]; parsed[i].radio = slot + 1;
        parsed[i].ssid_length = length;
        memcpy(parsed[i].ssid, wire + 14, length);
        at += 14 + length;
    }
    if (at != bytes) return false;
    memcpy(records, parsed, sizeof(parsed)); *count = n;
    return true;
}
