#include "wifi_beacon_session_private.h"
#include <string.h>

esp_err_t cerberus_s3_pinescan_init(void)
{ return cerberus_beacon_session_init(); }
esp_err_t cerberus_s3_pinescan_start(bool save_pcap)
{ return cerberus_beacon_session_start(CERBERUS_BEACON_SESSION_PINESCAN, save_pcap); }
void cerberus_s3_pinescan_stop(void)
{ cerberus_beacon_session_stop(CERBERUS_BEACON_SESSION_PINESCAN); }

esp_err_t cerberus_s3_pinescan_status(cerberus_s3_pinescan_status_t *out)
{
    if (!out) return ESP_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    cerberus_beacon_session_status_t state;
    esp_err_t error = cerberus_beacon_session_status(CERBERUS_BEACON_SESSION_PINESCAN, &state);
    if (error != ESP_OK) return error;
    *out = (cerberus_s3_pinescan_status_t){
        .capture = state.capture, .generation = state.generation, .following = state.following,
        .candidates = state.matches, .capacity = state.capacity,
        .valid_beacons = state.valid_beacons, .matching_beacons = state.matching_beacons,
        .rejected_frames = state.rejected_frames, .capacity_drops = state.capacity_drops,
        .stale_frames = state.stale_frames, .received_24ghz = state.received_24ghz,
        .received_5ghz = state.received_5ghz,
    };
    return ESP_OK;
}

esp_err_t cerberus_s3_pinescan_page(uint16_t offset,
    cerberus_s3_pinescan_row_t *rows, size_t capacity, cerberus_s3_pinescan_page_t *page)
{ return cerberus_beacon_session_page(CERBERUS_BEACON_SESSION_PINESCAN, offset, rows, capacity, page); }
esp_err_t cerberus_s3_pinescan_follow(const cerberus_s3_pinescan_row_t *selected, bool save_pcap)
{
    if (!selected || !selected->evidence.last_match.reasons) return ESP_ERR_INVALID_ARG;
    return cerberus_beacon_session_follow(CERBERUS_BEACON_SESSION_PINESCAN,
        selected->generation, &selected->evidence.latest, save_pcap);
}
esp_err_t cerberus_s3_pinescan_follow_status(cerberus_s3_pinescan_follow_t *out)
{ return cerberus_beacon_session_follow_status(CERBERUS_BEACON_SESSION_PINESCAN, out); }
esp_err_t cerberus_s3_pinescan_release(void)
{ return cerberus_beacon_session_release(CERBERUS_BEACON_SESSION_PINESCAN); }
