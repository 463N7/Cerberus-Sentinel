#include "s3_ble_snapshot.h"
#include "sentinel_watch_protocol.h"
#include "ble_ad_hints.h"
#include "ble_flock_oui.h"
#include "ble_rssi.h"

#include <string.h>

_Static_assert(sizeof(cerberus_s3_ble_discovery_evidence_t) == 64U,
    "Current HC source codes must fit the shared provenance padding");

static size_t smaller(size_t value, size_t limit)
{
    return value < limit ? value : limit;
}

static bool invalid_sid(const cerberus_s3_ble_report_t *report)
{ return report->sid_invalid || (report->sid_valid && report->sid > 15U); }

static bool valid_response_event(const cerberus_s3_ble_report_t *report)
{
    /* Legacy SCAN_RSP is event 4. Nonlegacy events are normalized by the
     * scanner to connectable/nonconnectable (0/3), with a valid SID. */
    return report->sid_valid ? (report->event_type == 0U || report->event_type == 3U) :
        report->event_type == 4U;
}

static void publish_discovery_focus(cerberus_s3_ble_record_t *record)
{
    const cerberus_s3_ble_discovery_evidence_t *evidence = &record->focus.discovery;
    uint8_t adv = evidence->findmy_advertisement.bits;
    uint8_t rsp = evidence->findmy_scan_response.bits;
    record->focus.category_bits &= (uint8_t)~(CERBERUS_S3_BLE_FOCUS_FIND_MY |
        CERBERUS_S3_BLE_FOCUS_FLIPPER | CERBERUS_S3_BLE_FOCUS_META_HINT |
        CERBERUS_S3_BLE_FOCUS_FLOCK | CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT);
    record->focus.findmy_evidence = NULL;
    record->focus.flipper_evidence = NULL;
    record->focus.meta_evidence = NULL;
    record->focus.flock_evidence = NULL;
    if (adv || rsp) {
        record->focus.category_bits |= CERBERUS_S3_BLE_FOCUS_FIND_MY;
        record->focus.findmy_evidence = adv && rsp ? "FIND MY HINTS IN ADV + MATCHED RESPONSE" :
            adv ? "FIND MY FORMAT HINT IN ADVERTISEMENT" : "FIND MY FORMAT HINT IN MATCHED RESPONSE";
    }
    adv = evidence->flipper_advertisement.bits;
    rsp = evidence->flipper_scan_response.bits;
    if (adv || rsp) {
        record->focus.category_bits |= CERBERUS_S3_BLE_FOCUS_FLIPPER;
        record->focus.flipper_evidence = adv && rsp ? "FLIPPER HINTS IN ADV + MATCHED RESPONSE" :
            adv ? "FLIPPER SERVICE HINT IN ADVERTISEMENT" : "FLIPPER SERVICE HINT IN MATCHED RESPONSE";
    }
    adv = cerberus_ble_meta_evidence_allowed(&evidence->meta_advertisement);
    rsp = cerberus_ble_meta_evidence_allowed(&evidence->meta_scan_response);
    if (!record->focus.meta_suppressed && (adv || rsp)) {
        record->focus.category_bits |= CERBERUS_S3_BLE_FOCUS_META_HINT;
        record->focus.meta_evidence = adv && rsp ? "META ID HINTS IN ADV + MATCHED RESPONSE" :
            adv ? "META ID HINT IN ADVERTISEMENT" : "META ID HINT IN MATCHED RESPONSE";
    }
    adv = cerberus_ble_flock_hints_level(&evidence->flock_advertisement);
    rsp = cerberus_ble_flock_hints_level(&evidence->flock_scan_response);
    if (adv || rsp || record->focus.flock_oui_clue) {
        record->focus.category_bits |= CERBERUS_S3_BLE_FOCUS_FLOCK;
        record->focus.flock_evidence = adv && rsp ? "FLOCK HINTS IN ADV + MATCHED RESPONSE" :
            adv ? "FLOCK HINT IN ADVERTISEMENT" : rsp ? "FLOCK HINT IN MATCHED RESPONSE" :
            "PUBLIC ADDRESS PREFIX CLUE / NOT IDENTITY";
    }
    adv = evidence->hc_advertisement;
    rsp = evidence->hc_scan_response;
    if (adv || rsp) {
        record->focus.category_bits |= CERBERUS_S3_BLE_FOCUS_HC_NAME_HINT;
    }
}

