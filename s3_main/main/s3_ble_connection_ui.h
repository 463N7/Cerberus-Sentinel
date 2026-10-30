#pragma once

#include <stdbool.h>
#include <string.h>
#include "cerberus_protocol.h"

typedef enum {
    CERBERUS_BLE_LINK_NONE = 0,
    CERBERUS_BLE_LINK_CONNECT,
    CERBERUS_BLE_LINK_CANCEL,
    CERBERUS_BLE_LINK_DISCONNECT,
    CERBERUS_BLE_LINK_RETRY_CLOSE,
} cerberus_ble_link_action_t;

typedef struct {
    cerberus_ble_link_action_t action;
    const char *label;
} cerberus_ble_link_control_t;

/* A close/cancel always belongs to the exact selected typed address. A failed
 * close remains owned until the host confirms release; a retry only requests
 * another close. Advertising connectability is irrelevant to closing a link. */
static inline cerberus_ble_link_control_t cerberus_ble_link_control(bool available,
    const cerberus_ble_connection_status_payload_t *status,
    const uint8_t address[6], uint8_t address_type, bool connectable)
{
    if (!available || !status || !address || address_type > 3U)
        return (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_NONE, "UNAVAILABLE"};
    bool active = status->state == CERBERUS_BLE_CONNECTION_CONNECTING ||
        status->state == CERBERUS_BLE_CONNECTION_CONNECTED ||
        status->state == CERBERUS_BLE_CONNECTION_DISCONNECTING;
    if (active && (status->address_type != address_type ||
        memcmp(status->address, address, 6U)))
        return (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_NONE, "IN USE"};
    switch (status->state) {
    case CERBERUS_BLE_CONNECTION_CONNECTING:
        return (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_CANCEL, "CANCEL"};
    case CERBERUS_BLE_CONNECTION_CONNECTED:
        return (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_DISCONNECT, "DISCONNECT"};
    case CERBERUS_BLE_CONNECTION_DISCONNECTING:
        return status->error_code ?
            (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_RETRY_CLOSE, "RETRY CLOSE"} :
            (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_NONE, "CLOSING"};
    case CERBERUS_BLE_CONNECTION_IDLE:
    case CERBERUS_BLE_CONNECTION_ERROR:
        return connectable ?
            (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_CONNECT, "CONNECT"} :
            (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_NONE, "ADV ONLY"};
    default:
        return (cerberus_ble_link_control_t){CERBERUS_BLE_LINK_NONE, "UNAVAILABLE"};
    }
}
