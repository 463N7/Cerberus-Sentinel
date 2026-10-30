#include "wardrive_wigle.h"

#include "cerberus_protocol.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static bool continuation(uint8_t byte)
{
    return (byte & 0xC0U) == 0x80U;
}

/* Wi-Fi SSIDs are byte strings, whereas WiGLE 1.6 writes UTF-8 CSV. Refuse
 * bytes that cannot be represented faithfully rather than inventing a name. */
static bool utf8_ssid_valid(const uint8_t *bytes, size_t length)
{
    for (size_t i = 0U; i < length;) {
        uint8_t first = bytes[i];
        if (first < 0x80U) {
            if (first < 0x20U || first == 0x7FU) return false;
            ++i;
            continue;
        }
        size_t followers;
        if (first >= 0xC2U && first <= 0xDFU) followers = 1U;
        else if (first >= 0xE0U && first <= 0xEFU) followers = 2U;
        else if (first >= 0xF0U && first <= 0xF4U) followers = 3U;
        else return false;
        if (followers >= length - i) return false;
        for (size_t j = 1U; j <= followers; ++j)
            if (!continuation(bytes[i + j])) return false;
        uint8_t second = bytes[i + 1U];
        if ((first == 0xE0U && second < 0xA0U) ||
            (first == 0xEDU && second > 0x9FU) ||
            (first == 0xF0U && second < 0x90U) ||
            (first == 0xF4U && second > 0x8FU)) return false;
        i += followers + 1U;
    }
    return true;
}

static bool quoted_ssid(const cerberus_wardrive_observation_t *observation,
    char out[CERBERUS_WARDRIVE_LABEL_CAPACITY * 2U + 3U])
{
    if (!utf8_ssid_valid(observation->label, observation->label_length)) return false;
    size_t used = 0U;
    out[used++] = '"';
    for (size_t i = 0U; i < observation->label_length; ++i) {
        uint8_t byte = observation->label[i];
        if (byte == '"') out[used++] = '"';
        out[used++] = (char)byte;
    }
    out[used++] = '"';
    out[used] = '\0';
    return true;
}

static const char *auth_mode(uint8_t security)
{
    /* Cerberus retains a coarse auth class, not a cipher/PSK assertion. */
    switch (security) {
    case CERBERUS_WIFI_SECURITY_OPEN: return "Open";
    case CERBERUS_WIFI_SECURITY_WEP: return "WEP";
    case CERBERUS_WIFI_SECURITY_WPA: return "WPA";
    case CERBERUS_WIFI_SECURITY_WPA2: return "WPA2";
    case CERBERUS_WIFI_SECURITY_WPA_WPA2: return "WPA/WPA2";
    case CERBERUS_WIFI_SECURITY_WPA3: return "WPA3";
    case CERBERUS_WIFI_SECURITY_WPA2_WPA3: return "WPA2/WPA3";
    case CERBERUS_WIFI_SECURITY_OWE: return "OWE";
    case CERBERUS_WIFI_SECURITY_ENTERPRISE: return "Enterprise";
    default: return "Unknown";
    }
}

static unsigned wifi_frequency_mhz(uint8_t channel)
{
    if (channel >= 1U && channel <= 13U) return 2407U + 5U * channel;
    if (channel == 14U) return 2484U;
    if ((channel >= 36U && channel <= 64U && channel % 4U == 0U) ||
        (channel >= 100U && channel <= 144U && channel % 4U == 0U) ||
        (channel >= 149U && channel <= 165U &&
         (channel - 149U) % 4U == 0U)) return 5000U + 5U * channel;
    return 0U;
}

static bool digits(const char *text, size_t start, size_t count)
{
    for (size_t i = start; i < start + count; ++i)
        if (text[i] < '0' || text[i] > '9') return false;
    return true;
}

static unsigned decimal(const char *text, size_t start, size_t count)
{
    unsigned value = 0U;
    for (size_t i = start; i < start + count; ++i)
        value = 10U * value + (unsigned)(text[i] - '0');
    return value;
}

static bool valid_rmc_utc(const char utc[25])
{
    if (utc[4] != '-' || utc[7] != '-' || utc[10] != 'T' ||
        utc[13] != ':' || utc[16] != ':' || utc[19] != '.' ||
        utc[23] != 'Z' || utc[24] != '\0' ||
        !digits(utc, 0U, 4U) || !digits(utc, 5U, 2U) ||
        !digits(utc, 8U, 2U) || !digits(utc, 11U, 2U) ||
        !digits(utc, 14U, 2U) || !digits(utc, 17U, 2U) ||
        !digits(utc, 20U, 3U)) return false;
    unsigned year = decimal(utc, 0U, 4U);
    unsigned month = decimal(utc, 5U, 2U);
    unsigned day = decimal(utc, 8U, 2U);
    if (month < 1U || month > 12U) return false;
    static const uint8_t days_in_month[12] =
        {31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U};
    unsigned limit = days_in_month[month - 1U];
    if (month == 2U && year % 4U == 0U &&
        (year % 100U != 0U || year % 400U == 0U)) ++limit;
    return year >= 1980U && day >= 1U && day <= limit &&
        decimal(utc, 11U, 2U) <= 23U &&
        decimal(utc, 14U, 2U) <= 59U &&
        decimal(utc, 17U, 2U) <= 59U;
}

