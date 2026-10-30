#pragma once
#include "radio_test.h"
#include "wifi_ap_job.h"
#include "esp_err.h"
esp_err_t cerberus_c5_test_init(void);
esp_err_t cerberus_c5_ap_start(const cerberus_wifi_ap_job_t *request,bool radio_busy);
esp_err_t cerberus_c5_ap_stage_begin(const cerberus_wifi_ap_station_begin_t *request,bool radio_busy);
esp_err_t cerberus_c5_ap_stage_chunk(const cerberus_wifi_ap_station_chunk_t *request);
esp_err_t cerberus_c5_test_begin(const cerberus_radio_test_begin_t *request);
esp_err_t cerberus_c5_test_chunk(const cerberus_radio_test_chunk_t *request);
esp_err_t cerberus_c5_test_start(uint16_t job_id, bool radio_busy);
esp_err_t cerberus_c5_test_stop(uint16_t job_id);
void cerberus_c5_test_status(uint16_t lease_job_id,cerberus_radio_test_status_t *status);
bool cerberus_c5_test_running(void);
/* Called by the scanner's existing callback; no allocation or blocking locks. */
void cerberus_c5_test_receive(const uint8_t *frame,size_t length,uint8_t channel);
