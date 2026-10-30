#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>

#include "s3_findmy_ui_route.h"
#include "ble_findmy_hints.h"
#include "ble_flipper_hints.h"
#include "ble_ad_hints.h"
#include "ibeacon.h"
#include "ble_flock_hints.h"
#include "ble_flock_oui.h"
#include "s3_ble_recent.h"

/* Discovery supports more formats than the unchanged raw Sound gate. Keep
 * those two decisions separate even when passive hints are displayed together. */
static void passive_hints_do_not_widen_raw_gate(void)
{
    const uint8_t address[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    const uint8_t fmna32[] = {5U, 5U, 0x44U, 0xFDU, 0U, 0U};
    const uint8_t fmna128[] = {17U, 7U,
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0U,0U,0x80U,
        0U,0x10U,0U,0U,0x44U,0xFDU,0U,0U};
    const uint8_t mixed[] = {3U, 3U, 0x44U, 0xFDU, 17U, 7U,
        0x85U,0x2AU,0x9FU,0x57U,0xC5U,0x2AU,0xEDU,0x88U,
        0x26U,0xC2U,0xF4U,0x12U,1U,0U,0x19U,0x15U};
    const uint8_t *reports[] = {fmna32, fmna128, mixed};
    const size_t lengths[] = {sizeof(fmna32), sizeof(fmna128), sizeof(mixed)};
    const uint8_t bits[] = {CERBERUS_BLE_FINDMY_HINT_FMNA,
        CERBERUS_BLE_FINDMY_HINT_FMNA,
        CERBERUS_BLE_FINDMY_HINT_FMNA | CERBERUS_BLE_FINDMY_HINT_DULT};
    for (size_t i = 0U; i < sizeof(reports) / sizeof(reports[0]); ++i) {
        cerberus_ble_findmy_hints_t hints = {0};
        assert(cerberus_ble_findmy_hints_parse(reports[i], lengths[i], &hints));
        assert(hints.bits == bits[i]);
        assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
            CERBERUS_FINDMY_RAW_ADV_IND, 0U, reports[i], lengths[i]) ==
            CERBERUS_FINDMY_PROFILE_NONE);
    }
    const uint8_t ordinary_adv[] = {2U, 1U, 6U};
    const uint8_t response[] = {3U, 3U, 0x44U, 0xFDU};
    cerberus_ble_findmy_hints_t hints = {0};
    assert(cerberus_ble_findmy_hints_parse(response, sizeof(response), &hints));
    assert(hints.bits == CERBERUS_BLE_FINDMY_HINT_FMNA);
    /* A response can enrich discovery; it cannot replace the original packet
     * supplied to the action gate or establish connectability. */
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, ordinary_adv, sizeof(ordinary_adv)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        4U, 0U, response, sizeof(response)) == CERBERUS_FINDMY_PROFILE_NONE);
    /* A different discovery family sharing the same provenance storage must
     * never supply a Find My action profile. */
    const uint8_t flipper[] = {2U, 1U, 6U, 3U, 3U, 0x81U, 0x30U, 2U, 0x0AU, 0xC5U};
    cerberus_ble_flipper_hints_t flipper_hints = {0};
    assert(cerberus_ble_flipper_hints_parse(flipper, sizeof(flipper), &flipper_hints));
    assert(flipper_hints.bits != 0U);
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, flipper, sizeof(flipper)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    const uint8_t meta[] = {17U, 7U,
        0xFBU,0x34U,0x9BU,0x5FU,0x80U,0U,0U,0x80U,
        0U,0x10U,0U,0U,0x5FU,0xFDU,0U,0U};
    cerberus_ble_ad_hints_t meta_hints = {0};
    assert(cerberus_ble_ad_hints_parse(meta, sizeof(meta), &meta_hints));
    assert(cerberus_ble_ad_meta_hint(&meta_hints));
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, meta, sizeof(meta)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    const uint8_t ibeacon[40] = {26U,0xFFU,0x4CU,0U,2U,0x15U,
        0U,1U,2U,3U,4U,5U,6U,7U,8U,9U,10U,11U,12U,13U,14U,15U,
        1U,2U,3U,4U,0xC5U};
    cerberus_ibeacon_t beacon = {0};
    assert(cerberus_ibeacon_parse_ad(ibeacon, sizeof(ibeacon), &beacon) == CERBERUS_IBEACON_FOUND);
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, ibeacon, sizeof(ibeacon)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    /* Also exercise subtype classification within the raw gate's 31-byte cap. */
    assert(cerberus_ibeacon_parse_ad(ibeacon, 27U, &beacon) == CERBERUS_IBEACON_FOUND);
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, ibeacon, 27U) == CERBERUS_FINDMY_PROFILE_NONE);
    const uint8_t flock[] = {15U,9U,'F','S',' ','E','x','t',' ','B','a','t','t','e','r','y',
        4U,0xFFU,0xC8U,9U,1U};
    cerberus_ble_flock_hints_t flock_hints = {0};
    assert(cerberus_ble_flock_hints_parse(flock, sizeof(flock), &flock_hints));
    assert(cerberus_ble_flock_hints_level(&flock_hints) == CERBERUS_BLE_FLOCK_LEVEL_CORROBORATED);
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, flock, sizeof(flock)) == CERBERUS_FINDMY_PROFILE_NONE);
    const uint8_t vendor_address[6] = {6U,5U,4U,0x52U,0x1EU,0xB4U};
    assert(cerberus_ble_flock_oui_hint(vendor_address, 0U));
    assert(cerberus_findmy_ui_raw_profile(vendor_address, 0U, vendor_address, 0U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, ordinary_adv, sizeof(ordinary_adv)) == CERBERUS_FINDMY_PROFILE_NONE);
    uint8_t hc[] = {6U,9U,'H','C','-','0','3'};
    const uint8_t hc_codes[] = {CERBERUS_BLE_HC_03, CERBERUS_BLE_HC_05, CERBERUS_BLE_HC_06};
    for (size_t i = 0U; i < sizeof(hc_codes); ++i) {
        hc[6] = (uint8_t)('0' + hc_codes[i]);
        cerberus_ble_ad_hints_t parsed = {0};
        assert(cerberus_ble_ad_hints_parse(hc, sizeof(hc), &parsed));
        assert(cerberus_ble_ad_hc_name_code(&parsed) == hc_codes[i]);
        assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
            CERBERUS_FINDMY_RAW_ADV_IND, 0U, hc, sizeof(hc)) == CERBERUS_FINDMY_PROFILE_NONE);
    }
}

