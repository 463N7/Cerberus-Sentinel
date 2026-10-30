#pragma once
#include "radio_test.h"
#include "esp_err.h"
esp_err_t cerberus_ble_test_start(const cerberus_radio_test_config_t *config);
void cerberus_ble_test_status(uint16_t lease_job_id,cerberus_radio_test_status_t *status);
void cerberus_ble_test_stop(uint16_t job_id);
