#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "wifi_ap_job.h"

/* Marauder's selected-AP AP-spam variant is a beacon with the scanned SSID,
 * channel and capability bytes, but a distinct locally administered BSSID.
 * The clone BSSID is frozen in job.station for mode 8, despite its AP-only
 * scope. C5 must obtain metadata from its locked scan snapshot. */
enum { CERBERUS_WIFI_AP_CLONE_MODE = 8, CERBERUS_WIFI_AP_CLONE_MAX_FRAME = 109 };

/* Returns an FCS-less beacon length (77..109), or zero for invalid input.
 * padded_ssid_length may exceed ssid_length only with ASCII spaces. With an
 * unknown/hidden source SSID (length zero) the frame cannot claim its name. */
size_t cerberus_wifi_ap_clone_beacon(const cerberus_wifi_ap_job_t *job,
    const uint8_t clone_bssid[6],const uint8_t *ssid,size_t ssid_length,
    const uint8_t capabilities[2],uint8_t padded_ssid_length,uint64_t timestamp,
    uint8_t *out,size_t capacity);

/* Strict frame matcher for S3 2.4 GHz reception. Caller must also check the
 * radio RX control channel and FCS status before passing the 802.11 bytes.
 * UINT8_MAX accepts any valid padded length, for per-frame random padding. */
bool cerberus_wifi_ap_clone_rx_classify(const cerberus_wifi_ap_job_t *job,
    const uint8_t clone_bssid[6],const uint8_t *ssid,size_t ssid_length,
    const uint8_t capabilities[2],uint8_t padded_ssid_length,
    const uint8_t *frame,size_t length);
