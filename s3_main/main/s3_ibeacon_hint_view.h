#pragma once

/* Render an advertised iBeacon tuple, never a physical-device identity.
 * CAL is the signed calibration byte; it is not the live receiver RSSI. */
#include "ibeacon.h"
#include <stdio.h>

static inline void cerberus_ibeacon_uuid_line(const cerberus_ibeacon_t *beacon,
    char *out, size_t capacity)
{
    if (!out || !capacity) return;
    if (!beacon) { out[0]='\0'; return; }
    snprintf(out,capacity,
        "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        beacon->uuid[0],beacon->uuid[1],beacon->uuid[2],beacon->uuid[3],
        beacon->uuid[4],beacon->uuid[5],beacon->uuid[6],beacon->uuid[7],
        beacon->uuid[8],beacon->uuid[9],beacon->uuid[10],beacon->uuid[11],
        beacon->uuid[12],beacon->uuid[13],beacon->uuid[14],beacon->uuid[15]);
}

static inline void cerberus_ibeacon_values_line(const cerberus_ibeacon_t *beacon,
    char *out, size_t capacity)
{
    if (!out || !capacity) return;
    if (!beacon) { out[0]='\0'; return; }
    snprintf(out,capacity,"IBEACON MAJOR %u MINOR %u CAL %d dBm",
        (unsigned)beacon->major,(unsigned)beacon->minor,(int)beacon->measured_power_dbm);
}

static inline void cerberus_ibeacon_source_line(bool response, bool current,
    uint32_t age_ms, bool compact, char *out, size_t capacity)
{
    if (!out || !capacity) return;
    snprintf(out,capacity,compact ? "IBEACON %s %s / %lus" :
        "IBEACON FORMAT / %s / %s / %lus",
        response ? "RSP" : "ADV",current ? "CURRENT" : "EARLIER",
        (unsigned long)(age_ms/1000U));
}
