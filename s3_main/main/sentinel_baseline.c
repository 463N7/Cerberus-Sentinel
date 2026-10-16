#include "sentinel_baseline.h"

#include "c5_spi_link.h"
#include "settings.h"

#include <string.h>

esp_err_t sentinel_baseline_compare(sentinel_baseline_t *summary)
{
    if (summary == NULL) return ESP_ERR_INVALID_ARG;
    memset(summary, 0, sizeof(*summary));
    summary->checked = true;
    summary->saved = cerberus_settings_wifi_trusted_count();
    cerberus_wifi_scan_status_payload_t status;
    summary->error = c5_spi_link_get_wifi_scan_status(&status);
    if (summary->error != ESP_OK) return summary->error;
    if (status.state != CERBERUS_WIFI_SCAN_COMPLETE) {
        summary->error = ESP_ERR_INVALID_STATE;
        return summary->error;
    }
    if (status.stored_count > CERBERUS_WIFI_SCAN_MAX_RESULTS) {
        summary->error = ESP_ERR_INVALID_RESPONSE;
        return summary->error;
    }
    sentinel_baseline_t candidate = *summary;
    candidate.generation = status.generation;
    for (uint8_t index = 0; index < status.stored_count; ++index) {
        cerberus_c5_wifi_result_t record;
        summary->error = c5_spi_link_get_wifi_scan_result(status.generation, index, &record);
        if (summary->error != ESP_OK) return summary->error;
        switch (cerberus_settings_wifi_trust_state(record.ssid, record.bssid, record.security)) {
        case CERBERUS_WIFI_TRUST_EXACT: ++candidate.exact; break;
        case CERBERUS_WIFI_TRUST_MISMATCH: ++candidate.review; break;
        default: ++candidate.other; break;
        }
        ++candidate.compared;
    }
    cerberus_wifi_scan_status_payload_t verified;
    summary->error = c5_spi_link_get_wifi_scan_status(&verified);
    if (summary->error != ESP_OK) return summary->error;
    if (verified.state != CERBERUS_WIFI_SCAN_COMPLETE ||
        verified.generation != status.generation || verified.stored_count != status.stored_count) {
        summary->error = ESP_ERR_INVALID_RESPONSE;
        return summary->error;
    }
    candidate.error = ESP_OK;
    *summary = candidate;
    return ESP_OK;
}