cerberus_wigle_result_t cerberus_wardrive_wigle_format_observation(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    const cerberus_wigle_time_provenance_t *time,
    char *out, size_t capacity, size_t *written)
{
    if (observation == NULL || gps == NULL || time == NULL ||
        out == NULL || written == NULL ||
        !cerberus_wardrive_observation_valid(observation) ||
        (observation->type != CERBERUS_WARDRIVE_WIFI &&
         observation->type != CERBERUS_WARDRIVE_BLE))
        return CERBERUS_WIGLE_INVALID_ARG;
    if (gps->state != CERBERUS_WARDRIVE_GPS_FRESH ||
        !gps->coordinates_valid || gps->fix_age_ms > CERBERUS_WIGLE_MAX_SAMPLE_AGE_MS ||
        !isfinite(gps->latitude) || !isfinite(gps->longitude) ||
        gps->latitude < -90.0 || gps->latitude > 90.0 ||
        gps->longitude < -180.0 || gps->longitude > 180.0)
        return CERBERUS_WIGLE_NO_LOCATION;
    if (!gps->utc_valid || !gps->utc_from_rmc ||
        gps->utc_age_ms > CERBERUS_WIGLE_MAX_SAMPLE_AGE_MS ||
        !valid_rmc_utc(gps->utc))
        return CERBERUS_WIGLE_NO_VALID_UTC;
    uint64_t observed_ms = time->source == CERBERUS_WIGLE_TIME_RF_CALLBACK ?
        time->rf_observed_monotonic_ms :
        time->source == CERBERUS_WIGLE_TIME_RESULT_RECEIPT ?
            time->result_received_monotonic_ms : 0U;
    if (observed_ms == 0U ||
        time->gps_snapshot_monotonic_ms < observed_ms ||
        time->gps_snapshot_monotonic_ms - observed_ms >
            CERBERUS_WIGLE_MAX_SNAPSHOT_DELAY_MS)
        return CERBERUS_WIGLE_UNCERTAIN_OBSERVATION_TIME;
    bool wifi = observation->type == CERBERUS_WARDRIVE_WIFI;
    unsigned frequency = wifi ? wifi_frequency_mhz(observation->channel) : 0U;
    if (wifi && frequency == 0U) return CERBERUS_WIGLE_UNSUPPORTED_CHANNEL;
    char ssid[CERBERUS_WARDRIVE_LABEL_CAPACITY * 2U + 3U];
    if (!quoted_ssid(observation, ssid)) return CERBERUS_WIGLE_UNREPRESENTABLE_SSID;
    if (gps->altitude_valid && !isfinite(gps->altitude_m))
        return CERBERUS_WIGLE_NO_LOCATION;
    char first_seen[20];
    memcpy(first_seen, gps->utc, 19U);
    first_seen[10] = ' ';
    first_seen[19] = '\0';
    char altitude[32] = "";
    if (gps->altitude_valid) {
        int length = snprintf(altitude, sizeof(altitude), "%.1f",
            (double)gps->altitude_m);
        if (length < 0 || (size_t)length >= sizeof(altitude))
            return CERBERUS_WIGLE_INVALID_ARG;
    }
    char row[384];
    char frequency_text[12] = "", manufacturer[8] = "";
    if (wifi) (void)snprintf(frequency_text, sizeof(frequency_text), "%u", frequency);
    else if (observation->manufacturer_id != UINT16_MAX)
        (void)snprintf(manufacturer, sizeof(manufacturer), "%u",
            (unsigned)observation->manufacturer_id);
    int length = snprintf(row, sizeof(row),
        "%02X:%02X:%02X:%02X:%02X:%02X,%s,%s,%s,%u,%s,%d,%.7f,%.7f,%s,,,%s,%s\n",
        observation->address[0], observation->address[1], observation->address[2],
        observation->address[3], observation->address[4], observation->address[5],
        ssid, wifi ? auth_mode(observation->security) : "[BLE]", first_seen,
        wifi ? (unsigned)observation->channel : 0U, frequency_text,
        (int)observation->rssi_dbm, gps->latitude, gps->longitude, altitude,
        manufacturer, wifi ? "WIFI" : "BLE");
    if (length < 0 || (size_t)length >= sizeof(row))
        return CERBERUS_WIGLE_BUFFER_TOO_SMALL;
    if (capacity <= (size_t)length) return CERBERUS_WIGLE_BUFFER_TOO_SMALL;
    memcpy(out, row, (size_t)length + 1U);
    *written = (size_t)length;
    return CERBERUS_WIGLE_OK;
}

cerberus_wigle_result_t cerberus_wardrive_wigle_format_wifi(
    const cerberus_wardrive_observation_t *observation,
    const cerberus_wardrive_gps_snapshot_t *gps,
    const cerberus_wigle_time_provenance_t *time,
    char *out, size_t capacity, size_t *written)
{
    if (!observation || observation->type != CERBERUS_WARDRIVE_WIFI)
        return CERBERUS_WIGLE_INVALID_ARG;
    return cerberus_wardrive_wigle_format_observation(observation, gps, time,
        out, capacity, written);
}
