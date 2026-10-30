/* Private implementation included after the snapshot's shared parsers. */

static bool discovery_record_matches(const cerberus_s3_ble_record_t *record,
    cerberus_s3_ble_filter_t family)
{
    return family == CERBERUS_S3_BLE_FILTER_ALL ||
        (record->focus.category_bits & (uint8_t)(1U << (family - 1U))) != 0U;
}

static bool discovery_record_clock(const cerberus_s3_ble_record_t *record,
    uint32_t now)
{
    const cerberus_s3_ble_discovery_evidence_t *source = &record->focus.discovery;
    return (int32_t)(now - source->advertisement_observed_ms) >= 0 &&
        (int32_t)(now - record->last_advertisement_ms) >= 0 &&
        (!(source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) ||
         (int32_t)(now - source->scan_response_observed_ms) >= 0);
}

static cerberus_s3_ble_record_t *discovery_find(cerberus_s3_ble_record_t *records,
    size_t count, const cerberus_s3_ble_report_t *report, size_t *index)
{
    for (size_t i = 0U; i < count; ++i) {
        if (records[i].address_type == report->address_type &&
            memcmp(records[i].address, report->address, 6U) == 0) {
            if (index) *index = i;
            return &records[i];
        }
    }
    return NULL;
}

static bool discovery_parse(const cerberus_s3_ble_report_t *report,
    cerberus_ble_ad_hints_t *parsed, cerberus_ble_findmy_hints_t *findmy,
    cerberus_ble_flipper_hints_t *flipper, cerberus_ble_flock_hints_t *flock)
{
    return report->complete && report->address_type <= 3U && !invalid_sid(report) &&
        (!report->length || report->data) && report->length <= SENTINEL_WATCH_PROTOCOL_BLE_MAX &&
        cerberus_ble_ad_hints_parse(report->data, report->length, parsed) &&
        cerberus_ble_findmy_hints_parse(report->data, report->length, findmy) &&
        cerberus_ble_flipper_hints_parse(report->data, report->length, flipper) &&
        cerberus_ble_flock_hints_parse(report->data, report->length, flock);
}

static void discovery_raw_original(cerberus_s3_ble_record_t *record,
    const cerberus_s3_ble_report_t *report, uint32_t now, bool valid)
{
    if (!report->complete || (report->length && !report->data) ||
        report->length > SENTINEL_WATCH_PROTOCOL_BLE_MAX) return;
    if (cerberus_ble_rssi_valid(report->rssi) &&
        (!cerberus_ble_rssi_valid(record->rssi) || report->rssi > record->rssi))
        record->rssi = report->rssi;
    if (report->connectable) record->flags |= CERBERUS_BLE_RESULT_CONNECTABLE;
    size_t length = smaller(report->length, sizeof(record->advertisement));
    memset(record->advertisement, 0, sizeof(record->advertisement));
    if (length) memcpy(record->advertisement, report->data, length);
    record->advertisement_length = (uint8_t)length;
    record->advertisement_event_type = report->event_type;
    record->advertisement_flags = report->length > length ? CERBERUS_BLE_ADVERTISEMENT_TRUNCATED : 0U;
    if (!valid) record->advertisement_flags |= CERBERUS_BLE_ADVERTISEMENT_MALFORMED;
    record->last_advertisement_ms = now;
    record->advertisement_sid = report->sid;
    record->advertisement_sid_valid = report->sid_valid;
    if (record->reports_seen != UINT16_MAX) ++record->reports_seen;
}

static bool discovery_apply(cerberus_s3_ble_record_t *record,
    const cerberus_s3_ble_report_t *report, uint8_t generation,
    uint32_t sequence, uint32_t now)
{
    if (!sequence || !discovery_record_clock(record, now)) return false;
    cerberus_s3_ble_discovery_evidence_t *source = &record->focus.discovery;
    cerberus_ble_ad_hints_t parsed;
    cerberus_ble_findmy_hints_t findmy;
    cerberus_ble_flipper_hints_t flipper;
    cerberus_ble_flock_hints_t flock;
    if (report->scan_response) {
        const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
        if (!report->length || !valid_response_event(report) ||
            (source->flags & required) != required || !source->advertisement_sequence ||
            source->generation != generation || sequence <= source->advertisement_sequence ||
            (uint32_t)(now - source->advertisement_observed_ms) > 2000U ||
            ((source->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) != 0U) != report->sid_valid ||
            (report->sid_valid && source->sid != report->sid) ||
            !discovery_parse(report, &parsed, &findmy, &flipper, &flock)) return false;
        parse_fields(record, report->data, report->length, true);
        source->findmy_scan_response = findmy;
        source->flipper_scan_response = flipper;
        source->meta_scan_response = parsed.meta;
        source->flock_scan_response = flock;
        source->hc_scan_response = cerberus_ble_ad_hc_name_code(&parsed);
        source->scan_response_sequence = sequence;
        source->scan_response_observed_ms = now;
        source->flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    } else {
        /* Invalidate before any malformed/incomplete return, including SID
         * changes. No subsequent response may bind the previous original. */
        begin_discovery_original(record, report, generation, sequence, now);
        bool valid = report->event_type <= 3U &&
            discovery_parse(report, &parsed, &findmy, &flipper, &flock);
        discovery_raw_original(record, report, now, valid);
        if (!valid) return false;
        parse_fields(record, report->data, report->length, false);
        source->findmy_advertisement = findmy;
        source->flipper_advertisement = flipper;
        source->meta_advertisement = parsed.meta;
        source->flock_advertisement = flock;
        source->hc_advertisement = cerberus_ble_ad_hc_name_code(&parsed);
        source->flags |= CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
        record->focus.flock_oui_clue = cerberus_ble_flock_oui_hint(report->address, report->address_type);
    }
    record->focus.meta_suppressed |= parsed.blocked_id;
    observe_ibeacon(record, report, sequence, now);
    publish_discovery_focus(record);
    return true;
}

