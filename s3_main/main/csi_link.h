#pragma once
#include "csi_session.h"
#include "esp_err.h"

void cerberus_csi_link_ops(cerberus_csi_ops_t *ops);
/* Read-only aggregate query. Never initializes or starts a radio/session. */
esp_err_t cerberus_csi_link_diagnostics(cerberus_csi_info_t *info,
    cerberus_csi_observation_t *observation);
