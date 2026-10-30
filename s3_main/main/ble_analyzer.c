#include "ble_analyzer.h"

#include "ibeacon.h"
#include "ble_ad_hints.h"
#include "ble_flock_oui.h"

#include <string.h>

_Static_assert(CERBERUS_BLE_ANALYZER_NAME_MAX == CERBERUS_BLE_AD_HINT_NAME_MAX,
    "Analyzer and shared hint parser name bounds must match");

static uint32_t plus_one(uint32_t value)
{
    return value == UINT32_MAX ? value : value + 1U;
}

static void use_evidence(cerberus_ble_analyzer_t *analyzer,
    cerberus_ble_analyzer_evidence_kind_t kind,
    sentinel_watch_confidence_t confidence, const char *label,
    const char *reason)
{
    if (confidence <= analyzer->evidence_confidence) return;
    analyzer->evidence_kind = kind;
    analyzer->evidence_confidence = confidence;
    analyzer->evidence_label = label;
    analyzer->evidence_reason = reason;
}

void cerberus_ble_analyzer_begin(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms)
{
    if (!analyzer) return;
    memset(analyzer, 0, sizeof(*analyzer));
    analyzer->bucket_start_ms = now_ms;
    analyzer->active = true;
}

void cerberus_ble_analyzer_stop(cerberus_ble_analyzer_t *analyzer)
{
    if (analyzer) analyzer->active = false;
}

void cerberus_ble_analyzer_advance(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms)
{
    if (!analyzer || !analyzer->active) return;
    uint32_t elapsed = now_ms - analyzer->bucket_start_ms;
    if (elapsed > INT32_MAX) return; /* Reject an out-of-order small rewind. */
    uint32_t steps = elapsed / CERBERUS_BLE_ANALYZER_BUCKET_MS;
    if (steps == 0U) return;
    if (steps > CERBERUS_BLE_ANALYZER_HISTORY) {
        memset(analyzer->history, 0, sizeof(analyzer->history));
        analyzer->next_history = (uint8_t)((analyzer->next_history +
            (steps % CERBERUS_BLE_ANALYZER_HISTORY)) %
            CERBERUS_BLE_ANALYZER_HISTORY);
        analyzer->history_count = CERBERUS_BLE_ANALYZER_HISTORY;
        analyzer->current_events = 0U;
    } else {
        for (uint32_t i = 0U; i < steps; ++i) {
            analyzer->history[analyzer->next_history] = analyzer->current_events;
            analyzer->next_history = (uint8_t)((analyzer->next_history + 1U) %
                CERBERUS_BLE_ANALYZER_HISTORY);
            if (analyzer->history_count < CERBERUS_BLE_ANALYZER_HISTORY)
                ++analyzer->history_count;
            analyzer->current_events = 0U;
        }
    }
    analyzer->bucket_start_ms += steps * CERBERUS_BLE_ANALYZER_BUCKET_MS;
}

bool cerberus_ble_analyzer_window(const cerberus_ble_analyzer_view_t *view,
    cerberus_ble_analyzer_window_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!view || view->history_count > CERBERUS_BLE_ANALYZER_HISTORY) return false;
    out->buckets = view->history_count;
    for (unsigned i = 0U; i < view->history_count; ++i) {
        uint16_t events = view->history[i];
        out->events += events; /* At most 64 * UINT16_MAX, fits uint32_t. */
        if (events > out->peak) out->peak = events;
        if (events == UINT16_MAX) out->capped = true;
    }
    return true;
}