static void discovery_supporting_observers(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now, bool timed, uint64_t callback_us)
{
    if (report->scan_response) return;
    cerberus_ble_activity_observe(&snapshot->activity, report->address, report->address_type,
        report->data, report->length, report->complete, now);
    sentinel_watch_protocol_record_t protocol;
    bool matched = report->complete && report->address_type <= 3U &&
        sentinel_watch_protocol_decode_ble(report->data, report->length, &protocol);
    if (timed)
        cerberus_watch_capture_observe_timed(&snapshot->watch, matched ? &protocol : NULL,
            report->address, report->address_type, report->rssi, 0U, callback_us);
    else
        cerberus_watch_capture_observe(&snapshot->watch, matched ? &protocol : NULL,
            report->address, report->address_type, report->rssi, 0U);
    if (snapshot->activity.status.identities > snapshot->status.total_count)
        snapshot->status.total_count = snapshot->activity.status.identities;
}

static void discovery_cache_insert(cerberus_s3_ble_binding_cache_t *cache,
    const cerberus_s3_ble_record_t *record)
{
    size_t index = cache->count;
    if (index == CERBERUS_S3_BLE_DISCOVERY_BINDINGS) {
        index = 0U;
        for (size_t i = 1U; i < cache->count; ++i)
            if (cache->records[i].focus.discovery.advertisement_sequence <
                cache->records[index].focus.discovery.advertisement_sequence) index = i;
        if (cache->overflows != UINT16_MAX) ++cache->overflows;
    } else {
        ++cache->count;
    }
    cache->records[index] = *record; /* Whole replacement clears prior identity and raw eligibility. */
}

static bool discovery_promote(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_record_t *record, uint32_t now)
{
    size_t index = snapshot->status.stored_count;
    if (index == CERBERUS_BLE_SCAN_MAX_RESULTS) {
        cerberus_s3_ble_record_t *victim = oldest_inventory_record(snapshot, now);
        if (!victim) return false;
        for (size_t i = 0U; i < snapshot->status.stored_count; ++i) {
            if (!discovery_record_matches(&snapshot->records[i], snapshot->family)) {
                victim = &snapshot->records[i];
                break;
            }
        }
        index = (size_t)(victim - snapshot->records);
        if (snapshot->inventory_replacements != UINT16_MAX) ++snapshot->inventory_replacements;
    } else {
        ++snapshot->status.stored_count;
    }
    snapshot->records[index] = *record;
    return true;
}

static void discovery_observe_focused(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t sequence, uint32_t now,
    bool timed, uint64_t callback_us)
{
    discovery_supporting_observers(snapshot, report, now, timed, callback_us);
    if (!sequence || report->address_type > 3U ||
        (int32_t)(now - snapshot->activity.started_ms) < 0 || !snapshot->bindings) return;
    /* A backwards producer clock must not evict an unrelated pending binding
     * or a qualifying record. The short procedure admits no half-range gap. */
    for (size_t i = 0U; i < snapshot->status.stored_count; ++i)
        if (!discovery_record_clock(&snapshot->records[i], now)) return;
    for (size_t i = 0U; i < snapshot->bindings->count; ++i)
        if (!discovery_record_clock(&snapshot->bindings->records[i], now)) return;
    cerberus_s3_ble_record_t *record = discovery_find(snapshot->records,
        snapshot->status.stored_count, report, NULL);
    if (record) {
        (void)discovery_apply(record, report, snapshot->status.generation, sequence, now);
        return;
    }
    size_t pending_index = 0U;
    cerberus_s3_ble_binding_cache_t *cache = snapshot->bindings;
    record = discovery_find(cache->records, cache->count, report, &pending_index);
    if (record) {
        (void)discovery_apply(record, report, snapshot->status.generation, sequence, now);
        if (discovery_record_matches(record, snapshot->family) && discovery_promote(snapshot, record, now)) {
            --cache->count;
            if (pending_index < cache->count)
                memmove(&cache->records[pending_index], &cache->records[pending_index + 1U],
                    (cache->count - pending_index) * sizeof(cache->records[0]));
            memset(&cache->records[cache->count], 0, sizeof(cache->records[0]));
        }
        return;
    }
    if (report->scan_response) return;
    cerberus_s3_ble_record_t candidate = {0};
    memcpy(candidate.address, report->address, sizeof(candidate.address));
    candidate.address_type = report->address_type;
    candidate.rssi = CERBERUS_BLE_RSSI_UNAVAILABLE;
    candidate.manufacturer_id = CERBERUS_BLE_MANUFACTURER_UNKNOWN;
    /* A new row has no older timestamp; initialize both clocks so a normal
     * 32-bit uptime wrap cannot look like a backwards jump from zero. */
    candidate.last_advertisement_ms = now;
    candidate.focus.discovery.advertisement_observed_ms = now;
    if (!discovery_apply(&candidate, report, snapshot->status.generation, sequence, now)) return;
    if (discovery_record_matches(&candidate, snapshot->family)) {
        if (!discovery_promote(snapshot, &candidate, now) && snapshot->status.dropped_reports != UINT16_MAX)
            ++snapshot->status.dropped_reports;
    } else {
        discovery_cache_insert(cache, &candidate);
    }
}
