#pragma once

#include "wardrive_core.h"

#include <stddef.h>
#include <stdint.h>

/* WiGLE 1.6 formatter. The runtime records GPS at result receipt, matching the
 * separate Wardriving firmware. This is explicitly not an RF timestamp. */
#define CERBERUS_WIGLE_METADATA_HEADER \
    "WigleWifi-1.6,appRelease=Cerberus-Unified,model=Cerberus,release=unversioned," \
    "device=Cerberus,display=ST7796,board=ESP32-S3-C5,brand=Cerberus," \
    "locationSource=GPS-at-result-receipt,timeSource=latest-RMC,rfTime=unavailable\n"
#define CERBERUS_WIGLE_COLUMN_HEADER \
    "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude," \
    "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n"

#define CERBERUS_WIGLE_MAX_SAMPLE_AGE_MS 1000U
#define CERBERUS_WIGLE_MAX_SNAPSHOT_DELAY_MS 1000U

typedef enum {
    CERBERUS_WIGLE_TIME_UNKNOWN = 0,
    /* Timestamp must be captured when the radio reports the frame, then
     * translated to S3 monotonic milliseconds before calling this module. */
    CERBERUS_WIGLE_TIME_RF_CALLBACK = 1,
    CERBERUS_WIGLE_TIME_RESULT_RECEIPT = 2,
} cerberus_wigle_time_source_t;

typedef struct {
    cerberus_wigle_time_source_t source;
    uint64_t rf_observed_monotonic_ms;
    uint64_t gps_snapshot_monotonic_ms;
    uint64_t result_received_monotonic_ms;
} cerberus_wigle_time_provenance_t;

typedef enum {
    CERBERUS_WIGLE_OK = 0,
    CERBERUS_WIGLE_INVALID_ARG,
    CERBERUS_WIGLE_NO_LOCATION,
    CERBERUS_WIGLE_NO_VALID_UTC,
    CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME,
    CERBERUS_WIGLE_UNREPRESENTABLE_SSID,
    CERBERUS_WIGLE_UNSUPPORTED_CHANNEL,
    CERBERUS_WIGLE_BUFFER_TOO_SMALL,
} cerberus_wigle_result_t;

/* Writes one Wi-Fi row only after all location, UTC, declared time, SSID and channel
 * gates pass. A failure leaves out and *written unchanged. Blank altitude and
 * accuracy mean unavailable measurements; HDOP is never reported as meters.
 * SSID bytes are validated as UTF-8 and CSV escaped using label_length. */
cerberus_wigle_result_t cerberus_wardrive_wigle_format_wifi(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    const cerberus_wigle_time_provenance_t *time,
    char *out, size_t capacity, size_t *written);

/* Wi-Fi and BLE export; BLE uses channel 0 and no invented frequency/device
 * type. Result-receipt GPS must be explicitly identified in the file header. */
cerberus_wigle_result_t cerberus_wardrive_wigle_format_observation(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    const cerberus_wigle_time_provenance_t *time,
    char *out, size_t capacity, size_t *written);
