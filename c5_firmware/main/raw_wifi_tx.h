#pragma once
#include "wifi_ap_job.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>
esp_err_t cerberus_c5_raw_deauth(const uint8_t *frame,size_t length);
esp_err_t cerberus_c5_raw_association(const cerberus_wifi_ap_job_t *job,const uint8_t *ssid,size_t length);
/* Accept only the exact bounded group-19 frame for this selected AP. */
esp_err_t cerberus_c5_raw_sae_commit(const cerberus_wifi_ap_job_t *job,
    const uint8_t *frame,size_t length);
