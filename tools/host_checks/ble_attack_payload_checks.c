#include "ble_attack_payload.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bool same_frame(const cerberus_ble_attack_frame_t *a,
    const cerberus_ble_attack_frame_t *b)
{
    return a->kind == b->kind && a->length == b->length &&
        memcmp(a->data, b->data, a->length) == 0 &&
        memcmp(a->address, b->address, 6U) == 0;
}

static void check_ad_parser(void)
{
    const uint8_t good[] = {2,1,6,3,3,0x2C,0xFE,0,0};
    const uint8_t bad_size[] = {4,1,6};
    const uint8_t bad_after_zero[] = {2,1,6,0,1};
    assert(cerberus_ble_attack_ad_valid(good, sizeof(good)));
    assert(!cerberus_ble_attack_ad_valid(bad_size, sizeof(bad_size)));
    assert(!cerberus_ble_attack_ad_valid(bad_after_zero, sizeof(bad_after_zero)));
    assert(!cerberus_ble_attack_ad_valid(NULL, 1));
    assert(!cerberus_ble_attack_ad_valid(good, 0));
    assert(!cerberus_ble_attack_ad_valid(good, 32));
}

static void check_one_mode(cerberus_ble_attack_mode_t mode)
{
    cerberus_ble_attack_plan_t plan;
    assert(cerberus_ble_attack_plan_init(&plan, mode, 0x12345678U, NULL) ==
        CERBERUS_BLE_ATTACK_OK);
    for (uint32_t i = 0; i < 300U; ++i) {
        cerberus_ble_attack_frame_t frame;
        assert(cerberus_ble_attack_next(&plan, i * 1500U, &frame) ==
            CERBERUS_BLE_ATTACK_OK);
        assert(frame.length > 0U && frame.length <= CERBERUS_BLE_ATTACK_AD_MAX);
        assert(cerberus_ble_attack_ad_valid(frame.data, frame.length));
        assert(frame.address_type == 1U && (frame.address[5] & 0xC0U) == 0xC0U);
        assert(frame.active_ms == (mode == CERBERUS_BLE_ATTACK_SOUR_APPLE ? 60U :
            mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ? 500U : 10U));
        assert(frame.interval_units == (mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ? 0x20U : 0U));
        if (mode == CERBERUS_BLE_ATTACK_SWIFT_PAIR) {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_SWIFT_PAIR);
            assert(frame.length >= 8U && frame.length <= 17U);
            assert(frame.data[1] == 0xFFU && frame.data[2] == 0x06U && frame.data[3] == 0U);
            assert(frame.data[4] == 3U && frame.data[5] == 0U && frame.data[6] == 0x80U);
        } else if (mode == CERBERUS_BLE_ATTACK_SAMSUNG) {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_SAMSUNG && frame.length == 15U);
            assert(frame.data[0] == 14U && frame.data[1] == 0xFFU);
            assert(frame.data[2] == 0x75U && frame.data[3] == 0U);
        } else if (mode == CERBERUS_BLE_ATTACK_GOOGLE) {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_GOOGLE && frame.length == 14U);
            assert(frame.data[0] == 3U && frame.data[1] == 3U);
            assert(frame.data[2] == 0x2CU && frame.data[3] == 0xFEU);
            assert(frame.data[4] == 6U && frame.data[5] == 0x16U);
        } else if (mode == CERBERUS_BLE_ATTACK_FLIPPER) {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_FLIPPER && frame.length == 27U);
            assert(frame.data[0] == 2U && frame.data[1] == 1U && frame.data[2] == 6U);
            assert(frame.data[17] == 9U && frame.data[18] == 0xFFU);
            assert(frame.data[19] == 0xBAU && frame.data[20] == 0x0FU);
        } else {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_APPLE_ACTION ||
                frame.kind == CERBERUS_BLE_PAYLOAD_APPLE_DEVICE);
            assert(frame.length == (frame.kind == CERBERUS_BLE_PAYLOAD_APPLE_ACTION ? 11U : 21U));
            assert(frame.data[1] == 0xFFU && frame.data[2] == 0x4CU && frame.data[3] == 0U);
        }
    }
    assert(plan.sequence == 300U);
}

static void check_apple_cache(void)
{
    cerberus_ble_attack_plan_t plan;
    cerberus_ble_attack_frame_t first, same, changed;
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SOUR_APPLE,
        0xA51A7E31U, NULL) == CERBERUS_BLE_ATTACK_OK);
    assert(cerberus_ble_attack_next(&plan, 100U, &first) == CERBERUS_BLE_ATTACK_OK);
    assert(cerberus_ble_attack_next(&plan, 1099U, &same) == CERBERUS_BLE_ATTACK_OK);
    assert(same_frame(&first, &same));
    assert(cerberus_ble_attack_next(&plan, 1100U, &changed) == CERBERUS_BLE_ATTACK_OK);
    assert(!same_frame(&first, &changed));
    assert(first.active_ms == 60U);

    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SOUR_APPLE,
        0xA51A7E31U, NULL) == CERBERUS_BLE_ATTACK_OK);
    assert(cerberus_ble_attack_next(&plan, UINT32_MAX - 500U, &first) == CERBERUS_BLE_ATTACK_OK);
    assert(cerberus_ble_attack_next(&plan, 498U, &same) == CERBERUS_BLE_ATTACK_OK);
    assert(same_frame(&first, &same));
    assert(cerberus_ble_attack_next(&plan, 499U, &changed) == CERBERUS_BLE_ATTACK_OK);
    assert(!same_frame(&first, &changed));
}

