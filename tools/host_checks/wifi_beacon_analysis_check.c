#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wifi_beacon_analysis.h"

static size_t beacon(uint8_t *frame, const uint8_t *ssid, size_t length,
    uint8_t transmitter, uint8_t ds_channel)
{
    assert(length <= 32U);
    memset(frame, 0, 256U);
    frame[0] = 0x80U;
    memset(frame + 4U, 255, 6U);
    frame[10] = frame[16] = 0x02U; /* Locally administered is valid evidence. */
    frame[15] = frame[21] = transmitter;
    frame[22] = 0x50U;
    frame[23] = 0x23U;
    frame[34] = 0x31U;
    frame[36] = 0U;
    frame[37] = (uint8_t)length;
    if (length) memcpy(frame + 38U, ssid, length);
    size_t size = 38U + length;
    if (ds_channel) {
        frame[size++] = 3U;
        frame[size++] = 1U;
        frame[size++] = ds_channel;
    }
    return size;
}

static void rejected(const uint8_t *frame, size_t size)
{
    cerberus_wifi_beacon_observation_t out;
    memset(&out, 0xa5, sizeof(out));
    assert(!cerberus_wifi_beacon_decode(frame, size, 8U, -60, 10U, &out));
    cerberus_wifi_beacon_observation_t zero = {0};
    assert(memcmp(&out, &zero, sizeof(out)) == 0);
}

static void parser_checks(void)
{
    uint8_t frame[256];
    size_t n = beacon(frame, (const uint8_t *)"Lab", 3U, 1U, 6U);
    cerberus_wifi_beacon_observation_t out;
    assert(cerberus_wifi_beacon_decode(frame, n, 8U, -67, 123456U, &out));
    assert(out.ssid_length == 3U && !out.hidden_like && memcmp(out.ssid, "Lab", 3U) == 0);
    assert(out.transmitter[5] == 1U && out.bssid[5] == 1U);
    assert(out.rx_channel == 8U && out.advertised_channel == 6U); /* Adjacent-channel claim stays separate. */
    assert(out.rssi == -67 && out.sequence == 0x235U && out.capabilities == 0x31U);
    assert(out.observed_us == 123456U);
    for (size_t i = 0U; i < 41U; ++i) rejected(frame, i);
    rejected(frame, n - 1U);
    rejected(NULL, n);
    assert(!cerberus_wifi_beacon_decode(frame, n, 0U, -50, 1U, &out));
    assert(!cerberus_wifi_beacon_decode(frame, n, 8U, -50, 1U, NULL));

    /* Unknown IEs may precede SSID; evidence is not tied to byte 37. */
    uint8_t reordered[256];
    memcpy(reordered, frame, 36U);
    reordered[36] = 1U; reordered[37] = 2U; reordered[38] = 0x82U; reordered[39] = 0x84U;
    memcpy(reordered + 40U, frame + 36U, n - 36U);
    assert(cerberus_wifi_beacon_decode(reordered, n + 4U, 8U, -50, 1U, &out));
    assert(out.ssid_length == 3U && memcmp(out.ssid, "Lab", 3U) == 0);

    uint8_t binary[32];
    for (size_t i = 0U; i < sizeof(binary); ++i) binary[i] = (uint8_t)i;
    n = beacon(frame, binary, sizeof(binary), 2U, 0U);
    assert(cerberus_wifi_beacon_decode(frame, n, 36U, -55, 0U, &out));
    assert(out.ssid_length == 32U && !out.hidden_like && !memcmp(out.ssid, binary, sizeof(binary)));
    assert(out.rx_channel == 36U && out.advertised_channel == 0U && out.observed_us == 0U);
    frame[37] = 33U; rejected(frame, n + 1U);

    n = beacon(frame, NULL, 0U, 1U, 8U);
    assert(cerberus_wifi_beacon_decode(frame, n, 8U, -70, 2U, &out) && out.hidden_like);
    uint8_t blank[32] = {0};
    n = beacon(frame, blank, sizeof(blank), 1U, 8U);
    assert(cerberus_wifi_beacon_decode(frame, n, 8U, -70, 2U, &out) && out.hidden_like);

    n = beacon(frame, (const uint8_t *)"Lab", 3U, 1U, 8U);
    frame[n] = 221U; rejected(frame, n + 1U); /* Incomplete IE header. */
    frame[n + 1U] = 3U; frame[n + 2U] = 0U; rejected(frame, n + 3U);
    frame[n] = 0U; frame[n + 1U] = 0U; rejected(frame, n + 2U); /* Duplicate SSID. */
    frame[n] = 3U; frame[n + 1U] = 1U; frame[n + 2U] = 8U; rejected(frame, n + 3U);
    frame[n - 1U] = 0U; rejected(frame, n); /* Zero advertised DS channel. */
    frame[n - 1U] = 8U; frame[n - 2U] = 2U; rejected(frame, n + 1U);
    n = beacon(frame, (const uint8_t *)"Lab", 3U, 1U, 8U);
    frame[36] = 1U; rejected(frame, n); /* Missing SSID. */

    n = beacon(frame, (const uint8_t *)"Lab", 3U, 1U, 8U);
    const uint8_t invalid_fc[] = {0x81U, 0x88U, 0x50U, 0xc0U};
    for (size_t i = 0U; i < sizeof(invalid_fc); ++i) {
        frame[0] = invalid_fc[i]; rejected(frame, n);
    }
    frame[0] = 0x80U;
    const uint8_t invalid_flags[] = {1U, 2U, 4U, 0x40U, 0x80U};
    for (size_t i = 0U; i < sizeof(invalid_flags); ++i) {
        frame[1] = invalid_flags[i]; rejected(frame, n);
    }
    frame[1] = 8U; /* Retry does not create a new SSID by itself. */
    assert(cerberus_wifi_beacon_decode(frame, n, 8U, -60, 10U, &out));
    frame[1] = 0U; frame[22] |= 1U; rejected(frame, n); frame[22] &= 0xf0U;
    frame[4] = 0x02U; rejected(frame, n); frame[4] = 255U;
    frame[10] = 1U; rejected(frame, n); frame[10] = 0x02U;
    memset(frame + 16U, 0, 6U); rejected(frame, n);
    rejected(frame, CERBERUS_WIFI_BEACON_MAX_BYTES + 1U);
}