static void recent_visibility_does_not_supply_an_action_profile(void)
{
    const uint8_t address[6] = {1U,2U,3U,4U,5U,6U};
    const uint8_t original[] = {2U,1U,6U};
    cerberus_s3_ble_focus_t focus = {0};
    focus.discovery.flags = CERBERUS_S3_BLE_EVIDENCE_ADV_COMPLETE |
        CERBERUS_S3_BLE_EVIDENCE_ADV_VALID | CERBERUS_S3_BLE_EVIDENCE_RESPONSE_VALID;
    focus.discovery.advertisement_sequence = 1U;
    focus.discovery.advertisement_observed_ms = 1000U;
    focus.discovery.scan_response_sequence = 2U;
    focus.discovery.scan_response_observed_ms = 1010U;
    focus.discovery.findmy_scan_response.bits = CERBERUS_BLE_FINDMY_HINT_FMNA;
    cerberus_s3_ble_focus_t saved = focus;
    /* The response's qualifying hint is still within its own window after the
     * original time expires. Visibility cannot replace that ordinary raw ADV. */
    assert(cerberus_s3_ble_focus_recent(&focus, CERBERUS_S3_BLE_FILTER_FIND_MY, 31005U));
    assert(cerberus_findmy_ui_raw_profile(address, 1U, address, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, original, sizeof(original)) == CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_s3_ble_focus_recent(&focus, CERBERUS_S3_BLE_FILTER_FIND_MY, 31010U));
    assert(!cerberus_s3_ble_focus_recent(&focus, CERBERUS_S3_BLE_FILTER_FIND_MY, 31011U));
    assert(memcmp(&focus, &saved, sizeof(focus)) == 0);
}

int main(void)
{
    passive_hints_do_not_widen_raw_gate();
    recent_visibility_does_not_supply_an_action_profile();
    const uint8_t selected[6] = {1U, 2U, 3U, 4U, 5U, 6U};
    const uint8_t other[6] = {1U, 2U, 3U, 4U, 5U, 7U};
    const uint8_t fmna[] = {3U, 3U, 0x44U, 0xFDU};
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, fmna, sizeof(fmna)) ==
        CERBERUS_FINDMY_PROFILE_FMNA);
    /* Inventory may retain a prior connectable flag at this address. The
     * latest raw packet is nonconnectable and must block both UI routes. */
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        3U, 0U, fmna, sizeof(fmna)) == CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        2U, 0U, fmna, sizeof(fmna)) == CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        4U, 0U, fmna, sizeof(fmna)) == CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, other, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, fmna, sizeof(fmna)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 0U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, fmna, sizeof(fmna)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 1U, fmna, sizeof(fmna)) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_raw_profile(selected, 1U, selected, 1U,
        CERBERUS_FINDMY_RAW_ADV_IND, 0U, fmna, sizeof(fmna) - 1U) ==
        CERBERUS_FINDMY_PROFILE_NONE);
    assert(cerberus_findmy_ui_fresh(UINT32_MAX - 100U, 100U));
    assert(cerberus_findmy_ui_fresh(1000U, 31000U));
    assert(!cerberus_findmy_ui_fresh(1000U, 31001U));
    puts("Find My UI raw-ADV route: exact address/type, current connectability, complete AD: pass");
}