static void begin_discovery_original(cerberus_s3_ble_record_t *record,
    const cerberus_s3_ble_report_t *report, uint8_t generation,
    uint32_t sequence, uint32_t observed_ms)
{
    cerberus_s3_ble_discovery_evidence_t *evidence = &record->focus.discovery;
    memset(evidence, 0, sizeof(*evidence));
    evidence->advertisement_sequence = sequence;
    evidence->advertisement_observed_ms = observed_ms;
    memcpy(evidence->address, report->address, sizeof(evidence->address));
    evidence->address_type = report->address_type;
    evidence->generation = generation;
    evidence->sid = report->sid;
    if (report->complete) evidence->flags |= CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE;
    if (report->complete && (!report->length || report->data) &&
        report->length <= sizeof(record->advertisement))
        evidence->flags |= CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    if (invalid_sid(report)) evidence->flags |= CERBERUS_S3_BLE_EVIDENCE_SID_INVALID;
    else if (report->sid_valid) evidence->flags |= CERBERUS_S3_BLE_EVIDENCE_SID_VALID;
    publish_discovery_focus(record);
}

size_t cerberus_s3_ble_focus_page(const uint8_t *category_bits, size_t count,
    cerberus_s3_ble_filter_t filter, size_t offset, uint8_t *indexes,
    size_t capacity)
{
    if ((!category_bits && count) || filter >= CERBERUS_S3_BLE_FILTER_COUNT ||
        count > UINT8_MAX + 1U || (capacity && !indexes)) return 0U;
    uint8_t wanted = filter == CERBERUS_S3_BLE_FILTER_ALL ? 0U :
        (uint8_t)(1U << (filter - 1U));
    size_t matched = 0U, written = 0U;
    for (size_t i = 0U; i < count; ++i) {
        if (wanted && !(category_bits[i] & wanted)) continue;
        if (matched >= offset && written < capacity) indexes[written++] = (uint8_t)i;
        ++matched;
    }
    return matched;
}

static void parse_fields(cerberus_s3_ble_record_t *record, const uint8_t *data,
    size_t length, bool scan_response)
{
    for (size_t offset = 0; offset < length;) {
        size_t field_length = data[offset++];
        if (field_length == 0U) return;
        uint8_t type = data[offset];
        const uint8_t *value = data + offset + 1U;
        size_t value_length = field_length - 1U;
        if (type == 0x09U && value_length && !record->name_complete) {
            size_t count = smaller(value_length, sizeof(record->name));
            memset(record->name, 0, sizeof(record->name));
            memcpy(record->name, value, count);
            record->name_length = (uint8_t)count;
            record->name_complete = true;
        } else if (type == 0x08U && value_length && !record->name_length) {
            size_t count = smaller(value_length, sizeof(record->name));
            memcpy(record->name, value, count);
            record->name_length = (uint8_t)count;
        } else if ((type == 0x02U || type == 0x03U || type == 0x04U || type == 0x05U ||
                    type == 0x06U || type == 0x07U) && !record->uuid_length) {
            size_t width = type <= 0x03U ? 2U : type <= 0x05U ? 4U : 16U;
            if (value_length >= width) {
                memcpy(record->uuid, value, width);
                record->uuid_length = (uint8_t)width;
            }
        } else if (!scan_response && type == 0xffU && value_length >= 2U &&
                   record->manufacturer_id == CERBERUS_BLE_MANUFACTURER_UNKNOWN) {
            record->manufacturer_id = (uint16_t)value[0] | ((uint16_t)value[1] << 8);
        }
        offset += field_length;
    }
}

static void observe_ibeacon(cerberus_s3_ble_record_t *record,
    const cerberus_s3_ble_report_t *report, uint32_t sequence, uint32_t observed_ms)
{
    cerberus_ibeacon_t beacon;
    if (cerberus_ibeacon_parse_ad(report->data, report->length, &beacon) !=
        CERBERUS_IBEACON_FOUND) return;
    cerberus_s3_ble_focus_t *focus = &record->focus;
    if (!sequence || (focus->ibeacon_valid &&
        (sequence <= focus->ibeacon_sequence ||
         (int32_t)(observed_ms - focus->ibeacon_observed_ms) < 0))) return;
    focus->ibeacon = beacon;
    focus->ibeacon_observed_ms = observed_ms;
    focus->ibeacon_sequence = sequence;
    focus->ibeacon_scan_response = report->scan_response;
    focus->ibeacon_valid = true;
    focus->category_bits |= CERBERUS_S3_BLE_FOCUS_IBEACON;
}

