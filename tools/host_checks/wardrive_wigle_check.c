#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_wigle.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed(cerberus_wardrive_gps_core_t *core, const char *payload,
    uint64_t at_ms)
{
    char line[160];
    unsigned checksum = 0U;
    for (const unsigned char *cursor = (const unsigned char *)payload;
         *cursor != '\0'; ++cursor) checksum ^= *cursor;
    int length = snprintf(line, sizeof(line), "$%s*%02X\r\n", payload, checksum);
    assert(length > 0 && (size_t)length < sizeof(line));
    cerberus_wardrive_gps_core_feed(core, (const uint8_t *)line,
        (size_t)length, at_ms);
}

static void expect_rejected(cerberus_wigle_result_t expected,
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    const cerberus_wigle_time_provenance_t *time)
{
    char row[384] = "UNCHANGED";
    size_t written = 777U;
    assert(cerberus_wardrive_wigle_format_wifi(observation, gps, time,
        row, sizeof(row), &written) == expected);
    assert(strcmp(row, "UNCHANGED") == 0 && written == 777U);
}

int main(void)
{
    assert(strncmp(CERBERUS_WIGLE_METADATA_HEADER, "WigleWifi-1.6,", 14U) == 0);
    assert(strstr(CERBERUS_WIGLE_METADATA_HEADER,
        "locationSource=GPS-at-result-receipt,timeSource=latest-RMC,rfTime=unavailable") != NULL);
    assert(strcmp(CERBERUS_WIGLE_COLUMN_HEADER,
        "MAC,SSID,AuthMode,FirstSeen,Channel,Frequency,RSSI,CurrentLatitude,"
        "CurrentLongitude,AltitudeMeters,AccuracyMeters,RCOIs,MfgrId,Type\n") == 0);
    cerberus_wardrive_observation_t ap = {
        .type = CERBERUS_WARDRIVE_WIFI,
        .radio = CERBERUS_WARDRIVE_RADIO_C5,
        .address = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55},
        .channel = 6,
        .rssi_dbm = -45,
        .security = 3, /* CERBERUS_WIFI_SECURITY_WPA2 */
    };
    static const char raw_ssid[] = "Cafe, \"Lab\"";
    ap.label_length = (uint8_t)strlen(raw_ssid);
    memcpy(ap.label, raw_ssid, ap.label_length);
    cerberus_wardrive_gps_core_t core;
    cerberus_wardrive_gps_core_reset(&core);
    cerberus_wardrive_gps_snapshot_t gps;
    cerberus_wardrive_gps_core_snapshot(&core, 1000U, &gps);
    assert(gps.utc_age_ms == UINT32_MAX);
    cerberus_wigle_time_provenance_t time = {
        .source = CERBERUS_WIGLE_TIME_RF_CALLBACK,
        .rf_observed_monotonic_ms = 1000U,
        .gps_snapshot_monotonic_ms = 1000U,
    };
    expect_rejected(CERBERUS_WIGLE_NO_LOCATION, &ap, &gps, &time);

    feed(&core, "GNRMC,123519.25,A,4807.038,N,01131.000,E,0.0,0.0,230394,,", 1000U);
    cerberus_wardrive_gps_core_snapshot(&core, 1000U, &gps);
    assert(gps.state == CERBERUS_WARDRIVE_GPS_FRESH &&
        gps.utc_valid && gps.utc_from_rmc && gps.utc_age_ms == 0U);
    char row[384] = {0};
    size_t written = 0U;
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    assert(written == strlen(row));
    assert(strcmp(row,
        "02:11:22:33:44:55,\"Cafe, \"\"Lab\"\"\",WPA2,1994-03-23 12:35:19,"
        "6,2437,-45,48.1173000,11.5166667,,,,,WIFI\n") == 0);

    /* A requested SSID is neither an AP nor a located BLE observation. Both
     * WiGLE entry points reject it transactionally even with valid GPS/time. */
    cerberus_wifi_probe_row_t source={.transmitter={2,1,2,3,4,5},
        .ssid={'F','l','o','c','k'},.ssid_length=5,.channel=6,.rssi=-50,
        .name_bits=CERBERUS_WIFI_PROBE_NAME_FLOCK,.frames=1};
    cerberus_wardrive_observation_t probe;
    assert(cerberus_wardrive_observation_from_probe(&source,&probe));
    char probe_output[384]="UNCHANGED";size_t probe_written=123;
    assert(cerberus_wardrive_wigle_format_observation(&probe,&gps,&time,
        probe_output,sizeof(probe_output),&probe_written)==CERBERUS_WIGLE_INVALID_ARG);
    assert(!strcmp(probe_output,"UNCHANGED")&&probe_written==123);
    expect_rejected(CERBERUS_WIGLE_INVALID_ARG,&probe,&gps,&time);

    cerberus_wigle_time_provenance_t receipt_time = {
        .source = CERBERUS_WIGLE_TIME_RESULT_RECEIPT,
        .result_received_monotonic_ms = 1000U,
        .gps_snapshot_monotonic_ms = 1000U,
    };
    assert(cerberus_wardrive_wigle_format_observation(&ap, &gps, &receipt_time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    receipt_time.result_received_monotonic_ms = 0U;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME, &ap, &gps, &receipt_time);
    receipt_time.result_received_monotonic_ms = 1000U;
    receipt_time.gps_snapshot_monotonic_ms = 2001U;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME, &ap, &gps, &receipt_time);

    char too_small[8] = "KEPT";
    size_t small_written = 9U;
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        too_small, sizeof(too_small), &small_written) ==
        CERBERUS_WIGLE_BUFFER_TOO_SMALL);
    assert(strcmp(too_small, "KEPT") == 0 && small_written == 9U);

    cerberus_wigle_time_provenance_t bad_time = time;
    bad_time.source = CERBERUS_WIGLE_TIME_UNKNOWN;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME,
        &ap, &gps, &bad_time);
    bad_time = time;
    bad_time.rf_observed_monotonic_ms = 0U;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME,
        &ap, &gps, &bad_time);
    bad_time = time;
    bad_time.gps_snapshot_monotonic_ms = 2001U;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME,
        &ap, &gps, &bad_time);
    bad_time = time;
    bad_time.gps_snapshot_monotonic_ms = 999U;
    expect_rejected(CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME,
        &ap, &gps, &bad_time);

    cerberus_wardrive_gps_snapshot_t no_utc = gps;
    no_utc.utc_valid = false;
    expect_rejected(CERBERUS_WIGLE_NO_VALID_UTC, &ap, &no_utc, &time);
    no_utc = gps;
    no_utc.utc_age_ms = 1001U;
    expect_rejected(CERBERUS_WIGLE_NO_VALID_UTC, &ap, &no_utc, &time);
    no_utc = gps;
    no_utc.utc[5] = '9';
    expect_rejected(CERBERUS_WIGLE_NO_VALID_UTC, &ap, &no_utc, &time);
    cerberus_wardrive_gps_snapshot_t no_fix = gps;
    no_fix.fix_age_ms = 1001U;
    expect_rejected(CERBERUS_WIGLE_NO_LOCATION, &ap, &no_fix, &time);
    no_fix = gps;
    no_fix.coordinates_valid = false;
    expect_rejected(CERBERUS_WIGLE_NO_LOCATION, &ap, &no_fix, &time);

    cerberus_wardrive_observation_t bad_ap = ap;
    bad_ap.label[2] = 0U;
    expect_rejected(CERBERUS_WIGLE_UNREPRESENTABLE_SSID,
        &bad_ap, &gps, &time);
    bad_ap = ap;
    bad_ap.label[0] = 0xC0U; /* Overlong UTF-8 lead. */
    expect_rejected(CERBERUS_WIGLE_UNREPRESENTABLE_SSID,
        &bad_ap, &gps, &time);
    bad_ap = ap;
    bad_ap.channel = 15U;
    expect_rejected(CERBERUS_WIGLE_UNSUPPORTED_CHANNEL,
        &bad_ap, &gps, &time);
    bad_ap = ap;
    bad_ap.type = CERBERUS_WARDRIVE_BLE;
    expect_rejected(CERBERUS_WIGLE_INVALID_ARG, &bad_ap, &gps, &time);

    ap.channel = 14U;
    ap.label_length = 0U; /* Hidden SSID is represented as empty quoted text. */
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    assert(strstr(row, ",\"\",WPA2,") != NULL &&
        strstr(row, ",14,2484,-45,") != NULL);
    ap.channel = 149U;
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    assert(strstr(row, ",149,5745,-45,") != NULL);

    static const uint8_t utf8_name[] = {'C', 'a', 'f', 0xC3U, 0xA9U};
    memcpy(ap.label, utf8_name, sizeof(utf8_name));
    ap.label_length = sizeof(utf8_name);
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    assert(strstr(row, "\"Caf\xC3\xA9\"") != NULL);

    feed(&core, "GPGGA,123519.25,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 1200U);
    cerberus_wardrive_gps_core_snapshot(&core, 1200U, &gps);
    time.rf_observed_monotonic_ms = 1200U;
    time.gps_snapshot_monotonic_ms = 1200U;
    assert(cerberus_wardrive_wigle_format_wifi(&ap, &gps, &time,
        row, sizeof(row), &written) == CERBERUS_WIGLE_OK);
    assert(strstr(row, ",545.4,,,,WIFI\n") != NULL);
    assert(strstr(row, ",0.9,") == NULL); /* HDOP is not meters. */

    /* A later valid GGA can refresh coordinates without refreshing RMC UTC. */
    feed(&core, "GPGGA,123519.25,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 1700U);
    cerberus_wardrive_gps_core_snapshot(&core, 2201U, &gps);
    assert(gps.state == CERBERUS_WARDRIVE_GPS_FRESH &&
        gps.fix_age_ms == 501U && gps.utc_age_ms == 1201U);
    time.rf_observed_monotonic_ms = 2201U;
    time.gps_snapshot_monotonic_ms = 2201U;
    expect_rejected(CERBERUS_WIGLE_NO_VALID_UTC, &ap, &gps, &time);

    cerberus_wardrive_gps_core_snapshot(&core, 6201U, &gps);
    expect_rejected(CERBERUS_WIGLE_NO_LOCATION, &ap, &gps, &time);
    puts("Wardrive WiGLE 1.6 formatter: strict GPS/UTC/time, SSID, frequency and blank metrics passed");
    return 0;
}
