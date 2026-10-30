#pragma once
#include "network_scan.h"
#include "esp_err.h"
esp_err_t cerberus_network_scanner_init(void);
esp_err_t cerberus_network_scanner_start(const cerberus_net_scan_request_t *request);
esp_err_t cerberus_network_scanner_stop(uint16_t job_id);
void cerberus_network_scanner_status(cerberus_net_scan_status_t *status);
esp_err_t cerberus_network_scanner_result(const cerberus_net_scan_query_t *query,
    cerberus_net_scan_result_t *result);
