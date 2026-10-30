#pragma once

#include <stdbool.h>
#include <stdint.h>

#define CERBERUS_BLE_ATTACK_AD_MAX 31U

/* The nine routes under Marauder develop's Bluetooth Attack menu. This module
 * builds advertisement plans only; radio ownership belongs to the S3 host. */
typedef enum {
    CERBERUS_BLE_ATTACK_SOUR_APPLE = 1,
    CERBERUS_BLE_ATTACK_APPLE_JUICE,
    CERBERUS_BLE_ATTACK_SWIFT_PAIR,
    CERBERUS_BLE_ATTACK_SAMSUNG,
    CERBERUS_BLE_ATTACK_GOOGLE,
    CERBERUS_BLE_ATTACK_FLIPPER,
    CERBERUS_BLE_ATTACK_ALL,
    CERBERUS_BLE_ATTACK_SPOOF_AIRTAG,
    CERBERUS_BLE_ATTACK_FIND_MY_SOUND,
} cerberus_ble_attack_mode_t;

typedef enum {
    CERBERUS_BLE_PAYLOAD_APPLE_ACTION = 1,
    CERBERUS_BLE_PAYLOAD_APPLE_DEVICE,
    CERBERUS_BLE_PAYLOAD_SWIFT_PAIR,
    CERBERUS_BLE_PAYLOAD_SAMSUNG,
    CERBERUS_BLE_PAYLOAD_GOOGLE,
    CERBERUS_BLE_PAYLOAD_FLIPPER,
    CERBERUS_BLE_PAYLOAD_CAPTURED_TRACKER,
} cerberus_ble_payload_kind_t;

typedef enum {
    CERBERUS_BLE_ATTACK_OK = 0,
    CERBERUS_BLE_ATTACK_INVALID,
    CERBERUS_BLE_ATTACK_NEEDS_CAPTURE,
    CERBERUS_BLE_ATTACK_NEEDS_GATT,
} cerberus_ble_attack_result_t;

/* A selected raw advertisement from a completed local BLE scan. Address is
 * NimBLE byte order; types 0/2 are public, 1/3 are random. */
typedef struct {
    uint8_t address[6];
    uint8_t address_type;
    uint8_t data[CERBERUS_BLE_ATTACK_AD_MAX];
    uint8_t length;
} cerberus_ble_attack_capture_t;

typedef struct {
    cerberus_ble_payload_kind_t kind;
    uint8_t data[CERBERUS_BLE_ATTACK_AD_MAX];
    uint8_t length;
    uint8_t address[6];
    uint8_t address_type;
    uint16_t active_ms;
    uint16_t interval_units; /* 0: host default; 0x20: Apple Juice reference */
    bool connectable;
    bool captured_identity; /* Replay requires a selected scan result. */
} cerberus_ble_attack_frame_t;

typedef struct {
    cerberus_ble_attack_mode_t mode;
    uint32_t rng;
    uint32_t sequence;
    uint32_t apple_refreshed_ms;
    bool apple_cached;
    cerberus_ble_attack_frame_t apple_cache;
    cerberus_ble_attack_capture_t capture;
} cerberus_ble_attack_plan_t;

/* Strict AD-structure validation. A zero terminator is accepted only when all
 * remaining bytes are zero padding. */
bool cerberus_ble_attack_ad_valid(const uint8_t *data, uint8_t length);

cerberus_ble_attack_result_t cerberus_ble_attack_plan_init(
    cerberus_ble_attack_plan_t *plan, cerberus_ble_attack_mode_t mode,
    uint32_t seed, const cerberus_ble_attack_capture_t *capture);

/* One bounded advertising burst per call. The caller schedules active_ms,
 * repeats until explicit Stop, and never assumes a controller accepted RF. */
cerberus_ble_attack_result_t cerberus_ble_attack_next(
    cerberus_ble_attack_plan_t *plan, uint32_t now_ms,
    cerberus_ble_attack_frame_t *frame);