/* The finite sweep is much shorter than a signed millisecond half-range.
 * Sequence orders original callbacks even at equal timestamps or timer wrap;
 * source timestamps are only a guard against a backward clock, not refreshed
 * or substituted by inventory retention. Stable ties keep the lowest index. */
static cerberus_s3_ble_record_t *oldest_inventory_record(
    cerberus_s3_ble_snapshot_t *snapshot, uint32_t now_ms)
{
    if ((int32_t)(now_ms - snapshot->activity.started_ms) < 0) return NULL;
    cerberus_s3_ble_record_t *oldest = NULL;
    for (uint8_t i = 0; i < snapshot->status.stored_count; ++i) {
        cerberus_s3_ble_record_t *candidate = &snapshot->records[i];
        const cerberus_s3_ble_discovery_evidence_t *source = &candidate->focus.discovery;
        if ((int32_t)(now_ms - source->advertisement_observed_ms) < 0 ||
            (int32_t)(now_ms - candidate->last_advertisement_ms) < 0 ||
            ((source->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) &&
             (int32_t)(now_ms - source->scan_response_observed_ms) < 0)) return NULL;
        if (!oldest || source->advertisement_sequence <
            oldest->focus.discovery.advertisement_sequence) oldest = candidate;
    }
    return oldest;
}

void cerberus_s3_ble_snapshot_begin_with_epoch(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended, uint64_t capture_epoch)
{
    if (!snapshot) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->status.state = CERBERUS_BLE_SCAN_RUNNING;
    snapshot->status.generation = generation;
    snapshot->revision = 1U;
    cerberus_watch_capture_begin_with_epoch(&snapshot->watch, CERBERUS_WATCH_RADIO_BLE,
        generation, now_ms, capture_epoch);
    cerberus_ble_activity_begin(&snapshot->activity, generation, now_ms, extended);
}

bool cerberus_s3_ble_snapshot_begin_discovery(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended, uint64_t capture_epoch,
    cerberus_s3_ble_filter_t family, cerberus_s3_ble_binding_cache_t *bindings)
{
    if (!snapshot || !capture_epoch || (unsigned)family >= CERBERUS_S3_BLE_FILTER_COUNT ||
        (family != CERBERUS_S3_BLE_FILTER_ALL && !bindings)) return false;
    cerberus_s3_ble_snapshot_begin_with_epoch(snapshot, generation, now_ms, extended, capture_epoch);
    snapshot->family = family;
    snapshot->bindings = family == CERBERUS_S3_BLE_FILTER_ALL ? NULL : bindings;
    if (snapshot->bindings) memset(snapshot->bindings, 0, sizeof(*snapshot->bindings));
    return true;
}

void cerberus_s3_ble_snapshot_begin(cerberus_s3_ble_snapshot_t *snapshot,
    uint8_t generation, uint32_t now_ms, bool extended)
{
    cerberus_s3_ble_snapshot_begin_with_epoch(snapshot, generation, now_ms, extended, 0U);
}

#include "s3_ble_discovery_collect.h"