static cerberus_wifi_multissid_event_t observe(cerberus_wifi_multissid_t *table,
    const uint8_t *name, size_t length, uint8_t transmitter, uint64_t us,
    int8_t rssi, uint16_t *index)
{
    uint8_t frame[256];
    size_t n = beacon(frame, name, length, transmitter, 8U);
    return cerberus_wifi_multissid_observe(table, frame, n, 8U, rssi, us, index);
}

static void accumulator_checks(void)
{
    cerberus_wifi_multissid_record_t storage[2], copy, saved;
    cerberus_wifi_multissid_t table;
    uint16_t index = 0U;
    assert(!cerberus_wifi_multissid_init(&table, storage, 0U));
    assert(!table.records && !table.capacity);
    assert(observe(&table, (const uint8_t *)"A", 1U, 1U, 0U, -70, &index) == CERBERUS_WIFI_MULTISSID_REJECTED);
    assert(index == UINT16_MAX);
    assert(!cerberus_wifi_multissid_init(&table, storage, CERBERUS_WIFI_MULTISSID_MAX_ROWS + 1U));
    assert(cerberus_wifi_multissid_init(&table, storage, 2U));

    assert(observe(&table, (const uint8_t *)"Aa", 2U, 1U, 0U, -70, &index) == CERBERUS_WIFI_MULTISSID_ADDED);
    assert(index == 0U && table.count == 1U && table.confirmed_count == 0U);
    /* "Aa" and "B@" collide at 2242 in the upstream 16-bit hash, but are
       distinct complete SSIDs. Exact byte evidence must retain both. */
    assert(observe(&table, (const uint8_t *)"B@", 2U, 1U, 10U, -60, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(observe(&table, (const uint8_t *)"B@", 2U, 1U, 20U, -80, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(storage[0].variant_count == 2U && storage[0].frames == 3U);
    assert(observe(&table, (const uint8_t *)"C", 1U, 1U, 30U, -65, &index) == CERBERUS_WIFI_MULTISSID_CONFIRMED);
    assert(table.confirmed_count == 1U && storage[0].variant_count == 3U && index == 0U);
    assert(storage[0].peak_rssi == -60 && storage[0].latest.rssi == -65);
    assert(storage[0].first_observed_us == 0U && storage[0].latest.observed_us == 30U);
    assert(cerberus_wifi_multissid_copy(&table, 0U, &copy)); saved = copy;
    assert(observe(&table, (const uint8_t *)"D", 1U, 1U, 40U, -50, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(table.confirmed_count == 1U && storage[0].variant_count == 3U);
    assert(storage[0].latest.ssid[0] == 'D' && !memcmp(&copy, &saved, sizeof(copy)));
    saved = storage[0];
    assert(observe(&table, (const uint8_t *)"E", 1U, 1U, 39U, -20, &index) == CERBERUS_WIFI_MULTISSID_STALE);
    assert(index == UINT16_MAX && table.stale_frames == 1U && !memcmp(&saved, &storage[0], sizeof(saved)));

    assert(observe(&table, NULL, 0U, 2U, 1U, -50, NULL) == CERBERUS_WIFI_MULTISSID_ADDED);
    uint8_t blank[32] = {0};
    assert(observe(&table, blank, 32U, 2U, 2U, -50, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(observe(&table, blank, 1U, 2U, 3U, -50, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(storage[1].variant_count == 1U); /* Blank lengths do not masquerade as different names. */
    uint8_t binary_a[] = {'A', 0U, 'A'}, binary_b[] = {'A', 0U, 'B'};
    assert(observe(&table, binary_a, 3U, 2U, 4U, -50, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(observe(&table, binary_b, 3U, 2U, 5U, -50, NULL) == CERBERUS_WIFI_MULTISSID_CONFIRMED);
    assert(table.confirmed_count == 2U && storage[1].variants[0].length == 0U);
    assert(storage[1].variants[1].length == 3U && storage[1].variants[2].bytes[2] == 'B');
    assert(observe(&table, (const uint8_t *)"A", 1U, 3U, 6U, -20, &index) == CERBERUS_WIFI_MULTISSID_FULL);
    assert(index == UINT16_MAX && table.capacity_drops == 1U && table.count == 2U);
    assert(observe(&table, (const uint8_t *)"Aa", 2U, 1U, 50U, -30, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(storage[0].peak_rssi == -30 && table.count == 2U);

    uint8_t frame[256];
    size_t n = beacon(frame, (const uint8_t *)"Aa", 2U, 1U, 6U);
    frame[21] = 99U;
    assert(cerberus_wifi_multissid_observe(&table, frame, n, 8U, -60, 60U, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(storage[0].multiple_bssids && storage[0].latest.bssid[5] == 99U);
    assert(storage[0].latest.rx_channel == 8U && storage[0].latest.advertised_channel == 6U);
    saved = storage[0]; frame[n] = 221U;
    assert(cerberus_wifi_multissid_observe(&table, frame, n + 1U, 8U, -10, 70U, NULL) == CERBERUS_WIFI_MULTISSID_REJECTED);
    assert(table.rejected_frames == 1U && !memcmp(&saved, &storage[0], sizeof(saved)));

    table.valid_beacons = UINT32_MAX; storage[0].frames = UINT32_MAX;
    assert(observe(&table, (const uint8_t *)"Aa", 2U, 1U, 80U, -30, NULL) == CERBERUS_WIFI_MULTISSID_UPDATED);
    assert(table.valid_beacons == UINT32_MAX && storage[0].frames == UINT32_MAX);
    table.capacity_drops = UINT32_MAX;
    assert(observe(&table, (const uint8_t *)"New", 3U, 4U, 90U, -30, NULL) == CERBERUS_WIFI_MULTISSID_FULL);
    assert(table.capacity_drops == UINT32_MAX);
    assert(!cerberus_wifi_multissid_copy(&table, 2U, &copy));
    cerberus_wifi_multissid_record_t zero = {0};
    assert(!memcmp(&copy, &zero, sizeof(copy)));
    assert(cerberus_wifi_multissid_init(&table, storage, 2U));
    assert(!table.count && !table.confirmed_count && !table.valid_beacons && !table.capacity_drops);
    assert(!memcmp(&storage[0], &zero, sizeof(zero)));
    assert(observe(&table, (const uint8_t *)"Reset", 5U, 1U, 0U, -60, NULL) == CERBERUS_WIFI_MULTISSID_ADDED);
}

int main(void)
{
    parser_checks();
    accumulator_checks();
    _Static_assert(sizeof(cerberus_wifi_multissid_record_t) <= 192U, "Keep default PSRAM footprint bounded");
    printf("Passive beacon and MultiSSID checks passed; %zu-byte row, %zu-byte default storage, no allocations\n",
        sizeof(cerberus_wifi_multissid_record_t),
        CERBERUS_WIFI_MULTISSID_DEFAULT_ROWS * sizeof(cerberus_wifi_multissid_record_t));
    return 0;
}
