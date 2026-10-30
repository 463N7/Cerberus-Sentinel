#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "s3_ble_snapshot.h"

#define ESP_OK 0
static char output[16384];
static size_t used;
static unsigned reads;
static int failed_row = -1;
static bool status_error;
static cerberus_s3_ble_focus_t rows[CERBERUS_BLE_SCAN_MAX_RESULTS];
static cerberus_ble_scan_status_payload_t current;

static void console_write(const char *text)
{
    size_t count = strlen(text);
    assert(count < sizeof(output) - used);
    memcpy(output + used, text, count + 1U);
    used += count;
}
static void console_printf(const char *format, ...)
{
    char text[1024];
    va_list args;
    va_start(args, format);
    int count = vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    assert(count >= 0 && (size_t)count < sizeof(text));
    console_write(text);
}
static int cerberus_s3_ble_scanner_get_focus(uint8_t generation, uint8_t index,
    cerberus_s3_ble_focus_t *out)
{
    ++reads;
    if (index == failed_row || generation != current.generation) return 1;
    assert(index < CERBERUS_BLE_SCAN_MAX_RESULTS);
    *out = rows[index];
    return ESP_OK;
}
static int cerberus_s3_ble_scanner_get_status(cerberus_ble_scan_status_payload_t *out)
{
    *out = current;
    return status_error ? 1 : ESP_OK;
}
#include "discovery_console_impl.h"

static void clear_output(void) { used = 0U; output[0] = '\0'; reads = 0U; }
static void has(const char *text) { assert(strstr(output, text) != NULL); }
static void lacks(const char *text) { assert(strstr(output, text) == NULL); }

