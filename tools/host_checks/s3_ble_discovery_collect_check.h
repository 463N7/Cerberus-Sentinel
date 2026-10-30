/* Actual focused collector, with an independent family/capacity oracle. */
static unsigned discovery_checks;
#define D_CHECK(condition) do { ++discovery_checks; assert(condition); } while (0)

static void discovery_emit(cerberus_s3_ble_snapshot_t *bank, uint8_t identity,
    uint8_t type, const uint8_t *data, size_t size, uint32_t now, bool response)
{
    cerberus_s3_ble_report_t report = {.address = {identity,2,3,4,5,6},
        .address_type = type, .rssi = -55, .event_type = response ? 4U : 0U,
        .connectable = !response, .scan_response = response, .complete = true,
        .data = data, .length = size};
    cerberus_s3_ble_snapshot_observe(bank, &report, now);
}

static void check_focused_discovery_collect(void)
{
    static const uint8_t ordinary[] = {2,1,6};
    static const uint8_t findmy[] = {3,3,0x44,0xfd};
    static const uint8_t flipper[] = {3,3,0x81,0x30};
    static const uint8_t flock[] = {15,9,'F','S',' ','E','x','t',' ','B','a','t','t','e','r','y'};
    static const uint8_t meta[] = {3,0x16,0x5f,0xfd};
    static const uint8_t hc[] = {6,9,'H','C','-','0','5'};
    static const uint8_t malformed[] = {6,9,'H'};
    uint8_t beacon[27];
    make_ibeacon(beacon, 1U, 2U, 3U, -30);
    const uint8_t *payloads[] = {NULL,beacon,findmy,flipper,flock,meta,hc};
    const size_t sizes[] = {0,sizeof(beacon),sizeof(findmy),sizeof(flipper),sizeof(flock),sizeof(meta),sizeof(hc)};
    cerberus_s3_ble_snapshot_t bank;
    cerberus_s3_ble_binding_cache_t cache;
    for (unsigned family = 1U; family < CERBERUS_S3_BLE_FILTER_COUNT; ++family) {
        D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 9U, 0U, false, 10U,
            (cerberus_s3_ble_filter_t)family, &cache));
        for (unsigned i = 0; i < 96U; ++i)
            discovery_emit(&bank, (uint8_t)i, 1U, ordinary, sizeof(ordinary), i, false);
        D_CHECK(bank.status.stored_count == 0U && cache.count == 8U && cache.overflows == 88U);
        discovery_emit(&bank, 200U, 1U, payloads[family], sizes[family], 100U, false);
        D_CHECK(bank.status.stored_count == 1U && bank.records[0].address[0] == 200U);
        D_CHECK(bank.records[0].focus.category_bits & (1U << (family - 1U)));
        for (unsigned i = 0; i < 96U; ++i)
            discovery_emit(&bank, (uint8_t)i, 1U, ordinary, sizeof(ordinary), 101U+i, false);
        D_CHECK(bank.status.stored_count == 1U && bank.records[0].address[0] == 200U &&
            bank.inventory_replacements == 0U && bank.status.dropped_reports == 0U);

        /* A response-only match promotes the exact original record, never
         * response bytes, RSSI, or arrival time as raw advertising evidence. */
        D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 10U, 100U, false, 11U,
            (cerberus_s3_ble_filter_t)family, &cache));
        discovery_emit(&bank, 77U, 1U, ordinary, sizeof(ordinary), 110U, false);
        discovery_emit(&bank, 77U, 1U, payloads[family], sizes[family], 112U, true);
        D_CHECK(bank.status.stored_count == 1U && cache.count == 0U);
        D_CHECK(bank.records[0].last_advertisement_ms == 110U && bank.records[0].reports_seen == 1U &&
            bank.records[0].advertisement_length == sizeof(ordinary) &&
            !memcmp(bank.records[0].advertisement, ordinary, sizeof(ordinary)));
        D_CHECK(bank.records[0].focus.discovery.advertisement_observed_ms == 110U &&
            bank.records[0].focus.discovery.scan_response_observed_ms == 112U);
        discovery_emit(&bank, 77U, 1U, ordinary, sizeof(ordinary), 113U, false);
        D_CHECK(bank.status.stored_count == 1U); /* Export typed retirement, not silent removal. */
        D_CHECK((bank.records[0].focus.category_bits & (1U << (family - 1U))) ==
            (family == CERBERUS_S3_BLE_FILTER_IBEACON ? CERBERUS_S3_BLE_FOCUS_IBEACON : 0U));
        D_CHECK(!(bank.records[0].focus.discovery.flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID));

        D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 11U, 100U, false, 12U,
            (cerberus_s3_ble_filter_t)family, &cache));
        discovery_emit(&bank, 77U, 1U, ordinary, sizeof(ordinary), 110U, false);
        discovery_emit(&bank, 77U, 2U, payloads[family], sizes[family], 111U, true);
        discovery_emit(&bank, 77U, 1U, payloads[family], sizes[family], 109U, true);
        discovery_emit(&bank, 77U, 1U, malformed, sizeof(malformed), 112U, false);
        discovery_emit(&bank, 77U, 1U, payloads[family], sizes[family], 113U, true);
        D_CHECK(bank.status.stored_count == 0U);
        discovery_emit(&bank, 78U, 1U, ordinary, sizeof(ordinary), 114U, false);
        discovery_emit(&bank, 78U, 1U, payloads[family], sizes[family], 2115U, true);
        D_CHECK(bank.status.stored_count == 0U);
        /* Complete invalid and partial sources cannot consume binding slots. */
        uint8_t count = cache.count;
        for (unsigned i = 0; i < 40U; ++i)
            discovery_emit(&bank, (uint8_t)i, 1U, malformed, sizeof(malformed), 2116U+i, false);
        D_CHECK(cache.count == count && bank.status.stored_count == 0U);
    }

    /* Fixed capacity and deterministic replacement independent of unrelated
     * traffic, with nonqualifying admitted rows selected before valid rows. */
    D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 1U, 0U, false, 20U,
        CERBERUS_S3_BLE_FILTER_FIND_MY, &cache));
    for (unsigned i = 0; i < 32U; ++i)
        discovery_emit(&bank, (uint8_t)i, 1U, findmy, sizeof(findmy), i, false);
    discovery_emit(&bank, 19U, 1U, ordinary, sizeof(ordinary), 40U, false);
    discovery_emit(&bank, 99U, 1U, findmy, sizeof(findmy), 41U, false);
    D_CHECK(bank.records[19].address[0] == 99U && bank.records[0].address[0] == 0U);
    D_CHECK(bank.status.stored_count == 32U && bank.inventory_replacements == 1U);
    for (unsigned i = 0; i < 100U; ++i) {
        discovery_emit(&bank, (uint8_t)(100U+i), 2U, ordinary, sizeof(ordinary), 42U+i, false);
        D_CHECK(bank.status.stored_count == 32U && bank.inventory_replacements == 1U);
    }
    discovery_emit(&bank, 98U, 1U, findmy, sizeof(findmy), 145U, false);
    D_CHECK(bank.records[0].address[0] == 98U && bank.inventory_replacements == 2U);
    cache.overflows = UINT16_MAX;
    discovery_emit(&bank, 240U, 1U, ordinary, sizeof(ordinary), 146U, false);
    D_CHECK(cache.overflows == UINT16_MAX);

    /* Source provenance still handles zero and the ordinary32 clock wrap. */
    D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 1U, UINT32_MAX-5U, true, 21U,
        CERBERUS_S3_BLE_FILTER_FIND_MY, &cache));
    discovery_emit(&bank, 1U, 1U, ordinary, sizeof(ordinary), UINT32_MAX-1U, false);
    discovery_emit(&bank, 1U, 1U, findmy, sizeof(findmy), 0U, true);
    D_CHECK(bank.status.stored_count == 1U && bank.records[0].last_advertisement_ms == UINT32_MAX-1U);
    cerberus_s3_ble_snapshot_finish(&bank, false, 0, 10U);
    cerberus_s3_ble_snapshot_t frozen = bank;
    discovery_emit(&bank, 2U, 1U, findmy, sizeof(findmy), 11U, false);
    D_CHECK(!memcmp(&bank, &frozen, sizeof(bank)));
    D_CHECK(!cerberus_s3_ble_snapshot_begin_discovery(&bank, 1U, 0U, false, 22U,
        CERBERUS_S3_BLE_FILTER_FIND_MY, NULL));
    D_CHECK(!memcmp(&bank, &frozen, sizeof(bank)));
    /* Extended SID provenance, exact2000ms response boundary and no source
     * promotion from truncated/malformed/mismatched callback metadata. */
    D_CHECK(cerberus_s3_ble_snapshot_begin_discovery(&bank, 3U, 0U, true, 30U,
        CERBERUS_S3_BLE_FILTER_FIND_MY, &cache));
    cerberus_s3_ble_report_t report = {.address = {1,2,3,4,5,6}, .address_type = 1U,
        .rssi = -42, .event_type = 0U, .complete = true, .sid_valid = true,
        .sid = 3U, .data = ordinary, .length = sizeof(ordinary)};
    cerberus_s3_ble_snapshot_observe(&bank, &report, 0U);
    D_CHECK(cache.count == 1U && cache.records[0].last_advertisement_ms == 0U);
    report.scan_response = true;
    report.data = findmy;
    report.length = sizeof(findmy);
    report.sid = 4U;
    cerberus_s3_ble_snapshot_observe(&bank, &report, 1U);
    report.sid = 3U;
    report.sid_invalid = true;
    cerberus_s3_ble_snapshot_observe(&bank, &report, 2U);
    report.sid_invalid = false;
    report.complete = false;
    cerberus_s3_ble_snapshot_observe(&bank, &report, 3U);
    D_CHECK(bank.status.stored_count == 0U);
    report.complete = true;
    cerberus_s3_ble_snapshot_observe(&bank, &report, 2000U);
    D_CHECK(bank.status.stored_count == 1U && bank.records[0].last_advertisement_ms == 0U);
    D_CHECK(bank.records[0].focus.discovery.scan_response_observed_ms == 2000U);
    /* A changed SID original invalidates the old response; its new response
     * requires that exact replacement SID. */
    report.scan_response = false;
    report.sid = 4U;
    report.data = ordinary;
    report.length = sizeof(ordinary);
    cerberus_s3_ble_snapshot_observe(&bank, &report, 2001U);
    report.scan_response = true;
    report.sid = 3U;
    report.data = findmy;
    report.length = sizeof(findmy);
    cerberus_s3_ble_snapshot_observe(&bank, &report, 2002U);
    D_CHECK(!(bank.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY));
    report.sid = 4U;
    cerberus_s3_ble_snapshot_observe(&bank, &report, 2003U);
    D_CHECK(bank.records[0].focus.category_bits & CERBERUS_S3_BLE_FOCUS_FIND_MY);
    /* A complete source longer than the raw legacy copy can qualify without
     * making that truncated copy eligible for an active profile. */
    uint8_t extended[40] = {35U,0x30U};
    memcpy(extended + 36U, findmy, sizeof(findmy));
    discovery_emit(&bank, 8U, 1U, extended, sizeof(extended), 2004U, false);
    D_CHECK(bank.status.stored_count == 2U && bank.records[1].advertisement_length == 31U &&
        bank.records[1].advertisement_flags == CERBERUS_BLE_ADVERTISEMENT_TRUNCATED &&
        (bank.records[1].focus.discovery.flags & CERBERUS_S3_BLE_EVIDENCE_ADV_VALID) &&
        !(bank.records[1].focus.discovery.flags & CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE));
    for (unsigned i = 0; i < 8U; ++i)
        discovery_emit(&bank, (uint8_t)(30U+i), 1U, ordinary, sizeof(ordinary), 2100U+i, false);
    cerberus_s3_ble_binding_cache_t old_cache = cache;
    discovery_emit(&bank, 250U, 1U, ordinary, sizeof(ordinary), 2090U, false);
    D_CHECK(!memcmp(&cache, &old_cache, sizeof(cache)));
    bank.report_sequence = UINT32_MAX;
    discovery_emit(&bank, 251U, 1U, findmy, sizeof(findmy), 2200U, false);
    D_CHECK(bank.status.stored_count == 2U && !memcmp(&cache, &old_cache, sizeof(cache)));
    bank.revision = UINT64_MAX - 1U;
    frozen = bank;
    discovery_emit(&bank, 252U, 1U, findmy, sizeof(findmy), 2201U, false);
    D_CHECK(!memcmp(&bank, &frozen, sizeof(bank)));
    cerberus_s3_ble_snapshot_finish(&bank, false, 0, 2202U);
    D_CHECK(bank.revision == UINT64_MAX && bank.status.state == CERBERUS_BLE_SCAN_COMPLETE);
    printf("Focused discovery collector: %u checks; binding cache %zu bytes; independent family admission, response promotion, retirement and bounded retention passed.\n",
        discovery_checks, sizeof(cache));
}
#undef D_CHECK