static void check_combined_order(void)
{
    const cerberus_ble_payload_kind_t order[6] = {
        CERBERUS_BLE_PAYLOAD_GOOGLE, CERBERUS_BLE_PAYLOAD_SAMSUNG,
        CERBERUS_BLE_PAYLOAD_SWIFT_PAIR, CERBERUS_BLE_PAYLOAD_APPLE_ACTION,
        CERBERUS_BLE_PAYLOAD_APPLE_ACTION, CERBERUS_BLE_PAYLOAD_FLIPPER};
    cerberus_ble_attack_plan_t plan;
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_ALL,
        0x13579BDFU, NULL) == CERBERUS_BLE_ATTACK_OK);
    for (uint32_t i = 0U; i < 120U; ++i) {
        cerberus_ble_attack_frame_t frame;
        assert(cerberus_ble_attack_next(&plan, i * 100U, &frame) == CERBERUS_BLE_ATTACK_OK);
        assert(cerberus_ble_attack_ad_valid(frame.data, frame.length));
        if (i % 6U == 3U || i % 6U == 4U) {
            assert(frame.kind == CERBERUS_BLE_PAYLOAD_APPLE_ACTION ||
                frame.kind == CERBERUS_BLE_PAYLOAD_APPLE_DEVICE);
        } else assert(frame.kind == order[i % 6U]);
        assert(frame.active_ms == (i % 6U == 3U ? 60U : i % 6U == 4U ? 500U : 10U));
    }
}

static void check_capture_and_gatt(void)
{
    cerberus_ble_attack_plan_t plan;
    cerberus_ble_attack_frame_t frame;
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG,
        1U, NULL) == CERBERUS_BLE_ATTACK_NEEDS_CAPTURE);
    cerberus_ble_attack_capture_t captured = {
        .address = {1,2,3,4,5,6}, .address_type = 1U,
        .data = {5,0xFF,0x4C,0,0x12,0x19}, .length = 6U};
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG,
        1U, &captured) == CERBERUS_BLE_ATTACK_OK);
    assert(cerberus_ble_attack_next(&plan, 0U, &frame) == CERBERUS_BLE_ATTACK_OK);
    assert(frame.kind == CERBERUS_BLE_PAYLOAD_CAPTURED_TRACKER && frame.captured_identity);
    assert(frame.length == captured.length && memcmp(frame.data, captured.data, frame.length) == 0);
    assert(memcmp(frame.address, captured.address, 6U) == 0 && frame.address_type == 1U);
    captured.data[0] = 31U;
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG,
        1U, &captured) == CERBERUS_BLE_ATTACK_NEEDS_CAPTURE);
    captured.data[0] = 5U; captured.length = 32U;
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_SPOOF_AIRTAG,
        1U, &captured) == CERBERUS_BLE_ATTACK_NEEDS_CAPTURE);
    assert(cerberus_ble_attack_plan_init(&plan, CERBERUS_BLE_ATTACK_FIND_MY_SOUND,
        1U, &captured) == CERBERUS_BLE_ATTACK_NEEDS_GATT);
    assert(cerberus_ble_attack_next(&plan, 0U, &frame) == CERBERUS_BLE_ATTACK_NEEDS_GATT);
    assert(cerberus_ble_attack_plan_init(&plan, (cerberus_ble_attack_mode_t)0U,
        1U, NULL) == CERBERUS_BLE_ATTACK_INVALID);
}

int main(void)
{
    check_ad_parser();
    check_one_mode(CERBERUS_BLE_ATTACK_SOUR_APPLE);
    check_one_mode(CERBERUS_BLE_ATTACK_APPLE_JUICE);
    check_one_mode(CERBERUS_BLE_ATTACK_SWIFT_PAIR);
    check_one_mode(CERBERUS_BLE_ATTACK_SAMSUNG);
    check_one_mode(CERBERUS_BLE_ATTACK_GOOGLE);
    check_one_mode(CERBERUS_BLE_ATTACK_FLIPPER);
    check_apple_cache();
    check_combined_order();
    check_capture_and_gatt();
    puts("BLE payload plans passed: 9 upstream routes, bounded AD, rotation, captured replay and GATT routing.");
    return 0;
}
