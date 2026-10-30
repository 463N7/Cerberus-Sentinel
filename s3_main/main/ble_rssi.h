#pragma once

/* Shared receive-side measurement policy. HCI127 is unavailable; unsupported
 * values must not replace a measured peak or become a live signal sample.
 * Accept int so callers cannot accidentally validate a narrowed/wrapped value. */
#include <stdbool.h>

#define CERBERUS_BLE_RSSI_UNAVAILABLE 127

static inline bool cerberus_ble_rssi_valid(int value)
{
    return value >= -127 && value <= 20;
}
