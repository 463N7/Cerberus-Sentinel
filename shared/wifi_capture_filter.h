#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Masks and BSSID flag are defined in cerberus_protocol.h. The caller passes
 * complete 802.11 bytes, excluding radiotap and FCS. */
bool cerberus_wifi_capture_filter_match(uint8_t mode_mask,bool filter_bssid,
    const uint8_t bssid[6],const uint8_t *frame,size_t length);
