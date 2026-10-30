#pragma once

#include "s3_findmy_core.h"

/* BLE HCI legacy advertising report types. The S3 scanner normalizes a
 * connectable extended report to ADV_IND before storing the raw packet. */
#define CERBERUS_FINDMY_RAW_ADV_IND 0U
#define CERBERUS_FINDMY_RAW_DIR_IND 1U
#define CERBERUS_FINDMY_SELECTED_MAX_AGE_MS 30000U

static inline bool cerberus_findmy_ui_fresh(uint32_t observed_ms,
    uint32_t now_ms)
{
    return (uint32_t)(now_ms - observed_ms) <=
        CERBERUS_FINDMY_SELECTED_MAX_AGE_MS;
}

/* A result's connectable flag may be sticky across reports at one address.
 * Sound eligibility comes from the exact latest raw advertisement, not the
 * accumulated inventory flags or merged scan-response name/UUID fields. */
static inline cerberus_findmy_profile_t cerberus_findmy_ui_raw_profile(
    const uint8_t selected_address[6], uint8_t selected_address_type,
    const uint8_t raw_address[6], uint8_t raw_address_type,
    uint8_t raw_event_type, uint8_t raw_flags,
    const uint8_t *raw_ad, size_t raw_ad_length)
{
    if (!selected_address || !raw_address ||
        selected_address_type != raw_address_type ||
        memcmp(selected_address, raw_address, 6U) != 0 || raw_flags != 0U ||
        (raw_event_type != CERBERUS_FINDMY_RAW_ADV_IND &&
         raw_event_type != CERBERUS_FINDMY_RAW_DIR_IND))
        return CERBERUS_FINDMY_PROFILE_NONE;
    return cerberus_findmy_classify_ad(raw_ad, raw_ad_length, true, false);
}
