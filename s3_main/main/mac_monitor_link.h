#pragma once
#include "esp_err.h"
#include "mac_monitor_session.h"

/* Caller-owned progress only; the controller's existing staging snapshot is
 * the receive buffer. Keep the first pre-PEEK time across lost replies/retries
 * so a pinned older snapshot never acquires a falsely recent age. */
typedef struct {
    cerberus_mac_binding_t binding;
    cerberus_mac_peek_t peek;
    uint64_t request_started_us, last_info_us;
    uint16_t offset;
    bool attempted, descriptor_valid, ready;
} cerberus_mac_c5_link_t;

void cerberus_mac_c5_link_init(cerberus_mac_c5_link_t *link,
    cerberus_mac_monitor_ops_t *ops);
/* Independent read-only C5 status; never starts/pins/renews a capture. */
esp_err_t cerberus_mac_c5_diagnostics(cerberus_mac_info_t *info,
    cerberus_mac_status_t *status, cerberus_mac_counters_t *counters);