static void snapshot_observe(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now_ms,
    bool timed, uint64_t callback_us)
{
    if (!snapshot || !report || snapshot->status.state != CERBERUS_BLE_SCAN_RUNNING) return;
    /* Reserve the final revision for procedure completion. An exhausted
     * publication counter cannot wrap or prematurely release the SDK lease. */
    if (snapshot->revision >= UINT64_MAX - 1U) return;
    ++snapshot->revision;
    uint32_t sequence = snapshot->report_sequence == UINT32_MAX ?
        0U : ++snapshot->report_sequence;
    if (snapshot->family != CERBERUS_S3_BLE_FILTER_ALL) {
        discovery_observe_focused(snapshot, report, sequence, now_ms, timed, callback_us);
        return;
    }
    cerberus_s3_ble_record_t *record = NULL;
    for (uint8_t i = 0; i < snapshot->status.stored_count; ++i) {
        cerberus_s3_ble_record_t *candidate = &snapshot->records[i];
        if (candidate->address_type == report->address_type &&
            memcmp(candidate->address, report->address, 6U) == 0) {
            record = candidate;
            break;
        }
    }
    /* Invalidate before completeness/length/parser returns. An intervening
     * incomplete original cannot leave the prior ADV eligible for response
     * binding, even though the raw capture API deliberately preserves it. */
    if (record && !report->scan_response)
        begin_discovery_original(record, report, snapshot->status.generation, sequence, now_ms);
    if (report->length && !report->data) return;
    cerberus_ble_ad_hints_t parsed;
    if (report->scan_response) {
        /* A scan response can enrich only a recent advertisement from this
         * sweep. It is never raw capture, Watch evidence, or a new identity. */
        if (!record || !sequence || !report->complete || report->address_type > 3U ||
            invalid_sid(report) || !valid_response_event(report) || !report->length ||
            report->length > SENTINEL_WATCH_PROTOCOL_BLE_MAX ||
            !cerberus_ble_ad_hints_parse(report->data, report->length, &parsed)) return;
        cerberus_s3_ble_discovery_evidence_t *evidence = &record->focus.discovery;
        const uint8_t required = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
        if ((evidence->flags & required) != required || !evidence->advertisement_sequence ||
            evidence->generation != snapshot->status.generation ||
            sequence <= evidence->advertisement_sequence ||
            (uint32_t)(now_ms - evidence->advertisement_observed_ms) > 2000U ||
            ((evidence->flags & CERBERUS_S3_BLE_EVIDENCE_SID_VALID) != 0U) != report->sid_valid ||
            (report->sid_valid && evidence->sid != report->sid) ||
            ((evidence->flags & CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID) &&
                (int32_t)(now_ms - evidence->scan_response_observed_ms) < 0)) return;
        cerberus_ble_findmy_hints_t hints;
        cerberus_ble_flipper_hints_t flipper;
        cerberus_ble_flock_hints_t flock;
        if (!cerberus_ble_findmy_hints_parse(report->data, report->length, &hints) ||
            !cerberus_ble_flipper_hints_parse(report->data, report->length, &flipper) ||
            !cerberus_ble_flock_hints_parse(report->data, report->length, &flock)) return;
        parse_fields(record, report->data, report->length, true);
        evidence->findmy_scan_response = hints;
        evidence->flipper_scan_response = flipper;
        evidence->meta_scan_response = parsed.meta;
        evidence->flock_scan_response = flock;
        evidence->hc_scan_response = cerberus_ble_ad_hc_name_code(&parsed);
        record->focus.meta_suppressed |= parsed.blocked_id;
        evidence->scan_response_sequence = sequence;
        evidence->scan_response_observed_ms = now_ms;
        evidence->flags |= CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
        observe_ibeacon(record, report, sequence, now_ms);
        publish_discovery_focus(record);
        return;
    }
    cerberus_ble_activity_observe(&snapshot->activity, report->address, report->address_type,
        report->data, report->length, report->complete, now_ms);
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
    if (!report->complete || report->address_type > 3U ||
        report->length > SENTINEL_WATCH_PROTOCOL_BLE_MAX) return;

    bool fields_validated = false;
    if (!record) {
        if (snapshot->status.stored_count >= CERBERUS_BLE_SCAN_MAX_RESULTS) {
            /* Only an independently valid new original can evict retained
             * evidence. Responses never reach this path, and malformed,
             * partial, invalid-SID/event or exhausted-sequence traffic cannot
             * churn the bank. No focused-family priority is inferred. */
            if (sequence && report->event_type <= 3U && !invalid_sid(report)) {
                fields_validated = cerberus_ble_ad_hints_parse(report->data, report->length, &parsed);
                if (fields_validated) record = oldest_inventory_record(snapshot, now_ms);
            }
            if (!record) {
                if (snapshot->status.dropped_reports != UINT16_MAX) ++snapshot->status.dropped_reports;
                return;
            }
            memset(record, 0, sizeof(*record));
            if (snapshot->inventory_replacements != UINT16_MAX) ++snapshot->inventory_replacements;
        } else {
            record = &snapshot->records[snapshot->status.stored_count++];
        }
        memcpy(record->address, report->address, 6U);
        record->address_type = report->address_type;
        record->rssi = CERBERUS_BLE_RSSI_UNAVAILABLE;
        record->manufacturer_id = CERBERUS_BLE_MANUFACTURER_UNKNOWN;
        begin_discovery_original(record, report, snapshot->status.generation, sequence, now_ms);
    }
    if (cerberus_ble_rssi_valid(report->rssi) &&
        (!cerberus_ble_rssi_valid(record->rssi) || report->rssi > record->rssi)) {
        record->rssi = report->rssi;
    }
    if (report->connectable) record->flags |= CERBERUS_BLE_RESULT_CONNECTABLE;
    size_t raw_length = smaller(report->length, sizeof(record->advertisement));
    memset(record->advertisement, 0, sizeof(record->advertisement));
    if (raw_length) memcpy(record->advertisement, report->data, raw_length);
    record->advertisement_length = (uint8_t)raw_length;
    record->advertisement_event_type = report->event_type;
    record->advertisement_flags = report->length > raw_length ? CERBERUS_BLE_ADVERTISEMENT_TRUNCATED : 0U;
    record->last_advertisement_ms = now_ms;
    record->advertisement_sid = report->sid;
    record->advertisement_sid_valid = report->sid_valid;
    if (!fields_validated && !cerberus_ble_ad_hints_parse(report->data, report->length, &parsed))
        record->advertisement_flags |= CERBERUS_BLE_ADVERTISEMENT_MALFORMED;
    else {
        parse_fields(record, report->data, report->length, false);
        cerberus_ble_findmy_hints_t hints;
        cerberus_ble_flipper_hints_t flipper;
        cerberus_ble_flock_hints_t flock;
        if (sequence && report->event_type <= 3U && !invalid_sid(report) &&
            cerberus_ble_findmy_hints_parse(report->data, report->length, &hints) &&
            cerberus_ble_flipper_hints_parse(report->data, report->length, &flipper) &&
            cerberus_ble_flock_hints_parse(report->data, report->length, &flock)) {
            record->focus.discovery.findmy_advertisement = hints;
            record->focus.discovery.flipper_advertisement = flipper;
            record->focus.discovery.meta_advertisement = parsed.meta;
            record->focus.discovery.flock_advertisement = flock;
            record->focus.discovery.hc_advertisement = cerberus_ble_ad_hc_name_code(&parsed);
            record->focus.flock_oui_clue = cerberus_ble_flock_oui_hint(report->address, report->address_type);
            record->focus.meta_suppressed |= parsed.blocked_id;
            record->focus.discovery.flags |= CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
            observe_ibeacon(record, report, sequence, now_ms);
            publish_discovery_focus(record);
        }
    }
    if (record->reports_seen != UINT16_MAX) ++record->reports_seen;
}

