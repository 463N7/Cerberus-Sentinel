#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wardrive_gps_core.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void sentence(const char *payload, char out[160])
{
    unsigned checksum = 0U;
    for (const unsigned char *cursor = (const unsigned char *)payload;
         *cursor != '\0'; ++cursor) checksum ^= *cursor;
    int length = snprintf(out, 160, "$%s*%02X\r\n", payload, checksum);
    assert(length > 0 && length < 160);
}

static void feed(cerberus_wardrive_gps_core_t *core, const char *payload,
    uint64_t time_ms)
{
    char line[160];
    sentence(payload, line);
    size_t length = strlen(line);
    size_t half = length / 2U;
    cerberus_wardrive_gps_core_feed(core, (const uint8_t *)line, half, time_ms);
    cerberus_wardrive_gps_core_feed(core,
        (const uint8_t *)line + half, length - half, time_ms);
}

static bool near(double a, double b)
{
    double difference = a - b;
    return difference > -0.00001 && difference < 0.00001;
}

int main(void)
{
    cerberus_wardrive_gps_core_t core;
    cerberus_wardrive_gps_snapshot_t status;
    cerberus_wardrive_gps_core_reset(&core);
    cerberus_wardrive_gps_core_snapshot(&core, 0U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_ABSENT);
    assert(!status.coordinates_valid && !status.utc_valid && status.latitude == 0.0);
    assert(status.utc_age_ms == UINT32_MAX);

    feed(&core, "GNRMC,123519.25,A,4807.038,N,01131.000,E,0.0,0.0,230394,,", 1000U);
    cerberus_wardrive_gps_core_snapshot(&core, 1000U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_FRESH);
    assert(status.coordinates_valid && near(status.latitude, 48.1173));
    assert(near(status.longitude, 11.5166667));
    assert(status.utc_valid && status.utc_from_rmc);
    assert(status.utc_age_ms == 0U);
    assert(strcmp(status.utc, "1994-03-23T12:35:19.250Z") == 0);
    assert(status.fix_updates == 1U && status.valid_sentences == 1U);

    feed(&core, "GPGGA,123519.25,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 1200U);
    cerberus_wardrive_gps_core_snapshot(&core, 1200U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_FRESH);
    assert(status.altitude_valid && status.altitude_m > 545.39f && status.altitude_m < 545.41f);
    assert(status.satellites == 8U && status.hdop > 0.89f && status.hdop < 0.91f);
    assert(status.utc_valid && status.fix_updates == 2U && status.utc_age_ms == 200U);

    char bad[160];
    sentence("GPRMC,123520.00,V,,,,,,,230394,,", bad);
    size_t bad_length = strlen(bad);
    bad[bad_length - 4U] = bad[bad_length - 4U] == '0' ? '1' : '0';
    cerberus_wardrive_gps_core_feed(&core, (const uint8_t *)bad, bad_length, 1500U);
    cerberus_wardrive_gps_core_snapshot(&core, 1500U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_FRESH);
    assert(status.bad_checksums == 1U && status.valid_sentences == 2U);

    feed(&core, "GPRMC,123520.00,V,,,,,,,230394,,", 1700U);
    cerberus_wardrive_gps_core_snapshot(&core, 1700U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);
    assert(!status.coordinates_valid && !status.utc_valid && status.latitude == 0.0);

    feed(&core, "GPRMC,123521.00,A,9100.000,N,01131.000,E,0.0,0.0,230394,,", 2000U);
    cerberus_wardrive_gps_core_snapshot(&core, 2000U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);
    assert(status.malformed_sentences >= 1U);

    feed(&core, "GPRMC,123522.00,A,4807.038,N,01131.000,E,0.0,0.0,310299,,", 2500U);
    cerberus_wardrive_gps_core_snapshot(&core, 2500U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_FRESH);
    assert(status.coordinates_valid && !status.utc_valid);
    assert(status.utc_age_ms == UINT32_MAX);

    feed(&core, "GPRMC,123523.00,A,4807.038,N,01131.000,E,0.0,0.0,290224,,", 3000U);
    cerberus_wardrive_gps_core_snapshot(&core, 3000U, &status);
    assert(status.utc_valid && strcmp(status.utc, "2024-02-29T12:35:23.000Z") == 0);

    feed(&core, "GPGGA,123524.00,4807.038,N,01131.000,E,1,09,1.2,20.0,M,,,", 3200U);
    cerberus_wardrive_gps_core_snapshot(&core, 3200U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_FRESH && !status.utc_valid);
    assert(status.utc_age_ms == UINT32_MAX);

    feed(&core, "GPGSV,1,1,00", 8201U);
    cerberus_wardrive_gps_core_snapshot(&core, 8201U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_STALE);
    assert(!status.coordinates_valid && !status.utc_valid && status.longitude == 0.0);
    cerberus_wardrive_gps_core_snapshot(&core, 18202U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_ABSENT);
    assert(!status.coordinates_valid && !status.utc_valid);

    feed(&core, "GPGGA,123525.00,,,,,0,00,99.9,,,,,,", 19000U);
    cerberus_wardrive_gps_core_snapshot(&core, 19000U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);

    feed(&core, "GPRMC,123526.00,A,4807.038,N,01131.000,E,0.0,0.0,290224,,", 19100U);
    feed(&core, "GPGGA,123526.00,4807.038,N,01131.000,E,1,08,0.9,999999.0,M,,,", 19200U);
    cerberus_wardrive_gps_core_snapshot(&core, 19200U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);

    char interrupted[160];
    sentence("GPRMC,123527.00,A,4807.038,N,01131.000,E,0.0,0.0,290224,,", interrupted);
    size_t before_cr = strlen(interrupted) - 4U;
    interrupted[before_cr] = '\r';
    cerberus_wardrive_gps_core_feed(&core, (const uint8_t *)interrupted,
        strlen(interrupted), 19300U);
    cerberus_wardrive_gps_core_snapshot(&core, 19300U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);

    char overlong[180];
    memset(overlong, 'A', sizeof(overlong));
    overlong[sizeof(overlong) - 1U] = '\n';
    cerberus_wardrive_gps_core_feed(&core, (const uint8_t *)overlong,
        sizeof(overlong), 20000U);
    assert(core.overflow_lines == 1U && core.line_length == 0U);
    feed(&core, "GPGSV,1,1,00", 20100U);
    cerberus_wardrive_gps_core_snapshot(&core, 20100U, &status);
    assert(status.state == CERBERUS_WARDRIVE_GPS_NO_FIX);

    char without_checksum[] = "$GPRMC,123519,A,4807.038,N,01131.000,E,0,0,230394";
    assert(!cerberus_wardrive_gps_core_ingest_line(&core, without_checksum,
        strlen(without_checksum), 21000U));
    cerberus_wardrive_gps_core_snapshot(&core, 21000U, &status);
    assert(status.valid_sentences == 12U);

    puts("Wardriving GPS core checks passed: strict NMEA, fix age, absent/no-fix/stale/fresh, UTC provenance, overflow.");
    return 0;
}