int main(void)
{
    cerberus_ble_scan_status_payload_t scan = {0};
    print_ble_discovery_hints(&scan);
    has("meta_evidence=not_complete\r\n");
    has("ibeacon_evidence=not_complete\r\n");
    has("flock_evidence=not_complete\r\n");
    has("hc_evidence=not_complete\r\n");
    assert(reads == 0U);
    scan.state = CERBERUS_BLE_SCAN_COMPLETE;
    scan.generation = 9U;
    scan.stored_count = 3U;
    current = scan;
    rows[0].discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE | CERBERUS_S3_BLE_EVIDENCE_ADV_VALID |
        CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID | CERBERUS_S3_BLE_EVIDENCE_RAW_COMPLETE;
    rows[0].category_bits = CERBERUS_S3_BLE_FOCUS_META_HINT;
    cerberus_ble_meta_evidence_add(&rows[0].discovery.meta_advertisement, 0xFD5FU, CERBERUS_BLE_META_LIST128);
    cerberus_ble_meta_evidence_add(&rows[0].discovery.meta_scan_response, 0xFEB7U, CERBERUS_BLE_META_DATA32);
    rows[1].discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_ADV_VALID | CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    rows[1].meta_suppressed = true;
    cerberus_ble_meta_evidence_add(&rows[1].discovery.meta_advertisement, 0x004CU, CERBERUS_BLE_META_MFG16);
    /* Invalid source bytes must not leak through even if a stale matrix exists. */
    cerberus_ble_meta_evidence_add(&rows[2].discovery.meta_advertisement, 0x058EU, CERBERUS_BLE_META_DATA16);
    rows[0].ibeacon_valid = true;
    rows[0].ibeacon_sequence = rows[0].discovery.advertisement_sequence = 7U;
    rows[0].ibeacon_observed_ms = rows[0].discovery.advertisement_observed_ms = 100U;
    rows[1].ibeacon_valid = true;
    rows[1].ibeacon_scan_response = true;
    rows[1].discovery.advertisement_sequence = 8U;
    rows[1].discovery.advertisement_observed_ms = 110U;
    rows[1].ibeacon_sequence = rows[1].discovery.scan_response_sequence = 9U;
    rows[1].ibeacon_observed_ms = rows[1].discovery.scan_response_observed_ms = 120U;
    rows[2].ibeacon_valid = true; /* Earlier match survives an invalid current source. */
    rows[2].ibeacon_sequence = 4U;
    rows[2].ibeacon_observed_ms = 50U;
    rows[0].discovery.flock_advertisement.facts = CERBERUS_BLE_FLOCK_FS_BATTERY;
    rows[0].discovery.flock_scan_response.facts = CERBERUS_BLE_FLOCK_MFG_09C8 |
        CERBERUS_BLE_FLOCK_MFG_PAYLOAD | CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
    rows[1].discovery.flock_advertisement.facts = CERBERUS_BLE_FLOCK_MFG_09C8 |
        CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
    rows[1].discovery.flock_scan_response.facts = CERBERUS_BLE_FLOCK_FS_BATTERY;
    rows[2].flock_oui_clue = true;
    rows[2].discovery.flock_advertisement.facts = CERBERUS_BLE_FLOCK_FS_BATTERY |
        CERBERUS_BLE_FLOCK_MFG_09C8 | CERBERUS_BLE_FLOCK_MFG_PAYLOAD; /* Invalid source. */
    rows[0].discovery.hc_advertisement = CERBERUS_BLE_HC_03;
    rows[0].discovery.hc_scan_response = CERBERUS_BLE_HC_05;
    rows[1].discovery.hc_scan_response = CERBERUS_BLE_HC_06;
    rows[2].discovery.hc_advertisement = CERBERUS_BLE_HC_06; /* Invalid source. */
    clear_output();
    print_ble_discovery_hints(&scan);
    has("meta_evidence=completed_sweep\r\n");
    has("meta_adv_evidence_rows=2\r\nmeta_response_evidence_rows=1\r\nmeta_filter_rows=1\r\n");
    has("meta_sweep_suppressed_rows=1\r\nmeta_raw_truncated_evidence_rows=1\r\n");
    has("meta_adv_unique_pairs=2\r\nmeta_adv_pair=FD5F/LIST128\r\nmeta_adv_pair=004C/MFG16\r\n");
    has("meta_response_unique_pairs=1\r\nmeta_response_pair=FEB7/DATA32\r\n");
    lacks("058E"); lacks("FD5F/DATA32"); lacks("FEB7/LIST128");
    has("ibeacon_evidence=completed_sweep\r\nibeacon_format_rows=3\r\n");
    has("ibeacon_current_adv_rows=1\r\nibeacon_current_response_rows=1\r\nibeacon_earlier_match_rows=1\r\n");
    has("flock_evidence=completed_sweep\r\nflock_specific_signature_rows=2\r\n");
    has("flock_weak_only_rows=1\r\nflock_public_oui_clue_rows=1\r\nflock_oui_only_rows=1\r\n");
    has("flock_adv_clue_rows=1\r\nflock_adv_signature_rows=1\r\nflock_adv_corroborated_rows=0\r\n");
    has("flock_response_clue_rows=1\r\nflock_response_signature_rows=1\r\nflock_response_corroborated_rows=0\r\n");
    has("hc_evidence=completed_sweep\r\nhc_name_hint_rows=2\r\n");
    has("hc_different_adv_response_name_rows=1\r\nhc_raw_truncated_hint_rows=1\r\n");
    has("hc_adv_03_rows=1\r\nhc_adv_05_rows=0\r\nhc_adv_06_rows=0\r\n");
    has("hc_response_03_rows=0\r\nhc_response_05_rows=1\r\nhc_response_06_rows=1\r\n");
    /* Only actual same-source corroboration raises its level. */
    rows[1].discovery.flock_scan_response.facts |= CERBERUS_BLE_FLOCK_MFG_09C8 |
        CERBERUS_BLE_FLOCK_MFG_PAYLOAD;
    clear_output(); print_ble_discovery_hints(&scan);
    has("flock_response_signature_rows=0\r\nflock_response_corroborated_rows=1\r\n");
    has("flock_adv_corroborated_rows=0\r\n");
    /* A failed row or incoherent generation/count/state emits no partial pairs. */
    failed_row = 1;
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_evidence=changed_during_read"); lacks("meta_adv_pair=");
    has("ibeacon_evidence=changed_during_read"); lacks("ibeacon_format_rows=");
    has("flock_evidence=changed_during_read"); lacks("flock_specific_signature_rows=");
    has("hc_evidence=changed_during_read"); lacks("hc_name_hint_rows=");
    failed_row = -1;
    current.stored_count = 2U;
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_evidence=changed_during_read"); lacks("meta_adv_pair=");
    current = scan; current.generation++;
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_evidence=changed_during_read"); lacks("meta_adv_pair=");
    current = scan; current.state = 0U;
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_evidence=changed_during_read"); lacks("meta_adv_pair=");
    current = scan; status_error = true;
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_evidence=changed_during_read"); lacks("meta_adv_pair=");
    status_error = false;
    /* All 77 pairs remain bounded, with duplicates counted once. */
    memset(rows, 0, sizeof(rows));
    for (unsigned row = 0U; row < 3U; ++row) {
        rows[row].discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_VALID;
        rows[row].discovery.flock_advertisement.facts = CERBERUS_BLE_FLOCK_NO_LOCAL_NAME;
        for (unsigned id = 0U; id < CERBERUS_BLE_META_ID_COUNT; ++id)
            for (unsigned field = 0U; field < CERBERUS_BLE_META_CONTEXT_COUNT; ++field)
                assert(cerberus_ble_meta_evidence_add(&rows[row].discovery.meta_advertisement,
                    cerberus_ble_meta_id_value(id), (cerberus_ble_meta_context_t)field));
    }
    clear_output(); print_ble_discovery_hints(&scan);
    has("meta_adv_unique_pairs=77\r\n"); has("meta_adv_pair=FEF3/DATA128\r\n");
    has("meta_response_unique_pairs=0\r\n");
    has("ibeacon_format_rows=0\r\n");
    has("flock_specific_signature_rows=0\r\nflock_weak_only_rows=0\r\n");
    has("hc_name_hint_rows=0\r\n");
    /* Unsupported codes never become unnamed generic HC hints. */
    rows[0].discovery.hc_advertisement = 4U;
    rows[1].discovery.hc_scan_response = CERBERUS_BLE_HC_05; /* Invalid response. */
    clear_output(); print_ble_discovery_hints(&scan);
    has("hc_name_hint_rows=0\r\n"); has("hc_response_05_rows=0\r\n");
    puts("BLE diagnostic sweep coherence, exact pairs, invalid-source exclusion and bounds: pass");
}