void cerberus_s3_ble_snapshot_observe(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now_ms)
{
    snapshot_observe(snapshot, report, now_ms, false, 0U);
}

void cerberus_s3_ble_snapshot_observe_timed(cerberus_s3_ble_snapshot_t *snapshot,
    const cerberus_s3_ble_report_t *report, uint32_t now_ms, uint64_t callback_us)
{
    snapshot_observe(snapshot, report, now_ms, true, callback_us);
}

void cerberus_s3_ble_snapshot_finish(cerberus_s3_ble_snapshot_t *snapshot,
    bool cancelled, int32_t error_code, uint32_t now_ms)
{
    if (!snapshot || snapshot->status.state != CERBERUS_BLE_SCAN_RUNNING) return;
    if (snapshot->revision != UINT64_MAX) ++snapshot->revision;
    uint8_t watch_state = cancelled ? CERBERUS_WATCH_IDLE :
        error_code ? CERBERUS_WATCH_ERROR : CERBERUS_WATCH_COMPLETE;
    cerberus_watch_capture_finish(&snapshot->watch, watch_state, now_ms);
    cerberus_ble_activity_finish(&snapshot->activity, watch_state, now_ms);
    if (cancelled || error_code) {
        uint8_t generation = snapshot->status.generation;
        memset(&snapshot->status, 0, sizeof(snapshot->status));
        snapshot->status.generation = generation;
        snapshot->status.state = cancelled ? CERBERUS_BLE_SCAN_IDLE : CERBERUS_BLE_SCAN_ERROR;
        snapshot->status.error_code = cancelled ? 0 : error_code;
        return;
    }
    for (uint8_t i = 0; i < snapshot->status.stored_count; ++i)
        if (!snapshot->records[i].name_length) ++snapshot->status.unnamed_count;
    snapshot->status.state = CERBERUS_BLE_SCAN_COMPLETE;
}