bool cerberus_ble_analyzer_observe(cerberus_ble_analyzer_t *analyzer,
    const cerberus_ble_analyzer_report_t *report, uint32_t now_ms)
{
    if (!analyzer || !analyzer->active || !report ||
        (report->advertisement_length && !report->advertisement) ||
        report->address_type > 3U) return false;
    cerberus_ble_analyzer_advance(analyzer, now_ms);
    if (analyzer->current_events != UINT16_MAX) ++analyzer->current_events;
    analyzer->total_events = plus_one(analyzer->total_events);
    analyzer->has_last = true;
    analyzer->last_seen_ms = now_ms;
    memcpy(analyzer->last_address, report->address, sizeof(report->address));
    analyzer->last_address_type = report->address_type;
    analyzer->last_rssi_dbm = report->rssi_dbm;
    analyzer->last_name_length = 0U;
    memset(analyzer->last_name, 0, sizeof(analyzer->last_name));
    memset(&analyzer->last_meta, 0, sizeof(analyzer->last_meta));
    memset(&analyzer->last_flock, 0, sizeof(analyzer->last_flock));
    analyzer->last_flock_oui_clue = false;
    analyzer->last_hc_name = CERBERUS_BLE_HC_NONE;
    memset(&analyzer->last_ibeacon, 0, sizeof(analyzer->last_ibeacon));
    analyzer->last_ibeacon_valid = false;
    analyzer->last_scan_response = report->scan_response;
    analyzer->last_report_valid = false;
    analyzer->evidence_kind = CERBERUS_BLE_ANALYZER_EVIDENCE_NONE;
    analyzer->evidence_confidence = SENTINEL_WATCH_CONFIDENCE_NONE;
    analyzer->evidence_label = NULL;
    analyzer->evidence_reason = NULL;
    if (!report->complete || report->advertisement_flags ||
        report->advertisement_length > CERBERUS_BLE_AD_HINT_MAX_LENGTH) {
        analyzer->unclassified_reports = plus_one(analyzer->unclassified_reports);
        return true;
    }
    cerberus_ble_ad_hints_t parsed;
    if (!cerberus_ble_ad_hints_parse(report->advertisement, report->advertisement_length,
            &parsed)) {
        analyzer->malformed_reports = plus_one(analyzer->malformed_reports);
        return true;
    }
    analyzer->last_name_length = parsed.name_length;
    memcpy(analyzer->last_name, parsed.name, parsed.name_length);
    analyzer->last_meta = parsed.meta;
    analyzer->last_report_valid = true;
    /* Watch keeps its existing whole-report 31-byte contract. The independent
     * iBeacon/Meta/HC parsers inspect the complete validated extended source. */
    if (report->advertisement_length <= SENTINEL_WATCH_MAX_AD_LENGTH) {
        sentinel_watch_ble_observation_t observation = {
            .name = parsed.name,
            .name_length = parsed.name_length,
            .advertisement = report->advertisement,
            .advertisement_length = report->advertisement_length,
            .address_type = report->address_type,
        };
        memcpy(observation.address, report->address, sizeof(observation.address));
        sentinel_watch_match_t match;
        if (sentinel_watch_classify_ble(&observation, &match)) {
            analyzer->watch_match_events = plus_one(analyzer->watch_match_events);
            use_evidence(analyzer, CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH,
                match.confidence, match.label, match.evidence);
        }
    }
    cerberus_ibeacon_t beacon;
    if (cerberus_ibeacon_parse_ad(report->advertisement,
            report->advertisement_length, &beacon) == CERBERUS_IBEACON_FOUND) {
        analyzer->last_ibeacon = beacon;
        analyzer->last_ibeacon_valid = true;
        analyzer->ibeacon_format_events = plus_one(analyzer->ibeacon_format_events);
        use_evidence(analyzer, CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT,
            SENTINEL_WATCH_CONFIDENCE_MEDIUM, "iBeacon format",
            "One complete report has a valid iBeacon field");
    }
    if (cerberus_ble_ad_meta_hint(&parsed)) {
        analyzer->meta_hint_events = plus_one(analyzer->meta_hint_events);
        use_evidence(analyzer, CERBERUS_BLE_ANALYZER_EVIDENCE_META_ID_HINT,
            SENTINEL_WATCH_CONFIDENCE_LOW, "Meta identifier hint",
            "A candidate identifier appears in a complete report; identity unknown");
    }
    analyzer->last_hc_name = cerberus_ble_ad_hc_name_code(&parsed);
    if (analyzer->last_hc_name != CERBERUS_BLE_HC_NONE) {
        analyzer->hc_name_hint_events = plus_one(analyzer->hc_name_hint_events);
        use_evidence(analyzer, CERBERUS_BLE_ANALYZER_EVIDENCE_HC_NAME_HINT,
            SENTINEL_WATCH_CONFIDENCE_LOW, "HC name hint",
            "Advertised name is HC-03, HC-05 or HC-06; purpose unknown");
    }
    if (cerberus_ble_flock_hints_parse(report->advertisement,
            report->advertisement_length, &analyzer->last_flock)) {
        analyzer->last_flock_oui_clue = cerberus_ble_flock_oui_hint(report->address, report->address_type);
        cerberus_ble_flock_level_t level = cerberus_ble_flock_hints_level(&analyzer->last_flock);
        if (level != CERBERUS_BLE_FLOCK_LEVEL_NONE || analyzer->last_flock_oui_clue) {
            analyzer->flock_hint_events = plus_one(analyzer->flock_hint_events);
            /* Preserve existing Watch/iBeacon presentation priority, not a
             * cross-family maximum-confidence claim. Low clues also preserve
             * earlier Meta/HC ties; specific Flock evidence may outrank those. */
            if (analyzer->evidence_kind != CERBERUS_BLE_ANALYZER_EVIDENCE_WATCH &&
                analyzer->evidence_kind != CERBERUS_BLE_ANALYZER_EVIDENCE_IBEACON_FORMAT) {
                sentinel_watch_confidence_t confidence = level == CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED ?
                    SENTINEL_WATCH_CONFIDENCE_HIGH : level == CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE ?
                    SENTINEL_WATCH_CONFIDENCE_MEDIUM : SENTINEL_WATCH_CONFIDENCE_LOW;
                const char *label = level == CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED ? "Flock corroborated hint" :
                    level == CERBERUS_BLE_FLOCK_LEVEL_SIGNATURE ? "Flock format signature" :
                    level == CERBERUS_BLE_FLOCK_LEVEL_CLUE ? "Flock broadcast clue" : "Flock address clue";
                const char *reason = level == CERBERUS_BLE_FLOCK_LEVEL_NONE ?
                    "Public address prefix only; device identity unknown" :
                    "Fields in one complete report; device identity unknown";
                use_evidence(analyzer, CERBERUS_BLE_ANALYZER_EVIDENCE_FLOCK_HINT, confidence, label, reason);
            }
        }
    }
    return true;
}

