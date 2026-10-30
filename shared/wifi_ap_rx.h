#pragma once
#include "wifi_ap_job.h"
enum { CERBERUS_AP_RX_BEACON=1, CERBERUS_AP_RX_MATCH=2 };
/* Packet without FCS. A matching identity/frame is evidence of reception,
 * not proof of which physical transmitter sent it or of client disconnection. */
unsigned cerberus_wifi_ap_rx_classify(const cerberus_wifi_ap_job_t *job,
    const uint8_t *frame,size_t length);
/* Count a frame only if it matches one of the immutable clients bound to this
 * AP job. Target AP beacons are still counted once, even with 32 clients. */
unsigned cerberus_wifi_ap_rx_classify_stations(const cerberus_wifi_ap_job_t *job,
    const cerberus_wifi_ap_station_set_t *stations,const uint8_t *frame,size_t length);