void cerberus_ble_analyzer_snapshot(cerberus_ble_analyzer_t *analyzer,
    uint32_t now_ms, cerberus_ble_analyzer_view_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!analyzer) return;
    cerberus_ble_analyzer_advance(analyzer, now_ms);
    out->active = analyzer->active;
    out->history_count = analyzer->history_count;
    out->current_events = analyzer->current_events;
    out->total_events = analyzer->total_events;
    out->unclassified_reports = analyzer->unclassified_reports;
    out->malformed_reports = analyzer->malformed_reports;
    out->watch_match_events = analyzer->watch_match_events;
    out->ibeacon_format_events = analyzer->ibeacon_format_events;
    out->meta_hint_events = analyzer->meta_hint_events;
    out->hc_name_hint_events = analyzer->hc_name_hint_events;
    out->flock_hint_events = analyzer->flock_hint_events;
    for (unsigned age = 0U; age < analyzer->history_count; ++age) {
        unsigned index = (analyzer->next_history +
            CERBERUS_BLE_ANALYZER_HISTORY - 1U - age) %
            CERBERUS_BLE_ANALYZER_HISTORY;
        out->history[age] = analyzer->history[index];
    }
    out->has_last = analyzer->has_last;
    if (!analyzer->has_last) return;
    out->last_seen_age_ms = now_ms - analyzer->last_seen_ms;
    out->last_fresh = out->last_seen_age_ms <= CERBERUS_BLE_ANALYZER_FRESH_MS;
    memcpy(out->last_address, analyzer->last_address, sizeof(out->last_address));
    out->last_address_type = analyzer->last_address_type;
    out->last_rssi_dbm = analyzer->last_rssi_dbm;
    memcpy(out->last_name, analyzer->last_name, sizeof(out->last_name));
    out->last_name_length = analyzer->last_name_length;
    out->last_meta = analyzer->last_meta;
    out->last_flock = analyzer->last_flock;
    out->last_flock_oui_clue = analyzer->last_flock_oui_clue;
    out->last_hc_name = analyzer->last_hc_name;
    out->last_scan_response = analyzer->last_scan_response;
    out->last_report_valid = analyzer->last_report_valid;
    out->last_ibeacon = analyzer->last_ibeacon;
    out->last_ibeacon_valid = analyzer->last_ibeacon_valid;
    out->evidence_kind = analyzer->evidence_kind;
    out->evidence_confidence = analyzer->evidence_confidence;
    out->evidence_label = analyzer->evidence_label;
    out->evidence_reason = analyzer->evidence_reason;
}
